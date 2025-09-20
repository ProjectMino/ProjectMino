#include <string>
#include <ctime>
#include <sstream>
#include <iomanip>

namespace Pluviohiems {

struct ReplayMetadata {
    std::string username;
    std::string start_iso8601;
    std::string end_iso8601;
    double duration_seconds = 0.0;
    int total_spins = 0;
    std::string spin_type; // e.g. "manual", "auto", etc.
    int count_singles = 0;
    int count_doubles = 0;
    int count_trios = 0;
    int count_quads = 0;

    // produce a compact JSON string (no external deps)
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
        gmtime_r(&t, &tm);
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

} // namespace Pluviohiems