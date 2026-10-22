// SPDX-License-Identifier: MIT
// Copyright (c) 2026 xanpavle. Ported to a Swofty Launcher addon by Swofty.
//
// The in-engine enforcement table of src/enforce.cpp (F1-25-RE-Optimization 0.2.5)
// and the per-mode decisions. Pure code: the plugin resolves each binding, the
// tests check the table and the decisions. Upstream's first godRays site (call
// RVA 0x2c8d638) is left out: it sits in the god-rays setter, not in a reader.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace reopt::enforce {

enum class Mode { Off = 0, Cheaper, Target };

Mode parse_mode(std::string_view m);
const char *mode_name(Mode m);

struct IntSite {
    const char *binding;
    const char *attr;
    int32_t tuned;
};

struct BoolSite {
    const char *binding;
    const char *attr;
    bool tuned;
};

inline constexpr IntSite kIntSites[] = {
    {"site_Ne_HwInt_WorldShadowMapSize", "worldShadowMapSize", 2048},
    {"site_Ne_HwInt_NightShadowMapSize", "nightShadowMapSize", 2048},
    {"site_Ne_HwInt_SkyShadowMapSize", "skyShadowMapSize", 2048},
    {"site_Ne_HwInt_SpotShadowMapSize", "spotShadowMapSize", 2048},
    {"site_Ne_HwInt_SkyShadowCascadeCount", "skyShadowCascadeCount", 4},
    {"site_Ne_HwInt_NightShadowSourceCount", "nightShadowSourceCount", 4},
    {"site_Ne_HwInt_SpotShadowSampling", "spotShadowSampling", 2},
    {"site_Ne_HwInt_ShadowSampling", "sampling", 2},
    {"site_Ne_HwInt_VolFogMapWidth", "volumetricFogMapWidth", 512},
    {"site_Ne_HwInt_VolFogMapHeight", "volumetricFogMapHeight", 288},
    {"site_Ne_HwInt_CrowdBillboardDistance", "billboardDistance", 2500},
    {"site_Ne_HwInt_TexStreamSizeInMiB", "sizeInMiB", 2048},
};

inline constexpr BoolSite kBoolSites[] = {
    {"site_Ne_HwBool_SkyShadowLowLod", "skyShadowLowLod", false},
    {"site_Ne_HwBool_SkyShadowMap32Bits", "skyShadowMap32Bits", false},
    {"site_Ne_HwBool_CrowdInShadows", "crowdInShadows", false},
    {"site_Ne_HwBool_GodRaysPost", "godRays", false},
    {"site_Ne_HwBool_LensStreak", "lensStreak", false},
    {"site_Ne_HwBool_MotionBlurV2", "motionBlurV2", false},
    {"site_Ne_HwBool_NightShadowCharCast", "nightShadowCharacterCast", true},
    {"site_Ne_HwBool_RainSplashes", "rainSplashes", true},
    {"site_Ne_HwBool_RainBeads", "rainBeads", true},
    {"site_Ne_HwBool_RainHigh", "rainHigh", true},
    {"site_Ne_HwBool_TrackHeatHaze", "trackHeatHaze", true},
    {"site_Ne_HwBool_RainSheets", "rainSheets", true},
    {"site_Ne_HwBool_GroundCoverEnabled", "groundCover.enabled", true},
    {"site_Ne_HwBool_GrassFlattening", "grassFlattening", true},
    {"site_Ne_HwBool_GroundCoverMeshes", "groundCover.meshes", true},
    {"site_Ne_HwBool_VolumetricFog", "volumetricFog", true},
};

inline constexpr size_t kIntCount = sizeof kIntSites / sizeof kIntSites[0];
inline constexpr size_t kBoolCount = sizeof kBoolSites / sizeof kBoolSites[0];

int32_t int_value(Mode mode, int32_t engine, int32_t tuned);

enum class Pin { Keep, On, Off };

Pin bool_pin(Mode mode, bool pins, bool tuned);

int32_t compare_result(Pin pin, int32_t engine);

}
