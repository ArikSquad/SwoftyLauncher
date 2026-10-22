// SPDX-License-Identifier: MIT
// Copyright (c) 2026 xanpavle. Ported to a Swofty Launcher addon by Swofty.
#include "enforce.hpp"

namespace reopt::enforce {

namespace {

bool ieq(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z')
            x = char(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z')
            y = char(y - 'A' + 'a');
        if (x != y)
            return false;
    }
    return true;
}

}

Mode parse_mode(std::string_view m)
{
    if (ieq(m, "target"))
        return Mode::Target;
    if (ieq(m, "off") || ieq(m, "none"))
        return Mode::Off;
    return Mode::Cheaper;
}

const char *mode_name(Mode m)
{
    switch (m) {
    case Mode::Target:
        return "target";
    case Mode::Off:
        return "off";
    default:
        return "cheaper";
    }
}

int32_t int_value(Mode mode, int32_t engine, int32_t tuned)
{
    switch (mode) {
    case Mode::Target:
        return tuned;
    case Mode::Cheaper:
        return engine < tuned ? engine : tuned;
    default:
        return engine;
    }
}

Pin bool_pin(Mode mode, bool pins, bool tuned)
{
    if (!pins || mode == Mode::Off)
        return Pin::Keep;
    if (!tuned)
        return Pin::Off;
    return mode == Mode::Target ? Pin::On : Pin::Keep;
}

int32_t compare_result(Pin pin, int32_t engine)
{
    switch (pin) {
    case Pin::On:
        return 0;
    case Pin::Off:
        return 1;
    default:
        return engine;
    }
}

}
