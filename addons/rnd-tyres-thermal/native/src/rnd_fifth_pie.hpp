// SPDX-License-Identifier: AGPL-3.0-or-later
//
// R&D: Tyres & Thermal Management - the development-potential pie's 5th (Tyres)
// slice, native half. Pairs with the addon's render_fifth_pie piece (buffer grow,
// 5th slot construct, populate guard relax) and with the 5-sector
// rnd_dev_potential.idf / laptop_r_and_d.ui edits. See INTEGRATION.md.
#pragma once

#include <cstdint>

namespace rnd_fifth_pie {

// Call once from SWOFTY_ON_LOAD, right AFTER install_fifth_pie(base).
// render_fifth_pie_on = the addon's render_fifth_pie setting as read at load.
void install(uintptr_t base, bool render_fifth_pie_on);

} // namespace rnd_fifth_pie
