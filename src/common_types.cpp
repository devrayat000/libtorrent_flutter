#include "common_types.h"

FILE* g_logfile = nullptr;
std::mutex g_log_mu;

void tb_log_init() {
    if (!g_logfile) {
        g_logfile = fopen("torrent_debug.log", "w");
        if (g_logfile) {
            setvbuf(g_logfile, nullptr, _IONBF, 0); // unbuffered
        }
    }
}

thread_local std::string g_last_error;

void set_err(const std::string& s) {
    g_last_error = s;
}

bool is_streamable(const std::string& name) {
    static const char* exts[] = {
        ".mkv",".mp4",".avi",".mov",".wmv",".flv",".webm",
        ".m4v",".ts",".m2ts",".mpg",".mpeg",".mp3",".aac",
        ".flac",".opus",".ogg",".wav", nullptr
    };
    auto d = name.rfind('.');
    if (d == std::string::npos) return false;
    std::string e = name.substr(d);
    for (auto& c : e) c = (char)tolower((unsigned char)c);
    for (int i = 0; exts[i]; ++i) if (e == exts[i]) return true;
    return false;
}

std::string get_mime(const std::string& name) {
    auto d = name.rfind('.');
    if (d == std::string::npos) return "video/mp4";
    std::string e = name.substr(d);
    for (auto& c : e) c = (char)tolower((unsigned char)c);
    if (e == ".mkv")  return "video/x-matroska";
    if (e == ".mp4" || e == ".m4v") return "video/mp4";
    if (e == ".avi")  return "video/x-msvideo";
    if (e == ".mov")  return "video/quicktime";
    if (e == ".webm") return "video/webm";
    if (e == ".ts" || e == ".m2ts") return "video/mp2t";
    if (e == ".flv")  return "video/x-flv";
    if (e == ".wmv")  return "video/x-ms-wmv";
    if (e == ".mpg" || e == ".mpeg") return "video/mpeg";
    if (e == ".mp3")  return "audio/mpeg";
    if (e == ".flac") return "audio/flac";
    if (e == ".aac")  return "audio/aac";
    if (e == ".ogg" || e == ".opus") return "audio/ogg";
    if (e == ".wav")  return "audio/wav";
    return "application/octet-stream";
}
