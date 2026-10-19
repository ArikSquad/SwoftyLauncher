// SPDX-License-Identifier: AGPL-3.0-or-later
//
// R&D: Tyres & Thermal Management - the development-potential pie's 5th slice.
//
// The addon's render_fifth_pie piece makes Career_Populate_RnDPieSlices
// (0x141f48030) process department code 4 (buffer 0x900->0xb40, 5th slot
// constructed after the pie ctor and gated at populate, `code < 4` guard
// relaxed). On its own that code-4 pass takes the jump-table DEFAULT case:
// name "error_department", description key
// lng_rnd_dept_error_department_overview_description, colour white, and an icon
// path overrides.career.rnd_wheel_icon.error_department that does not exist. The
// pie wedges themselves are bound in laptop_r_and_d.ui to a SECOND data source,
// focused_team_perf_root.department_performance.<dept>, published by the
// per-team performance publisher 0x1418914f0, which only ever lists departments
// that have native stat entries (codes 0..3 in the stock data) and names code 4
// "error_department" too.
//
// This translation unit finishes the job in three independently gated pieces:
//
//  pie_tyres_slice  - populate: code 4 becomes "tyres" (VM child `tyres`,
//                     department_name "tyres", lng_rnd_dept_tyres,
//                     lng_rnd_dept_tyres_overview_description), colour
//                     0xff0078ff, and the wheel icon borrows the `maintenance`
//                     frame (overrides.career.rnd_wheel_icon has no tyres frame).
//  pie_tyres_focus  - department_focused / highlight handler (0x141f4a9c0): the
//                     data string "tyres" now focuses the `tyres` child and raises
//                     highlight_pie_sector like the stock four, WITHOUT writing
//                     the box-grid selection index DAT_145f93c08.
//  pie_tyres_open   - the Tyres tile opens the Tyres department page: a "Tyres is
//                     the focused department" flag replaces the stale stock
//                     selection index for the departments screen's first tab, and
//                     the Performance page restores its focus to the Tyres tile.
//  pie_tyres_performance
//                   - publisher: department code 4 is added to the department
//                     loop that publishes department_performance (and only that
//                     loop), published as `tyres` with lng_rnd_dept_tyres,
//                     rnd_goto_tyres_department, colour 0xff0078ff, and a real
//                     perf / potential fraction derived from the car attributes
//                     the Tyres upgrades develop.
//
// Every piece: one exactly-one `.xtls` scan (hit must equal the expected RVA),
// every other site byte-verified at its fixed RVA (no ASLR), all hooks of a piece
// created all-or-nothing, every pointer a hook dereferences region-checked, and
// every hook acts ONLY on department code 4 - the stock four never change. The
// whole file is inert unless the addon's render_fifth_pie (default off) is on;
// the slice and focus pieces additionally require its code patches to be live
// in memory (populate guard imm == 4, slice buffer imm == 0xb40).
//
// All addresses are for build d873a1c8 (image base 0x140000000). See SITES.md
// for the disassembly that proves each site.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <utility>

#include <safetyhook.hpp>
#include <swofty/mixin.hpp>

#include "rnd_fifth_pie.hpp"

namespace {

constexpr uintptr_t kImageBase = 0x140000000ull;

constexpr uintptr_t rva(uintptr_t va)
{
    return va - kImageBase;
}

constexpr uint32_t djb2(const char *s)
{
    uint32_t h = 0x1505;
    for (; *s; ++s)
        h = h * 33u + static_cast<uint8_t>(*s);
    return h;
}

constexpr uint8_t kDeptTyres = 4;
constexpr uint32_t kColTyres = 0xff0078ff; // ABGR = RGB(255,120,0) orange
constexpr uint32_t kColWhite = 0xffffffff;

// The short name every code-4 lookup resolves to. The VM child it creates is
// djb2("tyres") under the same parent the stock aero/chassis/... children use.
const char kTyresName[] = "tyres";
// overrides.career.rnd_wheel_icon frames: aero chassis maintenance engine
// regulation shutdown (overrides.uidb). No `tyres` frame, so code 4 borrows one.
const char kIconBorrow[] = "maintenance";
static_assert(djb2("tyres") == 0x107666dcu, "tyres child hash drifted");

constexpr uintptr_t kVaErrorDept = 0x14587e10dull; // "error_department" (the default-case name)
const uint8_t kErrorDeptBytes[] = {'e', 'r', 'r', 'o', 'r', '_', 'd', 'e', 'p', 'a', 'r', 't', 'm', 'e', 'n', 't', 0};

// The addon's render_fifth_pie code patches, read back to prove they are live.
constexpr uintptr_t kVaPieGuardImm = 0x141f4821bull; // cmp r14d,3 -> 4
constexpr uintptr_t kVaPieBufImm   = 0x141f46f76ull; // mov edx,0x900 -> 0xb40

std::atomic<uintptr_t> g_err{0};

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

// Reads stop at the first mismatch or at the literal's terminator, so a valid
// engine C-string is never read past its own NUL.
bool cstr_is(const char *s, const char *lit)
{
    for (size_t i = 0;; ++i) {
        if (s[i] != lit[i])
            return false;
        if (lit[i] == '\0')
            return true;
    }
}

bool code4(const safetyhook::Context &c)
{
    return static_cast<uint32_t>(c.r14) == kDeptTyres;
}

int scan_one(const char *pattern, uintptr_t expect, const char *what)
{
    uintptr_t hit = 0;
    int matches = swofty::api->scan(".xtls", pattern, &hit);
    if (matches != 1 || !hit) {
        swofty::log("[rnd-tyres] pie5 %s: signature matched %d time(s); NOT installed (SAFE)", what, matches);
        return 0;
    }
    if (hit != expect) {
        swofty::log("[rnd-tyres] pie5 %s: scan hit 0x%llx != expected 0x%llx; NOT installed (SAFE)", what,
                    (unsigned long long)hit, (unsigned long long)expect);
        return 0;
    }
    return 1;
}

struct Site {
    uintptr_t va;
    void (*fn)(safetyhook::Context &);
    const char *what;
};

// Creates every hook of a piece or none: on any failure the already-created
// hooks are destroyed (unhooked) when `tmp` goes out of scope.
template <size_t N>
bool create_all(uintptr_t base, const Site (&sites)[N], std::array<safetyhook::MidHook, N> &out, const char *piece)
{
    std::array<safetyhook::MidHook, N> tmp;
    for (size_t i = 0; i < N; ++i) {
        uintptr_t at = base + rva(sites[i].va);
        auto r = safetyhook::MidHook::create(reinterpret_cast<void *>(at), sites[i].fn);
        if (!r) {
            swofty::log("[rnd-tyres] pie5 %s: MidHook::create failed for %s @0x%llx (err %d); piece rolled back, "
                        "NOT installed (SAFE)",
                        piece, sites[i].what, (unsigned long long)at, (int)r.error().type);
            return false;
        }
        tmp[i] = std::move(*r);
    }
    out = std::move(tmp);
    for (size_t i = 0; i < N; ++i)
        swofty::log("[rnd-tyres] pie5 %s: hooked %s @0x%llx (base+0x%llx)", piece, sites[i].what,
                    (unsigned long long)(base + rva(sites[i].va)), (unsigned long long)rva(sites[i].va));
    return true;
}

// ---------------------------------------------------------------------------
// Piece 1 - pie_tyres_slice: Career_Populate_RnDPieSlices (0x141f48030).
//
// r14d holds the department code for the whole loop body (written once, at
// 0x141f48213 `movzx r14d, byte [rbp]`, callee-saved across every call).
//   H1 0x141f482a0  join after the name jump table; rdi = name. Default case
//                   left rdi = "error_department" -> "tyres". Everything that
//                   follows (lng_rnd_dept_%s, the VM child hash, department_name
//                   via Vm_Holder_SetCStr, the icon path) reads rdi.
//   H2 0x141f485ab  the description Ne_Format_String call in the colour-switch
//                   DEFAULT case; r8 = the literal "error_department" -> "tyres"
//                   (lng_rnd_dept_tyres_overview_description).
//   H3 0x141f485b5  right after `mov eax,0xffffffff` (default colour); eax ->
//                   0xff0078ff before the join stores it to [rsp+0x64] (the
//                   colour later written to the slot's colour node).
//   H4 0x141f48a64  the icon path Format("%s.%s.%s", pkg, "rnd_wheel_icon",
//                   name): the 5th arg [rsp+0x20] was just set from rdi; when it
//                   is our "tyres" it becomes "maintenance" (borrowed frame).

SWOFTY_SETTING(bool, pie_tyres_slice, true);

SWOFTY_ON_CHANGE(pie_tyres_slice, bool on)
{
    swofty::log("[rnd-tyres] pie_tyres_slice toggled -> %s; install-time, relaunch to apply", on ? "on" : "off");
}

constexpr uintptr_t kVaSliceSig  = 0x141f48253ull; // name-switch default case (scan anchor)
constexpr uintptr_t kVaSliceName = 0x141f482a0ull; // H1
constexpr uintptr_t kVaSliceDesc = 0x141f485abull; // H2
constexpr uintptr_t kVaSliceCol  = 0x141f485b5ull; // H3
constexpr uintptr_t kVaSliceIcon = 0x141f48a64ull; // H4

const char kSliceSig[] = "48 C7 44 24 50 00 00 00 00 48 8D 3D AA 5E 93 03 EB 3B 48 C7 44 24 50 00 00 00 00";

const uint8_t kSliceNameBytes[] = {0x48, 0x8b, 0x5c, 0x24, 0x40};
// 0x141f485a4: lea r8,[error_department] ; call Ne_Format_String ; mov eax,0xffffffff
const uint8_t kSliceDefaultBytes[] = {0x4c, 0x8d, 0x05, 0x62, 0x5b, 0x93, 0x03, 0xe8, 0x40,
                                      0xab, 0x21, 0x02, 0xb8, 0xff, 0xff, 0xff, 0xff};
const uint8_t kSliceColBytes[] = {0x48, 0x8d, 0xb4, 0x24, 0xb0, 0x01, 0x00, 0x00};
// 0x141f48a55: mov [rsp+0x20],rdi ; mov rcx,rbx ; lea rdx,["%s.%s.%s"] ; lea r9,["rnd_wheel_icon"]
const uint8_t kSliceIconBytes[] = {0x48, 0x89, 0x7c, 0x24, 0x20, 0x48, 0x89, 0xd9, 0x48, 0x8d, 0x15,
                                   0x55, 0xd7, 0x6e, 0x03, 0x4c, 0x8d, 0x0d, 0x35, 0x58, 0x81, 0x03};

std::array<safetyhook::MidHook, 4> g_slice_hooks;
std::atomic<int> g_slice_log[4];

void on_slice_name(safetyhook::Context &c)
{
    if (!code4(c) || c.rdi != g_err.load())
        return;
    c.rdi = reinterpret_cast<uintptr_t>(kTyresName);
    if (g_slice_log[0].fetch_add(1) < 4)
        swofty::log("[rnd-tyres] pie5 slice: code 4 name error_department -> tyres (VM child tyres, "
                    "lng_rnd_dept_tyres)");
}

void on_slice_desc(safetyhook::Context &c)
{
    if (!code4(c) || c.r8 != g_err.load())
        return;
    c.r8 = reinterpret_cast<uintptr_t>(kTyresName);
    if (g_slice_log[1].fetch_add(1) < 4)
        swofty::log("[rnd-tyres] pie5 slice: code 4 description key -> lng_rnd_dept_tyres_overview_description");
}

void on_slice_colour(safetyhook::Context &c)
{
    if (!code4(c) || static_cast<uint32_t>(c.rax) != kColWhite)
        return;
    c.rax = kColTyres;
    if (g_slice_log[2].fetch_add(1) < 4)
        swofty::log("[rnd-tyres] pie5 slice: code 4 colour white -> 0x%08x", kColTyres);
}

void on_slice_icon(safetyhook::Context &c)
{
    if (!code4(c))
        return;
    auto *arg5 = reinterpret_cast<uintptr_t *>(c.rsp + 0x20);
    if (*arg5 != reinterpret_cast<uintptr_t>(kTyresName))
        return;
    *arg5 = reinterpret_cast<uintptr_t>(kIconBorrow);
    if (g_slice_log[3].fetch_add(1) < 4)
        swofty::log("[rnd-tyres] pie5 slice: code 4 wheel icon -> rnd_wheel_icon.maintenance (no tyres frame)");
}

void install_slice(uintptr_t base)
{
    if (!scan_one(kSliceSig, base + rva(kVaSliceSig), "slice"))
        return;
    if (!bytes_are(base + rva(kVaSliceName), kSliceNameBytes, sizeof kSliceNameBytes)
        || !bytes_are(base + rva(0x141f485a4ull), kSliceDefaultBytes, sizeof kSliceDefaultBytes)
        || !bytes_are(base + rva(kVaSliceCol), kSliceColBytes, sizeof kSliceColBytes)
        || !bytes_are(base + rva(0x141f48a55ull), kSliceIconBytes, sizeof kSliceIconBytes)) {
        swofty::log("[rnd-tyres] pie5 slice: site bytes differ from the disassembly; NOT installed (SAFE)");
        return;
    }
    static const Site sites[] = {
        {kVaSliceName, &on_slice_name, "populate name join (rdi)"},
        {kVaSliceDesc, &on_slice_desc, "populate default description (r8)"},
        {kVaSliceCol, &on_slice_colour, "populate default colour (eax)"},
        {kVaSliceIcon, &on_slice_icon, "populate wheel-icon arg ([rsp+0x20])"},
    };
    if (create_all(base, sites, g_slice_hooks, "slice"))
        swofty::log("[rnd-tyres] pie5 slice INSTALLED: code 4 populates as tyres / 0x%08x / icon maintenance",
                    kColTyres);
}

// ---------------------------------------------------------------------------
// Piece 2 - pie_tyres_focus: 0x141f4a9c0 (department_focused is dispatched to it
// by the class-B command handler 0x141f4ac40, cmd 0x3aae34e1 = djb2
// "department_focused", which tail-jumps here with the data string; it is also
// called from 0x141f4a959).
//
// The function compares the data chars (rbx) to aero/chassis/maintenance/engine,
// returns on no match, otherwise stores the index to DAT_145f93c08 (0x141f4aa49)
// and runs the common block at 0x141f4aa4f with rsi = this, rdi = the name:
// Vm_GetOrCreate_ChildNode(this->vm+8, djb2(name)) -> set as focused department
// (this->[0x670]+0x40) -> raise highlight_pie_sector.
//   F1 0x141f4aa28  just before the "engine" strcmp (rdi = "engine", rbx = data).
//                   For data "tyres": rdi = "tyres", resume at 0x141f4aa4f
//                   (ctx.rip), skipping the engine compare AND the index store.
// safetyhook's x64 mid-hook stub returns with `pop rsp ; ret` through the rip
// slot of the saved context, so a written ctx.rip resumes there with the original
// rsp (see SITES.md). The common block needs only rsi/rdi and the frame.

SWOFTY_SETTING(bool, pie_tyres_focus, true);

SWOFTY_ON_CHANGE(pie_tyres_focus, bool on)
{
    swofty::log("[rnd-tyres] pie_tyres_focus toggled -> %s; install-time, relaunch to apply", on ? "on" : "off");
}

constexpr uintptr_t kVaFocusSig    = 0x141f4aa21ull; // lea rdi,["engine"] ... (scan anchor)
constexpr uintptr_t kVaFocusHook   = 0x141f4aa28ull; // F1
constexpr uintptr_t kVaFocusResume = 0x141f4aa4full; // common block after the index store

const char kFocusSig[] = "48 8D 3D 7B 5E 72 03 48 89 D9 48 89 FA E8 1D 2E 41 03 85 C0 0F 85 F2 01 00 00 B0 03";
// 0x141f4aa49: mov [DAT_145f93c08],al ; mov rax,[rsi+0x10] ; lea rdx,[rax+8]
const uint8_t kFocusJoinBytes[] = {0x88, 0x05, 0xb9, 0x91, 0x04, 0x04, 0x48, 0x8b, 0x46, 0x10, 0x48, 0x8d, 0x50, 0x08};

std::array<safetyhook::MidHook, 1> g_focus_hooks;
std::atomic<uintptr_t> g_focus_resume{0};
std::atomic<int> g_focus_log{0};
std::atomic<bool> g_tyres_focus{false};

void on_focus(safetyhook::Context &c)
{
    const char *s = reinterpret_cast<const char *>(c.rbx);
    uintptr_t resume = g_focus_resume.load();
    if (!s || !resume || !region_readable(s, 1) || !cstr_is(s, kTyresName))
        return;
    c.rdi = reinterpret_cast<uintptr_t>(kTyresName);
    c.rip = resume;
    g_tyres_focus.store(true);
    if (g_focus_log.fetch_add(1) < 8)
        swofty::log("[rnd-tyres] pie5 focus: department_focused \"tyres\" -> focused_department = tyres, "
                    "highlight_pie_sector raised (selection index untouched)");
}

void install_focus(uintptr_t base)
{
    if (!scan_one(kFocusSig, base + rva(kVaFocusSig), "focus"))
        return;
    if (!bytes_are(base + rva(0x141f4aa49ull), kFocusJoinBytes, sizeof kFocusJoinBytes)) {
        swofty::log("[rnd-tyres] pie5 focus: common-block bytes differ from the disassembly; NOT installed (SAFE)");
        return;
    }
    g_focus_resume.store(base + rva(kVaFocusResume));
    static const Site sites[] = {{kVaFocusHook, &on_focus, "focus handler pre-engine compare"}};
    if (create_all(base, sites, g_focus_hooks, "focus"))
        swofty::log("[rnd-tyres] pie5 focus INSTALLED: data \"tyres\" resumes at 0x%llx",
                    (unsigned long long)g_focus_resume.load());
    else
        g_focus_resume.store(0);
}

// ---------------------------------------------------------------------------
// Piece 2b - pie_tyres_open: pressing the Tyres tile.
//
// The tile press raises goto_rnd_departments; laptop_container.ui turns it into the
// goto_rnd command and the departments flow opens. Its box grid
// (Career_Build_RnDDeptBoxGrid 0x141c85f70) picks the first tab from the stock
// selection index DAT_145f93c08, clamped < 4. The Tyres focus above never writes
// that index (every pie and box read clamps it to 0), so the page opened whichever
// stock department was focused before Tyres: the v0.7.1 log shows "box-grid builder
// reached ... selection=2" (Durability) right after the Tyres focus.
//
// This piece keeps a "Tyres is the focused department" flag next to the index:
//   set      on_focus (data "tyres")
//   cleared  at the only two writes of DAT_145f93c08, the stock focus paths:
//            0x141f4aa49 (class B, R&D > Performance) and 0x141f460a9 (class A, the
//            Team HQ research overview), `mov [DAT_145f93c08],al`
//   read     by rnd_departments.cpp at the box-grid first-tab join (0x141c86d03),
//            which selects the Tyres tab instead (never a dead end: no Tyres tab ->
//            the stock tab)
//   restore  populate 0x141f48476 `cmp r14b,al ; jne` (al = clamped index, r14 =
//            code): with the flag set, code 4 matches and codes 0..3 do not, so the
//            page's focused_department comes back as tyres when the Performance page
//            is rebuilt, exactly as the stock index does for the stock four.

SWOFTY_SETTING(bool, pie_tyres_open, true);

SWOFTY_ON_CHANGE(pie_tyres_open, bool on)
{
    swofty::log("[rnd-tyres] pie_tyres_open toggled -> %s; install-time, relaunch to apply", on ? "on" : "off");
}

constexpr uintptr_t kVaStoreB      = 0x141f4aa49ull; // class B stock index store (verified by install_focus)
constexpr uintptr_t kVaStoreAWin   = 0x141f4609bull; // class A index select + store
constexpr uintptr_t kVaStoreA      = 0x141f460a9ull;
constexpr uintptr_t kVaRestoreWin  = 0x141f48465ull; // populate focus-restore compare (scan anchor)
constexpr uintptr_t kVaRestore     = 0x141f48476ull;

const char kRestoreSig[] = "0F B6 05 9C B7 04 04 3C 04 B9 00 00 00 00 0F 43 C1 41 38 C6 75 2F";
// 0x141f4609b: mov al,3 ; jmp ; xor eax,eax ; jmp ; mov al,1 ; jmp ; mov al,2 ; mov [DAT_145f93c08],al ; mov rax,[rsi+0x10]
const uint8_t kStoreABytes[] = {0xb0, 0x03, 0xeb, 0x0a, 0x31, 0xc0, 0xeb, 0x06, 0xb0, 0x01, 0xeb, 0x02,
                                0xb0, 0x02, 0x88, 0x05, 0x59, 0xdb, 0x04, 0x04, 0x48, 0x8b, 0x46, 0x10};

std::array<safetyhook::MidHook, 3> g_open_hooks;
std::atomic<bool> g_open_live{false};
std::atomic<int> g_open_log[3];

void clear_tyres_focus(const safetyhook::Context &c, const char *page)
{
    if (!g_tyres_focus.exchange(false))
        return;
    if (g_open_log[0].fetch_add(1) < 16)
        swofty::log("[rnd-tyres] pie5 open: %s focused stock department index %u; Tyres focus cleared", page,
                    static_cast<unsigned>(c.rax & 0xff));
}

void on_store_b(safetyhook::Context &c)
{
    clear_tyres_focus(c, "R&D > Performance");
}

void on_store_a(safetyhook::Context &c)
{
    clear_tyres_focus(c, "Team HQ research overview");
}

void on_restore(safetyhook::Context &c)
{
    if (!g_tyres_focus.load())
        return;
    uint8_t want = code4(c) ? kDeptTyres : 0xff;
    c.rax = (c.rax & ~0xffull) | want;
    if (code4(c) && g_open_log[1].fetch_add(1) < 8)
        swofty::log("[rnd-tyres] pie5 open: populate restores focused_department = tyres (Tyres was the last focused "
                    "tile)");
}

void install_open(uintptr_t base)
{
    if (!g_focus_resume.load()) {
        swofty::log("[rnd-tyres] pie5 open: needs pie_tyres_focus installed; NOT installed (SAFE)");
        return;
    }
    if (!scan_one(kRestoreSig, base + rva(kVaRestoreWin), "open"))
        return;
    if (!bytes_are(base + rva(kVaStoreAWin), kStoreABytes, sizeof kStoreABytes)) {
        swofty::log("[rnd-tyres] pie5 open: Team HQ index store bytes differ from the disassembly; NOT installed (SAFE)");
        return;
    }
    static const Site sites[] = {
        {kVaStoreB, &on_store_b, "R&D > Performance stock index store"},
        {kVaStoreA, &on_store_a, "Team HQ research overview stock index store"},
        {kVaRestore, &on_restore, "populate focus restore compare"},
    };
    if (!create_all(base, sites, g_open_hooks, "open"))
        return;
    g_open_live.store(true);
    swofty::log("[rnd-tyres] pie5 open INSTALLED: the Tyres focus is tracked for the departments screen's first tab "
                "and restored on the Performance page");
}

// ---------------------------------------------------------------------------
// Piece 3 - pie_tyres_performance: the per-team publisher 0x1418914f0.
//
// The department list (FUN_143518cc0: codes whose summed stat max is non-zero)
// lives at [rsp+0xa0] (bytes, capacity 8) with its count at [rsp+0xa8]. It feeds
// several per-team loops (the performance chart's stacked bars and pips) AND the
// department_performance loop at 0x1418945a5. Only the latter gets code 4: the
// stored count is never changed.
//   P0 0x1418945b6  `lea rbx,[rsp+rax] ; add rbx,0xa0` (loop end pointer) with
//                   rax = count: when 4 is absent and count < 8, store 4 at
//                   [rsp+0xa0+count] (unused list storage) and pass count+1.
//   P1 0x1418946d4  after perf ([rsp+0x158]) and potential ([rsp+0x154]) are
//                   stored, xmm0 = F (FUN_143518f50), r8 = name for
//                   "lng_rnd_dept_%s": r8 -> "tyres"; derive the values.
//   P2 0x141894702  rax = name copied into the child-name buffer: -> "tyres"
//                   (child department_performance.tyres).
//   P4 0x141894e03  r8 = name for "rnd_goto_%s_department": -> "tyres".
//   colour: the bounded (<6) table 0x14571bce0 [4] white -> 0xff0078ff
//           (layout-verified data write, like the addon's ui_colour_init).
// rbp points at the current code byte inside [rsp+0xa0, rsp+0xa8) throughout
// the loop (0x1418945c1 lea rbp,[rsp+0xa0] ; 0x1418945e4 inc rbp).
//
// The value. The engine computes, per department d:
//   perf      = (sum cur(stat entries of d) + FUN_143518660(ctx, d)) / F
//   potential = sum max(stat entries of d) / F
// with stat entries = Upgrades +0x1a38..+0x1a40 (0x20 each: +0 type, +1 dept,
// +0xc cur, +0x10 max) and F = the field-wide best department potential. In the
// stock data no stat entry has dept 4, so potential(4) = 0 and perf(4) is only
// the pending gain of completed Tyres upgrades. When sum max(dept 4) == 0 the
// department is derived from the attribute types the Tyres upgrades develop:
//   T = { +0x70 type of this team's dept-4 instances with row flag 0xff,
//         excluding the hub (+0x58 == djb2("default_255")) }
//   perf      += sum over t in T of cur(first stat entry of type t) / F
//   potential  = sum over t in T of max(first stat entry of type t) / F
// (first entry per type, exactly like FUN_143518660's lookup), then
// 0 <= perf <= potential <= 1. If T matches no stat entry, potential = perf.

SWOFTY_SETTING(bool, pie_tyres_performance, true);

SWOFTY_ON_CHANGE(pie_tyres_performance, bool on)
{
    swofty::log("[rnd-tyres] pie_tyres_performance toggled -> %s; install-time, relaunch to apply",
                on ? "on" : "off");
}

constexpr uintptr_t kVaPerfSig    = 0x1418945a5ull; // mov rax,[rsp+0xa8] ; test ; je ; lea rbx ... (scan anchor)
constexpr uintptr_t kVaPerfList   = 0x1418945b6ull; // P0
constexpr uintptr_t kVaPerfValues = 0x1418946d4ull; // P1
constexpr uintptr_t kVaPerfChild  = 0x141894702ull; // P2
constexpr uintptr_t kVaPerfGoto   = 0x141894e03ull; // P4
constexpr uintptr_t kVaColTable   = 0x14571bce0ull; // 6 ABGR dept colours read by the publisher (bound < 6)

const char kPerfSig[] = "48 8B 84 24 A8 00 00 00 48 85 C0 0F 84 EA 09 00 00 48 8D 1C 04 48 81 C3 A0 00 00 00 "
                        "48 8D AC 24 A0 00 00 00";

// 0x141894674 vmovss [rsp+0x158],xmm8 (perf)
const uint8_t kPerfStoreBytes[] = {0xc5, 0x7a, 0x11, 0x84, 0x24, 0x58, 0x01, 0x00, 0x00};
// 0x1418946b5 vmovss [rsp+0x154],xmm1 (potential)
const uint8_t kPotStoreBytes[] = {0xc5, 0xfa, 0x11, 0x8c, 0x24, 0x54, 0x01, 0x00, 0x00};
// 0x1418946be lea r8,[error_department] ; cmp al,3 ; ja ; lea rcx,[names] ; mov r8,[rcx+rax*8] ; lea rcx,[rsp+0x40]
const uint8_t kPerfName1Bytes[] = {0x4c, 0x8d, 0x05, 0x48, 0x9a, 0xfe, 0x03, 0x3c, 0x03, 0x77, 0x0b, 0x48, 0x8d, 0x0d,
                                   0xf0, 0x75, 0xe8, 0x03, 0x4c, 0x8b, 0x04, 0xc1, 0x48, 0x8d, 0x4c, 0x24, 0x40};
// 0x1418946e5 movsx rcx,[rbp] ; lea rax,[error_department] ; cmp rcx,3 ; ja ; lea rax,[names] ;
//             mov rax,[rax+rcx*8] ; mov qword [rsp+0xe18],0x40
const uint8_t kPerfName2Bytes[] = {0x48, 0x0f, 0xbe, 0x4d, 0x00, 0x48, 0x8d, 0x05, 0x1c, 0x9a, 0xfe, 0x03, 0x48, 0x83,
                                   0xf9, 0x03, 0x77, 0x0b, 0x48, 0x8d, 0x05, 0xc2, 0x75, 0xe8, 0x03, 0x48, 0x8b, 0x04,
                                   0xc8, 0x48, 0xc7, 0x84, 0x24, 0x18, 0x0e, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00};
// 0x141894de6 movsx rax,[rbp] ; lea r8,[error_department] ; cmp rax,3 ; ja ; lea rcx,[names] ;
//             mov r8,[rcx+rax*8] ; lea r15,[rsp+0x48]
const uint8_t kPerfName3Bytes[] = {0x48, 0x0f, 0xbe, 0x45, 0x00, 0x4c, 0x8d, 0x05, 0x1b, 0x93, 0xfe, 0x03,
                                   0x48, 0x83, 0xf8, 0x03, 0x77, 0x0b, 0x48, 0x8d, 0x0d, 0xc1, 0x6e, 0xe8,
                                   0x03, 0x4c, 0x8b, 0x04, 0xc1, 0x4c, 0x8d, 0x7c, 0x24, 0x48};
// 0x141894cda movsx rcx,[rbp] ; mov eax,-1 ; cmp rcx,5 ; ja ; lea rax,[0x14571bce0] ; mov eax,[rax+rcx*4] ;
//             mov [rsp+0x9c],eax (the dept_colour read; proves the table + bound the data write targets)
const uint8_t kPerfColBytes[] = {0x48, 0x0f, 0xbe, 0x4d, 0x00, 0xb8, 0xff, 0xff, 0xff, 0xff, 0x48,
                                 0x83, 0xf9, 0x05, 0x77, 0x0a, 0x48, 0x8d, 0x05, 0xef, 0x6f, 0xe8,
                                 0x03, 0x8b, 0x04, 0x88, 0x89, 0x84, 0x24, 0x9c, 0x00, 0x00, 0x00};

constexpr uint32_t kStockCols[6] = {0xffeead00, 0xff3ec58d, 0xff16deff, 0xff7a2aed, kColWhite, kColWhite};

// Upgrades (0x1b10, one per team) - layout per native/HOOKS.md v0.6.0 and the
// stat-vector readers FUN_143518cc0 / FUN_143518660 / FUN_143518f50.
constexpr size_t   kUpSize       = 0x1b10;
constexpr size_t   kUpTeam       = 0xc;
constexpr size_t   kUpTrees      = 0xe0;
constexpr size_t   kUpTreeStride = 0xc40;
constexpr size_t   kUpNumTrees   = 0x1960;
constexpr size_t   kUpStatsBeg   = 0x1a38;
constexpr size_t   kUpStatsEnd   = 0x1a40;
constexpr size_t   kStatSize     = 0x20;
constexpr size_t   kStatType     = 0x0;
constexpr size_t   kStatDept     = 0x1;
constexpr size_t   kStatCur      = 0xc;
constexpr size_t   kStatMax      = 0x10;
constexpr size_t   kInstSize     = 0xe0;
constexpr size_t   kInstHash     = 0x58;
constexpr size_t   kInstType     = 0x70;
constexpr size_t   kInstDept     = 0x72;
constexpr size_t   kInstRowFlag  = 0x7c;
constexpr uint8_t  kTeamRow      = 0xff;
constexpr uint32_t kHubHash      = 0xbd057d45u;
static_assert(djb2("default_255") == kHubHash, "hub hash drifted");

// Publisher frame slots (rsp-relative, this build).
constexpr size_t kFrmList  = 0xa0;
constexpr size_t kFrmCount = 0xa8;
constexpr size_t kFrmPot   = 0x154;
constexpr size_t kFrmPerf  = 0x158;
constexpr size_t kFrmUp    = 0x160;

std::array<safetyhook::MidHook, 4> g_perf_hooks;
std::atomic<int> g_perf_log[6];

bool perf_code4(const safetyhook::Context &c)
{
    if (c.rbp < c.rsp + kFrmList || c.rbp >= c.rsp + kFrmList + 8)
        return false;
    return *reinterpret_cast<const uint8_t *>(c.rbp) == kDeptTyres;
}

void on_perf_list(safetyhook::Context &c)
{
    uint64_t n = c.rax;
    if (n == 0 || n >= 8 || *reinterpret_cast<const uint64_t *>(c.rsp + kFrmCount) != n)
        return;
    auto *list = reinterpret_cast<uint8_t *>(c.rsp + kFrmList);
    for (uint64_t i = 0; i < n; ++i) {
        if (list[i] == kDeptTyres) {
            if (g_perf_log[0].fetch_add(1) < 2)
                swofty::log("[rnd-tyres] pie5 perf: the engine already lists dept 4 (native stat entries); not added");
            return;
        }
    }
    list[n] = kDeptTyres;
    c.rax = n + 1;
    if (g_perf_log[0].fetch_add(1) < 2)
        swofty::log("[rnd-tyres] pie5 perf: department_performance loop gets dept 4 (%llu -> %llu entries; the "
                    "stored list is unchanged)",
                    (unsigned long long)n, (unsigned long long)(n + 1));
}

struct TypeSet {
    uint8_t t[16];
    int n = 0;
    void add(uint8_t v)
    {
        for (int i = 0; i < n; ++i)
            if (t[i] == v)
                return;
        if (n < 16)
            t[n++] = v;
    }
};

bool tyres_types(uintptr_t U, TypeSet &out)
{
    uint64_t nt = *reinterpret_cast<const uint64_t *>(U + kUpNumTrees);
    if (nt == 0 || nt > 2)
        return false;
    for (uint64_t k = 0; k < nt; ++k) {
        uintptr_t T = U + kUpTrees + k * kUpTreeStride;
        uintptr_t b = *reinterpret_cast<const uint64_t *>(T + 0x10);
        uintptr_t e = *reinterpret_cast<const uint64_t *>(T + 0x18);
        if (!b || e < b || (e - b) % kInstSize || (e - b) / kInstSize > 0x180
            || !region_readable(reinterpret_cast<void *>(b), e - b))
            continue;
        for (uintptr_t u = b; u < e; u += kInstSize) {
            const uint8_t *p = reinterpret_cast<const uint8_t *>(u);
            if (p[kInstDept] != kDeptTyres || p[kInstRowFlag] != kTeamRow)
                continue;
            if (*reinterpret_cast<const uint32_t *>(u + kInstHash) == kHubHash)
                continue;
            out.add(p[kInstType]);
        }
    }
    return true;
}

void derive_tyres(safetyhook::Context &c)
{
    float F = c.xmm0.f32[0];
    auto *perf = reinterpret_cast<float *>(c.rsp + kFrmPerf);
    auto *pot = reinterpret_cast<float *>(c.rsp + kFrmPot);
    uintptr_t U = *reinterpret_cast<const uintptr_t *>(c.rsp + kFrmUp);
    bool first = g_perf_log[2].fetch_add(1) < 24;
    if (!std::isfinite(F) || F <= 0.0f || !U || !region_readable(reinterpret_cast<void *>(U), kUpSize)) {
        if (first)
            swofty::log("[rnd-tyres] pie5 perf: dept 4 values left as computed (F=%.4f Upgrades=0x%llx unusable)",
                        (double)F, (unsigned long long)U);
        return;
    }
    uint32_t team = *reinterpret_cast<const uint32_t *>(U + kUpTeam);
    uintptr_t sb = *reinterpret_cast<const uintptr_t *>(U + kUpStatsBeg);
    uintptr_t se = *reinterpret_cast<const uintptr_t *>(U + kUpStatsEnd);
    if (!sb || se < sb || (se - sb) % kStatSize || (se - sb) / kStatSize > 4096
        || !region_readable(reinterpret_cast<void *>(sb), se - sb)) {
        if (first)
            swofty::log("[rnd-tyres] pie5 perf: team 0x%x stat vector unusable; dept 4 values left as computed", team);
        return;
    }
    float native_max = 0.0f;
    for (uintptr_t s = sb; s < se; s += kStatSize)
        if (*reinterpret_cast<const uint8_t *>(s + kStatDept) == kDeptTyres)
            native_max += *reinterpret_cast<const float *>(s + kStatMax);
    if (native_max != 0.0f) {
        if (first)
            swofty::log("[rnd-tyres] pie5 perf: team 0x%x has native dept-4 stats (max %.4f); engine values kept "
                        "perf=%.4f potential=%.4f",
                        team, (double)native_max, (double)*perf, (double)*pot);
        return;
    }
    TypeSet T;
    tyres_types(U, T);
    float cur = 0.0f, max = 0.0f;
    int matched = 0;
    for (int i = 0; i < T.n; ++i) {
        for (uintptr_t s = sb; s < se; s += kStatSize) {
            if (*reinterpret_cast<const uint8_t *>(s + kStatType) != T.t[i])
                continue;
            cur += *reinterpret_cast<const float *>(s + kStatCur);
            max += *reinterpret_cast<const float *>(s + kStatMax);
            ++matched;
            break;
        }
    }
    float engine_perf = std::isfinite(*perf) ? *perf : 0.0f;
    float p = engine_perf + cur / F;
    float q = max > 0.0f ? max / F : p;
    if (!std::isfinite(p) || !std::isfinite(q)) {
        if (first)
            swofty::log("[rnd-tyres] pie5 perf: team 0x%x derived values not finite; left as computed", team);
        return;
    }
    if (p < 0.0f)
        p = 0.0f;
    if (p > 1.0f)
        p = 1.0f;
    if (q < p)
        q = p;
    if (q > 1.0f)
        q = 1.0f;
    *perf = p;
    *pot = q;
    if (first) {
        if (matched == 0)
            swofty::log("[rnd-tyres] pie5 perf: team 0x%x Tyres types=%d matched no stat entry; potential = perf = "
                        "%.4f",
                        team, T.n, (double)p);
        else
            swofty::log("[rnd-tyres] pie5 perf: team 0x%x dept 4 from %d Tyres type(s) [%02x %02x %02x] (%d stat "
                        "match): F=%.4f engine perf=%.4f cur=%.4f max=%.4f -> perf=%.4f potential=%.4f",
                        team, T.n, T.n > 0 ? T.t[0] : 0xff, T.n > 1 ? T.t[1] : 0xff, T.n > 2 ? T.t[2] : 0xff,
                        matched, (double)F, (double)engine_perf, (double)cur, (double)max, (double)p, (double)q);
    }
}

void on_perf_values(safetyhook::Context &c)
{
    if (!perf_code4(c))
        return;
    if (c.r8 == g_err.load())
        c.r8 = reinterpret_cast<uintptr_t>(kTyresName);
    derive_tyres(c);
}

void on_perf_child(safetyhook::Context &c)
{
    if (!perf_code4(c) || c.rax != g_err.load())
        return;
    c.rax = reinterpret_cast<uintptr_t>(kTyresName);
    if (g_perf_log[3].fetch_add(1) < 2)
        swofty::log("[rnd-tyres] pie5 perf: dept 4 published as department_performance.tyres");
}

void on_perf_goto(safetyhook::Context &c)
{
    if (!perf_code4(c) || c.r8 != g_err.load())
        return;
    c.r8 = reinterpret_cast<uintptr_t>(kTyresName);
    if (g_perf_log[4].fetch_add(1) < 2)
        swofty::log("[rnd-tyres] pie5 perf: dept 4 dept_colour_anim -> rnd_goto_tyres_department");
}

void perf_colour(uintptr_t base)
{
    auto *tbl = reinterpret_cast<uint32_t *>(base + rva(kVaColTable));
    if (!region_readable(tbl, sizeof kStockCols)) {
        swofty::log("[rnd-tyres] pie5 perf: colour table @0x%llx not readable; dept_colour stays white (SAFE)",
                    (unsigned long long)(uintptr_t)tbl);
        return;
    }
    if (tbl[4] == kColTyres) {
        swofty::log("[rnd-tyres] pie5 perf: colour table [4] already 0x%08x", kColTyres);
        return;
    }
    for (int i = 0; i < 6; ++i) {
        if (tbl[i] != kStockCols[i]) {
            swofty::log("[rnd-tyres] pie5 perf: colour table @0x%llx [%d]=%08x differs from stock; NOT writing (SAFE)",
                        (unsigned long long)(uintptr_t)tbl, i, tbl[i]);
            return;
        }
    }
    DWORD old = 0;
    if (!VirtualProtect(&tbl[4], sizeof(uint32_t), PAGE_READWRITE, &old)) {
        swofty::log("[rnd-tyres] pie5 perf: VirtualProtect failed for colour[4]; dept_colour stays white (SAFE)");
        return;
    }
    *reinterpret_cast<volatile uint32_t *>(&tbl[4]) = kColTyres;
    DWORD tmp = 0;
    VirtualProtect(&tbl[4], sizeof(uint32_t), old, &tmp);
    swofty::log("[rnd-tyres] pie5 perf: publisher colour table [4] white -> 0x%08x (@0x%llx)", kColTyres,
                (unsigned long long)(uintptr_t)&tbl[4]);
}

void install_perf(uintptr_t base)
{
    if (!scan_one(kPerfSig, base + rva(kVaPerfSig), "perf"))
        return;
    if (!bytes_are(base + rva(0x141894674ull), kPerfStoreBytes, sizeof kPerfStoreBytes)
        || !bytes_are(base + rva(0x1418946b5ull), kPotStoreBytes, sizeof kPotStoreBytes)
        || !bytes_are(base + rva(0x1418946beull), kPerfName1Bytes, sizeof kPerfName1Bytes)
        || !bytes_are(base + rva(0x1418946e5ull), kPerfName2Bytes, sizeof kPerfName2Bytes)
        || !bytes_are(base + rva(0x141894de6ull), kPerfName3Bytes, sizeof kPerfName3Bytes)
        || !bytes_are(base + rva(0x141894cdaull), kPerfColBytes, sizeof kPerfColBytes)) {
        swofty::log("[rnd-tyres] pie5 perf: site bytes differ from the disassembly; NOT installed (SAFE)");
        return;
    }
    static const Site sites[] = {
        {kVaPerfList, &on_perf_list, "department_performance loop bound (rax)"},
        {kVaPerfValues, &on_perf_values, "perf/potential + lng name (r8)"},
        {kVaPerfChild, &on_perf_child, "child-name copy (rax)"},
        {kVaPerfGoto, &on_perf_goto, "rnd_goto name (r8)"},
    };
    if (!create_all(base, sites, g_perf_hooks, "perf"))
        return;
    perf_colour(base);
    swofty::log("[rnd-tyres] pie5 perf INSTALLED: department_performance.tyres published per team");
}

} // namespace

namespace rnd_fifth_pie {

void install(uintptr_t base, bool render_fifth_pie_on)
{
    if (!swofty::api || !base) {
        swofty::log("[rnd-tyres] pie5: no API/module base; NOT installed (SAFE)");
        return;
    }
    if (!render_fifth_pie_on) {
        swofty::log("[rnd-tyres] pie5: render_fifth_pie off; Tyres pie slice/focus/performance NOT installed (SAFE)");
        return;
    }
    if (!bytes_are(base + rva(kVaErrorDept), kErrorDeptBytes, sizeof kErrorDeptBytes)) {
        swofty::log("[rnd-tyres] pie5: \"error_department\" not at 0x%llx; build differs, NOT installed (SAFE)",
                    (unsigned long long)(base + rva(kVaErrorDept)));
        return;
    }
    g_err.store(base + rva(kVaErrorDept));

    bool perf_on = pie_tyres_performance.get();
    bool slice_on = pie_tyres_slice.get();
    bool focus_on = pie_tyres_focus.get();
    bool open_on = pie_tyres_open.get();
    swofty::log("[rnd-tyres] pie5: pie_tyres_slice=%s pie_tyres_focus=%s pie_tyres_open=%s pie_tyres_performance=%s",
                slice_on ? "on" : "off", focus_on ? "on" : "off", open_on ? "on" : "off", perf_on ? "on" : "off");

    if (perf_on)
        install_perf(base);

    const uint8_t kGuard4[] = {0x04};
    const uint8_t kBufB40[] = {0x40, 0x0b, 0x00, 0x00};
    bool live = bytes_are(base + rva(kVaPieGuardImm), kGuard4, 1) && bytes_are(base + rva(kVaPieBufImm), kBufB40, 4);
    if (!live) {
        if (slice_on || focus_on)
            swofty::log("[rnd-tyres] pie5: render_fifth_pie code patches are not live (guard imm != 4 or buffer imm "
                        "!= 0xb40); slice + focus NOT installed (SAFE)");
        return;
    }
    if (slice_on)
        install_slice(base);
    if (focus_on)
        install_focus(base);
    if (open_on)
        install_open(base);
}

bool tyres_focus_pending()
{
    return g_open_live.load() && g_tyres_focus.load();
}

bool open_tracking_live()
{
    return g_open_live.load();
}

} // namespace rnd_fifth_pie
