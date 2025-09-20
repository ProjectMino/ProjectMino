#include <string>
#include <vector>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdint>
#include <cstring>
#include "replay.h"

namespace Pluviohiems {

// Format:
// 4 bytes magic "PMRP"
// uint32_t version (e.g. 1)
// uint32_t metadata_length
// metadata JSON bytes
// uint64_t event_count
// for each event:
//   double time_seconds
//   uint32_t payload_length
//   payload bytes

bool ensure_replays_folder(const std::string &base_path) {
    std::string dir = base_path + "/replays";
    struct stat st;
    if (stat(dir.c_str(), &st) == 0) return true;
    if (mkdir(dir.c_str(), 0755) == 0) return true;
    return false;
}

bool save_replay_to_file(const std::string &base_path,
                         const ReplayMetadata &meta,
                         const std::vector<ReplayEvent> &events,
                         std::string &out_filepath) {
    if (!ensure_replays_folder(base_path)) return false;
    // construct filename with username + timestamp
    std::string name = meta.username.empty() ? "guest" : meta.username;
    // sanitize simple (remove spaces)
    for (auto &c : name) if (c == ' ') c = '_';
    std::string fn = name + "_" + meta.start_iso8601 + ".pmrp";
    // replace ':' in ISO string
    for (auto &c : fn) if (c == ':') c = '-';
    std::string path = base_path + "/replays/" + fn;

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) return false;

    // write header
    ofs.write("PMRP", 4);
    uint32_t version = 1;
    ofs.write(reinterpret_cast<const char*>(&version), sizeof(version));

    std::string meta_json = meta.to_json();
    uint32_t meta_len = static_cast<uint32_t>(meta_json.size());
    ofs.write(reinterpret_cast<const char*>(&meta_len), sizeof(meta_len));
    ofs.write(meta_json.data(), meta_json.size());

    uint64_t evcount = static_cast<uint64_t>(events.size());
    ofs.write(reinterpret_cast<const char*>(&evcount), sizeof(evcount));
    for (const auto &e : events) {
        double t = e.time_seconds;
        ofs.write(reinterpret_cast<const char*>(&t), sizeof(t));
        uint32_t payload_len = static_cast<uint32_t>(e.payload.size());
        ofs.write(reinterpret_cast<const char*>(&payload_len), sizeof(payload_len));
        if (payload_len) ofs.write(reinterpret_cast<const char*>(e.payload.data()), payload_len);
    }
    ofs.close();
    if (!ofs) return false;

    out_filepath = path;
    return true;
}

// Convenience wrapper that uses the global recorder snapshot
bool save_current_recording(const std::string &base_path, std::string &out_filepath) {
    auto events = recorder_get_events();
    auto meta = recorder_get_metadata();
    return save_replay_to_file(base_path, meta, events, out_filepath);
}

} // namespace Pluviohiems