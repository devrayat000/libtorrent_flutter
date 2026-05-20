#pragma once

#include "stream_engine.h"

// Forward declaration of SessionWrapper
struct SessionWrapper;

// to_sw helper
SessionWrapper* to_sw(lt_session_t h);

// fill_status helper
void fill_status(lt_torrent_status& out, int64_t id, const lt::torrent_status& st);

struct SessionWrapper {
    lt::session session;

    std::mutex mu;
    std::unordered_map<int64_t, lt::torrent_handle> handles;
    std::unordered_set<int64_t> ephemeral_torrents;
    std::atomic<int64_t> next_id{1};

    std::mutex streams_mu;
    std::unordered_map<int64_t, std::unique_ptr<StreamEngine>> streams;
    std::atomic<int64_t> next_stream_id{1};

    // alert thread — sole consumer of session.pop_alerts()
    std::thread       alert_thread;
    std::atomic<bool> alert_running{false};

    // push callback (called from alert thread)
    lt_alert_callback  dart_callback  = nullptr;
    void*              dart_user_data = nullptr;
    std::mutex         cb_mu;

    // pull queue (for lt_poll_alerts)
    std::mutex              dart_queue_mu;
    std::deque<AlertRecord> dart_queue;

    explicit SessionWrapper(lt::settings_pack sp);

    // ── TorrServer config — port of settings.BTSets (session-level defaults) ──
    lt_bt_config bt_config;

    void init_default_config();
    int64_t id_for_handle(const lt::torrent_handle& h);
    void start_alert_thread();
    void process_alerts();
};
