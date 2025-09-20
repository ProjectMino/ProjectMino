#include <vector>
#include <chrono>
#include <mutex>
#include <cstdint>
#include <string>
#include "replay.h"

namespace Pluviohiems {

// Recorder singleton (simple)
class Recorder {
public:
    void start(const std::string &username_hint = "") {
        std::lock_guard<std::mutex> lk(mutex_);
        events_.clear();
        start_time_ = clock_now();
        running_ = true;
        metadata_ = ReplayMetadata();
        metadata_.username = username_hint.empty() ? make_guest() : username_hint;
        metadata_.start_iso8601 = ReplayMetadata::now_iso8601();
    }

    void stop() {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!running_) return;
        auto end = clock_now();
        metadata_.end_iso8601 = ReplayMetadata::now_iso8601();
        metadata_.duration_seconds = to_seconds(end - start_time_);
        running_ = false;
    }

    void record_input_blob(const uint8_t* data, size_t len) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!running_) return;
        ReplayEvent e;
        e.time_seconds = to_seconds(clock_now() - start_time_);
        e.payload.assign(data, data + len);
        events_.push_back(std::move(e));
    }

    void record_state_blob(const std::vector<uint8_t> &blob) {
        record_input_blob(blob.data(), blob.size());
    }

    const std::vector<ReplayEvent> snapshot_events() {
        std::lock_guard<std::mutex> lk(mutex_);
        return events_;
    }

    ReplayMetadata &metadata() { return metadata_; }

    void reset_counts() {
        metadata_.count_singles = metadata_.count_doubles = metadata_.count_trios = metadata_.count_quads = 0;
        metadata_.total_spins = 0;
    }

    // helpers to update metadata counts
    void add_spin(const std::string &spin_type) {
        std::lock_guard<std::mutex> lk(mutex_);
        metadata_.total_spins++;
        metadata_.spin_type = spin_type;
    }
    void add_single() { std::lock_guard<std::mutex> lk(mutex_); metadata_.count_singles++; }
    void add_double() { std::lock_guard<std::mutex> lk(mutex_); metadata_.count_doubles++; }
    void add_trio()   { std::lock_guard<std::mutex> lk(mutex_); metadata_.count_trios++; }
    void add_quad()   { std::lock_guard<std::mutex> lk(mutex_); metadata_.count_quads++; }

private:
    static std::chrono::steady_clock::time_point clock_now() { return std::chrono::steady_clock::now(); }
    static double to_seconds(const std::chrono::steady_clock::duration &d) {
        return std::chrono::duration_cast<std::chrono::duration<double>>(d).count();
    }
    static std::string make_guest() {
        auto t = std::time(nullptr);
        return std::string("Guest") + std::to_string(static_cast<uint64_t>(t % 1000000));
    }

    std::mutex mutex_;
    bool running_ = false;
    std::chrono::steady_clock::time_point start_time_;
    std::vector<ReplayEvent> events_;
    ReplayMetadata metadata_;
};

static Recorder g_recorder;

// Public API functions to call from game code:
void start_recording_game(const std::string &username = "") { g_recorder.start(username); }
void stop_recording_game() { g_recorder.stop(); }
void record_game_input(const uint8_t* data, size_t len) { g_recorder.record_input_blob(data, len); }
void record_game_state(const std::vector<uint8_t> &blob) { g_recorder.record_state_blob(blob); }
void recorder_add_spin(const std::string &spin_type) { g_recorder.add_spin(spin_type); }
void recorder_add_single() { g_recorder.add_single(); }
void recorder_add_double() { g_recorder.add_double(); }
void recorder_add_trio() { g_recorder.add_trio(); }
void recorder_add_quad() { g_recorder.add_quad(); }
ReplayMetadata recorder_get_metadata() { return g_recorder.metadata(); }
std::vector<ReplayEvent> recorder_get_events() { return g_recorder.snapshot_events(); }

} // namespace Pluviohiems