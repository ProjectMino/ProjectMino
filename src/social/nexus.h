#pragma once
#include <string>

namespace nexus {

void Init();
void Shutdown();

// Profile / UI state (thread-safe)
std::string GetDisplayName();
std::string GetSubtext();       
bool DropdownVisible();
void SetDropdownVisible(bool v);

// Login input editing (typing/backspace)
void InputAppend(const char* utf8);
void InputBackspace();
void SetInputFocus(int focus); // 0 none, 1 user, 2 pass

// Read current edited fields (for rendering)
std::string GetEditingUser();
std::string GetEditingPassMasked();

// Trigger login (async). returns immediately; status updated via GetLoginStatus()/InProgress()
void StartLogin();
bool LoginInProgress();
std::string GetLoginStatus();

// Poll/update to finish async work if needed (call from main loop)
void Update();

} // namespace nexus