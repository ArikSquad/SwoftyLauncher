# R&D: Tyres & Thermal Management

Adds a fifth career R&D department, Tyres & Thermal Management, alongside Aerodynamics, Chassis, Durability and Powertrain.

This folder is source only. The launcher's builds and releases leave it out.

## State

Version 0.7.2 works in game:

- The Tyres department page lists three upgrades that can be researched and developed, with Durability's research slots and R&D modifiers.
- R&D > Performance shows five department tiles (3+2) and a five-sector development-potential pie with an orange Tyres sector. The Tyres tile shows its fitted and total upgrades, highlights its sector and opens the Tyres page.
- The Develop Upgrade screen, the In Progress list, the department category rows, Team HQ popups and upgrade cards show Tyres upgrades with borrowed stock icons and the Tyres orange.

![Stock pie and the five-sector pie](preview/pie_compare.png)

Temporary test values, to restore before a release (`DESIGN.md`, "TEMPORARY test values"):

- `tyres_test_price` (on by default) makes the three upgrades cost 5 research points. The database cost is 1000.
- `db/tyres_tree.json` sets 5 days to research and 5 to develop. The real values are 20 and 25.
- `assets/text.json` names the upgrades "Pirelli Whisperer", "Brake Disc Chill Pill" and "Thermal Blanket of Destiny".

## What it changes

| part | files | change |
|---|---|---|
| Database | `db/departments.json`, `db/tyres_tree.json` | an `UpgradeDepartmentEntry_4` row (`department = Fake`, department code 4) cloned from Durability; three upgrades (Tyre Wear, Brakes, Upgrade All Durability) in all 46 `trees.<team>.fake` containers, linked from the tree hub |
| Text | `assets/text.json` | the department name and description, and the upgrade names and descriptions |
| UI | `erp/rnd_fifth_pie.json`, `ui/` | the five-tile R&D > Performance overview and the five-sector pie, in `flow_career_hub.erp` and `flow_team_hq.erp` |
| Engine | `native/rnd_departments.dll` | reads the fifth department row, gives the Tyres upgrades R&D states, and names, colours, counts and routes the fifth tile and pie sector |

The engine hooks are written for the Steam build 25251167 (`F1_25.exe` d873a1c8…). Each one checks its bytes and installs nothing when they don't match.

## Settings

| setting | default | does |
|---|---|---|
| `render_fifth_box` | on | builds a fifth department card in the R&D > Performance box grid |
| `attach_tyres_tree` | on | logs the Tyres upgrade tree container the save reads |
| `render_fifth_box_layout` | on | logs the box grid's slot and card vectors |
| `render_fifth_pie` | on | the fifth pie slot; master switch for the `pie_tyres_*` settings |
| `pie_tyres_slice` | on | the fifth tile and sector populate as Tyres |
| `pie_tyres_focus` | on | focusing the Tyres tile highlights the Tyres sector |
| `pie_tyres_open` | on | the Tyres tile opens the Tyres page |
| `pie_tyres_performance` | on | publishes the Tyres development potential |
| `pie_tyres_counts` | on | the Tyres tile's fitted and total upgrades |
| `tyres_research_capacity` | on | Tyres gets Durability's research slots and R&D modifiers |
| `diag_tree_rowcount` | on | logs the upgrade-tree row count per department |
| `select_fifth_dept` | off | v0.5.0 experiment: lets the box grid select index 4 |
| `instantiate_tyres_upgrades` | on | gives the three Tyres upgrades R&D states |
| `diag_tyres_instances` | on | logs each R&D page's upgrade instances per department |
| `tyres_upgrade_screen` | on | fixes Back, the card art and the performance comparison on the Develop Upgrade screen |
| `tyres_dept_tags` | on | Tyres icon and colour in R&D lists |
| `tyres_test_price` | on | the temporary 5-point price |

Most settings apply at install. Relaunch after changing one.

## Build the DLL

```sh
addons/rnd-tyres-thermal/native/build.sh
```

This writes `native/rnd_departments.dll`. It needs zig 0.16, or Nix to fetch it, and compiles against `packages/sdk/include` and `native/third_party/safetyhook`.

## Regenerate the UI assets

`ui/` and `erp/rnd_fifth_pie.json` are generated from the stock game archives. With the game restored to stock (`swofty restore`):

```sh
re/tools/py addons/rnd-tyres-thermal/tools/build_assets.py --game "<F1 25>"
python3 addons/rnd-tyres-thermal/tools/validate.py --game "<F1 25>"
```

`build_assets.py` rebuilds every file into `build/` and compares it with the shipped one. `--write` replaces the shipped files. See `tools/README.md` for each script, the pie design and the 72° wedge mesh format.

## Install into a profile

Build the DLL first, then:

```sh
swofty addons install addons/rnd-tyres-thermal
swofty --profile <profile> addons enable swofty.rnd-tyres-thermal
```

Use a modded profile and launcher v1.0.4 or later. The launcher applies the addon when you press Play.

## Details

- `native/HOOKS.md`: every hook site and address, the department-byte sweep and the expected log lines for each version.
- `DESIGN.md`: the department design, the database containers, the version history and the temporary test values.
- `native/PIE_INTEGRATION.md`, `native/PIE_SITES.md`: the pie's native half and its hook-site proofs.
- `tools/README.md`: the asset tools and the UI resources they build.
