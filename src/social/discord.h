#pragma once
#include <string>
#include <cstdint>

namespace social {

enum class Mode {
    MainMenu,
    Playing,
    Paused,
    Matchmaking,
    Custom
};

// TODO: replace 0ULL with your Discord Application ID (decimal or 0ULL placeholder)
constexpr uint64_t kDiscordAppId = 0ULL;

// API implemented in discord.cpp
bool InitDiscordRPC(uint64_t appId, const std::string& bigImageKey);
void ShutdownDiscordRPC();
void SetDiscordMode(Mode m);
void SetDiscordSmallImage(const std::string& key, const std::string& text = "");
void SetDiscordText(const std::string& details, const std::string& state);

} // namespace social