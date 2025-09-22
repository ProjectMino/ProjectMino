#include "discord.h"
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>

#if __has_include(<discord.h>)
#  include <discord.h>
#  define DISCORD_SDK_PRESENT 1
#elif __has_include("discord_game_sdk/discord.h")
#  include "discord_game_sdk/discord.h"
#  define DISCORD_SDK_PRESENT 1
#else
#  define DISCORD_SDK_PRESENT 0
#endif

namespace social {

#if DISCORD_SDK_PRESENT

class DiscordRPC {
public:
    static DiscordRPC& Get() {
        static DiscordRPC instance;
        return instance;
    }

    bool Init(uint64_t application_id, const std::string& large_image_key) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (core_) return true; // already initialized

        // store application id
        app_id_ = application_id;

        discord::Core* raw_core = nullptr;
        auto result = discord::Core::Create(application_id, DiscordCreateFlags_Default, &raw_core);
        if (result != discord::Result::Ok || !raw_core) {
            if (raw_core) delete raw_core;
            core_.reset();
            return false;
        }
        core_.reset(raw_core);

        // prepare base activity (large image fixed)
        activity_ = discord::Activity{};
        activity_.GetAssets().SetLargeImage(large_image_key.c_str());
        activity_.GetAssets().SetLargeText("ProjectMino");

        // start callbacks thread
        run_callbacks_.store(true);
        callbacks_thread_ = std::thread(&DiscordRPC::RunCallbacksLoop, this);

        // push an initial presence
        ApplyActivity();
        return true;
    }

    void RunCallbacksOnce() {
        std::lock_guard<std::mutex> lk(mutex_);
        if (core_) core_->RunCallbacks();
    }

    void SetMode(Mode mode) {
        std::lock_guard<std::mutex> lk(mutex_);
        mode_ = mode;
        ApplyModeToActivity();
        ApplyActivity();
    }

    void SetCustomText(const std::string& details, const std::string& state) {
        std::lock_guard<std::mutex> lk(mutex_);
        activity_.SetDetails(details.c_str());
        activity_.SetState(state.c_str());
        ApplyActivity();
    }

    void SetSmallImage(const std::string& small_image_key, const std::string& small_text) {
        std::lock_guard<std::mutex> lk(mutex_);
        activity_.GetAssets().SetSmallImage(small_image_key.c_str());
        if (!small_text.empty()) activity_.GetAssets().SetSmallText(small_text.c_str());
        ApplyActivity();
    }

    void Shutdown() {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (!core_) return;
            core_->ActivityManager().ClearActivity([](discord::Result){});
        }

        run_callbacks_.store(false);
        if (callbacks_thread_.joinable()) callbacks_thread_.join();

        std::lock_guard<std::mutex> lk(mutex_);
        core_.reset();
    }

private:
    DiscordRPC() = default;
    ~DiscordRPC() { Shutdown(); }

    void ApplyActivity() {
        if (!core_) return;
        discord::Activity to_send = activity_;
        core_->ActivityManager().UpdateActivity(to_send, [](discord::Result /*result*/) {
            // no-op
        });
    }

    void ApplyModeToActivity() {
        switch (mode_) {
            case Mode::MainMenu:
                activity_.SetDetails("In Main Menu");
                activity_.SetState("Browsing options");
                activity_.GetAssets().SetSmallImage("menu_icon");
                activity_.GetAssets().SetSmallText("Main Menu");
                break;
            case Mode::Playing:
                activity_.SetDetails("Playing a match");
                activity_.SetState("In Game");
                activity_.GetAssets().SetSmallImage("play_icon");
                activity_.GetAssets().SetSmallText("Live");
                break;
            case Mode::Paused:
                activity_.SetDetails("Game paused");
                activity_.SetState("Paused");
                activity_.GetAssets().SetSmallImage("pause_icon");
                activity_.GetAssets().SetSmallText("Paused");
                break;
            case Mode::Matchmaking:
                activity_.SetDetails("Looking for players");
                activity_.SetState("Matchmaking");
                activity_.GetAssets().SetSmallImage("mm_icon");
                activity_.GetAssets().SetSmallText("Matchmaking");
                break;
            case Mode::Custom:
                // leave as-is
                break;
        }
    }

    void RunCallbacksLoop() {
        while (run_callbacks_.load()) {
            {
                std::lock_guard<std::mutex> lk(mutex_);
                if (core_) core_->RunCallbacks();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1000 / 30));
        }
    }

    std::unique_ptr<discord::Core> core_{nullptr};
    discord::Activity activity_{};
    Mode mode_{Mode::MainMenu};
    uint64_t app_id_{0};

    std::thread callbacks_thread_;
    std::atomic<bool> run_callbacks_{false};
    std::mutex mutex_;
};

#else // DISCORD_SDK_PRESENT == 0

// Minimal stub implementation when the Discord SDK headers are not available.
// This lets the project build without the SDK. Behavior is a no-op.
class DiscordRPC {
public:
    static DiscordRPC& Get() {
        static DiscordRPC instance;
        return instance;
    }

    bool Init(uint64_t application_id, const std::string& /*large_image_key*/) {
        std::lock_guard<std::mutex> lk(mutex_);
        app_id_ = application_id;
        return true;
    }
    void RunCallbacksOnce() {}
    void SetMode(Mode m) { std::lock_guard<std::mutex> lk(mutex_); mode_ = m; }
    void SetCustomText(const std::string& /*details*/, const std::string& /*state*/) {}
    void SetSmallImage(const std::string& /*small_image_key*/, const std::string& /*small_text*/) {}
    void Shutdown() {}

private:
    DiscordRPC() = default;
    ~DiscordRPC() = default;

    Mode mode_{Mode::MainMenu};
    uint64_t app_id_{0};
    std::mutex mutex_;
};

#endif // DISCORD_SDK_PRESENT

// Convenience free functions (definitions must NOT repeat default args)
bool InitDiscordRPC(uint64_t appId, const std::string& bigImageKey) {
    return DiscordRPC::Get().Init(appId, bigImageKey);
}
void ShutdownDiscordRPC() { DiscordRPC::Get().Shutdown(); }
void SetDiscordMode(Mode m) { DiscordRPC::Get().SetMode(m); }
void SetDiscordSmallImage(const std::string& key, const std::string& text) { DiscordRPC::Get().SetSmallImage(key, text); }
void SetDiscordText(const std::string& details, const std::string& state) { DiscordRPC::Get().SetCustomText(details, state); }

} // namespace social