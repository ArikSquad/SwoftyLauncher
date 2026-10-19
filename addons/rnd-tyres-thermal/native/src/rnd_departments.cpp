// SPDX-License-Identifier: AGPL-3.0-or-later
//
// R&D: Tyres & Thermal Management - engine plugin for F1 25.
//
// Pairs with the database rows that add a fifth career R&D department.
//
// Hook A - team-department enum store. The "department" enum is resolved by a
// single, uniquely scannable case-insensitive FNV-1a-64 switch in F1_25.exe
// that already knows the token "Tyres" (value 4) as an unused spare. When the
// token being resolved is "Tyres" the stored enum byte is forced to 4. That is
// a confirm/activate of the spare, never a new value. Every resolution is
// logged (the only way to observe whether the path reaches the site).
//
// Hook B - department-table row count. The career department loader
// (Career_Load_DepartmentEntries @0x1412326e0) loops `row_count` times reading
// UpgradeDepartmentEntry_0..row_count-1. That row_count is produced by the
// shared table-cursor builder Gdb_Init_TableCursor (@0x1445a25c0): it reads the
// DB key "<container>._UpgradeDepartmentEntry_instancecount_" (stock = 4) and
// caches it at cursor+0x200. The single convergence store `mov [rsi+0x200],ecx`
// (@0x1445a28df, rsi = cursor, ecx = count) is hooked and, ONLY when the
// cursor's row word (cursor+0x100) is "UpgradeDepartmentEntry" and the resolved
// count is the stock 4, forces the stored count to 5 so our fifth row
// (UpgradeDepartmentEntry_4 in the two real containers) is read. Guarding on the
// table name keeps every other table cursor (entrants/vehicles/teams/...)
// untouched. Hooking the count source makes every consumer that inits an
// UpgradeDepartmentEntry cursor agree on 5.
//
// Each hook is gated on an exactly-one scan match. On 0 or >1 matches, or a
// failed install, it logs and installs nothing, so the DLL stays benign.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cstdint>

#include <safetyhook.hpp>
#include <swofty/mixin.hpp>

namespace {

safetyhook::MidHook g_hook;
safetyhook::MidHook g_count_hook;

// case-insensitive FNV-1a-64, exactly as the game hashes enum token values.
constexpr uint64_t ci_fnv1a64(const char *s)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (; *s; ++s) {
        uint8_t c = static_cast<uint8_t>(*s);
        if (c >= 'A' && c <= 'Z')
            c |= 0x20;
        h = (h ^ c) * 0x100000001b3ull;
    }
    return h;
}

// Our department's token. The database rows write department = "Tyres".
constexpr uint64_t kTyres = ci_fnv1a64("Tyres");
constexpr uint8_t kTyresValue = 4;

// Compile-time proof the hash matches the one captured in the probe log.
static_assert(kTyres == 0xac9965efa31b61d0ull, "Tyres token hash drifted");
static_assert(ci_fnv1a64("Powertrain") == 0xeacc53611edf399cull, "enum hashing drifted");
static_assert(ci_fnv1a64("Chassis") == 0x184f58717a1a6a3full, "enum hashing drifted");
static_assert(ci_fnv1a64("Aero") == 0xd0fae783d5a9fb7cull, "enum hashing drifted");

const char *token_name(uint64_t h)
{
    switch (h) {
    case 0xeacc53611edf399cull: return "Powertrain";
    case 0x184f58717a1a6a3full: return "Chassis";
    case 0xd0fae783d5a9fb7cull: return "Aero";
    case 0xac9965efa31b61d0ull: return "Tyres";
    default: return "<other>";
    }
}

std::atomic<int> g_log_count{0};

// Site: `mov byte [rdi+0x340], cl` in the single team-department resolver.
// rax = ci-FNV-1a-64 hash of the resolved "department" token value.
// cl  = the enum value the game resolved (0..4).
void on_dept_store(safetyhook::Context &c)
{
    uint64_t h = c.rax;
    unsigned resolved = static_cast<unsigned>(c.rcx & 0xffu);

    if (h == kTyres) {
        c.rcx = (c.rcx & ~0xffull) | kTyresValue;
        swofty::log("[rnd-tyres] dept store: token=Tyres(0x%016llx) game-resolved=%u -> forced=%u",
                    (unsigned long long)h, resolved, (unsigned)kTyresValue);
        return;
    }

    int n = g_log_count.fetch_add(1);
    if (n < 64)
        swofty::log("[rnd-tyres] dept store: token=%s(0x%016llx) value=%u", token_name(h),
                    (unsigned long long)h, resolved);
    else if (n == 64)
        swofty::log("[rnd-tyres] further non-Tyres dept stores suppressed (>64)");
}

// The department table's row word, as the loader writes it into the cursor at
// +0x100 before Gdb_Init_TableCursor computes the count.
constexpr char kDeptTable[] = "UpgradeDepartmentEntry";
constexpr uint32_t kStockDeptCount = 4;
constexpr uint32_t kNewDeptCount = 5;

// Exact, null-terminated match of the cursor's row word against kDeptTable.
bool cursor_table_is(uintptr_t cursor, const char *lit)
{
    const volatile char *p = reinterpret_cast<const volatile char *>(cursor + 0x100);
    for (size_t i = 0; i < 48; ++i) {
        char a = static_cast<char>(p[i]);
        char b = lit[i];
        if (a != b)
            return false;
        if (b == '\0')
            return true;
    }
    return false;
}

std::atomic<int> g_count_log{0};

// Site: `mov dword [rsi+0x200], ecx` in Gdb_Init_TableCursor.
// rsi = GdbTableCursor; ecx = row count just resolved from the instancecount key.
void on_dept_count_store(safetyhook::Context &c)
{
    if (!cursor_table_is(c.rsi, kDeptTable))
        return;
    uint32_t cnt = static_cast<uint32_t>(c.rcx & 0xffffffffu);
    if (cnt == kStockDeptCount) {
        c.rcx = (c.rcx & ~0xffffffffull) | kNewDeptCount;
        swofty::log("[rnd-tyres] dept-count: UpgradeDepartmentEntry row_count %u -> forced %u (cursor=0x%llx)", cnt,
                    kNewDeptCount, (unsigned long long)c.rsi);
    } else {
        int n = g_count_log.fetch_add(1);
        if (n < 16)
            swofty::log("[rnd-tyres] dept-count: UpgradeDepartmentEntry row_count=%u (not the stock 4; left unchanged)",
                        cnt);
    }
}

// ---------------------------------------------------------------------------
// R&D > Performance UI: groundwork + instrumentation (no live 5th box/slice).
//
// The department box grid (Career_Build_RnDDeptBoxGrid @0x141c85f70) and the
// DEVELOPMENT POTENTIAL pie (Career_Build_RnDPotentialPie @0x141f41d60) are
// hardcoded to four departments and cannot be grown blind without a crash:
//   - the box card vector is sized to EXACTLY six pointers (four dept cards +
//     two non-dept panels) then truncated; all six slots are used. A fifth dept
//     card needs the vector grown to seven, a fully constructed 0x950 card, and
//     a fifth authored UIDF box node (each dept card binds to a per-dept UIDF
//     node by a compile-time path suffix).
//   - the pie allocates its per-sector array at EXACTLY 0x900 bytes
//     (0x80 header + 4 * 0x220) and its prologue initialises EXACTLY four slots
//     via the game's own (non-decompiled) slot constructor. A fifth sector needs
//     a larger allocation and a fifth constructed slot.
// Relaxing either builder's `< 4` guard without that work overruns a fixed-size,
// per-slot-initialised buffer. So the live 5th box / 5th slice is a follow-up
// (see DESIGN.md / native/HOOKS.md). Additionally, the launcher's declarative
// UIDF/ERP patch pipeline (operations.ui / patch-pkg) is not built yet, so the
// fifth UIDF node cannot be authored through this addon at all right now.
//
// What ships here is the part that is correct AND safe: colour table slot [4]
// (the stock white spare the fifth card would read) is set to a distinct Tyres
// colour, and both builders are instrumented so the next (crash-prone) pass runs
// against ground truth from the player's own build.
//
// All sites are addressed by RVA from the reversed image base (0x140000000). A
// colour-table layout probe confirms the build matches the decompile before any
// RVA write/hook; on any mismatch nothing is touched.

constexpr uintptr_t kImageBase = 0x140000000ull;

constexpr uintptr_t kRvaColourTable = 0x145755e78ull - kImageBase; // k_Career_RnDDeptColourTable, 6 ABGR entries
constexpr uintptr_t kRvaBoxGrid     = 0x141c85f70ull - kImageBase; // Career_Build_RnDDeptBoxGrid
constexpr uintptr_t kRvaPie         = 0x141f41d60ull - kImageBase; // Career_Build_RnDPotentialPie
constexpr uintptr_t kRvaSelIndex    = 0x145f93c08ull - kImageBase; // DAT_145f93c08 (selected dept index, u8)

// Stock colours as read from the decompile, in card-enum order.
constexpr uint32_t kColAero    = 0xffeead00; // [0] aero, blue
constexpr uint32_t kColChassis = 0xff3ec58d; // [1] chassis, green
constexpr uint32_t kColDura    = 0xff16deff; // [2] durability, yellow
constexpr uint32_t kColPower   = 0xff7a2aed; // [3] powertrain, pink
constexpr uint32_t kColSpare   = 0xffffffff; // [4],[5] white spare
// Distinct Tyres & Thermal colour, ABGR (0xAABBGGRR) = RGB(255,120,0) orange.
constexpr uint32_t kColTyres   = 0xff0078ff;

std::atomic<bool> g_rva_valid{false};

bool region_readable(const void *p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(p, &mbi, sizeof mbi) != sizeof mbi)
        return false;
    if (mbi.State != MEM_COMMIT)
        return false;
    if ((mbi.Protect & PAGE_GUARD) || (mbi.Protect & PAGE_NOACCESS))
        return false;
    uintptr_t end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    return reinterpret_cast<uintptr_t>(p) + n <= end;
}

uint32_t sel_index(uintptr_t base)
{
    const uint8_t *p = reinterpret_cast<const uint8_t *>(base + kRvaSelIndex);
    return region_readable(p, 1) ? *p : 0xffu;
}

// Confirm the colour table matches the decompile, mark RVAs trustworthy, then
// colour the [4] white spare so a future fifth card is drawn in the Tyres hue.
void ui_colour_init(uintptr_t base)
{
    const uint32_t *tbl = reinterpret_cast<const uint32_t *>(base + kRvaColourTable);
    if (!region_readable(tbl, 6 * sizeof(uint32_t))) {
        swofty::log("[rnd-tyres] colour table @0x%llx not readable; UI RVA patches SKIPPED (SAFE)",
                    (unsigned long long)(uintptr_t)tbl);
        return;
    }
    uint32_t c[6];
    for (int i = 0; i < 6; ++i)
        c[i] = tbl[i];
    swofty::log("[rnd-tyres] colour table: [0]=%08x [1]=%08x [2]=%08x [3]=%08x [4]=%08x [5]=%08x", c[0], c[1], c[2],
                c[3], c[4], c[5]);
    bool layout_ok = c[0] == kColAero && c[1] == kColChassis && c[2] == kColDura && c[3] == kColPower
                     && c[4] == kColSpare && c[5] == kColSpare;
    if (!layout_ok) {
        swofty::log("[rnd-tyres] colour table layout differs from the decompile; this build does not match - "
                    "UI RVA patches SKIPPED (SAFE)");
        return;
    }
    g_rva_valid.store(true);
    swofty::log("[rnd-tyres] colour table layout matches the decompile; UI RVAs trusted for this build");

    if (c[4] == kColTyres) {
        swofty::log("[rnd-tyres] colour[4] already = Tyres 0x%08x", kColTyres);
        return;
    }
    void *slot = reinterpret_cast<void *>(base + kRvaColourTable + 4 * sizeof(uint32_t));
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(uint32_t), PAGE_READWRITE, &old)) {
        swofty::log("[rnd-tyres] VirtualProtect failed for colour[4] @0x%llx; NOT writing (SAFE)",
                    (unsigned long long)(uintptr_t)slot);
        return;
    }
    *reinterpret_cast<volatile uint32_t *>(slot) = kColTyres;
    DWORD tmp = 0;
    VirtualProtect(slot, sizeof(uint32_t), old, &tmp);
    swofty::log("[rnd-tyres] colour[4] white spare -> Tyres 0x%08x (@0x%llx); a future 5th card uses this colour",
                kColTyres, (unsigned long long)(uintptr_t)slot);
}

safetyhook::MidHook g_box_probe;
safetyhook::MidHook g_pie_probe;
std::atomic<int> g_box_seen{0};
std::atomic<int> g_pie_seen{0};

// Entry of Career_Build_RnDDeptBoxGrid. rcx = the view builder (param_1). Logged
// once: confirms the R&D Performance page opened and dumps the card-vector shape.
void on_box_grid(safetyhook::Context &c)
{
    if (g_box_seen.fetch_add(1) != 0)
        return;
    uintptr_t self = c.rcx;
    uintptr_t base = swofty::api ? swofty::api->module_base() : 0;
    unsigned long long begin = 0, end = 0, cap = 0;
    if (region_readable(reinterpret_cast<void *>(self + 0x280), 0x18)) {
        begin = *reinterpret_cast<uint64_t *>(self + 0x280);
        end = *reinterpret_cast<uint64_t *>(self + 0x288);
        cap = *reinterpret_cast<uint64_t *>(self + 0x290);
    }
    swofty::log("[rnd-tyres] box-grid builder reached: this=0x%llx pre-build card vec{begin=0x%llx end=0x%llx cap=%llu}"
                " selection=%u (4 cards hardcoded; 5th box deferred)",
                (unsigned long long)self, begin, end, cap, sel_index(base));
}

// Entry of Career_Build_RnDPotentialPie. Logged once.
void on_pie(safetyhook::Context &c)
{
    if (g_pie_seen.fetch_add(1) != 0)
        return;
    uintptr_t base = swofty::api ? swofty::api->module_base() : 0;
    swofty::log("[rnd-tyres] pie builder reached: this=0x%llx selection=%u "
                "(sector buffer 0x900 = 4*0x220 hardcoded; 5th slice deferred)",
                (unsigned long long)c.rcx, sel_index(base));
}

void install_ui_probe(safetyhook::MidHook &slot, uintptr_t addr, void (*fn)(safetyhook::Context &), const char *what,
                      uintptr_t base)
{
    if (!region_readable(reinterpret_cast<void *>(addr), 16)) {
        swofty::log("[rnd-tyres] %s builder @0x%llx not committed; probe SKIPPED (SAFE)", what,
                    (unsigned long long)addr);
        return;
    }
    auto r = safetyhook::MidHook::create(reinterpret_cast<void *>(addr), fn);
    if (!r) {
        swofty::log("[rnd-tyres] %s builder MidHook::create failed @0x%llx (err %d); probe SKIPPED (SAFE)", what,
                    (unsigned long long)addr, (int)r.error().type);
        return;
    }
    slot = std::move(*r);
    swofty::log("[rnd-tyres] instrumented %s builder @0x%llx (base+0x%llx)", what, (unsigned long long)addr,
                (unsigned long long)(addr - base));
}

void ui_groundwork()
{
    const uintptr_t base = swofty::api ? swofty::api->module_base() : 0;
    if (!base) {
        swofty::log("[rnd-tyres] no module base; UI groundwork SKIPPED (SAFE)");
        return;
    }
    ui_colour_init(base);
    if (!g_rva_valid.load()) {
        swofty::log("[rnd-tyres] UI builder instrumentation SKIPPED (build/RVAs unconfirmed)");
        return;
    }
    install_ui_probe(g_box_probe, base + kRvaBoxGrid, &on_box_grid, "box-grid", base);
    install_ui_probe(g_pie_probe, base + kRvaPie, &on_pie, "pie", base);
}

} // namespace

SWOFTY_ON_LOAD
{
    const unsigned long long base = swofty::api ? (unsigned long long)swofty::api->module_base() : 0;
    swofty::log("[rnd-tyres] loaded; API v%u; game base 0x%llx; Tyres token=0x%016llx value=%u",
                swofty::api ? swofty::api->version : 0u, base, (unsigned long long)kTyres, (unsigned)kTyresValue);

    // UI groundwork + instrumentation first, so it runs even if the data hooks bail.
    ui_groundwork();

    uintptr_t hit = 0;
    int matches = swofty::api->scan(".xtls", "75 06 B1 03 EB 02 31 C9 88 8F 40 03 00 00", &hit);
    if (matches != 1 || !hit) {
        swofty::log("[rnd-tyres] dept-enum store signature matched %d time(s); NOT hooking (SAFE)", matches);
        return;
    }

    uintptr_t site = hit + 8; // the `88 8F 40 03 00 00` store (mov [rdi+0x340], cl)
    auto r = safetyhook::MidHook::create(reinterpret_cast<void *>(site), &on_dept_store);
    if (!r) {
        swofty::log("[rnd-tyres] MidHook::create failed at 0x%llx (err %d); NOT hooking (SAFE)",
                    (unsigned long long)site, (int)r.error().type);
        return;
    }
    g_hook = std::move(*r);
    swofty::log("[rnd-tyres] hooked dept-enum store at 0x%llx (base+0x%llx); Tyres resolutions forced to %u",
                (unsigned long long)site, (unsigned long long)(site - base), (unsigned)kTyresValue);

    uintptr_t chit = 0;
    int cmatches = swofty::api->scan(".xtls", "89 8E 00 02 00 00 48 81 C4 20 01 00 00", &chit);
    if (cmatches != 1 || !chit) {
        swofty::log("[rnd-tyres] dept-count store signature matched %d time(s); count NOT hooked (SAFE)", cmatches);
        return;
    }
    auto rc = safetyhook::MidHook::create(reinterpret_cast<void *>(chit), &on_dept_count_store);
    if (!rc) {
        swofty::log("[rnd-tyres] dept-count MidHook::create failed at 0x%llx (err %d); count NOT hooked (SAFE)",
                    (unsigned long long)chit, (int)rc.error().type);
        return;
    }
    g_count_hook = std::move(*rc);
    swofty::log("[rnd-tyres] hooked dept-count store at 0x%llx (base+0x%llx); UpgradeDepartmentEntry count %u -> %u",
                (unsigned long long)chit, (unsigned long long)(chit - base), (unsigned)kStockDeptCount,
                (unsigned)kNewDeptCount);
}

SWOFTY_ADDON()
