#include "social/nexus.h"

void OnProfileButtonClicked() {
    // Replace or adjust existing behavior that opened the sidebar/modal for unauthenticated users
    std::string disp = nexus::GetDisplayName();
    if (disp.empty() || disp == "Guest") {
        // Show inline dropdown under the profile card (do not open the sidebar/modal)
        nexus::SetDropdownVisible(true);
        return;
    }

    // ...existing code for logged-in behavior (open sidebar)...
}

void RenderProfileCard(...) {
    // ...existing profile rendering code...

    if (nexus::DropdownVisible()) {
        // Inline login form under profile card
        UI::BeginContainer("profile-login-inline");

        UI::Text("Login or Register.");
        UI::Spacing();

        UI::Label("Username");
        // show current editing user (UI/Input system must drive nexus::InputAppend / InputBackspace)
        std::string curUser = nexus::GetEditingUser();
        if (UI::InputText("profile_user", curUser)) {
            nexus::SetInputFocus(1);
            // If your input system can't directly write back to nexus, call nexus::InputAppend from key handler
        }

        UI::Label("Password");
        std::string masked = nexus::GetEditingPassMasked();
        if (UI::InputText("profile_pass", masked)) {
            nexus::SetInputFocus(2);
        }

        if (!nexus::LoginInProgress()) {
            if (UI::Button("Submit")) {
                nexus::StartLogin();
            }
        } else {
            UI::Text(nexus::GetLoginStatus().c_str());
        }

        UI::EndContainer();
    }

    // ...existing profile rendering code...
}