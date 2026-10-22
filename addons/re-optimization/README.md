# F1 25 RE-Optimization

A Swofty Launcher addon port of [F1 25 RE-Optimization](https://github.com/xanpavle/F1-25-RE-Optimization) by [xanpavle](https://github.com/xanpavle), used and redistributed under its MIT licence (see `LICENSE`). Ported by Swofty with xanpavle's permission.

This port tracks upstream version 0.2.5.

Offline, single-player only. It ships with the launcher and stays off until you turn it on. Supported on the Steam build of F1 25 (build 25251167, `F1_25.exe` sha256 `d873a1c8...`) only.

## What it does

| Part | Original | This port |
|---|---|---|
| Loading | proxy `dinput8.dll` | engine plugin `native/re_optimization.dll`, loaded by the launcher's `version.dll` loader at the early stage (inside the game's `CreateMutexW("Codemasters F1 25")` call) |
| Graphics profile | rewrites `hardware_settings_config.xml` on disk before the engine reads it, keeps a `.untuned` backup | hands the engine's XML parser a tuned copy of the text in memory; the file on disk is never written by the addon, and when the engine saves the file itself, every attribute that still holds a tuned value is written back with your own value |
| In-engine enforcement | detours the integer parser at RVA `0x534dcd8` keyed on the caller's return address, and rewrites 17 `call` instructions to the bool check at RVA `0x534d918` into `mov eax, imm32` | a wrap on the binding `Ne_Parse_Long` keyed on the return addresses of 12 call-site bindings, and a redirect on each of the 17 boolean call-site bindings, all found by signature |
| Preset guard | hooks `hardwareSettings::Options::apply` and `applyNode` and writes the delta back to the file after a menu change | not ported (see [Not ported](#not-ported)) |
| Engine overrides | detours the getters at RVA `0x03a43b90` (bool) and `0x03a43240` (32-bit), runs the original, then overrides the names in `[override]` | the same semantics through the mixin system, on the bindings `Ne_Get_SettingBool` and `Ne_Get_SettingU32`, found by signature |
| Upscaler | GPU detection and `[upscaler] mode` | the same detection and choice, as the Upscaler option |
| `f125_tune.ini` | settings | addon options on the Addons page and in game under ADDON SETTINGS |

### Graphics profile

The graphics profile lowers: volumetric fog map (to 512x288), world and night shadow maps (to 2048), shadow sampling (to 2), 32-bit sky shadow map, crowd shadows, SSR (quality 2), SSAO (algorithm 1), god rays, lens streak, motion blur v2, high particles, crowd billboard distance (2500), car reflection scales and cube-map update mode (one face per frame), procedural cloud RT scale (2.0), grass draw distance (80 m), hair, skidmark parallax and the texture streaming bias (0.25).
It turns off ray-traced ambient occlusion (`rt_ao`) and DDGI (`rt_ddgi`) unless Keep ray-traced AO and DDGI is on.
It turns on the engine's generic GPU culling (`cs_culling`).
Mirrors, ray-traced shadows and reflections, rain and `aa_quality` are not touched.

### In-engine enforcement

The engine clamps preset-managed attributes to the graphics preset after it reads the settings file.
Enforcement substitutes the tuned value where the engine's settings readers parse each attribute.

| Attribute | Tuned value |
|---|---|
| `worldShadowMapSize`, `nightShadowMapSize`, `skyShadowMapSize`, `spotShadowMapSize` | 2048 |
| `skyShadowCascadeCount`, `nightShadowSourceCount` | 4 |
| `spotShadowSampling`, shadow `sampling` | 2 |
| `volumetricFogMapWidth` x `volumetricFogMapHeight` | 512 x 288 |
| crowd `billboardDistance` | 2500 |
| texture streaming `sizeInMiB` | 2048 |

| Mode | Integer attributes | Effect pins |
|---|---|---|
| Cheaper (default) | the lower of the engine's value and the tuned value | the off pins only |
| Target | the tuned value | the off pins and the on pins |
| Off | the engine's value | none |

- Off pins: sky-shadow low LOD, 32-bit sky shadow map, crowd in shadows, god rays (both reads), lens streak and motion blur v2.
- On pins: rain splashes, rain beads, high rain, rain sheets, track heat haze, ground cover, grass flattening, ground-cover meshes, volumetric fog and night character shadows.
- Cheaper never turns an effect on; upstream 0.2.5 applies the on pins in Cheaper mode as well.
- Pin effects off leaves every boolean attribute to the engine, like upstream's `[enforce] bools = 0`.

### Upscaler

The engine reads `taa`, `dlss` and `xess` as `true`/`false` and `fsr3` and `cas` as numbers.
FSR 3 is written as `fsr3="1"` with `cas="1"`; every other choice writes `fsr3="0"`.
DLSS, XeSS and TAA are written as `true`/`false`, as in upstream's `profile.cpp`.
The quality level (`aa_quality`) and frame generation are never changed.

### Graphics menu changes

This port writes nothing to the settings file, and it adds no hook on `hardwareSettings::Options::apply` or `applyNode`.
The settings readers that hold the enforced call sites are listener callbacks the engine keeps per settings section and calls with that section's XML node.
After a menu change, enforced values apply again when the engine re-reads them; this comes from static analysis and is not yet confirmed in a game session.
Graphics-profile attributes that are not enforced (SSAO, SSR, cloud RT scale, grass distance and the rest) follow the menu for the rest of the session and return at the next launch.

## Options

| Option | Default | Effect |
|---|---|---|
| Apply the graphics profile | on | the tuned delta above, applied in memory at launch |
| Keep ray-traced AO and DDGI | off | on leaves `rt_ao` and `rt_ddgi` as you set them |
| In-engine enforcement | Cheaper | Cheaper, Target or Off, as above |
| Pin effects | on | the boolean pins above |
| Upscaler | Auto | Auto: DLSS on NVIDIA RTX 20 and newer, FSR 3 on NVIDIA GTX and AMD, XeSS on Intel Arc, FSR 3 on other Intel GPUs, unchanged on an unknown GPU. DLSS, FSR 3, XeSS, TAA and Off force a backend; Keep changes nothing |
| VRS and async compute | on | `Renderer.Gfx.enableVRS = 1`, `Renderer.Gfx.enableAsyncCompute = 1` |
| Grass and ground cover caps | on | `groundCoverForceUltraQuality = 0`, `groundCoverMaxInstances = 40000`, `groundCover2MaxInstances = 80000`, `groundCover2MaxMeshInstances = 4000` |
| Static instance caps | on | `Renderer.maxStaticInstances = 20000`, `Renderer.maxPVSRenderInstances = 20000` |
| Log setting reads | off | the original's discovery mode (`[log] all = 1`): every setting the engine reads, then each change, goes to `swofty/loader.log` |

The graphics profile, the upscaler and the enforcement mode apply on the next launch.

## Not ported

- The proxy `dinput8.dll`, the installer and `f125_tune.ini`.
- Per-attribute `[enforce]` overrides (`<name> = <value>` or `off`); addon options hold toggles, sliders and choices only, and Pin effects replaces `bools`.
- The preset guard on `hardwareSettings::Options::apply` and `applyNode` (see [Graphics menu changes](#graphics-menu-changes)).
- The force-scan switch (`f125_force_scan.txt`), `scantest` and the name-driven site discovery; the launcher's signature bindings, checked by `re/tools/sigcheck.py`, find every address instead.
- The session report (`[log] report`); the resolution of each site goes to `swofty/loader.log`.

## Safety

- Every hook target is a signature binding in the launcher's knowledge base (`re/db/signatures.json`), each matching exactly once in the Steam build.
- Graphics profile and engine overrides: `core.setting_get_bool`, `core.setting_get_u32`, `core.hw_xml_parse_call`, `core.hw_config_parse_call`, `core.hw_xml_write_call`.
- Enforcement: `core.parse_long` (`Ne_Parse_Long`, the game's `strtol`), `core.compare_string_no_case` (`Ne_Compare_StringNoCase`, the game's `_stricmp`) and the 29 call sites `core.hw_int_*` and `core.hw_bool_*` (`site_Ne_HwInt_*` and `site_Ne_HwBool_*` in `re/db/globals.tsv`).
- A binding that does not resolve leaves its feature off and logs `SAFE`.
- An enforcement site that does not resolve, or whose `call` does not reach the resolved parser, is skipped and logged.
- Seven call sites sit in `.data1` bodies: the four integer ones are only compared against return addresses, and the three boolean ones (crowd in shadows, night character shadows, rain sheets) are redirected under the knowledge base's `.data1` exception.
- The graphics profile installs only when the save site resolves too.
- Log lines start with `[re-opt]` in `swofty/loader.log` in the game folder.

## Build and test

```sh
native/build.sh      # zig cross-build of native/re_optimization.dll
native/test/run.sh   # host tests: the delta, its reverse, the override table, GPU detection, the upscaler matrix and the enforcement table and modes
```

## Changes

### 0.2.5 (upstream 0.2.5)

- Upscaler: FSR 3 is written as `fsr3="1"` with `cas="1"`, and the other choices write `fsr3="0"`; `fsr3="true"` parsed as 0, so FSR 3 never engaged before.
- In-engine enforcement of 12 integer attributes and 17 boolean reads, with the Cheaper, Target and Off modes and the Pin effects option.
- The graphics profile turns off `rt_ao` and `rt_ddgi`, with the Keep ray-traced AO and DDGI option.
- An addon icon.

### 0.1.0 (upstream 0.1.0)

- First port: the graphics profile in memory, the engine overrides, the GPU-aware upscaler and the read log.
