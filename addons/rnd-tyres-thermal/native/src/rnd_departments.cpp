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

// ---------------------------------------------------------------------------
// R&D > Performance: the LIVE fifth department box (Tyres & Thermal Management).
//
// Career_Build_RnDDeptBoxGrid (0x141c85f70) builds exactly four department cards
// (begin[0..3], enums 0/1/3/2) plus two non-dept panels (begin[4] purchase,
// begin[5] perks) into a pointer vector at builder+0x280..+0x290, then attaches
// every slot to the container node builder+0x2d8. There is NO data path to a
// fifth card. This injection grows the vector to seven and constructs a real
// fifth Tyres card the game's way so the stock attach loop renders it too.
//
// Correction to the earlier scoping note: the card's compile-time "%s.%s.%s"
// argument is the ICON ATLAS path "<pkg>.rnd_dept_icons_small.<frame>" (6th arg),
// NOT a per-dept UIDF node path; the 5th arg is the NAME lang key
// ("lng_rnd_dept_aero"). A dept card therefore needs NO per-dept authored UIDF
// node and NO launcher UIDF pipeline - it renders through the same card VM node
// (card+0x268) the other four use, attached to the same container. So the fifth
// card is built by cloning a stock card's construction: name = our own
// "lng_rnd_dept_tyres" literal, icon = a BORROWED existing frame (aero), enum = 4
// (colour table slot [4] = the Tyres orange written by ui_colour_init), and it is
// placed in a grown slot so the builder's own attach loop binds it.
//
// Injection site: 0x141c86567 (start of the purchase-panel alloc, right after the
// 4th dept card is stored and before the panels are built). At that instruction
// rsi = the builder (param_1) and rdi = the icon-prefix object (lVar8, non-volatile
// and still live here), and all four dept cards exist. Reached by an exactly-one
// scan of a unique signature whose match+8 is the site. Everything is feature-
// flagged and region-guarded; on any miss it injects nothing and the other hooks
// are untouched. The <4 selection clamp is deliberately NOT relaxed (a saved
// index 4 would read colour[0] as a char* via the name table - crash), so the
// fifth box only RENDERS this round; selection stays within the stock four.

SWOFTY_SETTING(bool, render_fifth_box, true);
std::atomic<bool> g_fifth_enabled{true};

SWOFTY_ON_CHANGE(render_fifth_box, bool on)
{
    g_fifth_enabled.store(on);
    swofty::log("[rnd-tyres] render_fifth_box toggled -> %s", on ? "on" : "off");
}

// Fixed RVAs from the reversed image base (0x140000000). The one-scan signature at
// the hook site proves this exact build, so these siblings are valid; each is also
// region-guarded before use.
constexpr uintptr_t kRvaCardCtor       = 0x141c7aac0ull - kImageBase; // Career_Construct_RnDDeptCard
constexpr uintptr_t kRvaNeMake         = 0x1441974e0ull - kImageBase; // Ne_Make_StrFromCStr
constexpr uintptr_t kRvaNeFormat       = 0x1441630f0ull - kImageBase; // Ne_Format_String (varargs)
constexpr uintptr_t kRvaDefaultAlloc   = 0x146ca54a8ull - kImageBase; // g_Ne_DefaultAllocator (free = vt+0x38)

constexpr uint8_t kTyresDeptEnum = 4;          // reads colour table slot [4]
constexpr size_t  kDeptCardSize  = 0x950;      // Career_Construct_RnDDeptCard object size
constexpr size_t  kNeStrChars    = 9;          // NeStr::chars byte offset
constexpr size_t  kNeStrRefcount = 4;          // NeStr::refcount (u16) byte offset

using NeMakeFn   = uint32_t (*)(void **out, const char *s, uint32_t flags, uintptr_t len);
using NeFormatFn = int64_t (*)(void **out, const char *fmt, ...);
using CardCtorFn = void *(*)(void *card, void *ctx, void *builder, void *allocator, const char *name,
                             const char *icon, uintptr_t dept_enum);
using VtAllocFn  = void *(*)(void *allocator, size_t bytes, size_t align);
using VtFreeFn   = void (*)(void *allocator, void *ptr);

bool region_writable(const void *p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(p, &mbi, sizeof mbi) != sizeof mbi)
        return false;
    if (mbi.State != MEM_COMMIT)
        return false;
    const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if ((mbi.Protect & PAGE_GUARD) || (mbi.Protect & PAGE_NOACCESS) || !(mbi.Protect & writable))
        return false;
    uintptr_t endp = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    return reinterpret_cast<uintptr_t>(p) + n <= endp;
}

void *vt_alloc(void *allocator, size_t bytes, size_t align)
{
    if (!region_readable(allocator, 8))
        return nullptr;
    uintptr_t vt = *reinterpret_cast<uintptr_t *>(allocator);
    if (!region_readable(reinterpret_cast<void *>(vt + 0x18), 8))
        return nullptr;
    auto fn = *reinterpret_cast<VtAllocFn *>(vt + 0x18);
    return fn(allocator, bytes, align);
}

void vt_free(void *allocator, void *ptr)
{
    if (!region_readable(allocator, 8))
        return;
    uintptr_t vt = *reinterpret_cast<uintptr_t *>(allocator);
    if (!region_readable(reinterpret_cast<void *>(vt + 0x38), 8))
        return;
    auto fn = *reinterpret_cast<VtFreeFn *>(vt + 0x38);
    fn(allocator, ptr);
}

// Drop our reference on a temporary NeStr exactly as the game does: atomic
// refcount--, and if it hit zero free it through g_Ne_DefaultAllocator (vt+0x38).
void release_nestr(uintptr_t base, void *s)
{
    if (!s || !region_writable(reinterpret_cast<uint8_t *>(s) + kNeStrRefcount, sizeof(uint16_t)))
        return;
    auto *rc = reinterpret_cast<volatile uint16_t *>(reinterpret_cast<uint8_t *>(s) + kNeStrRefcount);
    uint16_t after = __atomic_sub_fetch(rc, 1, __ATOMIC_SEQ_CST);
    if (after != 0)
        return;
    uint16_t expected = 0;
    if (!__atomic_compare_exchange_n(rc, &expected, static_cast<uint16_t>(0xffff), false, __ATOMIC_SEQ_CST,
                                     __ATOMIC_SEQ_CST))
        return;
    void **pa = reinterpret_cast<void **>(base + kRvaDefaultAlloc);
    if (!region_readable(pa, 8))
        return;
    void *alloc = *pa;
    if (alloc)
        vt_free(alloc, s);
}

safetyhook::MidHook g_fifth_hook;
std::atomic<int> g_fifth_log{0};

// At the injection site: rsi = builder (param_1), rdi = icon-prefix object (lVar8).
// Grow the 6-pointer card vector to 7, construct a Tyres card into the new slot,
// and leave the builder's own attach loop to bind it. The two panels are stored
// into begin[4]/begin[5] by the resumed stock code afterwards (fixed offsets off a
// freshly-read begin), so the grown slot [6] is the fifth card.
void on_fifth_box(safetyhook::Context &c)
{
    if (!g_fifth_enabled.load())
        return;
    bool first = g_fifth_log.fetch_add(1) == 0;
    uintptr_t base = swofty::api ? swofty::api->module_base() : 0;
    uintptr_t self = c.rsi;   // builder (param_1)
    uintptr_t lvar8 = c.rdi;  // icon-prefix object

    auto bail = [&](const char *why) {
        if (first)
            swofty::log("[rnd-tyres] 5th box aborted: %s (self=0x%llx)", why, (unsigned long long)self);
    };

    if (!base) { bail("no module base"); return; }
    if (!region_readable(reinterpret_cast<void *>(self + 0x278), 0x28)) { bail("builder vector fields unreadable"); return; }

    void *vec_alloc  = *reinterpret_cast<void **>(self + 0x278);
    uintptr_t begin  = *reinterpret_cast<uintptr_t *>(self + 0x280);
    uintptr_t endp   = *reinterpret_cast<uintptr_t *>(self + 0x288);
    uintptr_t cap    = *reinterpret_cast<uintptr_t *>(self + 0x290); // units of pointers
    void *card_alloc = *reinterpret_cast<void **>(self + 0x298);
    void *ctx28      = *reinterpret_cast<void **>(self + 0x28);

    if (begin == 0) { bail("null card vector"); return; }
    uintptr_t used = (endp - begin) / 8;
    if (used != 6) {
        if (first)
            swofty::log("[rnd-tyres] 5th box aborted: card vector has %llu slots, expected 6 "
                        "(begin=0x%llx end=0x%llx cap=%llu)",
                        (unsigned long long)used, (unsigned long long)begin, (unsigned long long)endp,
                        (unsigned long long)cap);
        return;
    }
    if (!region_readable(reinterpret_cast<void *>(lvar8 + 0x660), 8)) { bail("icon-prefix slot unreadable"); return; }
    char *prefix = *reinterpret_cast<char **>(lvar8 + 0x660);
    if (!region_readable(prefix, 1)) { bail("icon-prefix string unreadable"); return; }

    if (first)
        swofty::log("[rnd-tyres] 5th box: page opened; pre-grow card vec{begin=0x%llx end=0x%llx cap=%llu} "
                    "prefix=\"%s\"",
                    (unsigned long long)begin, (unsigned long long)endp, (unsigned long long)cap, prefix);

    // 1) Grow the pointer vector to >= 7 (realloc to 8 when needed).
    uintptr_t newbegin = begin;
    if (cap < 7) {
        void *nb = vt_alloc(vec_alloc, 8 * sizeof(void *), sizeof(void *));
        if (!nb || !region_writable(nb, 8 * sizeof(void *))) { bail("vector realloc failed"); return; }
        uintptr_t *dst = reinterpret_cast<uintptr_t *>(nb);
        const uintptr_t *src = reinterpret_cast<const uintptr_t *>(begin);
        for (int i = 0; i < 6; ++i)
            dst[i] = src[i];
        dst[6] = 0;
        dst[7] = 0;
        vt_free(vec_alloc, reinterpret_cast<void *>(begin));
        newbegin = reinterpret_cast<uintptr_t>(nb);
        *reinterpret_cast<uintptr_t *>(self + 0x280) = newbegin;
        *reinterpret_cast<uintptr_t *>(self + 0x290) = 8;
    } else if (!region_writable(reinterpret_cast<void *>(begin + 0x30), 8)) {
        bail("slot[6] not writable");
        return;
    }
    // end = begin + 7 pointers; the stock code still fills begin[4]/begin[5] (panels).
    *reinterpret_cast<uintptr_t *>(self + 0x288) = newbegin + 0x38;

    // 2) Construct the real Tyres card.
    void *card = vt_alloc(card_alloc, kDeptCardSize, 8);
    if (!card) {
        *reinterpret_cast<uintptr_t *>(self + 0x288) = newbegin + 0x30; // undo: never leave a null slot[6]
        bail("card alloc failed");
        return;
    }

    void *s = nullptr;
    auto ne_make = reinterpret_cast<NeMakeFn>(base + kRvaNeMake);
    auto ne_format = reinterpret_cast<NeFormatFn>(base + kRvaNeFormat);
    const char *icon = ""; // no texture: fall back to an empty icon if the NeStr build fails
    if (region_readable(reinterpret_cast<void *>(base + kRvaNeMake), 1)
        && region_readable(reinterpret_cast<void *>(base + kRvaNeFormat), 1)) {
        ne_make(&s, "", 0, 0);
        ne_format(&s, "%s.%s.%s", prefix, "rnd_dept_icons_small", "aero");
        if (s && region_readable(reinterpret_cast<uint8_t *>(s) + kNeStrChars, 1))
            icon = reinterpret_cast<const char *>(reinterpret_cast<uint8_t *>(s) + kNeStrChars);
    }

    auto ctor = reinterpret_cast<CardCtorFn>(base + kRvaCardCtor);
    if (!region_readable(reinterpret_cast<void *>(base + kRvaCardCtor), 1)) {
        *reinterpret_cast<uintptr_t *>(self + 0x288) = newbegin + 0x30;
        release_nestr(base, s);
        bail("card ctor address unreadable");
        return;
    }
    ctor(card, ctx28, reinterpret_cast<void *>(self), card_alloc, "lng_rnd_dept_tyres", icon, kTyresDeptEnum);

    *reinterpret_cast<uintptr_t *>(newbegin + 0x30) = reinterpret_cast<uintptr_t>(card);
    release_nestr(base, s);

    if (first) {
        uint32_t colour = 0;
        const uint32_t *ctbl = reinterpret_cast<const uint32_t *>(base + kRvaColourTable);
        if (region_readable(ctbl, 6 * sizeof(uint32_t)))
            colour = ctbl[kTyresDeptEnum];
        swofty::log("[rnd-tyres] 5th box INJECTED: card=0x%llx slot[6]@0x%llx begin=0x%llx end=0x%llx cap=8 "
                    "name=lng_rnd_dept_tyres icon-borrowed=\"%s.rnd_dept_icons_small.aero\" enum=%u colour[4]=0x%08x",
                    (unsigned long long)reinterpret_cast<uintptr_t>(card), (unsigned long long)(newbegin + 0x30),
                    (unsigned long long)newbegin, (unsigned long long)(newbegin + 0x38), prefix, (unsigned)kTyresDeptEnum,
                    colour);
    }
}

// Independent gate: one unique scan of the site signature (match+8 = the inject
// instruction), region check, MidHook. INDEPENDENT of Hooks A/B and the colour
// write - on any miss it installs nothing and they keep working.
void install_fifth_box(uintptr_t base)
{
    uintptr_t hit = 0;
    int matches = swofty::api->scan(".xtls", "48 C7 40 18 00 00 00 00 48 8B 8E 98 02 00 00", &hit);
    if (matches != 1 || !hit) {
        swofty::log("[rnd-tyres] 5th-box site signature matched %d time(s); NOT hooking (SAFE)", matches);
        return;
    }
    uintptr_t site = hit + 8; // the `mov rcx,[rsi+0x298]` that starts the purchase-panel alloc
    if (!region_readable(reinterpret_cast<void *>(site), 16)) {
        swofty::log("[rnd-tyres] 5th-box site @0x%llx not committed; NOT hooking (SAFE)", (unsigned long long)site);
        return;
    }
    auto r = safetyhook::MidHook::create(reinterpret_cast<void *>(site), &on_fifth_box);
    if (!r) {
        swofty::log("[rnd-tyres] 5th-box MidHook::create failed @0x%llx (err %d); NOT hooking (SAFE)",
                    (unsigned long long)site, (int)r.error().type);
        return;
    }
    g_fifth_hook = std::move(*r);
    swofty::log("[rnd-tyres] 5th-box inject hook installed @0x%llx (base+0x%llx); render_fifth_box=%s",
                (unsigned long long)site, (unsigned long long)(site - base), g_fifth_enabled.load() ? "on" : "off");
}

} // namespace

SWOFTY_ON_LOAD
{
    const unsigned long long base = swofty::api ? (unsigned long long)swofty::api->module_base() : 0;
    swofty::log("[rnd-tyres] loaded; API v%u; game base 0x%llx; Tyres token=0x%016llx value=%u",
                swofty::api ? swofty::api->version : 0u, base, (unsigned long long)kTyres, (unsigned)kTyresValue);

    // UI groundwork + instrumentation first, so it runs even if the data hooks bail.
    ui_groundwork();

    // The live fifth department box - its OWN independent gate + feature flag.
    g_fifth_enabled.store(render_fifth_box.get());
    install_fifth_box(base);

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
