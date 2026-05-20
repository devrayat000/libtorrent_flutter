#include "session_wrapper.h"

SessionWrapper* to_sw(lt_session_t h) {
    return reinterpret_cast<SessionWrapper*>(h);
}

void fill_status(lt_torrent_status& out, int64_t id, const lt::torrent_status& st) {
    out.id    = id;
    out.state = static_cast<int32_t>(st.state);

    if ((st.state == lt::torrent_status::finished ||
         st.state == lt::torrent_status::seeding) && st.progress < 0.999f)
        out.state = LT_STATE_DOWNLOADING;

    out.progress      = st.progress;
    out.download_rate = st.download_rate;
    out.upload_rate   = st.upload_rate;
    out.total_done    = st.total_done;
    out.total_wanted  = st.total_wanted;
    out.total_uploaded = st.total_payload_upload;
    out.num_peers     = st.num_peers;
    out.num_seeds     = st.num_seeds;
    out.num_pieces    = (int32_t)st.num_pieces;

    int have = 0;
    for (int i = 0; i < (int)st.pieces.size(); ++i)
        if (st.pieces.get_bit(lt::piece_index_t(i))) have++;
    out.pieces_done = (int32_t)have;

    out.is_paused   = (st.flags & lt::torrent_flags::paused) ? 1 : 0;
    out.is_finished = (st.progress >= 0.999f && st.is_finished) ? 1 : 0;
    out.has_metadata = st.has_metadata ? 1 : 0;

    int qp = static_cast<int>(st.queue_position);
    out.queue_position = (qp < 0) ? -1 : qp;

    out.download_limit  = st.handle.download_limit();
    out.upload_limit    = st.handle.upload_limit();
    out.is_auto_managed = (st.flags & lt::torrent_flags::auto_managed) ? 1 : 0;

    std::string name = st.name;
    if (st.has_metadata) {
        auto ti = st.handle.torrent_file();
        if (ti) name = ti->name();
    }
    std::strncpy(out.name, name.c_str(), sizeof(out.name) - 1);
    out.name[sizeof(out.name) - 1] = 0;

    std::strncpy(out.save_path, st.save_path.c_str(), sizeof(out.save_path) - 1);
    out.save_path[sizeof(out.save_path) - 1] = 0;

    if (st.errc) {
        std::string e = st.errc.message();
        std::strncpy(out.error_msg, e.c_str(), sizeof(out.error_msg) - 1);
        out.error_msg[sizeof(out.error_msg) - 1] = 0;
        out.state = LT_STATE_ERROR;
    } else {
        out.error_msg[0] = 0;
    }
}

SessionWrapper::SessionWrapper(lt::settings_pack sp) : session(std::move(sp)) {}

void SessionWrapper::init_default_config() {
    bt_config.cache_size = 64 * 1024 * 1024;       // 64 MB
    bt_config.reader_read_ahead = 95;               // 95%
    bt_config.preload_cache = 50;                   // 50%
    bt_config.connections_limit = 25;
    bt_config.torrent_disconnect_timeout = 30;      // 30 seconds
    bt_config.force_encrypt = 0;                    // pe_enabled
    bt_config.disable_tcp = 0;
    bt_config.disable_utp = 0;
    bt_config.disable_upload = 0;
    bt_config.disable_dht = 0;
    bt_config.disable_upnp = 0;
    bt_config.enable_ipv6 = 0;
    bt_config.download_rate_limit = 0;              // unlimited
    bt_config.upload_rate_limit = 0;                // unlimited
    bt_config.peers_listen_port = 0;                // random
    bt_config.responsive_mode = 1;                  // enabled by default
}

int64_t SessionWrapper::id_for_handle(const lt::torrent_handle& h) {
    std::lock_guard<std::mutex> lk(mu);
    for (auto& kv : handles)
        if (kv.second == h) return kv.first;
    return -1;
}

void SessionWrapper::start_alert_thread() {
    alert_running = true;
    alert_thread = std::thread([this]() { process_alerts(); });
}

void SessionWrapper::process_alerts() {
    while (alert_running.load()) {
        try {
            if (!session.wait_for_alert(lt::milliseconds(100)))
                continue;
            std::vector<lt::alert*> alerts;
            session.pop_alerts(&alerts);

            for (auto* a : alerts) {
                if (!a) continue;
                try {
                    if (auto* rpa = lt::alert_cast<lt::read_piece_alert>(a)) {
                        int p = static_cast<int>(rpa->piece);
                        std::lock_guard<std::mutex> slk(streams_mu);
                        for (auto& kv : streams) {
                            auto& s = kv.second;
                            if (!s->active || s->handle != rpa->handle) continue;
                            s->on_piece_read(p,
                                rpa->error ? nullptr : rpa->buffer.get(),
                                rpa->error ? 0 : rpa->size,
                                !rpa->error);
                            break;
                        }
                        continue;
                    }

                    if (auto* pfa = lt::alert_cast<lt::piece_finished_alert>(a)) {
                        int p = static_cast<int>(pfa->piece_index);
                        std::lock_guard<std::mutex> slk(streams_mu);
                        for (auto& kv : streams) {
                            auto& s = kv.second;
                            if (!s->active || s->handle != pfa->handle) continue;
                            if (p >= s->start_piece && p <= s->end_piece)
                                s->on_piece_finished(p);
                            break;
                        }
                    }

                    else if (auto* hf = lt::alert_cast<lt::hash_failed_alert>(a)) {
                        int p = static_cast<int>(hf->piece_index);
                        std::lock_guard<std::mutex> slk(streams_mu);
                        for (auto& kv : streams) {
                            auto& s = kv.second;
                            if (!s->active || s->handle != hf->handle) continue;
                            if (p >= s->start_piece && p <= s->end_piece)
                                s->on_hash_failed(p);
                            break;
                        }
                    }

                    else if (auto* mra = lt::alert_cast<lt::metadata_received_alert>(a)) {
                        int64_t mid = id_for_handle(mra->handle);
                        bool is_ephemeral = false;
                        {
                            std::lock_guard<std::mutex> lk(mu);
                            is_ephemeral = ephemeral_torrents.count(mid) > 0;
                        }
                        if (is_ephemeral) {
                            try {
                                auto ti = mra->handle.torrent_file();
                                if (ti) {
                                    int nf = ti->files().num_files();
                                    std::vector<lt::download_priority_t> p(
                                        (size_t)nf, lt::dont_download);
                                    mra->handle.prioritize_files(p);
                                    mra->handle.pause();
                                }
                            } catch (...) {}
                        }
                    }

                    lt_torrent_id tid = -1;
                    if (auto* ta = dynamic_cast<lt::torrent_alert*>(a))
                        tid = id_for_handle(ta->handle);

                    {
                        std::lock_guard<std::mutex> ql(dart_queue_mu);
                        if (dart_queue.size() < 2048)
                            dart_queue.push_back({a->type(), tid, a->message()});
                    }

                    {
                        std::lock_guard<std::mutex> cl(cb_mu);
                        if (dart_callback)
                            dart_callback(a->type(), tid, a->message().c_str(), dart_user_data);
                    }
                } catch (...) {}
            }

        } catch (...) {}
    }
}
