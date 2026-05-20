#include "session_wrapper.h"
#include "http_server.h"
#include "stream_engine.h"
#include "common_types.h"

extern "C"
{

    TORRENT_API lt_session_t lt_create_session(const char *iface, int dl, int ul)
    {
        try
        {
            lt::settings_pack sp;

            // alert categories
            sp.set_int(lt::settings_pack::alert_mask,
                       lt::alert_category::status | lt::alert_category::error | lt::alert_category::storage | lt::alert_category::piece_progress);

            sp.set_str(lt::settings_pack::listen_interfaces,
                       (iface && *iface) ? iface : "0.0.0.0:6881,[::]:6881");

            if (dl > 0)
                sp.set_int(lt::settings_pack::download_rate_limit, dl);
            if (ul > 0)
                sp.set_int(lt::settings_pack::upload_rate_limit, ul);

            // ── connection speed — get peers fast ──
            sp.set_int(lt::settings_pack::connection_speed, 200);
            sp.set_int(lt::settings_pack::torrent_connect_boost, 200);
            sp.set_bool(lt::settings_pack::smooth_connects, false);
            sp.set_int(lt::settings_pack::connections_limit, 200);
            sp.set_int(lt::settings_pack::min_reconnect_time, 5);
            sp.set_int(lt::settings_pack::max_failcount, 3);
            sp.set_int(lt::settings_pack::peer_connect_timeout, 5);
            sp.set_int(lt::settings_pack::handshake_timeout, 5);

            // ── timeouts — detect slow/stalled peers quickly for streaming ──
            sp.set_int(lt::settings_pack::piece_timeout, 5);
            sp.set_int(lt::settings_pack::request_timeout, 5);
            sp.set_int(lt::settings_pack::peer_timeout, 15);
            sp.set_int(lt::settings_pack::inactivity_timeout, 15);

            // ── request pipeline — short queue time = fast seek response ──
            sp.set_int(lt::settings_pack::request_queue_time, 1);
            sp.set_int(lt::settings_pack::max_out_request_queue, 500);
            sp.set_int(lt::settings_pack::max_allowed_in_request_queue, 2000);

            // ── piece picking — WE control priorities ──
            sp.set_bool(lt::settings_pack::auto_sequential, false);
            sp.set_bool(lt::settings_pack::piece_extent_affinity, true);
            sp.set_bool(lt::settings_pack::strict_end_game_mode, false);
            sp.set_bool(lt::settings_pack::prioritize_partial_pieces, true);
            sp.set_int(lt::settings_pack::initial_picker_threshold, 0);

            // ── disk I/O ──
            sp.set_int(lt::settings_pack::aio_threads, 4);
            sp.set_int(lt::settings_pack::hashing_threads, 2);
            sp.set_int(lt::settings_pack::max_queued_disk_bytes, 64 * 1024 * 1024);
            sp.set_int(lt::settings_pack::disk_io_read_mode, lt::settings_pack::enable_os_cache);
            sp.set_int(lt::settings_pack::disk_io_write_mode, lt::settings_pack::enable_os_cache);
            sp.set_int(lt::settings_pack::file_pool_size, 100);
            sp.set_bool(lt::settings_pack::no_atime_storage, true);

            // ── upload — unlimited unchoke so peers reciprocate ──
            sp.set_int(lt::settings_pack::unchoke_slots_limit, -1);
            sp.set_int(lt::settings_pack::active_seeds, 0);
            sp.set_int(lt::settings_pack::suggest_mode, lt::settings_pack::suggest_read_cache);

            // ── DHT + discovery ──
            sp.set_bool(lt::settings_pack::enable_dht, true);
            sp.set_bool(lt::settings_pack::enable_lsd, true);
            sp.set_bool(lt::settings_pack::enable_upnp, true);
            sp.set_bool(lt::settings_pack::enable_natpmp, true);
            sp.set_str(lt::settings_pack::dht_bootstrap_nodes,
                       "dht.libtorrent.org:25401,"
                       "router.bittorrent.com:6881,"
                       "dht.transmissionbt.com:6881,"
                       "router.utorrent.com:6881");
            sp.set_int(lt::settings_pack::dht_announce_interval, 60);
            sp.set_bool(lt::settings_pack::announce_to_all_trackers, true);
            sp.set_bool(lt::settings_pack::announce_to_all_tiers, true);

            // ── general ──
            sp.set_int(lt::settings_pack::active_downloads, 3);
            sp.set_int(lt::settings_pack::active_seeds, 3);
            sp.set_int(lt::settings_pack::active_limit, 5);
            sp.set_int(lt::settings_pack::alert_queue_size, 10000);
            sp.set_bool(lt::settings_pack::close_redundant_connections, true);
            sp.set_int(lt::settings_pack::peer_turnover, 5);
            sp.set_int(lt::settings_pack::peer_turnover_interval, 30);
            sp.set_bool(lt::settings_pack::no_recheck_incomplete_resume, true);
            sp.set_bool(lt::settings_pack::allow_multiple_connections_per_ip, true);
            sp.set_bool(lt::settings_pack::rate_limit_ip_overhead, false);
            sp.set_int(lt::settings_pack::whole_pieces_threshold, 0);
            sp.set_int(lt::settings_pack::max_peerlist_size, 8000);
            sp.set_bool(lt::settings_pack::dont_count_slow_torrents, true);

            // ── encryption (MSE/PE) ──
            sp.set_int(lt::settings_pack::in_enc_policy, lt::settings_pack::pe_enabled);
            sp.set_int(lt::settings_pack::out_enc_policy, lt::settings_pack::pe_enabled);
            sp.set_int(lt::settings_pack::allowed_enc_level, lt::settings_pack::pe_both);
            sp.set_bool(lt::settings_pack::prefer_rc4, false);
            sp.set_int(lt::settings_pack::mixed_mode_algorithm, lt::settings_pack::peer_proportional);

            // ── reciprocity boost ──
            sp.set_int(lt::settings_pack::predictive_piece_announce, 500);

            // ── tracker discovery ──
            sp.set_bool(lt::settings_pack::prefer_udp_trackers, true);

            // spoof as qBittorrent 4.3.9
            sp.set_str(lt::settings_pack::user_agent, "qBittorrent/4.3.9");
            sp.set_str(lt::settings_pack::peer_fingerprint, "-qB4390-");
            sp.set_str(lt::settings_pack::handshake_client_version, "qBittorrent/4.3.9");

            // buffers
            sp.set_int(lt::settings_pack::send_buffer_watermark, 2 * 1024 * 1024);
            sp.set_int(lt::settings_pack::send_buffer_low_watermark, 64 * 1024);
            sp.set_int(lt::settings_pack::send_buffer_watermark_factor, 150);
            sp.set_int(lt::settings_pack::recv_socket_buffer_size, 1024 * 1024);
            sp.set_int(lt::settings_pack::send_socket_buffer_size, 1024 * 1024);

            auto *sw = new SessionWrapper(std::move(sp));
            sw->init_default_config();
            sw->start_alert_thread();
            set_err("");
            return reinterpret_cast<lt_session_t>(sw);
        }
        catch (const std::exception &e)
        {
            set_err(e.what());
            return nullptr;
        }
    }

    TORRENT_API void lt_destroy_session(lt_session_t session)
    {
        if (!session)
            return;
        auto *sw = to_sw(session);

        try
        {
            std::vector<std::unique_ptr<StreamEngine>> streams_to_destroy;
            {
                std::lock_guard<std::mutex> lk(sw->streams_mu);
                for (auto &kv : sw->streams)
                {
                    try
                    {
                        kv.second->active = false;
                        kv.second->preloading.store(false);
                        kv.second->wake_all();
                        if (kv.second->listen_sock != SOCKET_INVALID)
                        {
                            CLOSESOCKET(kv.second->listen_sock);
                            kv.second->listen_sock = SOCKET_INVALID;
                        }
                    }
                    catch (...)
                    {
                    }
                    streams_to_destroy.push_back(std::move(kv.second));
                }
                sw->streams.clear();
            }
            for (auto &stream : streams_to_destroy)
            {
                try
                {
                    if (stream->preload_thread.joinable())
                        stream->preload_thread.join();
                }
                catch (...)
                {
                }
                try
                {
                    if (stream->server_thread.joinable())
                        stream->server_thread.join();
                }
                catch (...)
                {
                }
                try
                {
                    if (stream->cache)
                        stream->cache->close();
                }
                catch (...)
                {
                }
            }
            streams_to_destroy.clear();
        }
        catch (...)
        {
        }

        try
        {
            sw->alert_running = false;
            if (sw->alert_thread.joinable())
                sw->alert_thread.join();
        }
        catch (...)
        {
        }

        try
        {
            std::lock_guard<std::mutex> lk(sw->mu);
            for (auto &kv : sw->handles)
                if (kv.second.is_valid())
                    try
                    {
                        kv.second.save_resume_data(lt::torrent_handle::flush_disk_cache);
                    }
                    catch (...)
                    {
                    }
        }
        catch (...)
        {
        }

        std::this_thread::sleep_for(chr::milliseconds(200));
        try
        {
            delete sw;
        }
        catch (...)
        {
        }
    }

    TORRENT_API void lt_set_alert_callback(lt_session_t session,
                                           lt_alert_callback cb, void *ud)
    {
        if (!session)
            return;
        auto *sw = to_sw(session);
        std::lock_guard<std::mutex> lk(sw->cb_mu);
        sw->dart_callback = cb;
        sw->dart_user_data = ud;
    }

    TORRENT_API void lt_poll_alerts(lt_session_t session,
                                    lt_alert_callback cb, void *ud)
    {
        if (!session || !cb)
            return;
        auto *sw = to_sw(session);
        std::deque<AlertRecord> local;
        {
            std::lock_guard<std::mutex> lk(sw->dart_queue_mu);
            local.swap(sw->dart_queue);
        }
        for (auto &r : local)
            cb(r.type, r.torrent_id, r.message.c_str(), ud);
    }

    TORRENT_API lt_torrent_id lt_add_magnet(lt_session_t session,
                                            const char *uri, const char *path,
                                            int stream_only, int paused, int auto_managed)
    {
        if (!session || !uri || !path)
        {
            set_err("null arg");
            return -1;
        }
        auto *sw = to_sw(session);
        try
        {
            lt::error_code ec;
            lt::add_torrent_params atp = lt::parse_magnet_uri(uri, ec);
            if (ec)
            {
                set_err(ec.message());
                return -1;
            }
            atp.save_path = path;

            if (paused)
            {
                atp.flags |= lt::torrent_flags::paused;
            }
            else
            {
                atp.flags &= ~lt::torrent_flags::paused;
            }

            if (auto_managed)
            {
                atp.flags |= lt::torrent_flags::auto_managed;
            }
            else
            {
                atp.flags &= ~lt::torrent_flags::auto_managed;
            }

            if (atp.trackers.empty())
            {
                static const char *kDefaultTrackers[] = {
                    "udp://tracker.opentrackr.org:1337/announce",
                    "udp://open.demonii.com:1337/announce",
                    "udp://open.stealth.si:80/announce",
                    "udp://tracker.torrent.eu.org:451/announce",
                    "udp://exodus.desync.com:6969/announce",
                    "udp://tracker.openbittorrent.com:6969/announce",
                    "udp://tracker.dler.org:6969/announce",
                    "udp://explodie.org:6969/announce",
                };
                for (auto *t : kDefaultTrackers)
                    atp.trackers.emplace_back(t);
            }

            if (stream_only)
            {
                atp.storage_mode = lt::storage_mode_sparse;
                atp.flags |= lt::torrent_flags::stop_when_ready;
            }

            lt::torrent_handle h = sw->session.add_torrent(std::move(atp), ec);
            if (ec)
            {
                set_err(ec.message());
                return -1;
            }
            if (!paused)
            {
                h.resume();
            }

            int64_t id = sw->next_id.fetch_add(1);
            {
                std::lock_guard<std::mutex> lk(sw->mu);
                sw->handles[id] = h;
                if (stream_only)
                    sw->ephemeral_torrents.insert(id);
            }
            set_err("");
            return id;
        }
        catch (const std::exception &e)
        {
            set_err(e.what());
            return -1;
        }
    }

    TORRENT_API lt_torrent_id lt_add_torrent_file(lt_session_t session,
                                                  const char *fp, const char *path,
                                                  int stream_only, int paused, int auto_managed)
    {
        if (!session || !fp || !path)
        {
            set_err("null arg");
            return -1;
        }
        auto *sw = to_sw(session);
        try
        {
            lt::error_code ec;
            auto ti = std::make_shared<lt::torrent_info>(fp, ec);
            if (ec)
            {
                set_err(ec.message());
                return -1;
            }
            lt::add_torrent_params atp;
            atp.ti = ti;
            atp.save_path = path;

            if (paused)
            {
                atp.flags |= lt::torrent_flags::paused;
            }
            else
            {
                atp.flags &= ~lt::torrent_flags::paused;
            }

            if (auto_managed)
            {
                atp.flags |= lt::torrent_flags::auto_managed;
            }
            else
            {
                atp.flags &= ~lt::torrent_flags::auto_managed;
            }

            if (stream_only)
            {
                atp.storage_mode = lt::storage_mode_sparse;
                atp.flags |= lt::torrent_flags::stop_when_ready;
            }

            lt::torrent_handle h = sw->session.add_torrent(std::move(atp), ec);
            if (ec)
            {
                set_err(ec.message());
                return -1;
            }
            if (!paused)
            {
                h.resume();
            }

            int64_t id = sw->next_id.fetch_add(1);
            {
                std::lock_guard<std::mutex> lk(sw->mu);
                sw->handles[id] = h;
                if (stream_only)
                    sw->ephemeral_torrents.insert(id);
            }
            set_err("");
            return id;
        }
        catch (const std::exception &e)
        {
            set_err(e.what());
            return -1;
        }
    }

    TORRENT_API void lt_remove_torrent(lt_session_t session,
                                       lt_torrent_id id, int del)
    {
        if (!session)
            return;
        auto *sw = to_sw(session);
        std::lock_guard<std::mutex> lk(sw->mu);
        auto it = sw->handles.find(id);
        if (it == sw->handles.end())
            return;
        sw->session.remove_torrent(it->second,
                                   del ? lt::session::delete_files : lt::remove_flags_t{});
        sw->handles.erase(it);
        sw->ephemeral_torrents.erase(id);
    }

    TORRENT_API void lt_pause_torrent(lt_session_t session, lt_torrent_id id)
    {
        if (!session)
            return;
        auto *sw = to_sw(session);
        std::lock_guard<std::mutex> lk(sw->mu);
        auto it = sw->handles.find(id);
        if (it != sw->handles.end() && it->second.is_valid())
            try
            {
                it->second.pause();
            }
            catch (...)
            {
            }
    }

    TORRENT_API void lt_resume_torrent(lt_session_t session, lt_torrent_id id)
    {
        if (!session)
            return;
        auto *sw = to_sw(session);
        std::lock_guard<std::mutex> lk(sw->mu);
        auto it = sw->handles.find(id);
        if (it != sw->handles.end() && it->second.is_valid())
            try
            {
                it->second.resume();
            }
            catch (...)
            {
            }
    }

    TORRENT_API void lt_recheck_torrent(lt_session_t session, lt_torrent_id id)
    {
        if (!session)
            return;
        auto *sw = to_sw(session);
        std::lock_guard<std::mutex> lk(sw->mu);
        auto it = sw->handles.find(id);
        if (it != sw->handles.end() && it->second.is_valid())
            try
            {
                it->second.force_recheck();
            }
            catch (...)
            {
            }
    }

    TORRENT_API int lt_get_torrent_count(lt_session_t session)
    {
        if (!session)
            return 0;
        auto *sw = to_sw(session);
        std::lock_guard<std::mutex> lk(sw->mu);
        return (int)sw->handles.size();
    }

    TORRENT_API int lt_get_all_statuses(lt_session_t session,
                                        lt_torrent_status *out, int max)
    {
        if (!session || !out || max <= 0)
            return 0;
        auto *sw = to_sw(session);
        std::lock_guard<std::mutex> lk(sw->mu);
        int n = 0;
        for (auto &kv : sw->handles)
        {
            if (n >= max)
                break;
            if (!kv.second.is_valid())
                continue;
            try
            {
                fill_status(out[n++], kv.first,
                            kv.second.status(lt::torrent_handle::query_pieces));
            }
            catch (...)
            {
            }
        }
        return n;
    }

    TORRENT_API int lt_get_status(lt_session_t session, lt_torrent_id id,
                                  lt_torrent_status *out)
    {
        if (!session || !out)
            return 0;
        auto *sw = to_sw(session);
        std::lock_guard<std::mutex> lk(sw->mu);
        auto it = sw->handles.find(id);
        if (it == sw->handles.end() || !it->second.is_valid())
            return 0;
        try
        {
            fill_status(*out, id,
                        it->second.status(lt::torrent_handle::query_pieces));
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    TORRENT_API int lt_get_file_count(lt_session_t session, lt_torrent_id id)
    {
        if (!session)
            return 0;
        auto *sw = to_sw(session);
        std::lock_guard<std::mutex> lk(sw->mu);
        auto it = sw->handles.find(id);
        if (it == sw->handles.end() || !it->second.is_valid())
            return 0;
        try
        {
            auto ti = it->second.torrent_file();
            return ti ? ti->num_files() : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    TORRENT_API int lt_get_files(lt_session_t session, lt_torrent_id id,
                                 lt_file_info *out, int max)
    {
        if (!session || !out || max <= 0)
            return 0;
        auto *sw = to_sw(session);
        std::lock_guard<std::mutex> lk(sw->mu);
        auto it = sw->handles.find(id);
        if (it == sw->handles.end() || !it->second.is_valid())
            return 0;
        try
        {
            auto ti = it->second.torrent_file();
            if (!ti)
                return 0;
            const lt::file_storage &fs = ti->files();
            std::vector<lt::download_priority_t> priorities = it->second.get_file_priorities();
            int n = 0;
            for (int i = 0; i < fs.num_files() && n < max; ++i, ++n)
            {
                lt::file_index_t fi{i};
                out[n].index = i;
                out[n].size = fs.file_size(fi);
                out[n].is_streamable = is_streamable(fs.file_name(fi).to_string()) ? 1 : 0;
                std::string nm = fs.file_name(fi).to_string();
                std::string pt = fs.file_path(fi);
                std::strncpy(out[n].name, nm.c_str(), sizeof(out[n].name) - 1);
                std::strncpy(out[n].path, pt.c_str(), sizeof(out[n].path) - 1);
                out[n].name[sizeof(out[n].name) - 1] = 0;
                out[n].path[sizeof(out[n].path) - 1] = 0;

                int priority_val = 4;
                if (i < (int)priorities.size())
                {
                    priority_val = static_cast<int>(priorities[i]);
                }
                out[n].priority = priority_val;
            }
            return n;
        }
        catch (...)
        {
            return 0;
        }
    }

    TORRENT_API void lt_set_file_priorities(lt_session_t session, lt_torrent_id id,
                                            const int32_t *priorities, int count)
    {
        if (!session || !priorities || count <= 0)
            return;
        auto *sw = to_sw(session);
        std::lock_guard<std::mutex> lk(sw->mu);
        auto it = sw->handles.find(id);
        if (it == sw->handles.end() || !it->second.is_valid())
            return;
        try
        {
            auto ti = it->second.torrent_file();
            if (!ti)
                return;
            int nf = ti->files().num_files();
            std::vector<lt::download_priority_t> p;
            p.reserve(nf);
            for (int i = 0; i < nf; ++i)
            {
                int val = (i < count) ? priorities[i] : 4;
                if (val < 0)
                    val = 0;
                if (val > 7)
                    val = 7;
                p.push_back(static_cast<lt::download_priority_t>(val));
            }
            it->second.prioritize_files(p);
            it->second.unset_flags(lt::torrent_flags::stop_when_ready);
            it->second.resume();
        }
        catch (...)
        {
        }
    }

    TORRENT_API int lt_get_file_priorities(lt_session_t session, lt_torrent_id id,
                                           int32_t *out, int max_count)
    {
        if (!session || !out || max_count <= 0)
            return 0;
        auto *sw = to_sw(session);
        std::lock_guard<std::mutex> lk(sw->mu);
        auto it = sw->handles.find(id);
        if (it == sw->handles.end() || !it->second.is_valid())
            return 0;
        try
        {
            std::vector<lt::download_priority_t> p = it->second.get_file_priorities();
            int n = 0;
            for (int i = 0; i < (int)p.size() && n < max_count; ++i)
            {
                out[n++] = static_cast<int32_t>(p[i]);
            }
            return n;
        }
        catch (...)
        {
            return 0;
        }
    }

    TORRENT_API lt_stream_id lt_start_stream(lt_session_t session,
                                             lt_torrent_id torrent_id,
                                             int file_index,
                                             int64_t max_cache_bytes)
    {
        if (!session)
        {
            set_err("null session");
            return -1;
        }
        auto *sw = to_sw(session);

        lt::torrent_handle handle;
        {
            std::lock_guard<std::mutex> lk(sw->mu);
            auto it = sw->handles.find(torrent_id);
            if (it == sw->handles.end())
            {
                set_err("torrent not found");
                return -1;
            }
            handle = it->second;
        }
        if (!handle.is_valid())
        {
            set_err("invalid handle");
            return -1;
        }

        auto ti = handle.torrent_file();
        if (!ti)
        {
            set_err("no metadata yet");
            return -1;
        }
        const lt::file_storage &fs = ti->files();

        if (file_index < 0)
        {
            int64_t best = -1;
            file_index = 0;
            for (int i = 0; i < fs.num_files(); ++i)
            {
                int64_t sz = fs.file_size(lt::file_index_t{i});
                if (sz > best && is_streamable(fs.file_name(lt::file_index_t{i}).to_string()))
                {
                    best = sz;
                    file_index = i;
                }
            }
        }
        if (file_index < 0 || file_index >= fs.num_files())
        {
            set_err("invalid file index");
            return -1;
        }

        auto s = std::make_unique<StreamEngine>();
        s->id = sw->next_stream_id.fetch_add(1);
        s->torrent_id = torrent_id;
        s->file_index = file_index;
        s->handle = handle;
        s->ti = ti;
        s->piece_length = ti->piece_length();
        s->file_size = fs.file_size(lt::file_index_t{file_index});
        s->file_offset = fs.file_offset(lt::file_index_t{file_index});
        s->start_piece = std::max(0, (int)(s->file_offset / s->piece_length));
        s->end_piece = std::min((int)ti->num_pieces() - 1,
                                (int)((s->file_offset + s->file_size - 1) / s->piece_length));
        s->total_pieces = s->end_piece - s->start_piece + 1;

        int prot_pieces = (int)((StreamEngine::PROTECT_BYTES + s->piece_length - 1) / s->piece_length);
        s->head_end_piece = std::min(s->start_piece + prot_pieces - 1, s->end_piece);
        s->tail_start_piece = std::max(s->end_piece - prot_pieces + 1, s->start_piece);

        {
            float duration_guess;
            if (s->file_size < 400LL * 1024 * 1024)
                duration_guess = 3600.0f;
            else if (s->file_size < 1LL * 1024 * 1024 * 1024)
                duration_guess = 5400.0f;
            else if (s->file_size < 3LL * 1024 * 1024 * 1024)
                duration_guess = 6600.0f;
            else if (s->file_size < 8LL * 1024 * 1024 * 1024)
                duration_guess = 7200.0f;
            else
                duration_guess = 9000.0f;
            s->estimated_bitrate_bps = (float)s->file_size / duration_guess;

            int startup_bytes = (int)(s->estimated_bitrate_bps * 2.0f);
            startup_bytes = std::max(startup_bytes, s->piece_length);
            s->critical_startup_pieces = std::clamp(startup_bytes / s->piece_length, 1, 5);

            TB_LOG("ADAPTIVE: file_size=%lldMB piece=%dKB bitrate_est=%.0fKB/s critical_pieces=%d",
                   (long long)(s->file_size / (1024 * 1024)), s->piece_length / 1024,
                   s->estimated_bitrate_bps / 1024.0f, s->critical_startup_pieces);
        }

        int64_t cache_capacity = (max_cache_bytes > 0)            ? max_cache_bytes
                                 : (sw->bt_config.cache_size > 0) ? sw->bt_config.cache_size
                                                                  : (64 * 1024 * 1024);
        s->cache = std::make_unique<TorrCache>();
        s->cache->init(cache_capacity, s->piece_length, ti->num_pieces(), handle);
        if (sw->bt_config.connections_limit > 0)
        {
            s->cache->connections_limit = sw->bt_config.connections_limit;
            try
            {
                handle.set_max_connections(sw->bt_config.connections_limit);
            }
            catch (...)
            {
            }
        }
        if (sw->bt_config.reader_read_ahead >= 5 && sw->bt_config.reader_read_ahead <= 100)
            s->cache->reader_read_ahead_pct = sw->bt_config.reader_read_ahead;

        try
        {
            handle.unset_flags(lt::torrent_flags::stop_when_ready);

            std::vector<lt::download_priority_t> prios(
                (size_t)ti->num_pieces(), lt::dont_download);

            int crit = s->critical_startup_pieces;
            for (int p = s->start_piece; p <= std::min(s->start_piece + crit - 1, s->end_piece); ++p)
                prios[p] = lt::top_priority;

            for (int p = s->start_piece + crit; p <= s->head_end_piece; ++p)
                prios[p] = lt::download_priority_t(4);

            for (int p = s->tail_start_piece; p <= s->end_piece; ++p)
                prios[p] = lt::download_priority_t(5);

            handle.prioritize_pieces(prios);

            for (int i = 0; i < crit && s->start_piece + i <= s->end_piece; ++i)
                handle.set_piece_deadline(
                    lt::piece_index_t(s->start_piece + i), i * 50);
            for (int p = s->tail_start_piece; p <= s->end_piece; ++p)
                handle.set_piece_deadline(lt::piece_index_t(p), 1000);

            handle.resume();

            handle.unset_flags(lt::torrent_flags::sequential_download);

            lt::torrent_status ts = handle.status(lt::torrent_handle::query_pieces);
            for (int p = s->start_piece; p <= s->end_piece; ++p)
            {
                if (ts.pieces.get_bit(lt::piece_index_t(p)))
                {
                    s->pieces_have.insert(p);
                    auto *cp = s->cache->get_piece(p);
                    if (cp)
                        cp->mark_complete();
                    bool is_head = (p <= s->start_piece + 1);
                    bool is_tail = (p >= s->tail_start_piece);
                    if (is_head || is_tail)
                    {
                        try
                        {
                            handle.read_piece(lt::piece_index_t(p));
                        }
                        catch (...)
                        {
                        }
                    }
                }
            }
        }
        catch (...)
        {
        }

        lt_stream_id sid = s->id;
        StreamEngine *raw = s.get();

        {
            std::lock_guard<std::mutex> lk(sw->streams_mu);
            sw->streams[sid] = std::move(s);
        }

        raw->server_thread = std::thread([sw, raw]()
                                         { run_http_server(sw, raw); });

        for (int i = 0; i < 200 && !raw->running.load(); ++i)
            std::this_thread::sleep_for(chr::milliseconds(10));

        if (!raw->running.load())
        {
            raw->active = false;
            if (raw->server_thread.joinable())
                raw->server_thread.join();
            std::lock_guard<std::mutex> lk(sw->streams_mu);
            sw->streams.erase(sid);
            set_err("HTTP server failed to bind");
            return -1;
        }

        set_err("");
        return sid;
    }

    TORRENT_API void lt_stop_stream(lt_session_t session, lt_stream_id sid)
    {
        if (!session)
            return;
        auto *sw = to_sw(session);
        std::unique_ptr<StreamEngine> stream;
        {
            std::lock_guard<std::mutex> lk(sw->streams_mu);
            auto it = sw->streams.find(sid);
            if (it == sw->streams.end())
                return;
            stream = std::move(it->second);
            sw->streams.erase(it);
        }

        try
        {
            lt_torrent_id tid = stream->torrent_id;
            stream->active = false;
            stream->preloading.store(false);
            stream->wake_all();

            if (stream->listen_sock != SOCKET_INVALID)
            {
                CLOSESOCKET(stream->listen_sock);
                stream->listen_sock = SOCKET_INVALID;
            }

            if (stream->preload_thread.joinable())
                stream->preload_thread.join();
            if (stream->server_thread.joinable())
                stream->server_thread.join();
            if (stream->cache)
                stream->cache->close();

            bool ephemeral = false;
            {
                std::lock_guard<std::mutex> lk(sw->mu);
                if (sw->ephemeral_torrents.count(tid))
                {
                    ephemeral = true;
                    sw->ephemeral_torrents.erase(tid);
                }
            }

            if (ephemeral)
            {
                lt_remove_torrent(session, tid, 1);
            }
            else
            {
                try
                {
                    auto ti2 = stream->handle.torrent_file();
                    if (ti2)
                    {
                        int nf = ti2->files().num_files();
                        std::vector<lt::download_priority_t> p((size_t)nf, lt::default_priority);
                        stream->handle.prioritize_files(p);
                    }
                }
                catch (...)
                {
                }
            }
        }
        catch (...)
        {
        }
    }

    TORRENT_API int lt_get_stream_status(lt_session_t session,
                                         lt_stream_id sid, lt_stream_status *out)
    {
        if (!session || !out)
            return 0;
        auto *sw = to_sw(session);
        std::lock_guard<std::mutex> lk(sw->streams_mu);
        auto it = sw->streams.find(sid);
        if (it == sw->streams.end())
            return 0;
        fill_stream_status(out, it->second.get());
        return 1;
    }

    TORRENT_API int lt_get_all_stream_statuses(lt_session_t session,
                                               lt_stream_status *out, int max)
    {
        if (!session || !out || max <= 0)
            return 0;
        auto *sw = to_sw(session);
        std::lock_guard<std::mutex> lk(sw->streams_mu);
        int n = 0;
        for (auto &kv : sw->streams)
        {
            if (n >= max)
                break;
            fill_stream_status(&out[n++], kv.second.get());
        }
        return n;
    }

    TORRENT_API void lt_set_download_limit(lt_session_t session, int bps)
    {
        if (!session)
            return;
        lt::settings_pack sp;
        sp.set_int(lt::settings_pack::download_rate_limit, bps);
        to_sw(session)->session.apply_settings(sp);
    }

    TORRENT_API void lt_set_upload_limit(lt_session_t session, int bps)
    {
        if (!session)
            return;
        lt::settings_pack sp;
        sp.set_int(lt::settings_pack::upload_rate_limit, bps);
        to_sw(session)->session.apply_settings(sp);
    }

    TORRENT_API const char *lt_last_error(void) { return g_last_error.c_str(); }
    TORRENT_API const char *lt_version(void) { return LIBTORRENT_VERSION; }

    TORRENT_API int lt_preload_stream(lt_session_t session, lt_stream_id sid,
                                      int64_t preload_bytes)
    {
        if (!session)
            return 0;
        auto *sw = to_sw(session);
        std::lock_guard<std::mutex> lk(sw->streams_mu);
        auto it = sw->streams.find(sid);
        if (it == sw->streams.end())
            return 0;

        auto *s = it->second.get();
        if (s->preloading.load() || !s->active.load())
            return 0;

        if (preload_bytes <= 0)
            preload_bytes = 16 * 1024 * 1024;

        s->preload_thread = std::thread([s, preload_bytes]()
                                        { preload_stream(s, preload_bytes); });
        return 1;
    }

    TORRENT_API void lt_set_cache_settings(lt_session_t session, lt_stream_id sid,
                                           int64_t capacity,
                                           int read_ahead_pct,
                                           int connections_limit)
    {
        if (!session)
            return;
        auto *sw = to_sw(session);
        std::lock_guard<std::mutex> lk(sw->streams_mu);
        auto it = sw->streams.find(sid);
        if (it == sw->streams.end())
            return;

        auto *s = it->second.get();
        if (!s->cache)
            return;

        if (capacity > 0)
            s->cache->capacity = capacity;
        if (read_ahead_pct >= 5 && read_ahead_pct <= 100)
            s->cache->reader_read_ahead_pct = read_ahead_pct;
        if (connections_limit > 0)
        {
            s->cache->connections_limit = connections_limit;
            try
            {
                s->handle.set_max_connections(connections_limit);
            }
            catch (...)
            {
            }
        }
    }

    TORRENT_API void lt_configure_session(lt_session_t session,
                                          const lt_bt_config *config)
    {
        if (!session || !config)
            return;
        auto *sw = to_sw(session);

        lt_bt_config cfg = *config;

        if (cfg.cache_size == 0)
            cfg.cache_size = 64 * 1024 * 1024;
        if (cfg.connections_limit == 0)
            cfg.connections_limit = 25;
        if (cfg.torrent_disconnect_timeout == 0)
            cfg.torrent_disconnect_timeout = 30;
        if (cfg.reader_read_ahead < 5)
            cfg.reader_read_ahead = 5;
        if (cfg.reader_read_ahead > 100)
            cfg.reader_read_ahead = 100;
        if (cfg.preload_cache < 0)
            cfg.preload_cache = 0;
        if (cfg.preload_cache > 100)
            cfg.preload_cache = 100;

        sw->bt_config = cfg;

        lt::settings_pack sp;

        if (!cfg.enable_ipv6)
        {
            sp.set_str(lt::settings_pack::listen_interfaces, "0.0.0.0:6881");
        }

        if (cfg.disable_utp && !cfg.disable_tcp)
        {
            sp.set_bool(lt::settings_pack::enable_outgoing_utp, false);
            sp.set_bool(lt::settings_pack::enable_incoming_utp, false);
        }

        sp.set_bool(lt::settings_pack::enable_upnp, !cfg.disable_upnp);
        sp.set_bool(lt::settings_pack::enable_natpmp, !cfg.disable_upnp);

        sp.set_bool(lt::settings_pack::enable_dht, !cfg.disable_dht);

        if (cfg.disable_upload)
        {
            sp.set_int(lt::settings_pack::upload_rate_limit, 1);
            sp.set_int(lt::settings_pack::unchoke_slots_limit, 0);
            sp.set_int(lt::settings_pack::active_seeds, 0);
        }

        sp.set_int(lt::settings_pack::connections_limit, cfg.connections_limit * 20);

        if (cfg.force_encrypt)
        {
            sp.set_int(lt::settings_pack::in_enc_policy, lt::settings_pack::pe_forced);
            sp.set_int(lt::settings_pack::out_enc_policy, lt::settings_pack::pe_forced);
        }
        else
        {
            sp.set_int(lt::settings_pack::in_enc_policy, lt::settings_pack::pe_enabled);
            sp.set_int(lt::settings_pack::out_enc_policy, lt::settings_pack::pe_enabled);
        }

        if (cfg.download_rate_limit > 0)
        {
            sp.set_int(lt::settings_pack::download_rate_limit, cfg.download_rate_limit * 1024);
        }
        else
        {
            sp.set_int(lt::settings_pack::download_rate_limit, 0);
        }
        if (cfg.upload_rate_limit > 0)
        {
            sp.set_int(lt::settings_pack::upload_rate_limit, cfg.upload_rate_limit * 1024);
        }
        else if (!cfg.disable_upload)
        {
            sp.set_int(lt::settings_pack::upload_rate_limit, 0);
        }

        sp.set_str(lt::settings_pack::user_agent, "qBittorrent/4.3.9");
        sp.set_str(lt::settings_pack::peer_fingerprint, "-qB4390-");
        sp.set_str(lt::settings_pack::handshake_client_version, "qBittorrent/4.3.9");

        if (cfg.active_downloads_limit >= 0)
        {
            sp.set_int(lt::settings_pack::active_downloads, cfg.active_downloads_limit);
        }
        if (cfg.active_seeds_limit >= 0)
        {
            sp.set_int(lt::settings_pack::active_seeds, cfg.active_seeds_limit);
        }
        if (cfg.active_limit >= 0)
        {
            sp.set_int(lt::settings_pack::active_limit, cfg.active_limit);
        }

        try
        {
            sw->session.apply_settings(sp);
        }
        catch (...)
        {
        }

        {
            std::lock_guard<std::mutex> lk(sw->streams_mu);
            for (auto &kv : sw->streams)
            {
                auto *s = kv.second.get();
                if (!s || !s->cache)
                    continue;

                if (cfg.cache_size > 0)
                    s->cache->capacity = cfg.cache_size;
                if (cfg.reader_read_ahead >= 5 && cfg.reader_read_ahead <= 100)
                    s->cache->reader_read_ahead_pct = cfg.reader_read_ahead;
                if (cfg.connections_limit > 0)
                {
                    s->cache->connections_limit = cfg.connections_limit;
                    try
                    {
                        s->handle.set_max_connections(cfg.connections_limit);
                    }
                    catch (...)
                    {
                    }
                }
            }
        }
    }

    TORRENT_API void lt_get_default_config(lt_bt_config *out)
    {
        if (!out)
            return;
        out->cache_size = 64 * 1024 * 1024;
        out->reader_read_ahead = 95;
        out->preload_cache = 50;
        out->connections_limit = 25;
        out->torrent_disconnect_timeout = 30;
        out->force_encrypt = 0;
        out->disable_tcp = 0;
        out->disable_utp = 0;
        out->disable_upload = 0;
        out->disable_dht = 0;
        out->disable_upnp = 0;
        out->enable_ipv6 = 0;
        out->download_rate_limit = 0;
        out->upload_rate_limit = 0;
        out->peers_listen_port = 0;
        out->active_downloads_limit = 3;
        out->active_seeds_limit = 3;
        out->active_limit = 5;
        out->responsive_mode = 1;
    }

    TORRENT_API int lt_get_active_streams(lt_session_t session)
    {
        (void)session;
        return StreamEngine::active_streams.load();
    }

    TORRENT_API void lt_set_torrent_download_limit(lt_session_t session, lt_torrent_id id, int bytes_per_sec)
    {
        if (!session)
            return;
        auto *sw = to_sw(session);
        std::lock_guard<std::mutex> lk(sw->mu);
        auto it = sw->handles.find(id);
        if (it == sw->handles.end() || !it->second.is_valid())
            return;
        try
        {
            it->second.set_download_limit(bytes_per_sec);
        }
        catch (...)
        {
        }
    }

    TORRENT_API void lt_set_torrent_upload_limit(lt_session_t session, lt_torrent_id id, int bytes_per_sec)
    {
        if (!session)
            return;
        auto *sw = to_sw(session);
        std::lock_guard<std::mutex> lk(sw->mu);
        auto it = sw->handles.find(id);
        if (it == sw->handles.end() || !it->second.is_valid())
            return;
        try
        {
            it->second.set_upload_limit(bytes_per_sec);
        }
        catch (...)
        {
        }
    }

    TORRENT_API void lt_set_torrent_auto_managed(lt_session_t session, lt_torrent_id id, int auto_managed)
    {
        if (!session)
            return;
        auto *sw = to_sw(session);
        std::lock_guard<std::mutex> lk(sw->mu);
        auto it = sw->handles.find(id);
        if (it == sw->handles.end() || !it->second.is_valid())
            return;
        try
        {
            if (auto_managed)
            {
                it->second.set_flags(lt::torrent_flags::auto_managed);
            }
            else
            {
                it->second.unset_flags(lt::torrent_flags::auto_managed);
            }
        }
        catch (...)
        {
        }
    }

    TORRENT_API void lt_queue_up(lt_session_t session, lt_torrent_id id)
    {
        if (!session)
            return;
        auto *sw = to_sw(session);
        std::lock_guard<std::mutex> lk(sw->mu);
        auto it = sw->handles.find(id);
        if (it == sw->handles.end() || !it->second.is_valid())
            return;
        try
        {
            it->second.queue_position_up();
        }
        catch (...)
        {
        }
    }

    TORRENT_API void lt_queue_down(lt_session_t session, lt_torrent_id id)
    {
        if (!session)
            return;
        auto *sw = to_sw(session);
        std::lock_guard<std::mutex> lk(sw->mu);
        auto it = sw->handles.find(id);
        if (it == sw->handles.end() || !it->second.is_valid())
            return;
        try
        {
            it->second.queue_position_down();
        }
        catch (...)
        {
        }
    }

    TORRENT_API void lt_queue_top(lt_session_t session, lt_torrent_id id)
    {
        if (!session)
            return;
        auto *sw = to_sw(session);
        std::lock_guard<std::mutex> lk(sw->mu);
        auto it = sw->handles.find(id);
        if (it == sw->handles.end() || !it->second.is_valid())
            return;
        try
        {
            it->second.queue_position_top();
        }
        catch (...)
        {
        }
    }

    TORRENT_API void lt_queue_bottom(lt_session_t session, lt_torrent_id id)
    {
        if (!session)
            return;
        auto *sw = to_sw(session);
        std::lock_guard<std::mutex> lk(sw->mu);
        auto it = sw->handles.find(id);
        if (it == sw->handles.end() || !it->second.is_valid())
            return;
        try
        {
            it->second.queue_position_bottom();
        }
        catch (...)
        {
        }
    }

} // extern "C"
