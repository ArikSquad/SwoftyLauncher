// SPDX-License-Identifier: AGPL-3.0-or-later
//
// R&D: Tyres & Thermal Management - research capacity for department code 4.
//
// The develop-upgrade panel update (0x141c8d540) computes the research-limit
// flag (panel+0x6d2, read by the option rows 0x141c8cbb0 to block the buy):
//
//   0x141c8e280  list  = TeamModifiers_Get(career+0xc90, team)      (0x1434fb4b0)
//   0x141c8e2c6  slots = (int) sum of entry.value over entries with
//                        kind == 0, dept == panel dept, active != 0
//   0x141c8e5df  in-use = enabled upgrades of the panel dept whose last
//                        state is InProgress (2) for this team
//   0x141c8e766  limit = gate([rsp+0x57]) && slots <= in-use
//
// A team modifier list is a vector of 8-byte entries {u8 kind, u8 dept, u8 source,
// u8 active, f32 value}. Every entry is created by the per-team init functions,
// which are fully unrolled over departments 0..3 and never emit department 4:
// 0x1434f9770 (232 Set calls: 23 kinds x depts 0..3, then a {0,1,2,3} loop that
// fills the base values) and 0x1434f58b0 (80 Set calls, the other list layout).
// The DB-driven listeners (registered by 0x141238520, callback 0x1412bc4c0) and
// the save loader (0x1434fc330) only UPDATE or RESTORE existing entries. The
// v0.6.1 department row (department = Fake) gives the kind maps a key 4, but no
// list ever holds a dept-4 entry, so slots = 0 and 0 <= 0 sets the flag.
//
// Fix: when the panel (or the R&D department page) reads the team list for
// department 4, mirror every Durability (dept 2) entry as a dept-4 entry: the
// missing ones are added with the game's own Set (0x1434f8a00: add-if-absent,
// stable sort by kind, list allocator), present ones get Durability's value. The
// stock departments' entries are never touched, and only department 4 reads the
// new ones. Once added the entries live in the game's list, so the listener
// callbacks keep the attribute-driven ones current and the stock save writer
// persists them.
//
// One exactly-one `.xtls` scan (hit must equal 0x141c8e280), every other site
// byte-verified at its fixed RVA (no ASLR), the panel hooks created
// all-or-nothing, every pointer region-checked, everything acts ONLY for
// department 4. All addresses are for build d873a1c8 (image base 0x140000000).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <utility>

#include <safetyhook.hpp>
#include <swofty/mixin.hpp>

#include "rnd_capacity.hpp"

namespace {

constexpr uintptr_t kImageBase = 0x140000000ull;

constexpr uintptr_t rva(uintptr_t va)
{
    return va - kImageBase;
}

constexpr uint8_t kDeptDurability = 2;
constexpr uint8_t kDeptTyres = 4;
constexpr uint8_t kKindResearchSlots = 0;
constexpr uint8_t kStateInProgress = 2;

constexpr size_t kMaxEntries = 4096;
constexpr size_t kMaxMirror = 512;
constexpr size_t kInUseCap = (0x27a0 - 0x1ba0) / 8;

constexpr uintptr_t kPanelCareer = 0x680;
constexpr uintptr_t kPanelTeam   = 0x6cc;
constexpr uintptr_t kPanelDept   = 0x6e8;
constexpr uintptr_t kCardCareer  = 0x678;
constexpr uintptr_t kCardTeam    = 0x6bc;

constexpr uintptr_t kInstSize   = 0xe0;
constexpr uintptr_t kInstDept   = 0x72;
constexpr uintptr_t kInstStates = 0xc0;
constexpr uintptr_t kStateSize  = 0x10;
constexpr uintptr_t kStateCode  = 0x8;

constexpr uintptr_t kVaPanelSig   = 0x141c8e280ull; // mov ecx,0xc90 ; add rcx,[rsi+0x680] ; ... (scan anchor)
constexpr uintptr_t kVaPanelSync  = 0x141c8e2a7ull; // mov qword [rsp+0x3c0],0 ; rax = list, bl = dept
constexpr uintptr_t kVaPanelCheck = 0x141c8e766ull; // cmp ebp,eax ; setle al ; and r12b,al
constexpr uintptr_t kVaInUseArray = 0x141c8e670ull; // mov [rsp+rax*8+0x1ba0],rdi ; count [rsp+0x27a0]
constexpr uintptr_t kVaPageWin    = 0x141c7dc9cull; // dept page: mov ecx,0xc90 ; ... ; call TeamModifiers_Get
constexpr uintptr_t kVaPageSync   = 0x141c7dccaull; // mov qword [rsp+0x1a40],0 ; rax = list, bl = dept
constexpr uintptr_t kVaModSet     = 0x1434f8a00ull; // TeamModifiers_Add(list, key, value)

const char kPanelSig[] = "B9 90 0C 00 00 48 03 8E 80 06 00 00 0F B6 9E E8 06 00 00 8B 96 CC 06 00 00 "
                         "E8 12 D2 86 01 48 85 C0 0F 84 A5 00 00 00 48 C7 84 24 C0 03 00 00 00 00 00 00";

// 0x141c8e766 cmp ebp,eax ; setle al ; and r12b,al ; mov [rsi+0x6d2],r12b
const uint8_t kCheckBytes[] = {0x39, 0xc5, 0x0f, 0x9e, 0xc0, 0x41, 0x20, 0xc4,
                               0x44, 0x88, 0xa6, 0xd2, 0x06, 0x00, 0x00};
// 0x141c8e670 mov [rsp+rax*8+0x1ba0],rdi ; mov rax,[rsp+0x27a0] ; inc rax ; mov [rsp+0x27a0],rax
const uint8_t kInUseBytes[] = {0x48, 0x89, 0xbc, 0xc4, 0xa0, 0x1b, 0x00, 0x00, 0x48, 0x8b, 0x84, 0x24, 0xa0, 0x27,
                               0x00, 0x00, 0x48, 0xff, 0xc0, 0x48, 0x89, 0x84, 0x24, 0xa0, 0x27, 0x00, 0x00};
// 0x141c7dc9c .. 0x141c7dcd5 (dept page: list fetch, null check, sum loop entry)
const uint8_t kPageBytes[] = {0xb9, 0x90, 0x0c, 0x00, 0x00, 0x48, 0x03, 0x8e, 0x78, 0x06, 0x00, 0x00, 0x0f, 0xb6, 0x9e,
                              0xd8, 0x06, 0x00, 0x00, 0x8b, 0x96, 0xbc, 0x06, 0x00, 0x00, 0xe8, 0xf6, 0xd7, 0x87, 0x01,
                              0x31, 0xed, 0x48, 0x85, 0xc0, 0x4c, 0x89, 0x74, 0x24, 0x78, 0x0f, 0x84, 0xc2, 0x00, 0x00,
                              0x00, 0x48, 0xc7, 0x84, 0x24, 0x40, 0x1a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
// 0x1434f8a00 prologue, AVX saves, then the find-by-key loop that returns when the key exists
const uint8_t kSetBytes[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0xb8, 0x68,
                             0x10, 0x00, 0x00, 0xe8, 0xba, 0x0d, 0x79, 0x01, 0x48, 0x29, 0xc4, 0xc5, 0xf8, 0x29,
                             0xbc, 0x24, 0x50, 0x10, 0x00, 0x00, 0xc5, 0xf8, 0x29, 0xb4, 0x24, 0x40, 0x10, 0x00,
                             0x00, 0xc5, 0xf8, 0x28, 0xf2, 0x89, 0xd7, 0x48, 0x89, 0xce, 0x48, 0x8b, 0x49, 0x08,
                             0x48, 0x8b, 0x46, 0x10, 0x48, 0x39, 0xc1, 0x74, 0x27, 0x48, 0x89, 0xca};

using ModSetFn = void (*)(void *list, uint32_t key, float value);

std::atomic<uintptr_t> g_set{0};
std::atomic<uintptr_t> g_last_panel{0};
std::atomic<int> g_last_tyres_slots{-1};
std::atomic<int> g_last_dur_slots{-1};
std::atomic<int> g_sync_log{0};
std::atomic<int> g_page_log{0};
std::atomic<int> g_check_log{0};
std::atomic<uint64_t> g_check_last{~0ull};

std::array<safetyhook::MidHook, 2> g_panel_hooks;
safetyhook::MidHook g_page_hook;

SWOFTY_SETTING(bool, tyres_research_capacity, true);

SWOFTY_ON_CHANGE(tyres_research_capacity, bool on)
{
    swofty::log("[rnd-tyres] tyres_research_capacity toggled -> %s; install-time, relaunch to apply",
                on ? "on" : "off");
}

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

struct RegionCache {
    uintptr_t lo = 0;
    uintptr_t hi = 0;

    bool ok(uintptr_t p, size_t n)
    {
        if (p >= lo && p + n <= hi && p + n >= p)
            return true;
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<void *>(p), &mbi, sizeof mbi) != sizeof mbi)
            return false;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) || (mbi.Protect & PAGE_NOACCESS))
            return false;
        uintptr_t b = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        uintptr_t e = b + mbi.RegionSize;
        if (p + n > e)
            return false;
        lo = b;
        hi = e;
        return true;
    }
};

bool region_writable(const void *p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(p, &mbi, sizeof mbi) != sizeof mbi)
        return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD))
        return false;
    DWORD w = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (!(mbi.Protect & w))
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

template <class T> T rd(uintptr_t a)
{
    T v;
    std::memcpy(&v, reinterpret_cast<const void *>(a), sizeof v);
    return v;
}

int scan_one(const char *pattern, uintptr_t expect, const char *what)
{
    uintptr_t hit = 0;
    int matches = swofty::api->scan(".xtls", pattern, &hit);
    if (matches != 1 || !hit) {
        swofty::log("[rnd-tyres] capacity %s: signature matched %d time(s); NOT installed (SAFE)", what, matches);
        return 0;
    }
    if (hit != expect) {
        swofty::log("[rnd-tyres] capacity %s: scan hit 0x%llx != expected 0x%llx; NOT installed (SAFE)", what,
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

template <size_t N>
bool create_all(uintptr_t base, const Site (&sites)[N], std::array<safetyhook::MidHook, N> &out, const char *piece)
{
    std::array<safetyhook::MidHook, N> tmp;
    for (size_t i = 0; i < N; ++i) {
        uintptr_t at = base + rva(sites[i].va);
        auto r = safetyhook::MidHook::create(reinterpret_cast<void *>(at), sites[i].fn);
        if (!r) {
            swofty::log("[rnd-tyres] capacity %s: MidHook::create failed for %s @0x%llx (err %d); piece rolled back, "
                        "NOT installed (SAFE)",
                        piece, sites[i].what, (unsigned long long)at, (int)r.error().type);
            return false;
        }
        tmp[i] = std::move(*r);
    }
    out = std::move(tmp);
    for (size_t i = 0; i < N; ++i)
        swofty::log("[rnd-tyres] capacity %s: hooked %s @0x%llx (base+0x%llx)", piece, sites[i].what,
                    (unsigned long long)(base + rva(sites[i].va)), (unsigned long long)rva(sites[i].va));
    return true;
}

// ---------------------------------------------------------------------------
// Team modifier list: {allocator*, begin, end, capacity}, 8-byte entries.

struct ListView {
    uintptr_t begin = 0;
    size_t count = 0;
};

bool list_view(uintptr_t list, ListView &v, RegionCache &rc)
{
    if (!list || !rc.ok(list, 0x20))
        return false;
    uintptr_t b = rd<uintptr_t>(list + 0x08);
    uintptr_t e = rd<uintptr_t>(list + 0x10);
    if (!b || e < b || ((e - b) & 7) || (e - b) / 8 > kMaxEntries)
        return false;
    if (e != b && !rc.ok(b, e - b))
        return false;
    v.begin = b;
    v.count = (e - b) / 8;
    return true;
}

uint8_t key_dept(uint32_t key)
{
    return static_cast<uint8_t>(key >> 8);
}

uintptr_t find_key(const ListView &v, uint32_t key)
{
    for (size_t i = 0; i < v.count; ++i)
        if (rd<uint32_t>(v.begin + i * 8) == key)
            return v.begin + i * 8;
    return 0;
}

int slots_of(uintptr_t list, uint8_t dept)
{
    RegionCache rc;
    ListView v;
    if (!list_view(list, v, rc))
        return -1;
    float sum = 0.0f;
    for (size_t i = 0; i < v.count; ++i) {
        uint32_t key = rd<uint32_t>(v.begin + i * 8);
        if ((key & 0xff) == kKindResearchSlots && key_dept(key) == dept && (key >> 24) != 0)
            sum += rd<float>(v.begin + i * 8 + 4);
    }
    return static_cast<int>(sum);
}

bool allocator_ok(uintptr_t list, RegionCache &rc)
{
    uintptr_t alloc = rd<uintptr_t>(list);
    if (!alloc || !rc.ok(alloc, 8))
        return false;
    uintptr_t vt = rd<uintptr_t>(alloc);
    return vt && region_readable(reinterpret_cast<void *>(vt), 0x40);
}

struct Mirror {
    bool ok = false;
    int src = 0;
    int inserted = 0;
    int updated = 0;
};

Mirror mirror_durability(uintptr_t list)
{
    Mirror m;
    RegionCache rc;
    ListView v;
    uintptr_t set = g_set.load();
    if (!set || !list_view(list, v, rc) || !allocator_ok(list, rc))
        return m;
    uint32_t keys[kMaxMirror];
    uint32_t vals[kMaxMirror];
    size_t n = 0;
    for (size_t i = 0; i < v.count && n < kMaxMirror; ++i) {
        uint32_t key = rd<uint32_t>(v.begin + i * 8);
        if (key_dept(key) != kDeptDurability)
            continue;
        keys[n] = key;
        vals[n] = rd<uint32_t>(v.begin + i * 8 + 4);
        ++n;
    }
    m.src = static_cast<int>(n);
    for (size_t i = 0; i < n; ++i) {
        uint32_t key4 = (keys[i] & 0xffff00ffu) | (static_cast<uint32_t>(kDeptTyres) << 8);
        uintptr_t at = find_key(v, key4);
        if (at) {
            if (rd<uint32_t>(at + 4) != vals[i]) {
                if (!region_writable(reinterpret_cast<void *>(at + 4), 4))
                    return m;
                std::memcpy(reinterpret_cast<void *>(at + 4), &vals[i], 4);
                ++m.updated;
            }
            continue;
        }
        float f;
        std::memcpy(&f, &vals[i], 4);
        reinterpret_cast<ModSetFn>(set)(reinterpret_cast<void *>(list), key4, f);
        RegionCache fresh;
        if (!list_view(list, v, fresh) || !find_key(v, key4))
            return m;
        ++m.inserted;
    }
    m.ok = n != 0;
    return m;
}

// ---------------------------------------------------------------------------
// Develop-upgrade panel update 0x141c8d540.
//   S1 0x141c8e2a7  after TeamModifiers_Get returned non-null; rax = list,
//                   bl = panel dept ([rsi+0x6e8]), rsi = panel. The slot sum
//                   loop reads the list right after this instruction.
//   S2 0x141c8e766  `cmp ebp,eax`: ebp = research slots, eax = InProgress count
//                   of the panel dept, r12b = the stock gate; the flag stored
//                   to [rsi+0x6d2] is gate && slots <= in-use. The owner-filtered
//                   enabled upgrades are at [rsp+0x1ba0], count [rsp+0x27a0].

void on_panel_sync(safetyhook::Context &c)
{
    if (static_cast<uint8_t>(c.rbx) != kDeptTyres)
        return;
    uintptr_t panel = c.rsi;
    uintptr_t list = c.rax;
    if (!region_readable(reinterpret_cast<void *>(panel + kPanelCareer), kPanelDept + 1 - kPanelCareer))
        return;
    uint32_t team = rd<uint32_t>(panel + kPanelTeam);
    Mirror m = mirror_durability(list);
    g_last_panel.store(panel);
    g_last_tyres_slots.store(slots_of(list, kDeptTyres));
    g_last_dur_slots.store(slots_of(list, kDeptDurability));
    if (!m.ok) {
        if (g_sync_log.fetch_add(1) < 8)
            swofty::log("[rnd-tyres] capacity: panel team 0x%x list 0x%llx has no Durability entries to mirror "
                        "(src %d, inserted %d); dept 4 left as is (SAFE)",
                        team, (unsigned long long)list, m.src, m.inserted);
        return;
    }
    if ((m.inserted || m.updated) && g_sync_log.fetch_add(1) < 32)
        swofty::log("[rnd-tyres] capacity: panel team 0x%x list 0x%llx: dept 4 mirrored from Durability (src %d, "
                    "inserted %d, updated %d) -> research slots dept4=%d durability=%d",
                    team, (unsigned long long)list, m.src, m.inserted, m.updated, g_last_tyres_slots.load(),
                    g_last_dur_slots.load());
}

int durability_in_use(uintptr_t rsp)
{
    RegionCache rc;
    if (!rc.ok(rsp + 0x1ba0, 0x27a8 - 0x1ba0))
        return -1;
    uint64_t n = rd<uint64_t>(rsp + 0x27a0);
    if (n > kInUseCap)
        return -1;
    int busy = 0;
    for (uint64_t i = 0; i < n; ++i) {
        uintptr_t inst = rd<uintptr_t>(rsp + 0x1ba0 + i * 8);
        if (!inst || !rc.ok(inst, kInstSize))
            continue;
        if (rd<uint8_t>(inst + kInstDept) != kDeptDurability)
            continue;
        uintptr_t sb = rd<uintptr_t>(inst + kInstStates);
        uintptr_t se = rd<uintptr_t>(inst + kInstStates + 8);
        if (!sb || se <= sb || ((se - sb) % kStateSize))
            continue;
        if (!rc.ok(se - kStateSize, kStateSize))
            continue;
        if (rd<uint8_t>(se - kStateSize + kStateCode) == kStateInProgress)
            ++busy;
    }
    return busy;
}

void on_panel_check(safetyhook::Context &c)
{
    uintptr_t panel = c.rsi;
    if (!region_readable(reinterpret_cast<void *>(panel + kPanelCareer), kPanelDept + 1 - kPanelCareer))
        return;
    if (rd<uint8_t>(panel + kPanelDept) != kDeptTyres || g_check_log.load() >= 48)
        return;
    int slots = static_cast<int>(static_cast<uint32_t>(c.rbp));
    int busy = static_cast<int>(static_cast<uint32_t>(c.rax));
    unsigned gate = static_cast<unsigned>(c.r12 & 0xff);
    unsigned limit = (gate & (slots <= busy ? 1u : 0u)) & 1u;
    uint32_t team = rd<uint32_t>(panel + kPanelTeam);
    bool same = g_last_panel.load() == panel;
    int dur_slots = same ? g_last_dur_slots.load() : -1;
    int dur_busy = durability_in_use(c.rsp);
    uint64_t sig = (static_cast<uint64_t>(static_cast<uint8_t>(slots)) << 0) |
                   (static_cast<uint64_t>(static_cast<uint8_t>(busy)) << 8) |
                   (static_cast<uint64_t>(static_cast<uint8_t>(dur_slots)) << 16) |
                   (static_cast<uint64_t>(static_cast<uint8_t>(dur_busy)) << 24) |
                   (static_cast<uint64_t>(gate & 1u) << 32) | (static_cast<uint64_t>(team) << 33);
    if (g_check_last.exchange(sig) == sig)
        return;
    if (g_check_log.fetch_add(1) >= 48)
        return;
    swofty::log("[rnd-tyres] capacity check: team 0x%x dept 4 slots=%d in-progress=%d gate=%u -> research-limit=%u "
                "| Durability slots=%d in-progress=%d",
                team, slots, busy, gate, limit, dur_slots, dur_busy);
}

// ---------------------------------------------------------------------------
// R&D department page 0x141c7b880 (the Tyres tab shows its own slot count).
//   S3 0x141c7dcca  after TeamModifiers_Get returned non-null; rax = list,
//                   bl = card dept ([rsi+0x6d8]), rsi = the department card.

void on_page_sync(safetyhook::Context &c)
{
    if (static_cast<uint8_t>(c.rbx) != kDeptTyres)
        return;
    uintptr_t card = c.rsi;
    if (!region_readable(reinterpret_cast<void *>(card + kCardCareer), kCardTeam + 4 - kCardCareer))
        return;
    uint32_t team = rd<uint32_t>(card + kCardTeam);
    Mirror m = mirror_durability(c.rax);
    if (!m.ok) {
        if (g_page_log.fetch_add(1) < 8)
            swofty::log("[rnd-tyres] capacity: Tyres page team 0x%x list 0x%llx has no Durability entries to mirror; "
                        "dept 4 left as is (SAFE)",
                        team, (unsigned long long)c.rax);
        return;
    }
    if ((m.inserted || m.updated) && g_page_log.fetch_add(1) < 32)
        swofty::log("[rnd-tyres] capacity: Tyres page team 0x%x list 0x%llx: dept 4 mirrored from Durability (src "
                    "%d, inserted %d, updated %d) -> research slots dept4=%d durability=%d",
                    team, (unsigned long long)c.rax, m.src, m.inserted, m.updated, slots_of(c.rax, kDeptTyres),
                    slots_of(c.rax, kDeptDurability));
}

void install_page(uintptr_t base)
{
    if (!bytes_are(base + rva(kVaPageWin), kPageBytes, sizeof kPageBytes)) {
        swofty::log("[rnd-tyres] capacity page: site bytes differ from the disassembly; Tyres tab slot count left "
                    "stock (SAFE)");
        return;
    }
    uintptr_t at = base + rva(kVaPageSync);
    auto r = safetyhook::MidHook::create(reinterpret_cast<void *>(at), &on_page_sync);
    if (!r) {
        swofty::log("[rnd-tyres] capacity page: MidHook::create failed @0x%llx (err %d); NOT installed (SAFE)",
                    (unsigned long long)at, (int)r.error().type);
        return;
    }
    g_page_hook = std::move(*r);
    swofty::log("[rnd-tyres] capacity page: hooked R&D page modifier list (rax) @0x%llx (base+0x%llx)",
                (unsigned long long)at, (unsigned long long)rva(kVaPageSync));
}

} // namespace

namespace rnd_capacity {

void install(uintptr_t base)
{
    if (!swofty::api || !base) {
        swofty::log("[rnd-tyres] capacity: no API/module base; NOT installed (SAFE)");
        return;
    }
    bool on = tyres_research_capacity.get();
    swofty::log("[rnd-tyres] capacity: tyres_research_capacity=%s", on ? "on" : "off");
    if (!on) {
        swofty::log("[rnd-tyres] capacity: tyres_research_capacity off; dept 4 research capacity left stock (SAFE)");
        return;
    }
    if (!scan_one(kPanelSig, base + rva(kVaPanelSig), "panel"))
        return;
    if (!bytes_are(base + rva(kVaPanelCheck), kCheckBytes, sizeof kCheckBytes)
        || !bytes_are(base + rva(kVaInUseArray), kInUseBytes, sizeof kInUseBytes)
        || !bytes_are(base + rva(kVaModSet), kSetBytes, sizeof kSetBytes)) {
        swofty::log("[rnd-tyres] capacity: site bytes differ from the disassembly; NOT installed (SAFE)");
        return;
    }
    g_set.store(base + rva(kVaModSet));
    static const Site sites[] = {
        {kVaPanelSync, &on_panel_sync, "panel modifier list (rax)"},
        {kVaPanelCheck, &on_panel_check, "panel research-limit check (ebp/eax)"},
    };
    if (!create_all(base, sites, g_panel_hooks, "panel")) {
        g_set.store(0);
        return;
    }
    swofty::log("[rnd-tyres] capacity INSTALLED: dept 4 mirrors Durability's team modifiers (research slots, "
                "scales, rush) when the develop-upgrade panel reads them; TeamModifiers_Add @0x%llx",
                (unsigned long long)g_set.load());
    install_page(base);
}

} // namespace rnd_capacity
