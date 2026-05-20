#include "http_server.h"

RangeReq parse_range(const char* buf, int len) {
    RangeReq r;
    std::string s(buf, (size_t)len);
    std::string sl = s;
    for (auto& c : sl) c = (char)tolower((unsigned char)c);

    auto pos = sl.find("range: bytes=");
    if (pos == std::string::npos) return r;

    r.valid = true;
    pos += 13;
    auto end = s.find('\r', pos);
    if (end == std::string::npos) end = s.size();
    std::string rs = s.substr(pos, end - pos);
    auto dash = rs.find('-');
    if (dash != std::string::npos) {
        try {
            if (dash > 0) r.start = std::stoll(rs.substr(0, dash));
            if (dash + 1 < rs.size()) r.end = std::stoll(rs.substr(dash + 1));
        } catch (...) {}
    }
    return r;
}

int send_all(socket_t sock, const char* data, int len) {
    int sent = 0;
    while (sent < len) {
        int n = ::send(sock, data + sent, len - sent, 0);
        if (n <= 0) return -1;
        sent += n;
    }
    return sent;
}

bool serve_range(StreamEngine* s, TorrReader* reader, socket_t cli,
                        int64_t range_start, int64_t range_end) {
    int my_gen = s->seek_generation.load();
    int64_t cursor = range_start;
    TB_LOG("serve_range: start=%lld end=%lld gen=%d", (long long)range_start, (long long)range_end, my_gen);

    bool is_tail = (range_start > s->file_size - s->piece_length * 10);

    if (!is_tail) {
        reader->offset = range_start;
        reader->last_access = TorrReader::now_unix();
        s->read_head.store(range_start);
    }

    while (cursor <= range_end && s->active.load()) {
        if (s->seek_generation.load() != my_gen) return false;

        int p = std::clamp(s->byte_to_piece(cursor), s->start_piece, s->end_piece);

        int64_t pfbeg, pfend;
        s->piece_file_range(p, pfbeg, pfend);
        pfend -= 1;

        int64_t sbeg = std::max(cursor, pfbeg);
        int64_t send_end = std::min(range_end, pfend);
        if (send_end < sbeg) { cursor = pfend + 1; continue; }

        bool have_it = false;
        {
            std::lock_guard<std::mutex> lk(s->piece_mu);
            have_it = s->pieces_have.count(p) > 0;
        }

        if (!have_it) {
            constexpr int PIPELINE_AHEAD = 16;
            TB_LOG("serve_range: piece=%d not ready, prioritizing p..p+%d", p, PIPELINE_AHEAD);
            try {
                s->handle.piece_priority(lt::piece_index_t(p), lt::top_priority);
                s->handle.set_piece_deadline(lt::piece_index_t(p), 0);
            } catch (...) {}
            for (int i = 1; i <= PIPELINE_AHEAD && p + i <= s->end_piece; ++i) {
                bool have_next = false;
                { std::lock_guard<std::mutex> lk(s->piece_mu); have_next = s->pieces_have.count(p+i) > 0; }
                if (!have_next) {
                    try {
                        auto pri = (i <= 2) ? lt::top_priority : lt::download_priority_t(6);
                        s->handle.piece_priority(lt::piece_index_t(p+i), pri);
                        s->handle.set_piece_deadline(lt::piece_index_t(p+i), i * 80);
                    } catch (...) {}
                }
            }

            TB_LOG("serve_range: WAITING for piece=%d", p);
            std::unique_lock<std::mutex> lk(s->piece_mu);
            bool ok = s->piece_cv.wait_for(lk, chr::seconds(60), [&] {
                return !s->active.load()
                    || s->seek_generation.load() != my_gen
                    || s->pieces_have.count(p) > 0;
            });

            if (!s->active.load() || s->seek_generation.load() != my_gen) {
                TB_LOG("serve_range: ABORT piece=%d gen_now=%d my_gen=%d", p, s->seek_generation.load(), my_gen);
                return false;
            }
            if (!ok || s->pieces_have.count(p) == 0) {
                TB_LOG("serve_range: TIMEOUT piece=%d", p);
                return false;
            }
            TB_LOG("serve_range: piece=%d READY", p);
        }

        ReadResult rd = read_piece_data(s, p, 10000, my_gen);
        if (!rd.ok || rd.data.empty()) {
            TB_LOG("serve_range: read_piece_data FAILED piece=%d", p);
            return false;
        }

        int64_t abs_start = s->file_offset + sbeg;
        int64_t piece_start = (int64_t)p * s->piece_length;
        int64_t off = abs_start - piece_start;
        int64_t nb  = send_end - sbeg + 1;

        if (off < 0 || (size_t)off >= rd.data.size()) { cursor = send_end + 1; continue; }
        if ((size_t)(off + nb) > rd.data.size()) nb = (int64_t)rd.data.size() - off;
        if (nb <= 0) { cursor = send_end + 1; continue; }

        for (int i = 1; i <= 2; ++i) {
            int np = p + i;
            if (np > s->end_piece) break;
            bool have_np = false;
            { std::lock_guard<std::mutex> lk(s->piece_mu); have_np = s->pieces_have.count(np) > 0; }
            if (!have_np) break;
            bool already = false;
            if (s->cache) {
                if (auto* cp = s->cache->get_piece(np)) {
                    std::shared_lock<std::shared_mutex> clk(cp->mu);
                    if (!cp->buffer.empty() && cp->complete) already = true;
                }
            }
            if (!already) {
                std::lock_guard<std::mutex> rlk(s->read_mu);
                if (s->read_results.count(np)) already = true;
            }
            if (already) continue;
            try { s->handle.read_piece(lt::piece_index_t(np)); } catch (...) {}
        }

        if (send_all(cli, rd.data.data() + off, (int)nb) < 0)
            return false;

        cursor = sbeg + nb;

        if (!is_tail) {
            reader->offset = cursor;
            reader->last_access = TorrReader::now_unix();
            s->read_head.store(cursor);
        }

        if (s->stream_state.load() != LT_STREAM_READY)
            s->stream_state.store(LT_STREAM_READY);

        {
            std::lock_guard<std::mutex> tlk(s->trailing_mu);
            if (s->trailing_pieces.empty() || s->trailing_pieces.back() != p)
                s->trailing_pieces.push_back(p);
            while ((int)s->trailing_pieces.size() > StreamEngine::TRAILING_WINDOW) {
                int old_p = s->trailing_pieces.front();
                s->trailing_pieces.pop_front();
                try {
                    s->handle.piece_priority(lt::piece_index_t(old_p), lt::dont_download);
                } catch (...) {}
                if (s->cache) {
                    auto* cp = s->cache->get_piece(old_p);
                    if (cp) {
                        std::unique_lock<std::shared_mutex> clk(cp->mu);
                        cp->buffer.clear();
                        cp->buffer.shrink_to_fit();
                        cp->size = 0;
                        cp->complete = false;
                    }
                }
            }
        }
    }
    TB_LOG("serve_range: DONE cursor=%lld end=%lld", (long long)cursor, (long long)range_end);
    return true;
}

void handle_connection(StreamEngine* s, socket_t cli, int reader_id) {
    StreamEngine::active_streams.fetch_add(1);

    TorrReader* reader = nullptr;
    try {
        reader = s->cache->new_reader(
            reader_id, s->file_index, s->file_offset, s->file_size);

        reader->set_readahead(StreamEngine::FIXED_READAHEAD);
        s->cache->adjust_readahead(StreamEngine::FIXED_READAHEAD);

        int opt = 1;
        ::setsockopt(cli, IPPROTO_TCP, TCP_NODELAY, (const char*)&opt, sizeof(opt));
        int sndbuf = 2 * 1024 * 1024;
        ::setsockopt(cli, SOL_SOCKET, SO_SNDBUF, (const char*)&sndbuf, sizeof(sndbuf));

#ifdef _WIN32
        DWORD tv = 36000000;
        ::setsockopt(cli, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        ::setsockopt(cli, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
#else
        struct timeval tv;
        tv.tv_sec = 36000; tv.tv_usec = 0;
        ::setsockopt(cli, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ::setsockopt(cli, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

        while (s->active.load()) {
            char buf[8192] = {};
            int total = 0;
            bool header_complete = false;
            while (total < (int)sizeof(buf) - 1 && s->active.load()) {
                int n = ::recv(cli, buf + total, (int)sizeof(buf) - 1 - total, 0);
                if (n <= 0) goto cleanup;
                total += n;
                for (int i = std::max(0, total - 4); i <= total - 4; ++i) {
                    if (buf[i]=='\r' && buf[i+1]=='\n' && buf[i+2]=='\r' && buf[i+3]=='\n') {
                        header_complete = true; break;
                    }
                }
                if (header_complete) break;
            }
            if (!header_complete || !s->active.load()) goto cleanup;

            {
                std::string req(buf, (size_t)total);

                bool is_options = req.find("OPTIONS ") != std::string::npos;
                bool is_head    = req.find("HEAD ") != std::string::npos;
                bool is_get     = req.find("GET ")  != std::string::npos;

                if (is_options) {
                    const char* cors =
                        "HTTP/1.1 204 No Content\r\n"
                        "Access-Control-Allow-Origin: *\r\n"
                        "Access-Control-Allow-Methods: GET, HEAD, OPTIONS\r\n"
                        "Access-Control-Allow-Headers: Range\r\n"
                        "Access-Control-Max-Age: 1728000\r\n"
                        "Content-Length: 0\r\n"
                        "Connection: keep-alive\r\n\r\n";
                    if (send_all(cli, cors, (int)strlen(cors)) < 0) goto cleanup;
                    continue;
                }

                if (!is_get && !is_head) goto cleanup;

                int64_t fsz = s->file_size;
                if (fsz <= 0) goto cleanup;

                RangeReq rr = parse_range(buf, total);
                int64_t rstart = (rr.valid && rr.start >= 0) ? rr.start : 0;
                int64_t rend   = (rr.valid && rr.end >= 0)   ? rr.end   : fsz - 1;
                rstart = std::clamp(rstart, (int64_t)0, fsz - 1);
                rend   = std::clamp(rend,   rstart,     fsz - 1);
                int64_t clen = rend - rstart + 1;
                bool is_partial = (rr.valid && rr.start >= 0);

                int64_t old_head = s->read_head.load();
                bool is_tail_req = (rstart > fsz - s->piece_length * 10);
                TB_LOG("handle_conn: rstart=%lld rend=%lld old_head=%lld is_tail=%d",
                       (long long)rstart, (long long)rend, (long long)old_head, is_tail_req?1:0);
                if (!is_tail_req && old_head > 0 && std::abs(rstart - old_head) > 65536) {
                    int new_gen = s->seek_generation.fetch_add(1) + 1;
                    TB_LOG("SEEK DETECTED: old_head=%lld new_pos=%lld seek_piece=%d gen=%d",
                           (long long)old_head, (long long)rstart,
                           std::clamp(s->byte_to_piece(rstart), s->start_piece, s->end_piece), new_gen);
                    s->stream_state.store(LT_STREAM_SEEKING);
                    s->read_head.store(rstart);

                    reader->offset = rstart;
                    reader->last_access = TorrReader::now_unix();
                    reader->reader_on();

                    s->piece_cv.notify_all();

                    {
                        std::lock_guard<std::mutex> rlk(s->read_mu);
                        s->read_results.clear();
                    }
                    s->read_cv.notify_all();

                    {
                        std::lock_guard<std::mutex> tlk(s->trailing_mu);
                        for (int old_p : s->trailing_pieces) {
                            try {
                                s->handle.piece_priority(
                                    lt::piece_index_t(old_p), lt::dont_download);
                            } catch (...) {}
                        }
                        s->trailing_pieces.clear();
                    }

                    int seek_piece = std::clamp(s->byte_to_piece(rstart),
                                                s->start_piece, s->end_piece);
                    try {
                        s->handle.clear_piece_deadlines();
                        s->handle.piece_priority(
                            lt::piece_index_t(seek_piece), lt::top_priority);
                        s->handle.set_piece_deadline(
                            lt::piece_index_t(seek_piece), 0);
                        s->handle.resume();
                        s->handle.read_piece(lt::piece_index_t(seek_piece));
                    } catch (...) {}
                }

                std::string filename = "video.mp4";
                if (s->ti) {
                    try { filename = s->ti->files().file_name(lt::file_index_t{s->file_index}).to_string(); }
                    catch (...) {}
                }

                std::string etag_raw = s->make_url();
                std::ostringstream hdr;
                if (is_partial) {
                    hdr << "HTTP/1.1 206 Partial Content\r\n";
                    hdr << "Content-Range: bytes " << rstart << "-" << rend << "/" << fsz << "\r\n";
                } else {
                    hdr << "HTTP/1.1 200 OK\r\n";
                }
                hdr << "Content-Type: " << get_mime(filename) << "\r\n";
                hdr << "Content-Length: " << clen << "\r\n";
                hdr << "Accept-Ranges: bytes\r\n";
                hdr << "Connection: close\r\n";
                hdr << "ETag: \"" << std::hash<std::string>{}(etag_raw) << "\"\r\n";
                hdr << "Access-Control-Allow-Origin: *\r\n";
                hdr << "Access-Control-Allow-Headers: Range\r\n";
                hdr << "transferMode.dlna.org: Streaming\r\n";
                hdr << "contentFeatures.dlna.org: DLNA.ORG_OP=01;DLNA.ORG_CI=0;"
                       "DLNA.ORG_FLAGS=01700000000000000000000000000000\r\n";
                hdr << "Cache-Control: no-store, no-cache\r\n\r\n";

                std::string h = hdr.str();
                if (send_all(cli, h.c_str(), (int)h.size()) < 0) goto cleanup;

                if (is_get) {
                    TB_LOG("handle_conn: calling serve_range rstart=%lld rend=%lld", (long long)rstart, (long long)rend);
                    bool sr_ok = serve_range(s, reader, cli, rstart, rend);
                    TB_LOG("handle_conn: serve_range returned %d", (int)sr_ok);
                    if (!sr_ok) goto cleanup;
                }

                goto cleanup;
            }
        }

cleanup:
        try {
            if (s->cache && reader) s->cache->close_reader(reader);
        } catch (...) {}

        StreamEngine::active_streams.fetch_add(-1);

    } catch (...) {
        StreamEngine::active_streams.fetch_add(-1);
    }
}

void run_http_server(SessionWrapper* /*sw*/, StreamEngine* stream) {
    try {
        INIT_SOCKETS();

        socket_t sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == SOCKET_INVALID) return;

        int opt = 1;
        ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port        = 0;
        if (::bind(sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
            CLOSESOCKET(sock); return;
        }
        socklen_t_ al = sizeof(addr);
        ::getsockname(sock, (sockaddr*)&addr, (socklen_t*)&al);
        stream->server_port = ntohs(addr.sin_port);
        stream->listen_sock = sock;
        ::listen(sock, 16);
        stream->running = true;

        while (stream->active.load()) {
            fd_set fds; FD_ZERO(&fds); FD_SET(sock, &fds);
            timeval tv{0, 200000};
            if (::select((int)sock + 1, &fds, nullptr, nullptr, &tv) <= 0)
                continue;

            sockaddr_in ca{}; socklen_t_ cl = sizeof(ca);
            socket_t cli = ::accept(sock, (sockaddr*)&ca, (socklen_t*)&cl);
            if (cli == SOCKET_INVALID) continue;

            int rid = stream->next_reader_id.fetch_add(1);

            {
                std::lock_guard<std::mutex> lk(stream->clients_mu);
                stream->client_sockets[rid] = cli;

                for (auto it = stream->client_threads.begin(); it != stream->client_threads.end(); ) {
                    if (stream->finished_clients.count(it->first)) {
                        if (it->second.joinable()) it->second.join();
                        stream->finished_clients.erase(it->first);
                        it = stream->client_threads.erase(it);
                    } else {
                        ++it;
                    }
                }
            }

            std::thread t([stream, cli, rid]() {
                try {
                    handle_connection(stream, cli, rid);
                } catch (...) {}
                try { CLOSESOCKET(cli); } catch (...) {}
                try {
                    std::lock_guard<std::mutex> lk(stream->clients_mu);
                    stream->client_sockets.erase(rid);
                    stream->finished_clients.insert(rid);
                } catch (...) {}
            });

            {
                std::lock_guard<std::mutex> lk(stream->clients_mu);
                stream->client_threads[rid] = std::move(t);
            }
        }

        if (stream->listen_sock != SOCKET_INVALID) {
            CLOSESOCKET(sock);
            stream->listen_sock = SOCKET_INVALID;
        }

        {
            std::lock_guard<std::mutex> lk(stream->clients_mu);
            for (auto& kv : stream->client_sockets) {
#ifdef _WIN32
                ::shutdown(kv.second, SD_BOTH);
#else
                ::shutdown(kv.second, SHUT_RDWR);
#endif
            }
        }

        std::unordered_map<int, std::thread> threads_to_join;
        {
            std::lock_guard<std::mutex> lk(stream->clients_mu);
            threads_to_join = std::move(stream->client_threads);
            stream->client_threads.clear();
            stream->finished_clients.clear();
        }
        for (auto& kv : threads_to_join) {
            if (kv.second.joinable()) kv.second.join();
        }
    } catch (...) {}
}
