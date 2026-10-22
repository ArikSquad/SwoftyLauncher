// SPDX-License-Identifier: MIT
// Copyright (c) 2026 xanpavle. Ported to a Swofty Launcher addon by Swofty.
//
// Host tests for the settings delta, the override table, the upscaler choice
// (profiletest, tunetest and the upscaler matrix of the original test.bat) and the
// enforcement table and modes (enforcetest).
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "../src/delta.hpp"
#include "../src/enforce.hpp"
#include "../src/gpu.hpp"
#include "../src/overrides.hpp"

static int g_fail;
static int g_pass;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (cond) {                                                                                                    \
            ++g_pass;                                                                                                  \
        } else {                                                                                                       \
            ++g_fail;                                                                                                  \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                      \
        }                                                                                                              \
    } while (0)

static std::string read_file(const char *path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

static std::string line_of(const std::string &xml, const char *tag)
{
    std::string open = std::string("<") + tag + " ";
    size_t p = xml.find(open);
    if (p == std::string::npos)
        return {};
    size_t e = xml.find('\n', p);
    return xml.substr(p, e == std::string::npos ? std::string::npos : e - p);
}

static bool has(const std::string &line, const char *attr_eq_value)
{
    return line.find(attr_eq_value) != std::string::npos;
}

static void test_profile(const std::string &sample)
{
    reopt::Record rec;
    std::string out = reopt::transform(sample, reopt::Upscaler::Keep, {}, &rec);
    CHECK(has(line_of(out, "lighting"), "volumetricFogMapWidth=\"512\""));
    CHECK(has(line_of(out, "lighting"), "volumetricFogMapHeight=\"288\""));
    CHECK(has(line_of(out, "shadows"), "worldShadowMapSize=\"2048\""));
    CHECK(has(line_of(out, "shadows"), "nightShadowMapSize=\"2048\""));
    CHECK(has(line_of(out, "shadows"), "skyShadowMap32Bits=\"false\""));
    CHECK(has(line_of(out, "shadows"), " sampling=\"2\""));
    CHECK(has(line_of(out, "shadows"), "spotShadowSampling=\"2\""));
    CHECK(has(line_of(out, "shadows"), "crowdInShadows=\"false\""));
    CHECK(has(line_of(out, "ssao"), "algorithm=\"1\""));
    CHECK(has(line_of(out, "ssrt"), "quality=\"2\""));
    CHECK(has(line_of(out, "postprocess"), "godRays=\"false\" lensStreak=\"false\" motionBlurV2=\"false\""));
    CHECK(has(line_of(out, "particles"), "high=\"false\""));
    CHECK(has(line_of(out, "crowd"), "billboardDistance=\"2500\""));
    CHECK(has(line_of(out, "ground_cover"), "drawDistance=\"80.0\""));
    CHECK(has(line_of(out, "hair"), "enabled=\"false\""));
    CHECK(has(line_of(out, "cs_culling"), "enabled=\"true\""));
    CHECK(has(line_of(out, "rt_ao"), "enabled=\"false\""));
    CHECK(has(line_of(out, "rt_ddgi"), "enabled=\"false\""));
    CHECK(line_of(out, "rt_shadows") == line_of(sample, "rt_shadows"));
    CHECK(line_of(out, "rt_reflections") == line_of(sample, "rt_reflections"));
    CHECK(line_of(out, "antialiasing") == line_of(sample, "antialiasing"));
    CHECK(line_of(out, "aa_quality") == line_of(sample, "aa_quality"));
    CHECK(out.size() > 0 && out.find("<hardware_settings_config applyOOTB=\"false\">") != std::string::npos);
    CHECK(rec.changes.size() == 20);

    reopt::Record keep_rt;
    std::string kept = reopt::transform(sample, reopt::Upscaler::Keep, {true, false}, &keep_rt);
    CHECK(line_of(kept, "rt_ao") == line_of(sample, "rt_ao"));
    CHECK(line_of(kept, "rt_ddgi") == line_of(sample, "rt_ddgi"));
    CHECK(keep_rt.changes.size() == 18);
    CHECK(reopt::untransform(kept, keep_rt) == sample);

    size_t restored = 0;
    CHECK(reopt::untransform(out, rec, &restored) == sample);
    CHECK(restored == rec.changes.size());

    std::string engine = out;
    size_t p = engine.find("worldShadowMapSize=\"2048\"");
    engine.replace(p, std::strlen("worldShadowMapSize=\"2048\""), "worldShadowMapSize=\"1024\"");
    std::string back = reopt::untransform(engine, rec, &restored);
    CHECK(has(line_of(back, "shadows"), "worldShadowMapSize=\"1024\""));
    CHECK(has(line_of(back, "shadows"), "nightShadowMapSize=\"4096\""));
    CHECK(has(line_of(back, "lighting"), "volumetricFogMapWidth=\"1920\""));
    CHECK(restored == rec.changes.size() - 1);

    reopt::Record none;
    CHECK(reopt::transform(sample, reopt::Upscaler::Keep, {false, true}, &none) == sample);
    CHECK(none.empty());
    CHECK(reopt::untransform(sample, none) == sample);
}

static void test_upscaler_matrix(const std::string &sample)
{
    struct Row {
        reopt::Upscaler up;
        const char *line;
    };
    const Row rows[] = {
        {reopt::Upscaler::Keep, "<antialiasing taa=\"false\" cas=\"1\" dlss=\"false\" fsr3=\"1\" xess=\"false\" />"},
        {reopt::Upscaler::Dlss, "<antialiasing taa=\"false\" cas=\"1\" dlss=\"true\" fsr3=\"0\" xess=\"false\" />"},
        {reopt::Upscaler::Fsr3, "<antialiasing taa=\"false\" cas=\"1\" dlss=\"false\" fsr3=\"1\" xess=\"false\" />"},
        {reopt::Upscaler::Xess, "<antialiasing taa=\"false\" cas=\"1\" dlss=\"false\" fsr3=\"0\" xess=\"true\" />"},
        {reopt::Upscaler::Taa, "<antialiasing taa=\"true\" cas=\"1\" dlss=\"false\" fsr3=\"0\" xess=\"false\" />"},
        {reopt::Upscaler::Off, "<antialiasing taa=\"false\" cas=\"1\" dlss=\"false\" fsr3=\"0\" xess=\"false\" />"},
    };
    for (const Row &r : rows) {
        reopt::Record rec;
        std::string out = reopt::transform(sample, r.up, {}, &rec);
        std::string aa = line_of(out, "antialiasing");
        while (!aa.empty() && (aa.back() == '\r' || aa.back() == '\n'))
            aa.pop_back();
        CHECK(aa == r.line);
        CHECK(line_of(out, "aa_quality") == line_of(sample, "aa_quality"));
        CHECK(reopt::untransform(out, rec) == sample);
        std::printf("  %-18s %s\n", reopt::upscaler_name(r.up), aa.c_str());
    }
    reopt::Record only;
    std::string up_only = reopt::transform(sample, reopt::Upscaler::Dlss, {false, true}, &only);
    CHECK(has(line_of(up_only, "antialiasing"), "dlss=\"true\""));
    CHECK(has(line_of(up_only, "lighting"), "volumetricFogMapWidth=\"1920\""));
    CHECK(line_of(up_only, "rt_ao") == line_of(sample, "rt_ao"));

    const std::string taa = "<hardware_settings_config>\n<antialiasing taa=\"true\" cas=\"0\" dlss=\"false\" fsr3=\"0\" "
                            "xess=\"false\" />\n</hardware_settings_config>\n";
    reopt::Record fr;
    std::string fsr = reopt::transform(taa, reopt::Upscaler::Fsr3, {}, &fr);
    CHECK(line_of(fsr, "antialiasing") ==
          "<antialiasing taa=\"false\" cas=\"1\" dlss=\"false\" fsr3=\"1\" xess=\"false\" />");
    CHECK(fr.changes.size() == 3);
    CHECK(reopt::untransform(fsr, fr) == taa);
    std::string dlss = reopt::transform(taa, reopt::Upscaler::Dlss, {});
    CHECK(line_of(dlss, "antialiasing") ==
          "<antialiasing taa=\"false\" cas=\"0\" dlss=\"true\" fsr3=\"0\" xess=\"false\" />");
    for (reopt::Upscaler up : {reopt::Upscaler::Taa, reopt::Upscaler::Dlss, reopt::Upscaler::Fsr3, reopt::Upscaler::Xess,
                               reopt::Upscaler::Off}) {
        std::string aa = line_of(reopt::transform(taa, up, {}), "antialiasing");
        CHECK(!has(aa, "fsr3=\"true\"") && !has(aa, "fsr3=\"false\""));
    }
}

static void test_layouts(const std::string &sample)
{
    std::string flat;
    for (char c : sample)
        if (c != '\n' && c != '\r' && c != '\t')
            flat += c;
    reopt::Record rec;
    std::string tuned = reopt::transform(flat, reopt::Upscaler::Dlss, {}, &rec);
    CHECK(rec.changes.size() == 22);
    CHECK(tuned.find("volumetricFogMapWidth=\"512\"") != std::string::npos);
    CHECK(tuned.find("dlss=\"true\"") != std::string::npos);
    CHECK(tuned.find("<aa_quality value=\"0\" />") != std::string::npos);
    CHECK(reopt::untransform(tuned, rec) == flat);

    std::string wrapped = "<?xml version=\"1.0\"?>\n<hardware_settings_config>\n\t<shadows\n\t\tworldShadowMapSize=\"4096\"\n"
                          "\t\tsampling=\"3\" />\n\t<!-- <shadows worldShadowMapSize=\"4096\" /> -->\n</hardware_settings_config>\n";
    reopt::Record wrec;
    std::string wt = reopt::transform(wrapped, reopt::Upscaler::Keep, {}, &wrec);
    CHECK(wt.find("worldShadowMapSize=\"2048\"\n") != std::string::npos);
    CHECK(wt.find("sampling=\"2\"") != std::string::npos);
    CHECK(wt.find("<!-- <shadows worldShadowMapSize=\"4096\" /> -->") != std::string::npos);
    CHECK(wrec.changes.size() == 2);
    CHECK(reopt::untransform(wt, wrec) == wrapped);
}

static void test_root_detection(const std::string &sample)
{
    CHECK(reopt::is_hardware_settings_config(sample.data(), sample.size()));
    std::string bom = "\xEF\xBB\xBF<!-- c --><?xml version=\"1.0\"?>\r\n<hardware_settings_config>\n</hardware_settings_config>";
    CHECK(reopt::is_hardware_settings_config(bom.data(), bom.size()));
    std::string options = "<?xml version=\"1.0\"?>\n<hardware_settings_options>\n</hardware_settings_options>";
    CHECK(!reopt::is_hardware_settings_config(options.data(), options.size()));
    std::string nested = "<root><hardware_settings_config/></root>";
    CHECK(!reopt::is_hardware_settings_config(nested.data(), nested.size()));
    std::string prefix = "<hardware_settings_config_vr>";
    CHECK(!reopt::is_hardware_settings_config(prefix.data(), prefix.size()));
    CHECK(!reopt::is_hardware_settings_config("", 0));
}

static void test_overrides()
{
    const uint32_t all = reopt::GroupVrsAsync | reopt::GroupGroundCover | reopt::GroupInstanceCaps;
    uint8_t b = 0xff;
    uint32_t s = 0;
    CHECK(reopt::lookup_bool("Renderer.Gfx.enableVRS", all, &b) && b == 1);
    CHECK(reopt::lookup_bool("Renderer.Gfx.enableAsyncCompute", all, &b) && b == 1);
    CHECK(reopt::lookup_scalar("Renderer.Gfx.groundCoverMaxInstances", all, &s) && s == 40000);
    CHECK(reopt::lookup_scalar("Renderer.Gfx.groundCover2MaxInstances", all, &s) && s == 80000);
    CHECK(reopt::lookup_scalar("Renderer.Gfx.groundCover2MaxMeshInstances", all, &s) && s == 4000);
    CHECK(reopt::lookup_bool("Renderer.Gfx.groundCoverForceUltraQuality", all, &b) && b == 0);
    CHECK(reopt::lookup_scalar("Renderer.maxStaticInstances", all, &s) && s == 20000);
    CHECK(reopt::lookup_scalar("Renderer.maxPVSRenderInstances", all, &s) && s == 20000);
    CHECK(!reopt::lookup_bool("Nope.Nope", all, &b));
    CHECK(reopt::lookup_scalar("Renderer.Gfx.enableVRS", all, &s) && s == 1);
    CHECK(!reopt::lookup_scalar("Renderer.Gfx.groundCoverMaxInstances", reopt::GroupVrsAsync, &s));
    CHECK(!reopt::lookup_bool("Renderer.Gfx.enableVRS", reopt::GroupGroundCover | reopt::GroupInstanceCaps, &b));
    CHECK(!reopt::lookup_bool("Renderer.Gfx.enableVRS", 0, &b));
    CHECK(!reopt::lookup_bool(nullptr, all, &b));
    CHECK(reopt::override_count() == 8);

    uint32_t v = 0;
    bool f = false;
    CHECK(reopt::parse_value("true", &v, &f) && v == 1 && !f);
    CHECK(reopt::parse_value("Off", &v, &f) && v == 0 && !f);
    CHECK(reopt::parse_value("0x10", &v, &f) && v == 16 && !f);
    CHECK(reopt::parse_value("0.5", &v, &f) && f);
    float half;
    std::memcpy(&half, &v, 4);
    CHECK(half == 0.5f);
    CHECK(!reopt::parse_value("abc", &v, &f));
}

static void test_gpu()
{
    using namespace reopt::gpu;
    Info rtx = classify(VendorNvidia, L"NVIDIA GeForce RTX 4070 Ti");
    CHECK(rtx.series == 40 && rtx.supports_dlss && !rtx.supports_xess);
    CHECK(choose("auto", rtx) == reopt::Upscaler::Dlss);
    Info gtx = classify(VendorNvidia, L"NVIDIA GeForce GTX 1660 SUPER");
    CHECK(gtx.series == 16 && !gtx.supports_dlss);
    CHECK(choose("auto", gtx) == reopt::Upscaler::Fsr3);
    Info amd = classify(vendor_from_name(L"AMD Radeon RX 7800 XT"), L"AMD Radeon RX 7800 XT");
    CHECK(amd.vendor == VendorAmd && amd.series == 7000);
    CHECK(choose("auto", amd) == reopt::Upscaler::Fsr3);
    Info arc = classify(VendorIntel, L"Intel(R) Arc(TM) A770 Graphics");
    CHECK(arc.supports_xess);
    CHECK(choose("auto", arc) == reopt::Upscaler::Xess);
    Info igpu = classify(VendorIntel, L"Intel(R) UHD Graphics 770");
    CHECK(!igpu.supports_xess && score(igpu) < score(arc));
    CHECK(choose("auto", igpu) == reopt::Upscaler::Fsr3);
    Info unknown = classify(VendorUnknown, L"Some Adapter");
    CHECK(choose("auto", unknown) == reopt::Upscaler::Keep);
    CHECK(choose("dlss", amd) == reopt::Upscaler::Dlss);
    CHECK(choose("FSR", rtx) == reopt::Upscaler::Fsr3);
    CHECK(choose("keep", rtx) == reopt::Upscaler::Keep);
    CHECK(choose("off", rtx) == reopt::Upscaler::Off);
    CHECK(choose("taa", rtx) == reopt::Upscaler::Taa);
    CHECK(choose("xess", rtx) == reopt::Upscaler::Xess);
    CHECK(vendor_from_id(L"PCI\\VEN_10DE&DEV_2786") == VendorNvidia);
    CHECK(vendor_from_id(L"PCI\\VEN_1002&DEV_747E") == VendorAmd);
    CHECK(vendor_from_id(L"PCI\\VEN_8086&DEV_56A0") == VendorIntel);
    CHECK(vendor_from_id(L"PCI\\VEN_1234") == VendorUnknown);
    CHECK(score(rtx) > score(amd) && score(amd) > score(arc));
}

static void test_enforce(const char *globals_path)
{
    namespace e = reopt::enforce;
    CHECK(e::kIntCount == 12);
    CHECK(e::kBoolCount == 16);
    int on = 0, off = 0, god = 0;
    for (const e::BoolSite &b : e::kBoolSites) {
        (b.tuned ? on : off)++;
        god += std::strcmp(b.attr, "godRays") == 0;
    }
    CHECK(on == 10 && off == 6 && god == 1);
    struct Want {
        const char *attr;
        int32_t value;
    };
    const Want ints[] = {{"worldShadowMapSize", 2048}, {"nightShadowMapSize", 2048}, {"skyShadowMapSize", 2048},
                         {"spotShadowMapSize", 2048},  {"skyShadowCascadeCount", 4}, {"nightShadowSourceCount", 4},
                         {"spotShadowSampling", 2},    {"sampling", 2},             {"volumetricFogMapWidth", 512},
                         {"volumetricFogMapHeight", 288}, {"billboardDistance", 2500}, {"sizeInMiB", 2048}};
    for (const Want &w : ints) {
        int found = 0;
        for (const e::IntSite &s : e::kIntSites)
            found += std::strcmp(s.attr, w.attr) == 0 && s.tuned == w.value;
        CHECK(found == 1);
    }

    std::string globals = read_file(globals_path);
    CHECK(!globals.empty());
    auto in_kb = [&](const char *binding) { return globals.find(std::string("\t") + binding + "\tcode\t") != std::string::npos; };
    for (const e::IntSite &s : e::kIntSites)
        CHECK(in_kb(s.binding));
    for (const e::BoolSite &b : e::kBoolSites)
        CHECK(in_kb(b.binding));

    CHECK(e::parse_mode("target") == e::Mode::Target);
    CHECK(e::parse_mode("Cheaper") == e::Mode::Cheaper);
    CHECK(e::parse_mode("off") == e::Mode::Off);
    CHECK(e::parse_mode("") == e::Mode::Cheaper);
    CHECK(std::strcmp(e::mode_name(e::Mode::Cheaper), "cheaper") == 0);

    CHECK(e::int_value(e::Mode::Target, 4096, 2048) == 2048);
    CHECK(e::int_value(e::Mode::Target, 1024, 2048) == 2048);
    CHECK(e::int_value(e::Mode::Cheaper, 4096, 2048) == 2048);
    CHECK(e::int_value(e::Mode::Cheaper, 1024, 2048) == 1024);
    CHECK(e::int_value(e::Mode::Cheaper, 0, 512) == 0);
    CHECK(e::int_value(e::Mode::Off, 4096, 2048) == 4096);

    CHECK(e::bool_pin(e::Mode::Target, true, true) == e::Pin::On);
    CHECK(e::bool_pin(e::Mode::Target, true, false) == e::Pin::Off);
    CHECK(e::bool_pin(e::Mode::Cheaper, true, true) == e::Pin::Keep);
    CHECK(e::bool_pin(e::Mode::Cheaper, true, false) == e::Pin::Off);
    CHECK(e::bool_pin(e::Mode::Off, true, false) == e::Pin::Keep);
    CHECK(e::bool_pin(e::Mode::Target, false, true) == e::Pin::Keep);
    CHECK(e::bool_pin(e::Mode::Target, false, false) == e::Pin::Keep);
    CHECK(e::compare_result(e::Pin::On, 7) == 0);
    CHECK(e::compare_result(e::Pin::Off, 0) != 0);
    CHECK(e::compare_result(e::Pin::Keep, 0) == 0);
    CHECK(e::compare_result(e::Pin::Keep, -3) == -3);
}

int main(int argc, char **argv)
{
    const char *sample_path = argc > 1 ? argv[1] : "sample_settings.xml";
    const char *globals_path = argc > 2 ? argv[2] : "../../../../re/db/globals.tsv";
    std::string sample = read_file(sample_path);
    CHECK(!sample.empty());
    test_profile(sample);
    std::printf("upscaler matrix:\n");
    test_upscaler_matrix(sample);
    test_layouts(sample);
    test_root_detection(sample);
    test_overrides();
    test_gpu();
    test_enforce(globals_path);
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    std::printf(g_fail ? "FAIL\n" : "PASS\n");
    return g_fail ? 1 : 0;
}
