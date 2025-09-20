#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <functional>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <iostream>
#include <cstring> // <-- added to fix memcmp
#include "replay.h"

namespace Pluviohiems {

struct LoadedReplay {
    ReplayMetadata meta;
    std::vector<ReplayEvent> events;
};

static bool load_replay_from_file(const std::string &path, LoadedReplay &out) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return false;
    char magic[4];
    ifs.read(magic, 4);
    if (ifs.gcount() != 4) return false;
    if (memcmp(magic, "PMRP", 4) != 0) return false;
    uint32_t version = 0;
    ifs.read(reinterpret_cast<char*>(&version), sizeof(version));
    uint32_t meta_len = 0;
    ifs.read(reinterpret_cast<char*>(&meta_len), sizeof(meta_len));
    std::string meta_json;
    meta_json.resize(meta_len);
    if (meta_len) ifs.read(&meta_json[0], meta_len);

    // very small JSON parser for the fields we wrote (not robust, but enough for metadata produced here)
    auto get_value_str = [&](const std::string &key)->std::string {
        auto p = meta_json.find("\""+key+"\":\"");
        if (p == std::string::npos) return {};
        p += key.size()+3;
        auto q = meta_json.find('"', p);
        if (q == std::string::npos) return {};
        return meta_json.substr(p, q - p);
    };
    auto get_value_num = [&](const std::string &key)->double {
        auto p = meta_json.find("\""+key+"\":");
        if (p == std::string::npos) return 0.0;
        p += key.size()+3;
        size_t i = p;
        while (i < meta_json.size() && (meta_json[i]=='.' || meta_json[i]=='-' || (meta_json[i]>='0' && meta_json[i]<='9'))) i++;
        return std::stod(meta_json.substr(p, i - p));
    };

    out.meta.username = get_value_str("username");
    out.meta.start_iso8601 = get_value_str("start");
    out.meta.end_iso8601 = get_value_str("end");
    out.meta.duration_seconds = get_value_num("duration_seconds");
    out.meta.total_spins = static_cast<int>(get_value_num("total_spins"));
    out.meta.spin_type = get_value_str("spin_type");
    out.meta.count_singles = static_cast<int>(get_value_num("singles"));
    out.meta.count_doubles = static_cast<int>(get_value_num("doubles"));
    out.meta.count_trios = static_cast<int>(get_value_num("trios"));
    out.meta.count_quads = static_cast<int>(get_value_num("quads"));

    uint64_t evcount = 0;
    ifs.read(reinterpret_cast<char*>(&evcount), sizeof(evcount));
    out.events.clear();
    out.events.reserve(static_cast<size_t>(evcount));
    for (uint64_t i = 0; i < evcount; ++i) {
        ReplayEvent e;
        ifs.read(reinterpret_cast<char*>(&e.time_seconds), sizeof(e.time_seconds));
        uint32_t payload_len = 0;
        ifs.read(reinterpret_cast<char*>(&payload_len), sizeof(payload_len));
        if (payload_len) {
            e.payload.resize(payload_len);
            ifs.read(reinterpret_cast<char*>(e.payload.data()), payload_len);
        }
        out.events.push_back(std::move(e));
    }
    return true;
}

// Playback controller
class PlaybackController {
public:
    void load(const std::string &path) {
        pause();
        std::lock_guard<std::mutex> lk(mu_);
        if (load_replay_from_file(path, loaded_)) {
            position_seconds_ = 0.0;
            playing_ = false;
            last_tick_ = clock_now();
            // optionally precompute anything
        } else {
            // failed to load
        }
    }

    void play() {
        if (loaded_.events.empty()) return;
        playing_ = true;
        runner_thread_ = std::thread([this](){ this->runner(); });
    }

    void pause() {
        playing_ = false;
        if (runner_thread_.joinable()) runner_thread_.join();
    }

    void seek_relative(double delta_seconds) {
        std::lock_guard<std::mutex> lk(mu_);
        position_seconds_ += delta_seconds;
        if (position_seconds_ < 0) position_seconds_ = 0;
        if (position_seconds_ > loaded_.meta.duration_seconds) position_seconds_ = loaded_.meta.duration_seconds;
    }

    double position() const {
        std::lock_guard<std::mutex> lk(mu_);
        return position_seconds_;
    }

    const ReplayMetadata &metadata() const { return loaded_.meta; }

    // Set a callback that will be called with event payload bytes when an event should be applied.
    void set_apply_callback(std::function<void(const std::vector<uint8_t>&)> cb) {
        apply_cb_ = cb;
    }

    // UI placeholder render function - integrate with your renderer
    void render_bottom_bar(int window_w, int window_h) {
        // Implement actual UI with your engine. Minimal ASCII placeholders:
        std::string left = "Played by " + loaded_.meta.username + " on " + loaded_.meta.start_iso8601;
        std::string middle = playing_ ? "[Pause]" : "[Play]";
        std::string right = "[Back]";
        // Replace this with in-game rendering call
        std::cout << left << "  " << middle << "  " << right << std::endl;
    }

private:
    static std::chrono::steady_clock::time_point clock_now() { return std::chrono::steady_clock::now(); }
    static double to_seconds(const std::chrono::steady_clock::duration &d) {
        return std::chrono::duration_cast<std::chrono::duration<double>>(d).count();
    }

    void runner() {
        last_tick_ = clock_now();
        size_t next_event_idx = find_event_index_for(position_seconds_);
        while (playing_) {
            auto now = clock_now();
            double dt = to_seconds(now - last_tick_);
            last_tick_ = now;
            {
                std::lock_guard<std::mutex> lk(mu_);
                position_seconds_ += dt;
            }
            // dispatch events whose time <= position_seconds_
            while (next_event_idx < loaded_.events.size()) {
                double ev_time = loaded_.events[next_event_idx].time_seconds;
                if (ev_time <= position()) {
                    if (apply_cb_) apply_cb_(loaded_.events[next_event_idx].payload);
                    ++next_event_idx;
                } else break;
            }
            if (position() >= loaded_.meta.duration_seconds) {
                playing_ = false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); // tick
        }
    }

    size_t find_event_index_for(double pos) {
        size_t i = 0;
        while (i < loaded_.events.size() && loaded_.events[i].time_seconds < pos) ++i;
        return i;
    }

    mutable std::mutex mu_;
    LoadedReplay loaded_;
    std::atomic<bool> playing_{false};
    std::thread runner_thread_;
    std::function<void(const std::vector<uint8_t>&)> apply_cb_;
    std::chrono::steady_clock::time_point last_tick_;
    double position_seconds_ = 0.0;
};

static PlaybackController g_playback_controller;

// Public API for integrating drag-and-drop and controls
void playback_load_file(const std::string &filepath) {
    g_playback_controller.load(filepath);
    // By default set a naive apply callback that prints; replace with game state application
    g_playback_controller.set_apply_callback([](const std::vector<uint8_t> &payload){
        // TODO: interpret payload and apply to game (inputs / state)
        // Example: first byte = event type, rest = data
        if (!payload.empty()) {
            uint8_t t = payload[0];
            (void)t;
            // integrate with your input handling to simulate events
        }
    });
    // animate bottom bar in your UI: call render_bottom_bar from game render loop
}

void playback_play() { g_playback_controller.play(); }
void playback_pause() { g_playback_controller.pause(); }
void playback_seek_forward() { g_playback_controller.seek_relative(4.0); }
void playback_seek_backward() { g_playback_controller.seek_relative(-4.0); }
double playback_position() { return g_playback_controller.position(); }
const ReplayMetadata &playback_metadata() { return g_playback_controller.metadata(); }

// Example helper to wire drag-and-drop: when a file path is dropped on window, call playback_load_file(path)
// Integrate UI: show bottom bar, play/pause button, forward/backward buttons, and left/right texts as required.
} // namespace Pluviohiems