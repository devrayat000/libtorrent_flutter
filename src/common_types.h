#pragma once

#ifdef _WIN32
  #ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0601
  #endif
#endif

#include "torrent_bridge.h"

#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/version.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/download_priority.hpp>

// ── cross-platform sockets ──────────────────────────────────────────────────────
#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef SOCKET socket_t;
  #define SOCKET_INVALID  INVALID_SOCKET
  #define CLOSESOCKET(s)  ::closesocket(s)
  #define INIT_SOCKETS()  { WSADATA _w; ::WSAStartup(MAKEWORD(2,2), &_w); }
  typedef int socklen_t_;
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <signal.h>
  typedef int socket_t;
  #define SOCKET_INVALID  (-1)
  #define CLOSESOCKET(s)  ::close(s)
  #define INIT_SOCKETS()  { signal(SIGPIPE, SIG_IGN); }
  typedef socklen_t socklen_t_;
#endif

#include <thread>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>
#include <deque>
#include <memory>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cinttypes>
#include <functional>
#include <cstdio>

namespace lt  = libtorrent;
namespace chr = std::chrono;

// ── debug logging ───────────────────────────────────────────────────────────────
extern FILE* g_logfile;
extern std::mutex g_log_mu;

void tb_log_init();

#define TB_LOG(fmt, ...) do { \
    std::lock_guard<std::mutex> _lk(g_log_mu); \
    tb_log_init(); \
    if (g_logfile) { \
        auto _now = chr::steady_clock::now(); \
        auto _ms = chr::duration_cast<chr::milliseconds>(_now.time_since_epoch()).count() % 100000; \
        fprintf(g_logfile, "[%05lld] " fmt "\n", (long long)_ms, ##__VA_ARGS__); \
    } \
} while(0)

// ── error handling ──────────────────────────────────────────────────────────────
extern thread_local std::string g_last_error;
void set_err(const std::string& s);

// ── file extension checks ───────────────────────────────────────────────────────
bool is_streamable(const std::string& name);

// ── mime mappings ───────────────────────────────────────────────────────────────
std::string get_mime(const std::string& name);

// ── read result for async piece reads ───────────────────────────────────────────
struct ReadResult {
    std::vector<char> data;
    bool ok = false;
};

// ── alert record for dart queue ─────────────────────────────────────────────────
struct AlertRecord {
    int           type;
    lt_torrent_id torrent_id;
    std::string   message;
};
