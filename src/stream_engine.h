#pragma once

#include "common_types.h"

// forward declarations
struct TorrCache;

// PieceRange — port of torrstor.Range
struct PieceRange {
    int     start = 0;
    int     end_  = 0;
    int     file_index = -1;     // which file this range is for
    int64_t file_offset = 0;     // file's byte offset in torrent
    int64_t file_length = 0;     // file's total byte length
};

// CachePiece — port of torrstor.Piece + torrstor.MemPiece (memory-only mode)
struct CachePiece {
    int     id = 0;
    int64_t size = 0;
    bool    complete = false;
    int64_t accessed = 0;   // unix timestamp — port of Piece.Accessed

    std::vector<char> buffer;  // port of MemPiece.buffer
    std::shared_mutex mu;      // port of MemPiece.mu (RWMutex)

    TorrCache* cache = nullptr;

    CachePiece() = default;
    CachePiece(int piece_id, TorrCache* c) : id(piece_id), cache(c) {}

    int write_at(const char* b, int len, int64_t off);
    int read_at(char* b, int len, int64_t off);
    void mark_complete() { complete = true; }
    void mark_not_complete() { complete = false; }
    void release();
};

// TorrReader — port of torrstor.Reader
struct TorrReader {
    int     reader_id = 0;
    int64_t offset = 0;          // port of Reader.offset
    int64_t readahead = 0;       // port of Reader.readahead
    bool    is_closed = false;   // port of Reader.isClosed
    int64_t last_access = 0;     // port of Reader.lastAccess (unix timestamp)
    bool    is_use = true;       // port of Reader.isUse

    // file info for this reader
    int     file_index = -1;
    int64_t file_offset = 0;     // byte offset of file in torrent
    int64_t file_length = 0;     // file length

    TorrCache* cache = nullptr;
    std::mutex mu;               // port of Reader.mu

    TorrReader() = default;

    int get_piece_num(int64_t off) const;
    int get_reader_piece() const { return get_piece_num(offset); }
    int get_reader_rah_piece() const { return get_piece_num(offset + readahead); }
    void get_offset_range(int64_t& begin_off, int64_t& end_off) const;
    PieceRange get_pieces_range() const;
    void check_reader();
    void reader_on();
    void reader_off();
    int get_use_readers() const;
    void set_readahead(int64_t length);
    void close();

    static int64_t now_unix() {
        return (int64_t)chr::duration_cast<chr::seconds>(
            chr::system_clock::now().time_since_epoch()).count();
    }
};

// TorrCache — the core streaming cache
struct TorrCache {
    int64_t capacity = 0;        // port of Cache.capacity (bytes)
    int64_t filled = 0;          // port of Cache.filled
    int64_t piece_length = 0;    // port of Cache.pieceLength
    int     piece_count = 0;     // port of Cache.pieceCount

    std::unordered_map<int, std::unique_ptr<CachePiece>> pieces; // port of Cache.pieces

    std::unordered_map<int, TorrReader*> readers;  // port of Cache.readers (reader_id → reader)
    mutable std::mutex mu_readers;                 // port of Cache.muReaders

    std::atomic<bool> is_remove{false};  // port of Cache.isRemove
    std::atomic<bool> is_closed{false};  // port of Cache.isClosed
    std::mutex mu_remove;                // port of Cache.muRemove

    lt::torrent_handle handle;  // replaces Cache.torrent (anacrolix *torrent.Torrent)

    int reader_read_ahead_pct = 95;  // port of BTsets.ReaderReadAHead (5-100%)
    int connections_limit = 25;      // port of BTsets.ConnectionsLimit

    void init(int64_t cap, int64_t pl, int pc, const lt::torrent_handle& h);
    CachePiece* get_piece(int index);
    void remove_piece(CachePiece* piece);
    void adjust_readahead(int64_t ra);
    void clean_pieces() { return; }
    std::vector<CachePiece*> get_removable_pieces();
    void set_load_priority(const std::vector<PieceRange>& ranges) { return; }
    bool is_in_file_begin_end(const std::vector<PieceRange>& ranges, int id) const;
    TorrReader* new_reader(int reader_id, int file_idx, int64_t file_off, int64_t file_len);
    int get_use_readers() const;
    int reader_count() const;
    void close_reader(TorrReader* r);
    void clear_priority_impl() { return; }
    void close();
    int64_t get_capacity() const { return capacity; }
    void get_state(int64_t& out_capacity, int64_t& out_filled, int& out_pieces_count, int& out_readers) const;
};

// helper functions for ranges
bool in_ranges(const std::vector<PieceRange>& ranges, int ind);
std::vector<PieceRange> merge_ranges(std::vector<PieceRange> ranges);

// StreamEngine structure
struct StreamEngine {
    lt_stream_id  id;
    lt_torrent_id torrent_id;
    int           file_index;

    lt::torrent_handle                      handle;
    std::shared_ptr<const lt::torrent_info> ti;

    int64_t file_offset;
    int64_t file_size;
    int     piece_length;
    int     start_piece;
    int     end_piece;
    int     total_pieces;

    std::unique_ptr<TorrCache> cache;

    std::atomic<int64_t> read_head{0};
    std::atomic<int32_t> stream_state{LT_STREAM_BUFFERING};
    std::atomic<int32_t> seek_generation{0};

    static constexpr int64_t FIXED_READAHEAD = 16 * 1024 * 1024;
    static constexpr int64_t PROTECT_BYTES = 8 * 1024 * 1024;
    int head_end_piece;
    int tail_start_piece;

    std::thread       server_thread;
    std::atomic<bool> running{false};
    socket_t          listen_sock = SOCKET_INVALID;
    int               server_port = 0;

    static std::atomic<int32_t> active_streams;

    std::mutex                         clients_mu;
    std::unordered_map<int, std::thread> client_threads;
    std::unordered_map<int, socket_t>    client_sockets;   // track sockets for forced shutdown
    std::unordered_set<int>              finished_clients;  // signal thread completion
    std::atomic<int> next_reader_id{1};

    std::mutex              piece_mu;
    std::condition_variable piece_cv;
    std::set<int>           pieces_have;

    std::mutex              read_mu;
    std::condition_variable read_cv;
    std::unordered_map<int, ReadResult> read_results;

    std::atomic<int64_t> preload_size{0};
    std::atomic<int64_t> preloaded_bytes{0};
    std::atomic<bool>    preloading{false};
    std::thread          preload_thread;

    std::atomic<double> download_speed{0};
    std::atomic<double> upload_speed{0};

    static constexpr int TRAILING_WINDOW = 3;
    std::deque<int>  trailing_pieces;
    std::mutex       trailing_mu;

    float estimated_bitrate_bps = 625000.0f;
    int   critical_startup_pieces = 2;  // computed at init

    std::atomic<bool> active{true};

    int byte_to_piece(int64_t off) const;
    void piece_file_range(int p, int64_t& beg, int64_t& end_) const;
    std::string make_url() const;
    void on_piece_finished(int p);
    void on_piece_read(int p, const char* data, int size, bool ok);
    void on_hash_failed(int p);
    void wake_all();
};

bool wait_for_piece(StreamEngine* s, int piece, int timeout_ms, int gen = -1);
ReadResult read_piece_data(StreamEngine* s, int piece, int timeout_ms = 5000, int gen = -1);
void preload_stream(StreamEngine* s, int64_t preload_bytes);
void fill_stream_status(lt_stream_status* out, const StreamEngine* s);
