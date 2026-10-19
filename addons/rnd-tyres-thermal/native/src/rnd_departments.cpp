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
// v0.6.0 - Tyres upgrade instances. The Tyres page was empty because the game's
// upgrade-instance state machine special-cases department 4 ("Fake", the tree
// hub): the instance init gives it an Invalid, disabled sentinel and every state
// mutator bails on dept > 3, so the R&D page query (which keeps only enabled
// instances) drops it. Our three rows share dept 4, so they were dropped too.
// The fix lets ONLY our three instances take the normal path; the stock hub is
// untouched. See the v0.6.0 section below and native/HOOKS.md.
//
// Each hook is gated on an exactly-one scan match. On 0 or >1 matches, or a
// failed install, it logs and installs nothing, so the DLL stays benign.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <utility>

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

// ---------------------------------------------------------------------------
// R&D > Performance: the LIVE development-potential pie 5th slice (Tyres).
//
// The live Performance-page pie is built by Career_Build_RnDPieOverview
// (0x141f46eb0) + populate helper Career_Populate_RnDPieSlices (0x141f48030) on
// class-B vtable 0x14575e300 (NOT the dormant class-A builder 0x141f41d60, which
// does not open on this page). The builder allocates a per-slice buffer at
// EXACTLY 0x900 (= 0x80 + 4*0x220) at 0x141f46f75, constructs four slots via
// FUN_141f45650 (slot i at buf+0x80+i*0x220: +0x80/+0x2a0/+0x4c0/+0x6e0), stores
// the buffer at builder+0x670, then the populate loop (code table 0x145735d60)
// writes each dept code < 4 into its slot at builder+0x670 + i*0x220.
//
// This piece grows the buffer to 0xb20 (= 0x80 + 5*0x220), constructs a real 5th
// slot (FUN_141f45650 at buf+0x900), and relaxes the populate loop's `code < 4`
// guard (cmp r14d,3 @0x141f48218) to `< 5` so dept code 4 (the "fake"/Tyres
// spare, dept-name-table[4] = "fake") is populated into the 5th slot. These are
// NOT blind writes: the buffer size, slot stride (0x220) and slot constructor
// (FUN_141f45650) are exactly what the stock prologue uses for slots 0..3; the
// populate's second index guard (cmp r13d,4 @0x141f4821e) already permits five
// writes, so it is left untouched.
//
// DEFERRED (authored art, not shippable from a mixin): the pie-wheel WEDGE
// GEOMETRY and the jump-table `case 4` name/colour. The four wedges are
// pre-authored 90-degree sector nodes; code 4 takes the populate's DEFAULT
// jump-table case (name token "error_department", colour white) and resolves its
// sector node by name-hash under `rnd_wheel_icon`, getting an EMPTY handle on
// miss (benign - every populate write is node-guarded). So the 5th slice is
// POPULATED but NO wedge draws, and it is labelled via the error-department
// default, until a 5th authored sector node + a jump-table `case 4` (name
// lng_rnd_dept_tyres, colour 0xff0078ff) are added. Extending the 4-entry rel32
// jump table @0x141f48231 needs a code cave within +-2GB and is exactly the kind
// of blind patch that risks a crash, so it is NOT done here; see native/HOOKS.md.
// Because the slice has no visible payoff yet and this is the riskiest piece,
// render_fifth_pie DEFAULTS OFF.
//
// Independent gate: one unique scan of the buffer-alloc site; all sites then
// addressed by fixed RVA (no ASLR) and region-guarded, with the live immediate
// bytes verified before any write. The static code patches (buffer size + guard
// immediate) are applied ONLY when the flag is on at load, together with the
// slot-construct hook, and the guard patch is applied LAST, so the guard is never
// relaxed unless the buffer is grown and the 5th slot is constructed. Runtime
// toggling logs that a relaunch is needed (code patches are install-time).

SWOFTY_SETTING(bool, render_fifth_pie, false);
std::atomic<bool> g_pie_enabled{false};

SWOFTY_ON_CHANGE(render_fifth_pie, bool on)
{
    swofty::log("[rnd-tyres] render_fifth_pie toggled -> %s; pie code patches are install-time, relaunch to apply",
                on ? "on" : "off");
}

constexpr uintptr_t kRvaPieAllocSite = 0x141f46f6bull - kImageBase; // scan match: mov rbx,[rsi+0x270]
constexpr uintptr_t kRvaPieAllocImm  = 0x141f46f76ull - kImageBase; // the 0x900 immediate (4 bytes LE)
constexpr uintptr_t kRvaPieSlotHook  = 0x141f47012ull - kImageBase; // after the 4th FUN_141f45650; rbx = buffer
constexpr uintptr_t kRvaPieGuard1Imm = 0x141f4821bull - kImageBase; // the `03` in cmp r14d,3 (code < 4 guard)
constexpr uintptr_t kRvaPieSlotCtor  = 0x141f45650ull - kImageBase; // FUN_141f45650(slot)

constexpr size_t   kPieSlotStride = 0x220;
constexpr size_t   kPieSlot4Off   = 0x80 + 4 * kPieSlotStride; // 0x900
constexpr uint32_t kPieBufNew     = 0x80 + 5 * kPieSlotStride; // 0xb20

using PieSlotCtorFn = void (*)(void *slot);

safetyhook::MidHook g_pie_slot_hook;
std::atomic<int> g_pie_slot_log{0};

// At 0x141f47012 (right after the 4th slot ctor returns): rbx = the (grown) slice
// buffer. Construct the 5th slot at buf+0x900 the game's own way so the populate
// loop can write code 4 into it without touching uninitialised memory.
void on_pie_slot_construct(safetyhook::Context &c)
{
    if (!g_pie_enabled.load())
        return;
    uintptr_t base = swofty::api ? swofty::api->module_base() : 0;
    if (!base)
        return;
    uintptr_t buf = c.rbx;
    void *slot = reinterpret_cast<void *>(buf + kPieSlot4Off);
    bool first = g_pie_slot_log.fetch_add(1) == 0;
    if (buf == 0 || !region_writable(slot, kPieSlotStride)) {
        if (first)
            swofty::log("[rnd-tyres] pie: 5th slot @0x%llx not writable (buffer not grown?); NOT constructing (SAFE)",
                        (unsigned long long)(buf + kPieSlot4Off));
        return;
    }
    auto ctor = reinterpret_cast<PieSlotCtorFn>(base + kRvaPieSlotCtor);
    ctor(slot);
    if (first)
        swofty::log("[rnd-tyres] pie: constructed 5th slice slot @0x%llx (buffer=0x%llx stride=0x220)",
                    (unsigned long long)(buf + kPieSlot4Off), (unsigned long long)buf);
}

bool pie_bytes_are(uintptr_t addr, const uint8_t *want, size_t n)
{
    if (!region_readable(reinterpret_cast<void *>(addr), n))
        return false;
    const volatile uint8_t *p = reinterpret_cast<const volatile uint8_t *>(addr);
    for (size_t i = 0; i < n; ++i)
        if (p[i] != want[i])
            return false;
    return true;
}

bool pie_patch(uintptr_t addr, const uint8_t *bytes, size_t n, const char *what)
{
    void *p = reinterpret_cast<void *>(addr);
    if (!region_readable(p, n)) {
        swofty::log("[rnd-tyres] pie: %s @0x%llx not readable; NOT patching (SAFE)", what, (unsigned long long)addr);
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(p, n, PAGE_EXECUTE_READWRITE, &old)) {
        swofty::log("[rnd-tyres] pie: VirtualProtect failed for %s @0x%llx; NOT patching (SAFE)", what,
                    (unsigned long long)addr);
        return false;
    }
    for (size_t i = 0; i < n; ++i)
        reinterpret_cast<volatile uint8_t *>(p)[i] = bytes[i];
    DWORD tmp = 0;
    VirtualProtect(p, n, old, &tmp);
    return true;
}

// Independent gate: one unique scan of the buffer-alloc site. All sites then by
// fixed RVA, each region- and value-verified. Installs are ordered so the
// code-4 guard (the only thing that makes the populate touch the 5th slot) is
// relaxed LAST, after the buffer is grown and the slot-ctor hook is live.
void install_fifth_pie(uintptr_t base)
{
    if (!g_pie_enabled.load()) {
        swofty::log("[rnd-tyres] render_fifth_pie off; pie 5th slice NOT installed (SAFE)");
        return;
    }
    uintptr_t hit = 0;
    int matches = swofty::api->scan(
        ".xtls", "48 8B 9E 70 02 00 00 48 8B 03 BA 00 09 00 00 41 B8 10 00 00 00 48 89 D9", &hit);
    if (matches != 1 || !hit) {
        swofty::log("[rnd-tyres] pie buffer-alloc signature matched %d time(s); pie NOT patched (SAFE)", matches);
        return;
    }
    if (hit != base + kRvaPieAllocSite) {
        swofty::log("[rnd-tyres] pie scan hit 0x%llx != expected 0x%llx; pie NOT patched (SAFE)",
                    (unsigned long long)hit, (unsigned long long)(base + kRvaPieAllocSite));
        return;
    }

    const uint8_t kImm900[4] = {0x00, 0x09, 0x00, 0x00};
    const uint8_t kGuard3[4] = {0x41, 0x83, 0xfe, 0x03};
    uintptr_t imm_at   = base + kRvaPieAllocImm;
    uintptr_t guard_at = base + kRvaPieGuard1Imm; // the `03` operand (last byte of the cmp)
    uintptr_t guard_ins = base + (0x141f48218ull - kImageBase);
    uintptr_t slot_at  = base + kRvaPieSlotHook;

    // Verify every site matches the decompile for THIS build before touching anything.
    if (!pie_bytes_are(imm_at, kImm900, 4) || !pie_bytes_are(guard_ins, kGuard3, 4)
        || !region_readable(reinterpret_cast<void *>(slot_at), 16)
        || !region_readable(reinterpret_cast<void *>(base + kRvaPieSlotCtor), 16)) {
        swofty::log("[rnd-tyres] pie: site bytes differ from the decompile (imm/guard/slot/ctor); pie NOT patched (SAFE)");
        return;
    }

    // 1) 5th-slot constructor hook (does nothing until the buffer is grown).
    auto r = safetyhook::MidHook::create(reinterpret_cast<void *>(slot_at), &on_pie_slot_construct);
    if (!r) {
        swofty::log("[rnd-tyres] pie: slot-ctor MidHook::create failed @0x%llx (err %d); pie NOT patched (SAFE)",
                    (unsigned long long)slot_at, (int)r.error().type);
        return;
    }
    g_pie_slot_hook = std::move(*r);

    // 2) grow the slice buffer 0x900 -> 0xb20.
    const uint8_t newimm[4] = {(uint8_t)(kPieBufNew & 0xff), (uint8_t)((kPieBufNew >> 8) & 0xff),
                               (uint8_t)((kPieBufNew >> 16) & 0xff), (uint8_t)((kPieBufNew >> 24) & 0xff)};
    if (!pie_patch(imm_at, newimm, 4, "buffer-size immediate")) {
        swofty::log("[rnd-tyres] pie: buffer not grown; code-4 guard left intact (SAFE)");
        return;
    }

    // 3) LAST: relax the populate `code < 4` guard (cmp r14d,3 -> cmp r14d,4).
    const uint8_t four = 0x04;
    if (!pie_patch(guard_at, &four, 1, "code<4 guard immediate")) {
        swofty::log("[rnd-tyres] pie: guard not relaxed; 5th slot is built but unused, buffer grown (SAFE)");
        return;
    }

    swofty::log("[rnd-tyres] pie 5th slice INSTALLED: buffer 0x900->0x%x, slot-ctor hook @0x%llx (base+0x%llx), "
                "guard cmp r14d,3->4 @0x%llx; code 4 populates slot[4]@buf+0x900 (name=error_department default, "
                "colour=white; wedge geometry + jump-table case 4 DEFERRED - no wedge draws yet)",
                kPieBufNew, (unsigned long long)slot_at, (unsigned long long)(slot_at - base),
                (unsigned long long)guard_ins);
}

// ---------------------------------------------------------------------------
// R&D > Performance: 5th-box LAYOUT diagnostic (why the injected box is invisible).
//
// Static RE (this session) + the live v0.3.0 result settled the mechanism: the
// department box grid is an embedded UITC tab-container VM at builder+0x2d8.
// Whether a department gets a VISIBLE, selectable 2x2 cell is governed by the
// container's authored SLOT VECTOR at container+0x270..+0x278 (= builder+0x548..
// +0x550; 0x10 per entry: entry+8 = slotObj, slotObj+0x20 = bound card VM-node,
// slotObj+8 = inner content node). Those slots are created at SCREEN INIT from the
// authored tab/VM nodes under view_model_data.laptop_r_and_d_departments_container
// and the authored rnd_card_with_info scene instances in frontend_overlay.idf -
// keyed by node id, a FIXED set of four. The per-card attach (FUN_14431d720) only
// registers a child into the container's child collection (+0x28/+0x378); it NEVER
// adds a +0x270 slot. So the v0.3.0 5th card is parented but has no authored slot
// and no authored scene cell -> invisible (confirmed in-game).
//
// A genuine 5th VISIBLE cell needs an authored (or runtime-cloned) 5th +0x270 slot
// AND a 5th rnd_card_with_info scene instance with a real cell rect. The
// slot-register routine (UITC vt[0x48] init / screen builder 0x141c43290) is not in
// the decompiled corpus, and the 5th cell's rect lives in the frontend_overlay.idf
// scene (not dumped). Cloning a slot + fabricating a rect blind would risk a crash,
// and the launcher's declarative UIDF/ERP pipeline is not built - so NO visible-cell
// lever is shippable SAFELY from this addon this round. This piece ships a SAFE,
// read-only DIAGNOSTIC: on the next R&D > Performance visit it dumps the live
// slot-vector vs card-vector shape and every authored slot's bound/inner node, so
// the follow-up (an external f1-modding ERP scene patch adding a 5th authored cell,
// or a runtime slot+scene-node clone using the captured node pointers/rects) has
// exact ground truth. See native/HOOKS.md.
//
// Independent gate: one unique scan at a late point in the builder (0x141c86d86,
// after the selection loop has walked +0x548, so the slot vector is populated and
// the attach loop has run); rsi = builder there. Read-only, region-guarded, one-shot.

SWOFTY_SETTING(bool, render_fifth_box_layout, true);
std::atomic<bool> g_layout_enabled{true};
safetyhook::MidHook g_layout_hook;
std::atomic<int> g_layout_log{0};

SWOFTY_ON_CHANGE(render_fifth_box_layout, bool on)
{
    g_layout_enabled.store(on);
    swofty::log("[rnd-tyres] render_fifth_box_layout toggled -> %s", on ? "on" : "off");
}

uintptr_t rd_ptr(uintptr_t a)
{
    return region_readable(reinterpret_cast<void *>(a), 8) ? *reinterpret_cast<uintptr_t *>(a) : 0;
}

// Late in Career_Build_RnDDeptBoxGrid: rsi = builder. Dump the slot vector (the
// authoritative "which cells are visible" structure) against the card vector, plus
// each authored slot's bound/inner node, so the invisible-5th-card cause is
// captured as loader.log ground truth.
void on_box_layout(safetyhook::Context &c)
{
    if (!g_layout_enabled.load())
        return;
    if (g_layout_log.fetch_add(1) != 0)
        return;
    uintptr_t self = c.rsi;
    uintptr_t cbeg = rd_ptr(self + 0x280), cend = rd_ptr(self + 0x288);
    uintptr_t sbeg = rd_ptr(self + 0x548), send = rd_ptr(self + 0x550);
    unsigned long long cards = (cbeg && cend >= cbeg) ? (cend - cbeg) / 8 : 0;
    unsigned long long slots = (sbeg && send >= sbeg) ? (send - sbeg) / 0x10 : 0;
    swofty::log("[rnd-tyres] box-layout: card-vec=%llu slot-vec(container+0x270)=%llu "
                "(builder=0x%llx cards[0x280]=0x%llx..0x%llx slots[0x548]=0x%llx..0x%llx); a 5th VISIBLE cell needs "
                "a 5th authored/cloned slot + scene rect (see HOOKS.md)",
                cards, slots, (unsigned long long)self, (unsigned long long)cbeg, (unsigned long long)cend,
                (unsigned long long)sbeg, (unsigned long long)send);
    for (unsigned long long i = 0; i < slots && i < 8; ++i) {
        uintptr_t slotobj = rd_ptr(sbeg + i * 0x10 + 8);
        uintptr_t bound = slotobj ? rd_ptr(slotobj + 0x20) : 0;
        uintptr_t inner = slotobj ? rd_ptr(slotobj + 8) : 0;
        swofty::log("[rnd-tyres] box-layout slot[%llu]: slotObj=0x%llx bound-node=0x%llx inner-node=0x%llx", i,
                    (unsigned long long)slotobj, (unsigned long long)bound, (unsigned long long)inner);
    }
    for (unsigned long long i = 0; i < cards && i < 8; ++i) {
        uintptr_t card = rd_ptr(cbeg + i * 8);
        unsigned en = (card && region_readable(reinterpret_cast<void *>(card + 0x6d8), 1))
                          ? *reinterpret_cast<uint8_t *>(card + 0x6d8)
                          : 0xff;
        swofty::log("[rnd-tyres] box-layout card[%llu]: card=0x%llx node(+0x268)=0x%llx enum(+0x6d8)=%u", i,
                    (unsigned long long)card, (unsigned long long)(card ? card + 0x268 : 0), en);
    }
}

void install_box_layout_probe(uintptr_t base)
{
    if (!g_layout_enabled.load()) {
        swofty::log("[rnd-tyres] render_fifth_box_layout off; box-layout diagnostic NOT installed (SAFE)");
        return;
    }
    uintptr_t hit = 0;
    int matches = swofty::api->scan(".xtls", "48 8D 8E C0 07 00 00 48 8B 46 10 4C 8D 40 08", &hit);
    if (matches != 1 || !hit) {
        swofty::log("[rnd-tyres] box-layout site signature matched %d time(s); diagnostic NOT installed (SAFE)",
                    matches);
        return;
    }
    auto r = safetyhook::MidHook::create(reinterpret_cast<void *>(hit), &on_box_layout);
    if (!r) {
        swofty::log("[rnd-tyres] box-layout MidHook::create failed @0x%llx (err %d); NOT installed (SAFE)",
                    (unsigned long long)hit, (int)r.error().type);
        return;
    }
    g_layout_hook = std::move(*r);
    swofty::log("[rnd-tyres] box-layout diagnostic installed @0x%llx (base+0x%llx); render_fifth_box_layout=on",
                (unsigned long long)hit, (unsigned long long)(hit - base));
}

// ---------------------------------------------------------------------------
// Upgrade-tree attachment (piece 2): resolve + instrument the code-4 ("fake")
// container so the Tyres page can show buyable upgrades.
//
// The R&D upgrade loader Career_Load_UpgradeRows (0x143539bc0) loops the dept-code
// table 0x145735d60 (codes 0..4; breaks at 5) and, per code, formats the container
// path "%s.%s" = <team_link>.<deptname> from k_Career_UpgradeDeptNameTable
// (0x14584f880: 0=aerodynamics,1=chassis,2=durability,3=engine,4="fake"), opens the
// "upgrade" table under it and reads upgrade_<N> rows into one big vector. The
// "fake" container is NOT empty: it is the R&D tree's central hub - fake.upgrade_0
// = "central_fake_upgrade" (instancecount 1), linking the branch roots. MyTeam's
// tree is <handle>.trees.user_defined (engine-supplier choice is irrelevant to
// it); there are four shipped user_defined.fake containers (season 2025/2026 x a
// duplicate copy). db/tyres_tree.json therefore writes the 3 Tyres nodes as
// upgrade_1/2/3 into ALL FOUR (hashes {ae8319a01aa30478} {5927ed801bdb6765}
// {75a045fd77ae52b9} {f71aa592926ce9d9}), preserving upgrade_0, and bumps each
// _upgrade_instancecount_ 1 -> 4 so the loader reads the hub + the 3 new nodes.
//
// Each loaded upgrade is CLASSIFIED by its own upgrade_department field -> ENUM A
// (switch @0x14353a6dd: Chassis1/Durability2/Engine3/Fake4/Invalid5, else 0). The
// record stores only that ENUM-A byte, so the department page shows records whose
// ENUM A == its code. The Tyres (code-4) page therefore shows records with ENUM A
// == 4, i.e. upgrade_department = "Fake". db/tyres_tree.json sets
// upgrade_department = "Fake" (ENUM A = 4), NOT "Tyres" (which -> 0 = Aero's slot):
// that is the flagged economy-collision fix, and it is pure data.
//
// This gated, READ-ONLY hook CONFIRMS the placement on the player's build: at
// 0x143539c84 (r8 = team_link cstr, r9 = deptname cstr, rdi = code) it logs, per
// dept code, the formatted path and its FNV-1a-64 container hash; the code-4 line
// should equal one of the four container hashes above (and reveals the exact
// {hash} to use if a future build/save differs). SAFE: no writes, region-guarded
// reads only. Gate: one unique scan + flag attach_tyres_tree.

SWOFTY_SETTING(bool, attach_tyres_tree, true);
std::atomic<bool> g_tree_enabled{true};
safetyhook::MidHook g_tree_hook;
std::atomic<int> g_tree_log{0};

SWOFTY_ON_CHANGE(attach_tyres_tree, bool on)
{
    g_tree_enabled.store(on);
    swofty::log("[rnd-tyres] attach_tyres_tree toggled -> %s", on ? "on" : "off");
}

constexpr uint64_t kFnvBasis = 0xcbf29ce484222325ull;
constexpr uint64_t kFnvPrime = 0x100000001b3ull;

uint64_t fnv_add(uint64_t h, const char *s, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        h = (h ^ static_cast<uint8_t>(s[i])) * kFnvPrime;
    return h;
}

size_t copy_cstr(const char *src, char *dst, size_t cap)
{
    size_t i = 0;
    if (!src || !region_readable(src, 1)) {
        dst[0] = 0;
        return 0;
    }
    for (; i < cap - 1 && region_readable(src + i, 1) && src[i]; ++i)
        dst[i] = src[i];
    dst[i] = 0;
    return i;
}

// At 0x143539c84 in Career_Load_UpgradeRows: r8 = team_link cstr, r9 = deptname
// cstr, rdi = dept code. Log the resolved container path + FNV hash per code.
void on_upgrade_tree_load(safetyhook::Context &c)
{
    if (!g_tree_enabled.load())
        return;
    if (g_tree_log.fetch_add(1) >= 10) // ~one 5-code pass plus slack, then quiet
        return;
    unsigned code = static_cast<unsigned>(c.rdi & 0xff);
    char team[192], dept[48];
    size_t tlen = copy_cstr(reinterpret_cast<const char *>(c.r8), team, sizeof team);
    size_t dlen = copy_cstr(reinterpret_cast<const char *>(c.r9), dept, sizeof dept);
    uint64_t h = fnv_add(fnv_add(fnv_add(kFnvBasis, team, tlen), ".", 1), dept, dlen);
    if (code == 4) {
        uint64_t meta0 = fnv_add(h, ".upgrade_0._metadata_", 21);
        uint64_t cnt = fnv_add(h, "._upgrade_instancecount_", 24);
        swofty::log("[rnd-tyres] upgrade-tree code=4 FAKE/TYRES: path=\"%s.%s\" container FNV={%016llx} <<< place "
                    "db/tyres_tree.json rows under this: select \"{%016llx}.upgrade_N\" + \"{%016llx}\" "
                    "_upgrade_instancecount_ (ex: <team_link>.fake.upgrade_0._metadata_ key=%016llx, "
                    "_upgrade_instancecount_ key=%016llx)",
                    team, dept, (unsigned long long)h, (unsigned long long)h, (unsigned long long)h,
                    (unsigned long long)meta0, (unsigned long long)cnt);
    } else {
        swofty::log("[rnd-tyres] upgrade-tree code=%u dept=\"%s\" team_link=\"%s\" container FNV={%016llx}", code,
                    dept, team, (unsigned long long)h);
    }
}

void install_tree_probe(uintptr_t base)
{
    if (!g_tree_enabled.load()) {
        swofty::log("[rnd-tyres] attach_tyres_tree off; upgrade-tree instrument NOT installed (SAFE)");
        return;
    }
    uintptr_t hit = 0;
    int matches = swofty::api->scan(".xtls", "48 8D 8C 24 98 06 00 00 48 8D 15 E1 53 12 02", &hit);
    if (matches != 1 || !hit) {
        swofty::log("[rnd-tyres] upgrade-tree site signature matched %d time(s); instrument NOT installed (SAFE)",
                    matches);
        return;
    }
    auto r = safetyhook::MidHook::create(reinterpret_cast<void *>(hit), &on_upgrade_tree_load);
    if (!r) {
        swofty::log("[rnd-tyres] upgrade-tree MidHook::create failed @0x%llx (err %d); NOT installed (SAFE)",
                    (unsigned long long)hit, (int)r.error().type);
        return;
    }
    g_tree_hook = std::move(*r);
    swofty::log("[rnd-tyres] upgrade-tree instrument installed @0x%llx (base+0x%llx); attach_tyres_tree=on; "
                "db rows use upgrade_department=Fake (ENUM A=4, no Aero collision)",
                (unsigned long long)hit, (unsigned long long)(hit - base));
}

// ---------------------------------------------------------------------------
// R&D upgrade tiles: tile-source-count diagnostic (why the code-4 page is empty).
//
// The upgrade-tree tiles (laptop_research_upgrades, F1TS row_spawner over the VM
// collection @rows) are data-bound: node widgets are spawned from the loaded tree
// model, positioned by grid_coordinates and wired by links. Two loaders feed that
// model and BOTH loop the same dept-code table (0x145735d60, codes 0..4, break at
// 5) with NO exclusion of code 4:
//   - Career_Load_UpgradeRows (0x143539bc0): the flat 0x80-byte UpgradeRecord
//     vector (upgrade_name/type/size/department/cost/...), no grid.
//   - the grid/tree-node loader (0x14353b030, reached from the model builder
//     0x14353abb0, which also calls it per linked team): reads grid_coordinates.x/y
//     + every column and appends all departments' nodes into one combined model at
//     UpgradesComponent+0x68+0x28. Node labels resolve via Gdb_Find_RowByLabel
//     (0x1445a2bd0), a linear scan of rows 0..(_upgrade_instancecount_-1) - so rows
//     at index >= instancecount are INVISIBLE to link resolution. Our db bumps the
//     46 fake containers' _upgrade_instancecount_ 1->4 so upgrade_1/2/3 are in range.
//
// Both loaders get the per-container row count from Gdb_Init_TableCursor
// (<container>._upgrade_instancecount_). This read-only probe sits in the grid/tree
// loader at the first row_count test (0x14353b1af), right after that cursor init:
// rsp-frame [rsp+0x378] = cursor row_count (= cursor(+rsp+0x178)+0x200), [rsp+0x58]
// = &current dept-code byte. It logs, per code (esp. code 4 = the "fake"/Tyres
// container), how many rows the TILE DATA SOURCE yields. That bisects the
// invisible-tiles cause: row_count 4 for code 4 => our nodes ARE in the tree model
// (blocker is the display/active-department filter, not the data); row_count 1 =>
// the instancecount bump is not honoured at runtime (a data problem). SAFE: no
// writes, region-guarded reads only, one unique scan + flag diag_tree_rowcount.

SWOFTY_SETTING(bool, diag_tree_rowcount, true);
std::atomic<bool> g_trc_enabled{true};
safetyhook::MidHook g_trc_hook;
std::atomic<int> g_trc_log{0};

SWOFTY_ON_CHANGE(diag_tree_rowcount, bool on)
{
    g_trc_enabled.store(on);
    swofty::log("[rnd-tyres] diag_tree_rowcount toggled -> %s", on ? "on" : "off");
}

void on_tree_rowcount(safetyhook::Context &c)
{
    if (!g_trc_enabled.load())
        return;
    uintptr_t rsp = c.rsp;
    const void *code_pp = reinterpret_cast<const void *>(rsp + 0x58);
    const void *rc_p = reinterpret_cast<const void *>(rsp + 0x378);
    if (!region_readable(code_pp, 8) || !region_readable(rc_p, 4))
        return;
    uintptr_t code_ptr = *reinterpret_cast<const uintptr_t *>(code_pp);
    if (!region_readable(reinterpret_cast<const void *>(code_ptr), 1))
        return;
    unsigned code = *reinterpret_cast<const uint8_t *>(code_ptr);
    uint32_t row_count = *reinterpret_cast<const uint32_t *>(rc_p);
    int n = g_trc_log.fetch_add(1);
    if (code == 4) {
        if (n < 48)
            swofty::log("[rnd-tyres] tree-rows code=4 FAKE/TYRES: tree-view data source row_count=%u "
                        "(tiles the data layer yields for the Tyres branch; 4 = hub+our 3 loaded -> blocker is "
                        "the DISPLAY/active-dept filter; 1 = _upgrade_instancecount_ bump NOT honoured -> data)",
                        row_count);
    } else if (n < 16) {
        swofty::log("[rnd-tyres] tree-rows code=%u: tree-view data source row_count=%u", code, row_count);
    }
}

void install_tree_rowcount_probe(uintptr_t base)
{
    if (!g_trc_enabled.load()) {
        swofty::log("[rnd-tyres] diag_tree_rowcount off; tree-rows diagnostic NOT installed (SAFE)");
        return;
    }
    uintptr_t hit = 0;
    int matches =
        swofty::api->scan(".xtls", "83 BC 24 78 03 00 00 00 0F 84 F3 FE FF FF 45 31 C0 EB 2A 66", &hit);
    if (matches != 1 || !hit) {
        swofty::log("[rnd-tyres] tree-rows site signature matched %d time(s); diagnostic NOT installed (SAFE)",
                    matches);
        return;
    }
    if (!region_readable(reinterpret_cast<void *>(hit), 16)) {
        swofty::log("[rnd-tyres] tree-rows site @0x%llx not committed; NOT hooking (SAFE)",
                    (unsigned long long)hit);
        return;
    }
    auto r = safetyhook::MidHook::create(reinterpret_cast<void *>(hit), &on_tree_rowcount);
    if (!r) {
        swofty::log("[rnd-tyres] tree-rows MidHook::create failed @0x%llx (err %d); NOT installed (SAFE)",
                    (unsigned long long)hit, (int)r.error().type);
        return;
    }
    g_trc_hook = std::move(*r);
    swofty::log("[rnd-tyres] tree-rows diagnostic installed @0x%llx (base+0x%llx); diag_tree_rowcount=on",
                (unsigned long long)hit, (unsigned long long)(hit - base));
}

// ---------------------------------------------------------------------------
// R&D overview box-grid selection (v0.5.0 leftover, default OFF since v0.6.0).
//
// v0.5.0 relaxed two `cmp al,4 ; cmovae eax,0` clamps on DAT_145f93c08 to make
// dept 4 "selectable". Both were dead ends:
//   - DAT_145f93c08 is the R&D > Performance overview box-grid selection. A
//     hardware watchpoint proved it never changes while the department tab pages
//     are built, so it does not pick the page that lists upgrade tiles.
//   - the second clamp (TeamHQ VM command 0x55702d31 @0x1423380e9) indexes a
//     table whose slot 4 is "open_driver_ratings_rtt_panel"; it is unrelated to
//     R&D. That relax is REMOVED in v0.6.0.
// The real blocker is the upgrade-instance state machine (see the v0.6.0 section
// below). What stays: the box-grid name guard (always installed, a no-op for
// idx 0..3, keeps nameTable[4] = colour[0] from ever being dereferenced) and the
// box-grid clamp relax behind select_fifth_dept, now default off.

SWOFTY_SETTING(bool, select_fifth_dept, false);
std::atomic<bool> g_select_enabled{false};
safetyhook::MidHook g_name_hook;
std::atomic<int> g_name_log{0};

SWOFTY_ON_CHANGE(select_fifth_dept, bool on)
{
    swofty::log("[rnd-tyres] select_fifth_dept toggled -> %s; the box-grid clamp byte-patch is install-time, "
                "relaunch to apply",
                on ? "on" : "off");
}

// A valid C-string for active-dept index 4, so the box-grid name read never
// dereferences nameTable[4] (= colour[0], a wild char*). Static: lives forever.
const char kTyresDeptName[] = "Tyres & Thermal Management";

// Box-grid name read, hooked right after `mov rdx,[k_Career_RnDDeptNameTable +
// idx*8]`: rax = idx, rdx = the loaded name char*. For idx 4 rdx would be
// colour[0] as a char*; replace it with our name. No-op for idx 0..3.
void on_dept_name_read(safetyhook::Context &c)
{
    if ((c.rax & 0xffull) == 4ull) {
        c.rdx = reinterpret_cast<uintptr_t>(kTyresDeptName);
        int n = g_name_log.fetch_add(1);
        if (n < 16)
            swofty::log("[rnd-tyres] dept-name[4] overrun intercepted -> \"%s\" (nameTable[4]=colour[0] char* avoided)",
                        kTyresDeptName);
    }
}

// Verify a single clamp immediate byte is the stock 0x04, then relax it to 0x05.
bool relax_clamp_imm(uintptr_t imm_addr, const char *what)
{
    if (!region_readable(reinterpret_cast<void *>(imm_addr), 1)) {
        swofty::log("[rnd-tyres] %s @0x%llx not readable; NOT relaxing (SAFE)", what, (unsigned long long)imm_addr);
        return false;
    }
    uint8_t cur = *reinterpret_cast<volatile uint8_t *>(imm_addr);
    if (cur != 0x04) {
        swofty::log("[rnd-tyres] %s @0x%llx is 0x%02x not the stock 0x04; NOT relaxing (SAFE)", what,
                    (unsigned long long)imm_addr, (unsigned)cur);
        return false;
    }
    void *p = reinterpret_cast<void *>(imm_addr);
    DWORD old = 0;
    if (!VirtualProtect(p, 1, PAGE_EXECUTE_READWRITE, &old)) {
        swofty::log("[rnd-tyres] %s VirtualProtect failed @0x%llx; NOT relaxing (SAFE)", what,
                    (unsigned long long)imm_addr);
        return false;
    }
    *reinterpret_cast<volatile uint8_t *>(p) = 0x05;
    DWORD tmp = 0;
    VirtualProtect(p, 1, old, &tmp);
    swofty::log("[rnd-tyres] %s relaxed: cmp al,4 -> cmp al,5 @0x%llx", what, (unsigned long long)imm_addr);
    return true;
}

// One unique `.xtls` scan. The name guard is installed regardless of the flag
// (pure guard); the clamp is relaxed only when select_fifth_dept is on, and only
// after the guard is live.
void install_box_name_guard(uintptr_t base)
{
    uintptr_t hit_box = 0;
    int mb = swofty::api->scan(
        ".xtls", "0F B6 05 83 CF 30 04 31 C9 3C 04 0F 43 C1 48 0F BE C0 48 8D 0D C1 F1 AC 03 48 8B 14 C1", &hit_box);
    if (mb != 1 || !hit_box) {
        swofty::log("[rnd-tyres] box-grid clamp/name signature matched %d time(s); name guard NOT installed (SAFE)",
                    mb);
        return;
    }
    uintptr_t name_site = hit_box + 29;
    uintptr_t box_clamp = hit_box + 10;
    if (!region_readable(reinterpret_cast<void *>(name_site), 16)) {
        swofty::log("[rnd-tyres] box-grid name site @0x%llx not committed; NOT installing (SAFE)",
                    (unsigned long long)name_site);
        return;
    }
    auto r = safetyhook::MidHook::create(reinterpret_cast<void *>(name_site), &on_dept_name_read);
    if (!r) {
        swofty::log("[rnd-tyres] box-grid name-read MidHook::create failed @0x%llx (err %d); clamp NOT relaxed (SAFE)",
                    (unsigned long long)name_site, (int)r.error().type);
        return;
    }
    g_name_hook = std::move(*r);
    swofty::log("[rnd-tyres] box-grid name guard installed @0x%llx (base+0x%llx); idx 4 -> \"%s\"",
                (unsigned long long)name_site, (unsigned long long)(name_site - base), kTyresDeptName);

    if (!g_select_enabled.load()) {
        swofty::log("[rnd-tyres] select_fifth_dept off (default); box-grid clamp left stock. The v0.5.0 TeamHQ clamp "
                    "relax is removed.");
        return;
    }
    if (relax_clamp_imm(box_clamp, "box-grid active-dept clamp"))
        swofty::log("[rnd-tyres] box-grid 5th-dept selection ENABLED (name-guarded; overview only, not the tile page)");
}

// ---------------------------------------------------------------------------
// v0.6.0: instantiate the Tyres upgrades (why the Tyres department page is empty).
//
// Per team, the career R&D model (one Upgrades object of 0x1b10 bytes per team,
// built by FUN_14123ca40) holds up to two "trees" at +0xe0 + k*0xc40 (count at
// +0x1960). Career_Instantiate_UpgradeTrees (FUN_14352cac0) copies EVERY loaded
// UpgradeRecord (no department filter) into a 0xe0-byte upgrade INSTANCE (vector
// at tree+0x10/+0x18) and pushes its pointer into the tree's inline array
// (tree+0x30, count tree+0xc30 = Upgrades+0x110/+0xd10). So our three rows ARE
// instantiated; live memory confirms all three in every team's array. Each
// instance has a state history at +0xc0/+0xc8 (0x10-byte entries: +0 owner,
// +4 day, +8 state, +0xc enabled, +0xd facility-locked). The R&D page query
// (FUN_1435188d0) keeps an instance only if its last entry has enabled != 0.
//
// Exact skip mechanism, three layers, all keyed on the department byte +0x72:
//  1. Instance init/reset FUN_143517d80 (KB: Car_Reset_LastFittedUpgrade): at
//     0x143517e0c `cmp byte [rcx+0x72],4 ; jne normal`. A Fake (4) instance gets
//     ONE sentinel entry {owner 0x3fff0000, day 0xffff, state Invalid(5),
//     enabled 0} and returns, skipping the normal init (state Unavailable,
//     enabled 1). The query then drops it. This is what keeps the stock hub
//     (label "default", upgrade_name "central_fake_upgrade") off every page, and
//     it catches our three rows because they are upgrade_department = Fake too.
//  2. Every state mutator bails on dept > 3 with `cmp byte [rcx+0x72],3 ; ja ret`:
//     push state 0x1435230e0, enabled flag 0x143523510, facility lock 0x143523730,
//     fit flag 0x1435157d0, plus the auto-fit pass 0x1412484e0 (0x1412486b8).
//  3. The availability BFS FUN_1435288f0 treats any dept-4 node as the tree HUB
//     (`cmp byte [rax+0x72],4 ; je` @0x143528a05).
// A data-only fix is impossible: the Tyres card filters instances by +0x72 == 4,
// and the same byte must be <= 3 for layers 1-3.
//
// Fix: flip the flags at those conditional jumps for OUR three instances only,
// identified by content (dept 4, team row +0x7c == 0xff, and the node hash +0x58 =
// djb2("tyres_thermal_<n>_255") or upgrade_name), never by address (the init runs
// on a stack temp before the instance is moved into the tree). The hub keeps the
// stock path everywhere. Saves written by v0.4/v0.5 persist the sentinel, and the
// deserializer (FUN_143524c20) restores it over our init, so a sanitizer re-runs
// the game's own init on any of OUR instances whose last entry is still not
// enabled (deserialize end, refresh entry, and right before the page query), then
// mirrors FUN_14352e1c0: mode 0 invalidates the owner cache (+0x1aa8) so the next
// query recomputes availability; modes 1/2 call the refresh at deserialize end.
//
// Gate: one exactly-one `.xtls` scan of the init site (must equal the expected
// RVA), then every other site byte-verified at its fixed RVA. The core sites
// (init, 4 mutators, auto-fit, BFS) are all-or-nothing. Install-time only.

SWOFTY_SETTING(bool, instantiate_tyres_upgrades, true);
std::atomic<bool> g_inst_enabled{true};
std::atomic<bool> g_inst_live{false};

SWOFTY_ON_CHANGE(instantiate_tyres_upgrades, bool on)
{
    swofty::log("[rnd-tyres] instantiate_tyres_upgrades toggled -> %s; the instance hooks are install-time, relaunch "
                "to apply",
                on ? "on" : "off");
}

constexpr uint32_t djb2(const char *s)
{
    uint32_t h = 0x1505;
    for (; *s; ++s)
        h = h * 33u + static_cast<uint8_t>(*s);
    return h;
}

constexpr uint32_t kOurHash[3] = {djb2("tyres_thermal_1_255"), djb2("tyres_thermal_2_255"),
                                  djb2("tyres_thermal_3_255")};
constexpr const char *kOurNames[3] = {"tyres_wear_reduction", "tyres_brake_cooling", "tyres_thermal_reliability"};
static_assert(kOurHash[0] == 0x27e41c13u && kOurHash[1] == 0x27f63494u && kOurHash[2] == 0x28084d15u,
              "node hash drifted from the live instances");
static_assert(djb2("default_255") == 0xbd057d45u, "hub hash drifted from the live instance");

constexpr size_t   kInstSize       = 0xe0;
constexpr size_t   kInstName       = 0x10;
constexpr size_t   kInstHash       = 0x58;
constexpr size_t   kInstDept       = 0x72;
constexpr size_t   kInstRowFlag    = 0x7c;
constexpr size_t   kInstStatesBeg  = 0xc0;
constexpr size_t   kInstStatesEnd  = 0xc8;
constexpr uint8_t  kDeptFake       = 4;
constexpr uint8_t  kTeamRow        = 0xff;
constexpr size_t   kUpSize         = 0x1b10;
constexpr size_t   kUpMode         = 0x8;
constexpr size_t   kUpTeam         = 0xc;
constexpr size_t   kUpTrees        = 0xe0;
constexpr size_t   kUpTreeStride   = 0xc40;
constexpr size_t   kUpNumTrees     = 0x1960;
constexpr size_t   kUpOwnerCache   = 0x1aa8;
constexpr uint32_t kOwnerNone      = 0x3fff0000;
constexpr uintptr_t kFlagZF        = 0x40;

constexpr uintptr_t kRvaInstInitSig = 0x143517df4ull - kImageBase; // scan hit
constexpr uintptr_t kRvaInstInitFn  = 0x143517d80ull - kImageBase; // Upgrade instance init/reset
constexpr uintptr_t kRvaRefreshFn   = 0x14352f300ull - kImageBase; // availability refresh (Upgrades*, u16 day)
constexpr uintptr_t kRvaDeserEnd    = 0x14353610full - kImageBase; // Upgrades deserialize, after the trees
constexpr uintptr_t kRvaPagePre     = 0x141c7d8baull - kImageBase; // dept page, just before the query call

struct GuardSite {
    const char *what;
    uintptr_t verify_rva;  // first byte of the verified window (the cmp)
    uintptr_t hook_rva;    // the conditional jump that gets its flags adjusted
    uint8_t bytes[14];
    size_t len;
};

const GuardSite kInitSite = {"init dept==4 sentinel branch", 0x143517e0cull - kImageBase,
                             0x143517e10ull - kImageBase,
                             {0x80, 0x79, 0x72, 0x04, 0x75, 0x3a, 0x48, 0x83, 0xbe, 0xd0, 0x00, 0x00, 0x00, 0x00}, 14};
const GuardSite kGuardSites[] = {
    {"state push guard", 0x1435230f3ull - kImageBase, 0x1435230f7ull - kImageBase,
     {0x80, 0x79, 0x72, 0x03, 0x0f, 0x87, 0xf3, 0x03, 0x00, 0x00}, 10},
    {"enabled flag guard", 0x143523520ull - kImageBase, 0x143523524ull - kImageBase,
     {0x80, 0x79, 0x72, 0x03, 0x0f, 0x87, 0xe6, 0x01, 0x00, 0x00}, 10},
    {"facility lock guard", 0x143523740ull - kImageBase, 0x143523744ull - kImageBase,
     {0x80, 0x79, 0x72, 0x03, 0x0f, 0x87, 0xe6, 0x01, 0x00, 0x00}, 10},
    {"fit flag guard", 0x1435157e0ull - kImageBase, 0x1435157e4ull - kImageBase,
     {0x80, 0x79, 0x72, 0x03, 0x0f, 0x87, 0x36, 0x02, 0x00, 0x00}, 10},
    {"auto-fit pass guard", 0x1412486b8ull - kImageBase, 0x1412486bcull - kImageBase,
     {0x80, 0x79, 0x72, 0x03, 0x77, 0x92, 0x66, 0x44, 0x89, 0x74, 0x24, 0x20}, 12},
};
const GuardSite kBfsSite = {"availability BFS hub test", 0x143528a05ull - kImageBase, 0x143528a09ull - kImageBase,
                            {0x80, 0x78, 0x72, 0x04, 0x74, 0x7d, 0x48, 0x8b, 0x88, 0xc8, 0x00, 0x00, 0x00}, 13};
constexpr size_t kNumGuards = sizeof(kGuardSites) / sizeof(kGuardSites[0]);

const uint8_t kInitFnBytes[]  = {0x41, 0x56, 0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x48, 0x48, 0x89, 0xce,
                                 0xc7, 0x41, 0x7e, 0xff, 0xff, 0xff, 0xff};
const uint8_t kRefreshBytes[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57,
                                 0x55, 0x53, 0x48, 0x81, 0xec, 0xa8, 0x0c, 0x00, 0x00};
const uint8_t kDeserBytes[]   = {0x48, 0x8d, 0x15, 0x4b, 0x14, 0x31, 0x02, 0x48, 0x89, 0xd9};
const uint8_t kPagePreBytes[] = {0x48, 0x8d, 0x94, 0x24, 0xf0, 0x0d, 0x00, 0x00, 0x4c, 0x8d, 0x84, 0x24,
                                 0x8c, 0x00, 0x00, 0x00, 0x66, 0x41, 0xb9, 0xff, 0xff, 0xe8};

using InstInitFn = void (*)(void *inst);
using RefreshFn  = void (*)(void *upgrades, uint16_t day);

InstInitFn g_inst_init = nullptr;
RefreshFn g_refresh = nullptr;

safetyhook::MidHook g_init_hook;
safetyhook::MidHook g_guard_hooks[kNumGuards];
safetyhook::MidHook g_bfs_hook;
safetyhook::MidHook g_deser_hook;
safetyhook::MidHook g_refresh_hook;
safetyhook::MidHook g_page_pre_hook;

std::atomic<int> g_init_log{0};
std::atomic<int> g_hub_log{0};
std::atomic<int> g_guard_log[kNumGuards];
std::atomic<int> g_bfs_log{0};
std::atomic<int> g_sanitize_log{0};
std::atomic<int> g_sanitize_depth{0};
std::atomic<int> g_sanitize_fail{0};
std::atomic<bool> g_sanitize_off{false};

bool bytes_are(uintptr_t addr, const uint8_t *want, size_t n)
{
    if (!region_readable(reinterpret_cast<void *>(addr), n))
        return false;
    const volatile uint8_t *p = reinterpret_cast<const volatile uint8_t *>(addr);
    for (size_t i = 0; i < n; ++i)
        if (p[i] != want[i])
            return false;
    return true;
}

bool cstr_eq(const char *a, const char *b, size_t cap)
{
    for (size_t i = 0; i < cap; ++i) {
        if (a[i] != b[i])
            return false;
        if (b[i] == '\0')
            return true;
    }
    return false;
}

const char *inst_name(uintptr_t u)
{
    const char *s = *reinterpret_cast<const char *const *>(u + kInstName);
    return (s && region_readable(s, 32)) ? s : "?";
}

int our_index(uintptr_t u)
{
    const uint8_t *p = reinterpret_cast<const uint8_t *>(u);
    if (p[kInstDept] != kDeptFake || p[kInstRowFlag] != kTeamRow)
        return -1;
    uint32_t h = *reinterpret_cast<const uint32_t *>(u + kInstHash);
    for (int i = 0; i < 3; ++i)
        if (h == kOurHash[i])
            return i;
    const char *s = *reinterpret_cast<const char *const *>(u + kInstName);
    if (!s || !region_readable(s, 32))
        return -1;
    for (int i = 0; i < 3; ++i)
        if (cstr_eq(s, kOurNames[i], 32))
            return i;
    return -1;
}

void on_inst_init(safetyhook::Context &c)
{
    uintptr_t u = c.rcx;
    if (*reinterpret_cast<const uint8_t *>(u + kInstDept) != kDeptFake)
        return;
    int idx = our_index(u);
    if (idx < 0) {
        if (g_hub_log.fetch_add(1) < 3)
            swofty::log("[rnd-tyres] instance init: dept-4 \"%s\" hash=0x%08x is not ours -> stock Invalid sentinel "
                        "(hub stays hidden)",
                        inst_name(u), *reinterpret_cast<const uint32_t *>(u + kInstHash));
        return;
    }
    c.rflags &= ~kFlagZF;
    if (g_init_log.fetch_add(1) < 12)
        swofty::log("[rnd-tyres] instance init: tyres_thermal_%d \"%s\" -> normal init (Unavailable, enabled) instead "
                    "of the dept-4 sentinel",
                    idx + 1, inst_name(u));
}

template <size_t I> void on_guard(safetyhook::Context &c)
{
    uintptr_t u = c.rcx;
    if (*reinterpret_cast<const uint8_t *>(u + kInstDept) != kDeptFake)
        return;
    int idx = our_index(u);
    if (idx < 0)
        return;
    c.rflags |= kFlagZF;
    if (g_guard_log[I].fetch_add(1) < 3)
        swofty::log("[rnd-tyres] %s admitted tyres_thermal_%d \"%s\" (dept 4 treated as a real department)",
                    kGuardSites[I].what, idx + 1, inst_name(u));
}

using GuardFn = void (*)(safetyhook::Context &);
template <size_t... I> constexpr auto make_guard_fns(std::index_sequence<I...>)
{
    return std::array<GuardFn, sizeof...(I)>{&on_guard<I>...};
}
constexpr auto kGuardFns = make_guard_fns(std::make_index_sequence<kNumGuards>{});

void on_bfs_hub_test(safetyhook::Context &c)
{
    uintptr_t u = c.rax;
    int idx = our_index(u);
    if (idx < 0)
        return;
    c.rflags &= ~kFlagZF;
    if (g_bfs_log.fetch_add(1) < 3)
        swofty::log("[rnd-tyres] availability BFS: tyres_thermal_%d is a regular node, not the hub", idx + 1);
}

bool states_of(uintptr_t u, uintptr_t &sb, uintptr_t &se)
{
    sb = *reinterpret_cast<const uint64_t *>(u + kInstStatesBeg);
    se = *reinterpret_cast<const uint64_t *>(u + kInstStatesEnd);
    if (sb == se)
        return true;
    if (se < sb || (se - sb) % 0x10 || (se - sb) > 0x10000)
        return false;
    return region_readable(reinterpret_cast<void *>(se - 0x10), 0x10);
}

bool needs_init(uintptr_t u)
{
    uintptr_t sb = 0, se = 0;
    if (!states_of(u, sb, se))
        return false;
    if (sb == se)
        return true;
    return *reinterpret_cast<const uint8_t *>(se - 4) == 0;
}

int sanitize_upgrades(uintptr_t U, const char *site, bool may_refresh)
{
    if (!g_inst_live.load() || !g_inst_init || g_sanitize_off.load() || g_sanitize_depth.load() != 0)
        return 0;
    if (!U || !region_readable(reinterpret_cast<void *>(U), kUpSize))
        return 0;
    uint64_t nt = *reinterpret_cast<const uint64_t *>(U + kUpNumTrees);
    if (nt == 0 || nt > 2)
        return 0;
    g_sanitize_depth.fetch_add(1);
    int reinit = 0;
    for (uint64_t k = 0; k < nt; ++k) {
        uintptr_t T = U + kUpTrees + k * kUpTreeStride;
        uintptr_t b = *reinterpret_cast<const uint64_t *>(T + 0x10);
        uintptr_t e = *reinterpret_cast<const uint64_t *>(T + 0x18);
        if (!b || e < b || (e - b) % kInstSize || (e - b) / kInstSize > 0x180)
            continue;
        if (!region_readable(reinterpret_cast<void *>(b), e - b))
            continue;
        for (uintptr_t u = b; u < e; u += kInstSize) {
            if (*reinterpret_cast<const uint8_t *>(u + kInstDept) != kDeptFake)
                continue;
            int idx = our_index(u);
            if (idx < 0 || !needs_init(u))
                continue;
            g_inst_init(reinterpret_cast<void *>(u));
            ++reinit;
            uintptr_t sb = 0, se = 0;
            bool ok = states_of(u, sb, se) && se != sb;
            if (needs_init(u) && g_sanitize_fail.fetch_add(1) + 1 >= 8) {
                g_sanitize_off.store(true);
                swofty::log("[rnd-tyres] sanitize(%s): the game's init left \"%s\" disabled 8 times; sanitizer switched "
                            "OFF (SAFE)",
                            site, inst_name(u));
            }
            if (g_sanitize_log.fetch_add(1) < 48)
                swofty::log("[rnd-tyres] sanitize(%s): team 0x%x tree%llu tyres_thermal_%d \"%s\" had no enabled state "
                            "(persisted dept-4 sentinel) -> re-initialised: states=%llu last state=%u enabled=%u",
                            site, *reinterpret_cast<const uint32_t *>(U + kUpTeam), (unsigned long long)k, idx + 1,
                            inst_name(u), ok ? (unsigned long long)((se - sb) / 0x10) : 0ull,
                            ok ? (unsigned)*reinterpret_cast<const uint8_t *>(se - 8) : 0xffu,
                            ok ? (unsigned)*reinterpret_cast<const uint8_t *>(se - 4) : 0xffu);
        }
    }
    if (reinit) {
        uint8_t mode = *reinterpret_cast<const uint8_t *>(U + kUpMode);
        if (mode == 0) {
            *reinterpret_cast<volatile uint32_t *>(U + kUpOwnerCache) = kOwnerNone;
            swofty::log("[rnd-tyres] sanitize(%s): mode 0 -> owner cache invalidated so the next R&D query recomputes "
                        "availability",
                        site);
        } else if ((mode == 1 || mode == 2) && may_refresh && g_refresh) {
            g_refresh(reinterpret_cast<void *>(U), 0);
            swofty::log("[rnd-tyres] sanitize(%s): mode %u -> availability refreshed (day 0, as the game's reset does)",
                        site, (unsigned)mode);
        }
    }
    g_sanitize_depth.fetch_sub(1);
    return reinit;
}

void on_deser_end(safetyhook::Context &c)
{
    sanitize_upgrades(c.rdi, "load", true);
}

void on_refresh_entry(safetyhook::Context &c)
{
    sanitize_upgrades(c.rcx, "refresh", false);
}

void on_page_pre_query(safetyhook::Context &c)
{
    sanitize_upgrades(c.rcx, "page", false);
}

bool hook_flag_site(safetyhook::MidHook &slot, uintptr_t base, const GuardSite &s, GuardFn fn)
{
    auto r = safetyhook::MidHook::create(reinterpret_cast<void *>(base + s.hook_rva), fn);
    if (!r) {
        swofty::log("[rnd-tyres] %s MidHook::create failed @0x%llx (err %d)", s.what,
                    (unsigned long long)(base + s.hook_rva), (int)r.error().type);
        return false;
    }
    slot = std::move(*r);
    swofty::log("[rnd-tyres] %s hooked @0x%llx (base+0x%llx)", s.what, (unsigned long long)(base + s.hook_rva),
                (unsigned long long)s.hook_rva);
    return true;
}

void unhook_core()
{
    g_init_hook = {};
    for (auto &h : g_guard_hooks)
        h = {};
    g_bfs_hook = {};
}

void install_optional_site(safetyhook::MidHook &slot, uintptr_t addr, const uint8_t *bytes, size_t n,
                           void (*fn)(safetyhook::Context &), const char *what, uintptr_t base)
{
    if (!bytes_are(addr, bytes, n)) {
        swofty::log("[rnd-tyres] %s site @0x%llx bytes differ; that sanitizer is NOT installed (SAFE)", what,
                    (unsigned long long)addr);
        return;
    }
    auto r = safetyhook::MidHook::create(reinterpret_cast<void *>(addr), fn);
    if (!r) {
        swofty::log("[rnd-tyres] %s MidHook::create failed @0x%llx (err %d); NOT installed (SAFE)", what,
                    (unsigned long long)addr, (int)r.error().type);
        return;
    }
    slot = std::move(*r);
    swofty::log("[rnd-tyres] %s sanitizer installed @0x%llx (base+0x%llx)", what, (unsigned long long)addr,
                (unsigned long long)(addr - base));
}

void install_instantiate_tyres(uintptr_t base)
{
    if (!g_inst_enabled.load()) {
        swofty::log("[rnd-tyres] instantiate_tyres_upgrades off; Tyres upgrades keep the stock dept-4 sentinel "
                    "(page stays empty)");
        return;
    }
    uintptr_t hit = 0;
    int matches = swofty::api->scan(
        ".xtls", "C7 81 D8 00 00 00 00 00 00 00 48 8B 81 C0 00 00 00 48 89 81 C8 00 00 00 80 79 72 04 75 3A", &hit);
    if (matches != 1 || !hit) {
        swofty::log("[rnd-tyres] instance-init signature matched %d time(s); Tyres instances NOT enabled (SAFE)",
                    matches);
        return;
    }
    if (hit != base + kRvaInstInitSig) {
        swofty::log("[rnd-tyres] instance-init scan hit 0x%llx != expected 0x%llx; Tyres instances NOT enabled (SAFE)",
                    (unsigned long long)hit, (unsigned long long)(base + kRvaInstInitSig));
        return;
    }

    bool ok = bytes_are(base + kInitSite.verify_rva, kInitSite.bytes, kInitSite.len)
              && bytes_are(base + kBfsSite.verify_rva, kBfsSite.bytes, kBfsSite.len)
              && bytes_are(base + kRvaInstInitFn, kInitFnBytes, sizeof kInitFnBytes);
    for (size_t i = 0; ok && i < kNumGuards; ++i) {
        if (!bytes_are(base + kGuardSites[i].verify_rva, kGuardSites[i].bytes, kGuardSites[i].len)) {
            swofty::log("[rnd-tyres] %s @0x%llx bytes differ from the decompile", kGuardSites[i].what,
                        (unsigned long long)(base + kGuardSites[i].verify_rva));
            ok = false;
        }
    }
    if (!ok) {
        swofty::log("[rnd-tyres] instance core sites do not match this build; Tyres instances NOT enabled (SAFE)");
        return;
    }

    for (size_t i = 0; i < kNumGuards; ++i) {
        if (!hook_flag_site(g_guard_hooks[i], base, kGuardSites[i], kGuardFns[i])) {
            unhook_core();
            swofty::log("[rnd-tyres] instance core install aborted and rolled back (SAFE)");
            return;
        }
    }
    if (!hook_flag_site(g_bfs_hook, base, kBfsSite, &on_bfs_hub_test)
        || !hook_flag_site(g_init_hook, base, kInitSite, &on_inst_init)) {
        unhook_core();
        swofty::log("[rnd-tyres] instance core install aborted and rolled back (SAFE)");
        return;
    }
    g_inst_init = reinterpret_cast<InstInitFn>(base + kRvaInstInitFn);
    if (bytes_are(base + kRvaRefreshFn, kRefreshBytes, sizeof kRefreshBytes))
        g_refresh = reinterpret_cast<RefreshFn>(base + kRvaRefreshFn);
    g_inst_live.store(true);
    swofty::log("[rnd-tyres] Tyres upgrade instances ENABLED: tyres_thermal_1/2/3 (hash 0x%08x/0x%08x/0x%08x) take the "
                "normal init + state machine; the dept-4 hub \"default\"/central_fake_upgrade keeps the stock sentinel",
                kOurHash[0], kOurHash[1], kOurHash[2]);

    install_optional_site(g_deser_hook, base + kRvaDeserEnd, kDeserBytes, sizeof kDeserBytes, &on_deser_end,
                          "load (deserialize end)", base);
    if (g_refresh)
        install_optional_site(g_refresh_hook, base + kRvaRefreshFn, kRefreshBytes, sizeof kRefreshBytes,
                              &on_refresh_entry, "refresh entry", base);
    install_optional_site(g_page_pre_hook, base + kRvaPagePre, kPagePreBytes, sizeof kPagePreBytes,
                          &on_page_pre_query, "R&D page pre-query", base);
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

    // The live development-potential pie 5th slice - its OWN independent gate +
    // feature flag (default off; the wedge geometry is deferred, see above).
    g_pie_enabled.store(render_fifth_pie.get());
    install_fifth_pie(base);

    // The 5th-box layout diagnostic - its OWN independent gate + feature flag
    // (read-only; captures why the injected box has no visible 2x2 cell).
    g_layout_enabled.store(render_fifth_box_layout.get());
    install_box_layout_probe(base);

    // Upgrade-tree attachment instrument - its OWN independent gate + feature flag
    // (read-only; resolves the per-save <team_link>.fake upgrade container).
    g_tree_enabled.store(attach_tyres_tree.get());
    install_tree_probe(base);

    // Upgrade-tree tile-source-count diagnostic - its OWN independent gate + feature
    // flag (read-only; logs the code-4 tree-view row count to bisect data vs display).
    g_trc_enabled.store(diag_tree_rowcount.get());
    install_tree_rowcount_probe(base);

    // Box-grid name guard (always) + the v0.5.0 overview clamp relax (select_fifth_dept,
    // default off). The v0.5.0 TeamHQ clamp relax is gone.
    g_select_enabled.store(select_fifth_dept.get());
    install_box_name_guard(base);

    // v0.6.0: give the three Tyres upgrade instances real R&D states (the actual
    // reason the Tyres page was empty) - its OWN one-scan gate + feature flag.
    g_inst_enabled.store(instantiate_tyres_upgrades.get());
    install_instantiate_tyres(base);

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
