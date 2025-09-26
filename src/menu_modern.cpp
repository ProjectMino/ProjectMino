#include "menu_modern.h"
#include "menu_modern_common.h"
#include "menu_modern_modal.h"
#include "menu_modern_impl.h"

// This translation unit is now a thin includer: implementations live in the headers above.
// Keep this single .cpp to ensure only one TU provides the non-inline symbols.

// Global UI flag: when true the social sidebar is suppressed and a centered login prompt is shown.
bool g_loginPopupOpen = false;
