#ifndef PLUVIOHIEMS_REPLAY_H
#define PLUVIOHIEMS_REPLAY_H

#include <string>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <vector>
#include <cstdint>

namespace Pluviohiems {

struct ReplayEvent {
    double time_seconds = 0.0;
    std::vector<uint8_t> payload;
};

struct ReplayMetadata {
    std::string username;
    std::string start_iso8601;
    std::string end_iso8601;
    double duration_seconds = 0.0;
    int total_spins = 0;
    std::string spin_type;
    int count_singles = 0;
    int count_doubles = 0;
    int count_trios = 0;
    int count_quads = 0;

    std::string to_json() const {
        std::ostringstream o;
        o << "{";
        o << "\"username\":\"" << escape(username) << "\",";
        o << "\"start\":\"" << escape(start_iso8601) << "\",";
        o << "\"end\":\"" << escape(end_iso8601) << "\",";
        o.setf(std::ios::fixed); o<<std::setprecision(3);
        o << "\"duration_seconds\":" << duration_seconds << ",";
        o << "\"total_spins\":" << total_spins << ",";
        o << "\"spin_type\":\"" << escape(spin_type) << "\",";
        o << "\"singles\":" << count_singles << ",";
        o << "\"doubles\":" << count_doubles << ",";
        o << "\"trios\":" << count_trios << ",";
        o << "\"quads\":" << count_quads;
        o << "}";
        return o.str();
    }

    static std::string now_iso8601() {
        std::time_t t = std::time(nullptr);
        std::tm tm{};
#if defined(_POSIX_THREAD_SAFE_FUNCTIONS) || defined(__linux__)
        gmtime_r(&t, &tm);
#else
        std::tm *ptm = std::gmtime(&t);
        if (ptm) tm = *ptm;
#endif
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
        return std::string(buf);
    }

private:
    static std::string escape(const std::string &s) {
        std::string r;
        for(char c: s) {
            switch(c) {
                case '\\': r += "\\\\"; break;
                case '"': r += "\\\""; break;
                case '\n': r += "\\n"; break;
                case '\r': r += "\\r"; break;
                case '\t': r += "\\t"; break;
                default: r += c; break;
            }
        }
        return r;
    }
};

// Recorder API (implemented in record.cpp)
void start_recording_game(const std::string &username = "");
void stop_recording_game();
void record_game_input(const uint8_t* data, size_t len);
void record_game_state(const std::vector<uint8_t> &blob);
void recorder_add_spin(const std::string &spin_type);
void recorder_add_single();
void recorder_add_double();
void recorder_add_trio();
void recorder_add_quad();
ReplayMetadata recorder_get_metadata();
std::vector<ReplayEvent> recorder_get_events();

} // namespace Pluviohiems

#endif // PLUVIOHIEMS_REPLAY_H