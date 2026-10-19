// SPDX-License-Identifier: AGPL-3.0-or-later
//
// R&D: Tyres & Thermal Management - research capacity for department code 4.
// Install once from SWOFTY_ON_LOAD with the game's module base; the piece is
// gated by its own tyres_research_capacity setting. See rnd_capacity.cpp.
#pragma once

#include <cstdint>

namespace rnd_capacity {

void install(uintptr_t base);

} // namespace rnd_capacity
