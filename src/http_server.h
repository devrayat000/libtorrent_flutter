#pragma once

#include "stream_engine.h"

// Forward declaration if needed, but wait, http_server compiles with StreamEngine* directly.
struct SessionWrapper;

struct RangeReq {
    int64_t start = -1;
    int64_t end   = -1;
    bool    valid = false;
};

// socket helpers
RangeReq parse_range(const char* buf, int len);
int send_all(socket_t sock, const char* data, int len);

// core HTTP serving functions
bool serve_range(StreamEngine* s, TorrReader* reader, socket_t cli, int64_t range_start, int64_t range_end);
void handle_connection(StreamEngine* s, socket_t cli, int reader_id);
void run_http_server(SessionWrapper* sw, StreamEngine* stream);
