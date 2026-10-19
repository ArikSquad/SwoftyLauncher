# rnd_fifth_pie - integration notes

Native half of the 5-sector development-potential pie (Tyres = department code 4, short name
`tyres`, colour ABGR 0xff0078ff). Build d873a1c8 only (image base 0x140000000, no ASLR).
Site proofs: `PIE_SITES.md`. These notes were written for v0.7.0, when the pie code joined the
addon; later pie changes (v0.7.1 slot construction, v0.7.2 tile counts and tile route) are in
`HOOKS.md`.

## Files

| file | |
|---|---|
| `src/rnd_fifth_pie.hpp` / `src/rnd_fifth_pie.cpp` | the translation unit |
| `../tools/sites/fdis.py`, `xjump.py`, `sigscan.py`, `sitebytes.py` | the offline checks behind `PIE_SITES.md` (run with `re/tools/py`, `EXE=<F1_25.exe>`) |

## Wiring (3 lines)

```cpp
// native/src/rnd_departments.cpp
#include "rnd_fifth_pie.hpp"                       // after <swofty/mixin.hpp>
...
    g_pie_enabled.store(render_fifth_pie.get());
    install_fifth_pie(base);
    rnd_fifth_pie::install(base, g_pie_enabled.load());   // NEW, right after install_fifth_pie
```
```sh
# native/build.sh, final compile line
... -o rnd_departments.dll src/rnd_departments.cpp src/rnd_fifth_pie.cpp build/safetyhook.o build/Zydis.o
```
`SWOFTY_SETTING` / `SWOFTY_ON_CHANGE` work from a second TU: the mixin registries are `inline`
lists appended at static init, settings are `inline const` objects, and `SWOFTY_NS` lives in an
anonymous namespace, so only `SWOFTY_ADDON()` must stay single (it stays in rnd_departments.cpp).
The new TU has its own `region_readable`/`bytes_are` in an anonymous namespace - no clash.

## Flags and ordering

| option (default) | needs | does |
|---|---|---|
| `render_fifth_pie` (on) | - | master switch: `install()` returns immediately when it is off |
| `pie_tyres_slice` (on) | render_fifth_pie on AND its patches live in memory | populate: code 4 -> `tyres` name / description key / VM child / `department_name`, colour 0xff0078ff, wheel icon borrows `maintenance` |
| `pie_tyres_focus` (on) | same | `department_focused "tyres"` focuses the `tyres` child and raises `highlight_pie_sector`; `DAT_145f93c08` untouched |
| `pie_tyres_performance` (on) | render_fifth_pie on | publisher: `department_performance.tyres` per team (orange `dept_colour`, `lng_rnd_dept_tyres`, `rnd_goto_tyres_department`, derived `perf_fraction` / `potential_perf_fraction`, `max_perf_fraction` = 1 like the stock four) |

"Patches live" = byte 0x141f4821b == `04` (populate guard relaxed) and dword 0x141f46f76 ==
`0x00000b20` (slice buffer grown) when `install()` runs; `install_fifth_pie` applies the guard
last, so both being set means its slot hook is in too. The three pieces are independent:
each has its own exactly-one `.xtls` scan, byte-verified sites and all-or-nothing hook
creation; any miss logs and installs nothing for that piece. The whole file is inert while
`render_fifth_pie` is off. All flags are
install-time (ON_CHANGE only logs).

## Sites (original bytes are verified before hooking)

| id | VA | original bytes | action (only when the department code is 4) |
|---|---|---|---|
| scan | 0x141f48253 | `48 C7 44 24 50 00 00 00 00 48 8D 3D AA 5E 93 03 EB 3B 48 C7 44 24 50 00 00 00 00` | slice gate |
| H1 | 0x141f482a0 | `48 8B 5C 24 40` | rdi `error_department` -> `tyres` |
| H2 | 0x141f485ab | `E8 40 AB 21 02` (window 0x141f485a4: `4C 8D 05 62 5B 93 03 E8 40 AB 21 02 B8 FF FF FF FF`) | r8 `error_department` -> `tyres` (description key) |
| H3 | 0x141f485b5 | `48 8D B4 24 B0 01 00 00` | eax 0xffffffff -> 0xff0078ff |
| H4 | 0x141f48a64 | `4C 8D 0D 35 58 81 03` (window from 0x141f48a55: `48 89 7C 24 20 48 89 D9 48 8D 15 55 D7 6E 03` + H4) | [rsp+0x20] `tyres` -> `maintenance` |
| scan | 0x141f4aa21 | `48 8D 3D 7B 5E 72 03 48 89 D9 48 89 FA E8 1D 2E 41 03 85 C0 0F 85 F2 01 00 00 B0 03` | focus gate |
| F1 | 0x141f4aa28 | `48 89 D9 48 89 FA` (+ verified 0x141f4aa49 `88 05 B9 91 04 04 48 8B 46 10 48 8D 50 08`) | data "tyres": rdi = `tyres`, rip = 0x141f4aa4f |
| scan | 0x1418945a5 | `48 8B 84 24 A8 00 00 00 48 85 C0 0F 84 EA 09 00 00 48 8D 1C 04 48 81 C3 A0 00 00 00 48 8D AC 24 A0 00 00 00` | perf gate |
| P0 | 0x1418945b6 | `48 8D 1C 04 48 81 C3 A0 00 00 00` | 4 absent: list[count] = 4, rax = count+1 (stored count unchanged) |
| P1 | 0x1418946d4 | `48 8D 4C 24 40` (+ verified stores 0x141894674 `C5 7A 11 84 24 58 01 00 00`, 0x1418946b5 `C5 FA 11 8C 24 54 01 00 00`, name select 0x1418946be) | r8 -> `tyres`; derive [rsp+0x158] perf / [rsp+0x154] potential |
| P2 | 0x141894702 | `48 C7 84 24 18 0E 00 00 40 00 00 00` | rax -> `tyres` (child name) |
| P4 | 0x141894e03 | `4C 8D 7C 24 48` | r8 -> `tyres` (`rnd_goto_%s_department`) |
| data | 0x14571bce0 | `00 AD EE FF 8D C5 3E FF FF DE 16 FF ED 2A 7A FF FF FF FF FF FF FF FF FF` | [4] -> `FF 78 00 FF` (VirtualProtect, written only after the perf hooks are in) |
| check | 0x14587e10d | `error_department\0` | must match or nothing installs |

## The Tyres value (pie_tyres_performance)

Stat entries = `Upgrades+0x1a38..+0x1a40`, 0x20 each (+0 attribute type, +1 department, +0xc
current, +0x10 max); F = FUN_143518f50 = the field-wide best department max (xmm0 at P1).
- If this team has stat entries with department 4 (sum max != 0), the engine's own values are
  kept (logged once).
- Otherwise T = the +0x70 types of this team's dept-4 instances with row flag +0x7c == 0xff,
  hub (+0x58 == 0xbd057d45) excluded (tree vectors at Upgrades+0xe0+k*0xc40 +0x10/+0x18, 0xe0
  stride, k < Upgrades+0x1960, exactly as the addon's v0.6.0 diagnostic walks them), and
  `perf = engine_perf + sum cur(first entry of each t) / F`,
  `potential = sum max(first entry of each t) / F`, clamped `0 <= perf <= potential <= 1`.
  engine_perf is the engine's own dept-4 pending gain from completed Tyres upgrades
  (FUN_143518660(ctx, 4)). No stat match -> `potential = perf`, logged.
The tyre attributes are also counted in their stock department (Durability), and a subset of a
department's attributes gives a smaller wedge than a full department.

## Naming contract (for the .ui)

- Tile / populate VM child: `tyres` (djb2 0x107666dc) under the same parent as `aero` etc.;
  `department_name` = `tyres`; name `lng_rnd_dept_tyres`; description
  `lng_rnd_dept_tyres_overview_description` (both in the addon's `assets/text.json`; the
  description is `eng` only); colour leaf 0xff0078ff; `progress_wheel_bg_icon_tex` =
  `overrides.career.rnd_wheel_icon.maintenance`.
- Focus: the 5th tile must send `department_focused` with data `tyres` (`tools/build_pie.py`
  rebinds the 5th-box tile from `fake` to `tyres`). `focused_department.department_name` becomes
  `tyres`, so `highlight_department` / `focused_department` MVCDs need a `tyres` case.
- Pie data: `focused_team_perf_root.department_performance.tyres.{perf_fraction,
  potential_perf_fraction, max_perf_fraction, dept_colour, name, dept_colour_anim}`.
- `dept_colour_anim` = `rnd_goto_tyres_department`; no stock anim with that name was found
  (the `error_department` variant had none either).

## Expected log (render_fifth_pie on)

```
[rnd-tyres] pie5: pie_tyres_slice=on pie_tyres_focus=on pie_tyres_performance=on
[rnd-tyres] pie5 perf: hooked department_performance loop bound (rax) @0x1418945b6 (base+0x18945b6)   (x4 sites)
[rnd-tyres] pie5 perf: publisher colour table [4] white -> 0xff0078ff (@0x14571bcf0)
[rnd-tyres] pie5 perf INSTALLED: department_performance.tyres published per team
[rnd-tyres] pie5 slice: hooked populate name join (rdi) @0x141f482a0 ...                                (x4 sites)
[rnd-tyres] pie5 slice INSTALLED: code 4 populates as tyres / 0xff0078ff / icon maintenance
[rnd-tyres] pie5 focus: hooked focus handler pre-engine compare @0x141f4aa28 ...
[rnd-tyres] pie5 focus INSTALLED: data "tyres" resumes at 0x141f4aa4f
-- on R&D > Performance --
[rnd-tyres] pie5 slice: code 4 name error_department -> tyres (VM child tyres, lng_rnd_dept_tyres)
[rnd-tyres] pie5 slice: code 4 description key -> lng_rnd_dept_tyres_overview_description
[rnd-tyres] pie5 slice: code 4 colour white -> 0xff0078ff
[rnd-tyres] pie5 slice: code 4 wheel icon -> rnd_wheel_icon.maintenance (no tyres frame)
[rnd-tyres] pie5 perf: department_performance loop gets dept 4 (4 -> 5 entries; the stored list is unchanged)
[rnd-tyres] pie5 perf: team 0x.. dept 4 from N Tyres type(s) [..] (M stat match): F=.. engine perf=.. cur=.. max=.. -> perf=.. potential=..
[rnd-tyres] pie5 perf: dept 4 published as department_performance.tyres
[rnd-tyres] pie5 perf: dept 4 dept_colour_anim -> rnd_goto_tyres_department
-- focusing the Tyres tile --
[rnd-tyres] pie5 focus: department_focused "tyres" -> focused_department = tyres, highlight_pie_sector raised (selection index untouched)
```
Failure lines all end in `(SAFE)` and name the piece (`slice` / `focus` / `perf`).

## Live checks (gdb under Wine: `handle all nostop noprint pass` then `handle SIGTRAP stop noprint nopass`)

The hook sites start with a `jmp` once installed, so break on the instruction after each window,
with hardware breakpoints (max 4 at a time):
- `hbreak *0x141f482bb if $r14==4` -> `x/s $r8` = `tyres` (lng_rnd_dept_%s arg).
- `hbreak *0x141f48629 if $r14==4` -> `p/x $eax` = 0xff0078ff.
- `hbreak *0x141f48a6b if $r14==4` -> `x/s *(char**)($rsp+0x20)` = `maintenance`.
- `hbreak *0x141f4aa4f` then focus the Tyres tile -> `x/s $rdi` = `tyres`; `x/bx 0x145f93c08`
  unchanged from before the focus.
- `hbreak *0x1418946e0 if *(unsigned char*)$rbp==4` -> `x/s $r8` = `tyres`,
  `x/2fw $rsp+0x154` = potential, perf (both in 0..1, potential >= perf).
- `hbreak *0x141894d33 if *(unsigned char*)$rbp==4` -> `x/wx $rsp+0x9c` = 0xff0078ff.
- `x/6wx 0x14571bce0` -> fifth word 0xff0078ff.
