#include "stream_engine.h"

// ── PieceRange helpers ────────────────────────────────────────────────────────
bool in_ranges(const std::vector<PieceRange>& ranges, int ind) {
    for (auto& r : ranges) {
        if (ind >= r.start && ind <= r.end_)
            return true;
    }
    return false;
}

std::vector<PieceRange> merge_ranges(std::vector<PieceRange> ranges) {
    if (ranges.size() <= 1) return ranges;

    std::sort(ranges.begin(), ranges.end(), [](const PieceRange& a, const PieceRange& b) {
        if (a.start < b.start) return true;
        if (a.start == b.start && a.end_ < b.end_) return true;
        return false;
    });

    int j = 0;
    for (int i = 1; i < (int)ranges.size(); ++i) {
        if (ranges[j].end_ >= ranges[i].start) {
            if (ranges[j].end_ < ranges[i].end_)
                ranges[j].end_ = ranges[i].end_;
        } else {
            ++j;
            ranges[j] = ranges[i];
        }
    }
    ranges.resize(j + 1);
    return ranges;
}

// ── CachePiece methods ────────────────────────────────────────────────────────
int CachePiece::write_at(const char* b, int len, int64_t off) {
    std::unique_lock<std::shared_mutex> lk(mu);

    if (buffer.empty()) {
        buffer.resize((size_t)cache->piece_length, 0);
    }

    if (off < 0 || (size_t)off >= buffer.size()) return 0;
    int n = std::min(len, (int)(buffer.size() - (size_t)off));
    std::memcpy(buffer.data() + off, b, (size_t)n);
    size += (int64_t)n;
    if (size > cache->piece_length) size = cache->piece_length;
    accessed = TorrReader::now_unix();
    return n;
}

int CachePiece::read_at(char* b, int len, int64_t off) {
    std::shared_lock<std::shared_mutex> lk(mu);

    if (buffer.empty()) return -1; // EOF equivalent

    int avail = (int)buffer.size() - (int)off;
    if (avail <= 0) return -1;
    int n = std::min(len, avail);
    std::memcpy(b, buffer.data() + off, (size_t)n);
    accessed = TorrReader::now_unix();

    return n;
}

void CachePiece::release() {
    {
        std::unique_lock<std::shared_mutex> lk(mu);
        buffer.clear();
        buffer.shrink_to_fit();
        size = 0;
        complete = false;
    }
}

// ── TorrReader methods ────────────────────────────────────────────────────────
int TorrReader::get_piece_num(int64_t off) const {
    if (!cache || cache->piece_length <= 0) return 0;
    return (int)((off + file_offset) / cache->piece_length);
}

void TorrReader::get_offset_range(int64_t& begin_off, int64_t& end_off) const {
    int64_t num_readers = (int64_t)get_use_readers();
    if (num_readers == 0) num_readers = 1;

    begin_off = offset;  // nothing kept behind playback position
    end_off   = offset + (cache->capacity / num_readers);

    if (begin_off < 0) begin_off = 0;
    if (end_off > file_length) end_off = file_length;
}

PieceRange TorrReader::get_pieces_range() const {
    int64_t start_off, end_off;
    get_offset_range(start_off, end_off);
    PieceRange r;
    r.start = get_piece_num(start_off);
    r.end_  = get_piece_num(end_off);
    r.file_index = file_index;
    r.file_offset = file_offset;
    r.file_length = file_length;
    return r;
}

void TorrReader::check_reader() {
    if (!cache) return;
    int64_t now = now_unix();
    if (now > last_access + 60 && cache->readers.size() > 1) {
        reader_off();
    } else {
        reader_on();
    }
}

void TorrReader::reader_on() {
    std::lock_guard<std::mutex> lk(mu);
    if (!is_use) {
        is_use = true;
    }
}

void TorrReader::reader_off() {
    std::lock_guard<std::mutex> lk(mu);
    if (is_use) {
        readahead = 0;
        is_use = false;
    }
}

int TorrReader::get_use_readers() const {
    if (!cache) return 0;
    int count = 0;
    for (auto& kv : cache->readers)
        if (kv.second && kv.second->is_use) count++;
    return count;
}

void TorrReader::set_readahead(int64_t length) {
    if (cache && length > cache->capacity)
        length = cache->capacity;
    readahead = length;
}

void TorrReader::close() {
    is_closed = true;
}

// ── TorrCache methods ────────────────────────────────────────────────────────
void TorrCache::init(int64_t cap, int64_t pl, int pc, const lt::torrent_handle& h) {
    capacity = cap;
    if (capacity == 0) capacity = pl * 4;
    piece_length = pl;
    piece_count = pc;
    handle = h;

    for (int i = 0; i < pc; ++i)
        pieces[i] = std::make_unique<CachePiece>(i, this);
}

CachePiece* TorrCache::get_piece(int index) {
    auto it = pieces.find(index);
    return it != pieces.end() ? it->second.get() : nullptr;
}

void TorrCache::remove_piece(CachePiece* piece) {
    if (!is_closed.load())
        piece->release();
}

void TorrCache::adjust_readahead(int64_t ra) {
    if (capacity == 0) capacity = ra * 3;
    std::lock_guard<std::mutex> lk(mu_readers);
    for (auto& kv : readers) {
        if (kv.second) kv.second->set_readahead(ra);
    }
}

std::vector<CachePiece*> TorrCache::get_removable_pieces() {
    std::vector<CachePiece*> pieces_remove;
    int64_t fill = 0;

    std::vector<PieceRange> ranges;
    {
        std::lock_guard<std::mutex> lk(mu_readers);
        for (auto& kv : readers) {
            auto* r = kv.second;
            if (!r) continue;
            r->check_reader();
            if (r->is_use)
                ranges.push_back(r->get_pieces_range());
        }
    }
    ranges = merge_ranges(ranges);

    std::unordered_set<int> current_reader_pieces;
    {
        std::lock_guard<std::mutex> lk2(mu_readers);
        for (auto& kv2 : readers) {
            auto* r2 = kv2.second;
            if (r2 && r2->is_use) {
                int rp = r2->get_reader_piece();
                current_reader_pieces.insert(rp);
                current_reader_pieces.insert(rp + 1);
            }
        }
    }

    for (auto& kv : pieces) {
        int id = kv.first;
        auto* p = kv.second.get();
        if (p->size > 0)
            fill += p->size;

        if (current_reader_pieces.count(id)) continue;

        if (!ranges.empty()) {
            if (!in_ranges(ranges, id)) {
                if (p->size > 0 && !is_in_file_begin_end(ranges, id))
                    pieces_remove.push_back(p);
            }
        } else {
            if (p->size > 0 && !is_in_file_begin_end(ranges, id))
                pieces_remove.push_back(p);
        }
    }

    clear_priority_impl();
    set_load_priority(ranges);

    std::sort(pieces_remove.begin(), pieces_remove.end(),
        [](const CachePiece* a, const CachePiece* b) {
            return a->accessed < b->accessed;
        });

    filled = fill;
    return pieces_remove;
}

bool TorrCache::is_in_file_begin_end(const std::vector<PieceRange>& ranges, int id) const {
    int64_t file_range_not_delete = piece_length;
    if (file_range_not_delete < 8 * 1024 * 1024)
        file_range_not_delete = 8 * 1024 * 1024;

    for (auto& rng : ranges) {
        int ss = (int)(rng.file_offset / piece_length);
        int se = (int)((rng.file_offset + file_range_not_delete) / piece_length);
        int es = (int)((rng.file_offset + rng.file_length - file_range_not_delete) / piece_length);
        int ee = (int)((rng.file_offset + rng.file_length) / piece_length);

        if ((id >= ss && id < se) || (id > es && id <= ee))
            return true;
    }
    return false;
}

TorrReader* TorrCache::new_reader(int reader_id, int file_idx, int64_t file_off, int64_t file_len) {
    auto* r = new TorrReader();
    r->reader_id = reader_id;
    r->file_index = file_idx;
    r->file_offset = file_off;
    r->file_length = file_len;
    r->cache = this;
    r->is_use = true;
    r->set_readahead(0);
    r->last_access = TorrReader::now_unix();

    std::lock_guard<std::mutex> lk(mu_readers);
    readers[reader_id] = r;
    return r;
}

int TorrCache::get_use_readers() const {
    std::lock_guard<std::mutex> lk(mu_readers);
    int count = 0;
    for (auto& kv : readers)
        if (kv.second && kv.second->is_use) count++;
    return count;
}

int TorrCache::reader_count() const {
    std::lock_guard<std::mutex> lk(mu_readers);
    return (int)readers.size();
}

void TorrCache::close_reader(TorrReader* r) {
    if (!r) return;
    if (is_closed.load()) return;
    try {
        {
            std::lock_guard<std::mutex> lk(mu_readers);
            r->close();
            readers.erase(r->reader_id);
        }
        delete r;
    } catch (...) {}
}

void TorrCache::close() {
    if (is_closed.load()) return;
    is_closed.store(true);
    try {
        std::lock_guard<std::mutex> lk(mu_readers);
        for (auto& kv : readers) {
            try { delete kv.second; } catch (...) {}
        }
        readers.clear();
        pieces.clear();
    } catch (...) {}
}

void TorrCache::get_state(int64_t& out_capacity, int64_t& out_filled, int& out_pieces_count, int& out_readers) const {
    int64_t fill = 0;
    for (auto& kv : pieces)
        if (kv.second->size > 0) fill += kv.second->size;
    out_capacity = capacity;
    out_filled = fill;
    out_pieces_count = piece_count;
    out_readers = reader_count();
}

// ── StreamEngine static and methods ───────────────────────────────────────────
std::atomic<int32_t> StreamEngine::active_streams{0};

int StreamEngine::byte_to_piece(int64_t off) const {
    if (piece_length <= 0) return start_piece;
    return (int)((file_offset + off) / piece_length);
}

void StreamEngine::piece_file_range(int p, int64_t& beg, int64_t& end_) const {
    int64_t ps = (int64_t)p * piece_length;
    int64_t pe = ps + piece_length;
    beg  = std::max(ps, file_offset) - file_offset;
    end_ = std::min(pe, file_offset + file_size) - file_offset;
}

std::string StreamEngine::make_url() const {
    std::string hash = "0000000000000000000000000000000000000000";
    try {
        if (handle.is_valid()) {
            auto ih = handle.info_hashes();
            std::stringstream ss;
            if (ih.has_v1()) {
                for (auto b : ih.v1)
                    ss << std::hex << std::setw(2) << std::setfill('0') << (int)(uint8_t)b;
            } else if (ih.has_v2()) {
                for (auto b : ih.v2)
                    ss << std::hex << std::setw(2) << std::setfill('0') << (int)(uint8_t)b;
            }
            hash = ss.str().substr(0, 40);
        }
    } catch (...) {}
    return "http://127.0.0.1:" + std::to_string(server_port)
         + "/stream/" + hash + "/" + std::to_string(file_index);
}

void StreamEngine::on_piece_finished(int p) {
    TB_LOG("on_piece_finished: piece=%d", p);
    {
        std::lock_guard<std::mutex> lk(piece_mu);
        pieces_have.insert(p);
    }
    piece_cv.notify_all();
}

void StreamEngine::on_piece_read(int p, const char* data, int size, bool ok) {
    TB_LOG("on_piece_read: piece=%d ok=%d size=%d", p, (int)ok, size);

    {
        std::lock_guard<std::mutex> lk(read_mu);
        ReadResult r;
        if (ok && data && size > 0) {
            r.data.assign(data, data + size);
            r.ok = true;
        }
        read_results[p] = std::move(r);
    }

    if (ok && data && size > 0 && cache) {
        auto* cp = cache->get_piece(p);
        if (cp) {
            std::unique_lock<std::shared_mutex> lk(cp->mu);
            if (cp->buffer.empty()) {
                cp->buffer.assign(data, data + size);
                cp->size = (int64_t)size;
                cp->complete = true;
                cp->accessed = TorrReader::now_unix();
            }
        }
    }

    read_cv.notify_all();
    piece_cv.notify_all();
}

void StreamEngine::on_hash_failed(int p) {
    {
        std::lock_guard<std::mutex> lk(piece_mu);
        pieces_have.erase(p);
    }
    if (cache) {
        auto* cp = cache->get_piece(p);
        if (cp) cp->mark_not_complete();
    }
    try { handle.piece_priority(lt::piece_index_t(p), lt::top_priority); } catch (...) {}
}

void StreamEngine::wake_all() {
    piece_cv.notify_all();
    read_cv.notify_all();
}

// ── Piece helper / streaming functions ────────────────────────────────────────
bool wait_for_piece(StreamEngine* s, int piece, int timeout_ms, int gen) {
    if (gen < 0) gen = s->seek_generation.load();
    std::unique_lock<std::mutex> lk(s->piece_mu);
    if (s->pieces_have.count(piece)) return true;

    try {
        s->handle.set_piece_deadline(lt::piece_index_t(piece), 0);
        s->handle.piece_priority(lt::piece_index_t(piece), lt::top_priority);
    } catch (...) {}

    return s->piece_cv.wait_for(lk, chr::milliseconds(timeout_ms),
        [&]{ return !s->active.load()
                 || s->seek_generation.load() != gen
                 || s->pieces_have.count(piece) > 0; })
        && s->pieces_have.count(piece) > 0;
}

ReadResult read_piece_data(StreamEngine* s, int piece, int timeout_ms, int gen) {
    if (gen < 0) gen = s->seek_generation.load();

    if (s->cache) {
        auto* cp = s->cache->get_piece(piece);
        if (cp) {
            std::shared_lock<std::shared_mutex> lk(cp->mu);
            if (!cp->buffer.empty() && cp->complete) {
                ReadResult r;
                r.data.assign(cp->buffer.begin(), cp->buffer.end());
                r.ok = true;
                return r;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lk(s->read_mu);
        auto it = s->read_results.find(piece);
        if (it != s->read_results.end() && it->second.ok) {
            ReadResult r = std::move(it->second);
            s->read_results.erase(it);
            return r;
        }
        s->read_results.erase(piece);
    }

    try { s->handle.read_piece(lt::piece_index_t(piece)); } catch (...) {
        return {};
    }

    std::unique_lock<std::mutex> lk(s->read_mu);
    if (s->read_cv.wait_for(lk, chr::milliseconds(timeout_ms),
            [&]{ return !s->active.load()
                     || s->read_results.count(piece) > 0
                     || s->seek_generation.load() != gen; })) {
        auto it = s->read_results.find(piece);
        if (it != s->read_results.end()) {
            ReadResult r = std::move(it->second);
            s->read_results.erase(it);
            return r;
        }
    }
    return {};
}

void preload_stream(StreamEngine* s, int64_t preload_bytes) {
    if (preload_bytes <= 0) return;
    s->preload_size.store(preload_bytes);
    s->preloading.store(true);

    if (preload_bytes > s->file_size)
        preload_bytes = s->file_size;

    int64_t startend = s->piece_length;
    if (startend < 8 * 1024 * 1024)
        startend = 8 * 1024 * 1024;

    int64_t reader_start_end = preload_bytes - startend;
    if (reader_start_end < 0) reader_start_end = preload_bytes;
    if (reader_start_end > s->file_size) reader_start_end = s->file_size;

    int64_t reader_end_start = s->file_size - startend;

    int head_piece = s->start_piece;
    int head_end_piece = s->byte_to_piece(reader_start_end);
    head_end_piece = std::min(head_end_piece, s->end_piece);

    for (int p = head_piece; p <= head_end_piece && s->active.load() && s->preloading.load(); ++p) {
        if (!wait_for_piece(s, p, 30000)) break;
        for (int w = 0; w < 50 && s->active.load(); ++w) {
            auto* cp = s->cache ? s->cache->get_piece(p) : nullptr;
            if (cp && !cp->buffer.empty()) break;
            std::this_thread::sleep_for(chr::milliseconds(100));
        }
        s->preloaded_bytes.fetch_add(s->piece_length);
    }

    if (reader_end_start > reader_start_end) {
        int tail_piece = s->byte_to_piece(reader_end_start);
        tail_piece = std::max(tail_piece, s->start_piece);

        for (int p = tail_piece; p <= s->end_piece && s->active.load() && s->preloading.load(); ++p) {
            if (!wait_for_piece(s, p, 30000)) break;
            for (int w = 0; w < 50 && s->active.load(); ++w) {
                auto* cp = s->cache ? s->cache->get_piece(p) : nullptr;
                if (cp && !cp->buffer.empty()) break;
                std::this_thread::sleep_for(chr::milliseconds(100));
            }
            s->preloaded_bytes.fetch_add(s->piece_length);
        }
    }

    s->preloading.store(false);
}

void fill_stream_status(lt_stream_status* out, const StreamEngine* s) {
    out->id         = s->id;
    out->torrent_id = s->torrent_id;
    out->file_index = s->file_index;
    out->file_size  = s->file_size;
    out->read_head  = s->read_head.load();
    out->stream_state = s->stream_state.load();

    out->readahead_window = (s->piece_length > 0)
        ? (int)(StreamEngine::FIXED_READAHEAD / s->piece_length)
        : 16;

    int play = std::clamp(s->byte_to_piece(s->read_head.load()),
                          s->start_piece, s->end_piece);
    int contiguous = 0;
    {
        std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(s->piece_mu));
        int p = play;
        while (p <= s->end_piece && s->pieces_have.count(p)) {
            contiguous++; p++;
        }
    }
    out->buffer_pieces = contiguous;

    float bitrate = s->estimated_bitrate_bps;
    out->buffer_seconds = (float)contiguous * s->piece_length / bitrate;

    try {
        lt::torrent_status ts = s->handle.status();
        out->active_peers  = ts.num_peers;
        out->download_rate = ts.download_rate;
    } catch (...) {
        out->active_peers  = 0;
        out->download_rate = 0;
    }

    std::string url = s->make_url();
    std::strncpy(out->url, url.c_str(), sizeof(out->url) - 1);
    out->url[sizeof(out->url) - 1] = 0;
}
