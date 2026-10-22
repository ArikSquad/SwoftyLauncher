#include "wine_test.hpp"

// Scans for itself instead of using a binding, so on a packed build its first call meets encrypted code.
bool check()
{
    probe()[42] = probe()[42] + 1;
    swofty::api->lang->available();
    uintptr_t at = 0;
    if (swofty::api->scan(".xtls", "48 B8 53 57 46 54 50 4C 41 4E B8 05 00 00 00 C3", &at) != 1)
        return false;
    probe()[43] = static_cast<int64_t>(at);
    return true;
}

SWOFTY_ADDON(check)
