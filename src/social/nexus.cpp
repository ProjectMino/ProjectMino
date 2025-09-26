#include <string>

namespace nexus {
    // Minimal safe stubs for UI; do not call any network.
    std::string GetDisplayName() {
        // Always show Guest for OSS build
        return "Guest";
    }

    void SetDropdownVisible(bool) {
        // no-op
        (void)0;
    }

    std::string GetEditingUser() {
        // No persisted editing user in OSS build
        return "";
    }

    // AttemptLogin stub: do not perform any authentication in this build.
    bool AttemptLogin(const std::string& /*user*/, const std::string& /*pass*/, std::string& /*out_token*/) {
        return false;
    }
}