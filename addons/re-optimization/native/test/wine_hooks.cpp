// SPDX-License-Identifier: MIT
// Copyright (c) 2026 xanpavle. Ported to a Swofty Launcher addon by Swofty.
//
// Wine test of re_optimization.dll's enforcement hooks against synthetic call
// sites: a fake SwoftyApi resolves the bindings into a code page, and the test
// installs the registered wrap and redirects the way swofty_core does.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <swofty_api.h>

#include "../src/enforce.hpp"

static int g_fail, g_pass;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (cond) {                                                                                                    \
            ++g_pass;                                                                                                  \
        } else {                                                                                                       \
            ++g_fail;                                                                                                  \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                      \
        }                                                                                                              \
    } while (0)

namespace e = reopt::enforce;

static std::string g_log;
static std::map<std::string, uintptr_t> g_bind;
static std::vector<SwoftyMixin> g_mixins;
static std::map<std::string, std::pair<SwoftySettingFn, void *>> g_watch;
static std::string g_mode = "cheaper";
static bool g_pins = true;

static uint8_t *g_page;
static size_t g_used;

static int32_t fake_strtol(const char *s, char **end, int32_t base)
{
    return int32_t(std::strtol(s, end, base));
}

static int32_t fake_stricmp(const char *a, const char *b)
{
    return _stricmp(a, b);
}

static uint8_t *emit(const void *bytes, size_t n)
{
    uint8_t *p = g_page + g_used;
    std::memcpy(p, bytes, n);
    g_used += (n + 15) & ~size_t(15);
    return p;
}

static uint8_t *stub(void *target)
{
    uint8_t b[14] = {0xFF, 0x25, 0, 0, 0, 0};
    std::memcpy(b + 6, &target, 8);
    return emit(b, sizeof b);
}

static void retarget_stub(uint8_t *s, void *target)
{
    std::memcpy(s + 6, &target, 8);
}

static uint8_t *site_calling(const uint8_t *callee)
{
    uint8_t b[14] = {0x48, 0x83, 0xEC, 0x28, 0xE8, 0, 0, 0, 0, 0x48, 0x83, 0xC4, 0x28, 0xC3};
    uint8_t *p = emit(b, sizeof b);
    int32_t rel = int32_t(callee - (p + 9));
    std::memcpy(p + 5, &rel, 4);
    return p;
}

static void api_log(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    g_log += buf;
    g_log += '\n';
}

static uintptr_t api_resolve(const char *id)
{
    auto it = g_bind.find(id);
    return it == g_bind.end() ? 0 : it->second;
}

static int api_register(const SwoftyMixin *m, size_t n)
{
    g_mixins.assign(m, m + n);
    return 1;
}

static int api_setting(const char *id, uint32_t type, SwoftySettingValue *out)
{
    out->type = type;
    if (!std::strcmp(id, "enforce_mode")) {
        out->choice = g_mode.c_str();
        return 1;
    }
    if (!std::strcmp(id, "pin_effects")) {
        out->toggle = g_pins;
        return 1;
    }
    return 0;
}

static int api_watch(const char *id, SwoftySettingFn fn, void *user)
{
    g_watch[id] = {fn, user};
    return 1;
}

static void change(const char *id, const SwoftySettingValue &v)
{
    auto it = g_watch.find(id);
    if (it != g_watch.end())
        it->second.first(id, &v, it->second.second);
}

using IntSiteFn = int32_t (*)(const char *, char **, int32_t);
using BoolSiteFn = int32_t (*)(const char *, const char *);

int main(int argc, char **argv)
{
    const char *dll = argc > 1 ? argv[1] : "re_optimization.dll";
    const bool off_at_init = argc > 2 && !std::strcmp(argv[2], "off");
    if (off_at_init)
        g_mode = "off";
    g_page = static_cast<uint8_t *>(VirtualAlloc(nullptr, 1 << 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    uint8_t *parser = stub(reinterpret_cast<void *>(&fake_strtol));
    uint8_t *cmp = stub(reinterpret_cast<void *>(&fake_stricmp));
    uint8_t *other = stub(reinterpret_cast<void *>(&fake_stricmp));
    g_bind["Ne_Parse_Long"] = uintptr_t(parser);
    g_bind["Ne_Compare_StringNoCase"] = uintptr_t(cmp);

    uint8_t *ints[e::kIntCount], *bools[e::kBoolCount];
    for (size_t i = 0; i < e::kIntCount; i++) {
        ints[i] = site_calling(parser);
        if (i != 1)
            g_bind[e::kIntSites[i].binding] = uintptr_t(ints[i] + 4);
    }
    for (size_t i = 0; i < e::kBoolCount; i++) {
        bools[i] = site_calling(i == 2 ? other : cmp);
        g_bind[e::kBoolSites[i].binding] = uintptr_t(bools[i] + 4);
    }
    uint8_t *decoy = site_calling(parser);

    HMODULE h = LoadLibraryA(dll);
    CHECK(h != nullptr);
    if (!h)
        return 1;
    auto init = reinterpret_cast<SwoftyAddonInitFn>(GetProcAddress(h, "SwoftyAddonInit"));
    CHECK(init != nullptr);
    SwoftyApi api{};
    api.version = 8;
    api.log = api_log;
    api.resolve = api_resolve;
    api.register_mixins = api_register;
    api.setting = api_setting;
    api.watch_setting = api_watch;
    init(&api);

    if (off_at_init) {
        CHECK(g_mixins.empty());
        CHECK(g_log.find("enforce: mode off; nothing installed") != std::string::npos);
        std::printf("%s%d passed, %d failed\n%s\n", g_log.c_str(), g_pass, g_fail, g_fail ? "FAIL" : "PASS");
        return g_fail ? 1 : 0;
    }

    size_t wraps = 0, redirects = 0;
    for (const SwoftyMixin &m : g_mixins) {
        if (m.kind == SWOFTY_MIXIN_WRAP && !std::strcmp(m.target, "Ne_Parse_Long")) {
            *m.orig = reinterpret_cast<void *>(&fake_strtol);
            retarget_stub(parser, m.handler);
            wraps++;
        } else if (m.kind == SWOFTY_MIXIN_REDIRECT) {
            uint8_t *site = reinterpret_cast<uint8_t *>(api_resolve(m.target));
            *m.orig = site + 5 + *reinterpret_cast<int32_t *>(site + 1);
            int32_t rel = int32_t(stub(m.handler) - (site + 5));
            std::memcpy(site + 1, &rel, 4);
            redirects++;
        }
    }
    CHECK(wraps == 1);
    CHECK(redirects == e::kBoolCount - 1);
    CHECK(g_log.find("nightShadowMapSize (site_Ne_HwInt_NightShadowMapSize) does not resolve") != std::string::npos);
    CHECK(g_log.find("crowdInShadows (site_Ne_HwBool_CrowdInShadows) does not call the resolved parser") !=
          std::string::npos);
    CHECK(g_log.find("11/12 integer and 16/17 boolean sites resolve") != std::string::npos);

    auto int_at = [&](size_t i, const char *v) { return reinterpret_cast<IntSiteFn>(ints[i])(v, nullptr, 0); };
    auto bool_at = [&](size_t i, const char *v) { return reinterpret_cast<BoolSiteFn>(bools[i])(v, "true"); };
    auto find_bool = [](const char *attr) {
        for (size_t i = 0; i < e::kBoolCount; i++)
            if (!std::strcmp(e::kBoolSites[i].attr, attr))
                return i;
        return size_t(0);
    };
    const size_t world = 0, fog_w = 8, night = 1, god = find_bool("godRays"), rain = find_bool("rainSplashes"),
                 crowd = 2;

    CHECK(int_at(world, "4096") == 2048);
    CHECK(int_at(world, "1024") == 1024);
    CHECK(int_at(fog_w, "1920") == 512);
    CHECK(int_at(night, "4096") == 4096);
    CHECK(reinterpret_cast<IntSiteFn>(decoy)("4096", nullptr, 0) == 4096);
    CHECK(int_at(world, "0x1000") == 2048);
    CHECK(bool_at(god, "true") != 0);
    CHECK(bool_at(god, "false") != 0);
    CHECK(bool_at(rain, "false") != 0);
    CHECK(bool_at(rain, "TRUE") == 0);
    CHECK(bool_at(crowd, "true") == 0);

    SwoftySettingValue v{};
    v.type = SWOFTY_SETTING_CHOICE;
    v.choice = "target";
    change("enforce_mode", v);
    CHECK(int_at(world, "1024") == 2048);
    CHECK(int_at(world, "4096") == 2048);
    CHECK(bool_at(rain, "false") == 0);
    CHECK(bool_at(god, "true") != 0);

    SwoftySettingValue p{};
    p.type = SWOFTY_SETTING_TOGGLE;
    p.toggle = 0;
    change("pin_effects", p);
    CHECK(bool_at(god, "true") == 0);
    CHECK(bool_at(rain, "false") != 0);
    CHECK(int_at(world, "1024") == 2048);

    p.toggle = 1;
    change("pin_effects", p);
    v.choice = "off";
    change("enforce_mode", v);
    CHECK(int_at(world, "4096") == 4096);
    CHECK(bool_at(god, "true") == 0);
    CHECK(bool_at(rain, "false") != 0);

    CHECK(g_log.find("enforce: worldShadowMapSize -> 2048 (engine read 4096, mode cheaper)") != std::string::npos);
    CHECK(g_log.find("pin: godRays -> off (engine read on)") != std::string::npos);

    std::printf("%s%d passed, %d failed\n%s\n", g_log.c_str(), g_pass, g_fail, g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
