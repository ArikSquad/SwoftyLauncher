# Engine hooks — scoping result

Two reverse-engineering passes (findings in the session scratchpad `rnd/` and `rnd-dept/`).

## Verdict: likely doable — mostly data + ONE small mixin. No confirmed hard fixed-4 blocker.

There is no single `array[4]` gating departments on the parse, apply, or economy paths. The one enum that defines the team R&D departments is hookable at a **single, uniquely-scannable site**.

## Key facts
- **Team R&D departments (enum, single resolver @~0x1427743xx):** Powertrain=1, Chassis=2, Aerodynamics=3, **Tyres=4**, default=0. Unknown name → 0 (no crash). So at the enum level F1 25 already has a **Tyres** department; "Durability" belongs to the engine-supplier set, not the team-dept enum.
- **Per-upgrade `upgrade_department` enum (switch @0x14353a700, ~27 inlined copies):** Chassis=1, Durability=2, Engine=3, Fake=4, Invalid=5. Unknown → 0.
- **Department-entry loader `fcn.141232fff`:** loops by DB instance count (`cmp r8d,[rsp+0xa4]`), not 4; stores into a pointer-array `[rsi+0xc60]` stride 16. Residual fixed-capacity candidate: embedded sub-arrays at `rsi+0x738 + N*0x48` (not proven a cap).
- **Apply `fitUpgradesUpToRating @0x143514670`:** dynamic robin_hood iteration; groups by `==` on enum bytes; lerps rating between `DepartmentData` bounds `[r14+0x10]/[r14+0x14]`; writes car-stat delta to the vehicle attr array (count `[veh+0x1960]`, base `[veh+0xd10]`, stride 0xc40, via 0x1449e2850). A new department's upgrades apply with NO code change **if they reuse existing effect types** (tyre_wear/brakes/general_maintenance).
- **Departments UI (`laptop_r_and_d_departments_container.erp`):** data-bound tab nav (`tab_controller`/`primary_toggle_navigation`, `@department_name/colour/icon/summary`). Strong evidence tabs render from the department collection; not 100% proven spawner vs fixed 4.
- **Economy + AI:** data-driven; cost/time/fail fields parsed generically; AI uses per-TYPE `AIUpgradeData`, so reusing existing effect types needs no new AI rows.

## The mixin (1 hook)
`SWOFTY_MIXIN_SITE` on the team-dept enum store. Scan signature (verified unique, count=1):
```
75 06 B1 03 EB 02 31 C9 88 8F 40 03 00 00
```
Site = the `88 8F 40 03 00 00` store (`mov [rdi+0x340], cl`). Handler: `rax` still holds the ci-FNV-1a-64 hash of the department token; for our two new DB tokens, set `rcx` low byte to 5/6 so the store persists the new enum value. Only needed for a genuinely new enum value — if "Tyres"(4) is an unused spare, department A may need zero enum code.

## Data (pure, no code)
- 1 `UpgradeDepartmentEntry` row (cost/time/fail scales + `department` = the token) per season family + bumped instance count.
- `upgrade_N` tree reusing existing `upgrade_type`/`affect_on_vehicle_unit_type`; bumped instancecount.
- `lang.set` strings for name/description/icon. Optional ERP group.

## First-cut findings (this build's deeper RE; scope: single "Tyres & Thermal Management" department)

Ground-truth reads of the shipping GVDB (742,857 records) + the live `[rnd-probe]` session refined the
scaffold verdict above:

- **`Tyres`(4) is a confirmed unused spare.** No row anywhere has `department = Tyres`; the four stock
  `UpgradeDepartmentEntry` rows carry `department` = `Engine`/`Durability`/`Chassis`/`Aerodynamics`. So the
  new department claims value 4 by writing `department = "Tyres"` — **no new enum value is minted**.
- **The enum store site `0x142774258` never fired during the career/MyTeam R&D session** (the probe hooked
  it; zero `[#0]` lines). The `UpgradeDepartmentEntry` loader reads `department` as a **demasked string**
  (generic getter `0x1445a2cf0` + a copy loop at `0x141232a91`), not through that hash-switch. So the
  mixin at `0x142774258` is a **safe confirm/activate + diagnostic**, not the load-bearing lever for the
  department tab.
- **The department rows live at `UpgradeDepartmentEntry_4..7`** under the two season containers
  (`{016caf2bb4d4f128}`, `{3121442cecc7bb08}`); the loop reads 4 of them. The new row is index **8**.
- **No `_UpgradeDepartmentEntry_instancecount_` record exists** in the DB; the count (4) is built at
  runtime (`0x1445a25c0` fills a descriptor, count at `+0x200`). The count key is set to 5 as the data
  hypothesis; whether the game honours it is the #1 in-game-test question.
- **The loader's embedded sub-arrays at `rsi+0x738 + N*0x48` have ~18 slots** (prologue builds 18
  pointers), so the residual fixed-capacity risk has headroom well past a 5th department.

## Make-or-break unknowns → the in-game test resolves these
1. Does the game read the 5th row? (count built at runtime; no DB instancecount record — a native
   count-bump is the fallback if the tab does not appear.)
2. UI tab list: spawner (+N) or fixed 4? Which lang key binds the department name?
3. Is the TEAM car-stat apply the same `fitUpgradesUpToRating` or a separate fn?
4. Tree attachment: which hashed `{...}` container the Tyres tab loads (the dept→tree lookup is unresolved).
5. Save-game tolerance of the new enum value/rows.
6. Enum overflow: a consumer that indexes a fixed array by `[obj+0x340]` and over-runs at value 4 (none
   found; `rsi+0x738+N*0x48` has ~18 slots).

## Count mechanism — RESOLVED (rnd-tyres iteration, RE + live rnd-probe trace)

The department row count is **not** a literal 4 and **not** "built from nothing".
`Career_Load_DepartmentEntries` (0x1412326e0) loops `r8d = 0 .. row_count-1`
(`cmp r8d,[rsp+0xa4]` @0x141232a28), calling `Gdb_Get_RowByIndex(cursor, out, i)`
which reads the row **`UpgradeDepartmentEntry_<i>`** (literal 0-based index).
`row_count` comes from `Gdb_Init_TableCursor` (0x1445a25c0): it builds the DB key
`<container>._UpgradeDepartmentEntry_instancecount_`, looks it up, and stores the
int at `cursor+0x200` (`mov [rsi+0x200],ecx` @0x1445a28df). Missing key → 0.

Ground truth (base GVDB + the `rnd-probe` loader.log, db_count=4, idx 0..3, two
`rsi` objects): the two containers the loader actually reads are
**`{5e21a18f9f59dbf6}`** and **`{1f304d36c3c2b947}`**, each with
`_UpgradeDepartmentEntry_instancecount_ = 4` and rows `_0.._3`
(Aero/Chassis/Durability/Powertrain). The static families `{016caf2bb4d4f128}`/
`{3121442cecc7bb08}` (rows `_4..7`, **no** instancecount key) are a different,
unread copy — the earlier "add at `_8`" plan targeted the wrong container.

Fix: data row at `UpgradeDepartmentEntry_4` in `{5e21…}`/`{1f30…}`; native Hook B
forces the count 4→5 at the `+0x200` store (sig `gamedb.dept_count_store`,
site `site_Gdb_DeptCountStore` @0x1445a28df), gated on `cursor+0x100 ==
"UpgradeDepartmentEntry"` so no other table is touched.

Caveat: `department = Tyres` → per-upgrade `upgrade_department` **ENUM A = 0**
(Aero's slot) in this loader's inline switch; the economy entry may overwrite
Aero's slot rather than add a 5th. The team-dept **ENUM B** (Tyres=4, Hook A) is
separate and drives the dept identity. Whether a 5th tab renders needs the
in-game test.

## R&D department UI — RESOLVED (hardcoded 4, NOT data-driven) {rnd-tyres UI iteration}

The R&D > Performance page (the 2x2 department box grid + the DEVELOPMENT
POTENTIAL pie) does **not** enumerate the `UpgradeDepartmentEntry` data model.
The department set is **hardcoded to 4 in native code**, so the loader-count
bump to 5 (Hook B) loads a 5th economy row but produces **no** 5th UI box. The
box grid and the pie are two independent bespoke builders, and the box/pie
geometry is **pre-authored UIDF** (4 slots), so a 5th box+slice is a
code + ERP + asset rework, not a data/asset lever. (KB: career.md#dept-ui, all
L1, build b171e9793323.)

### Box grid — `Career_Build_RnDDeptBoxGrid` @0x141c85f70 (no E8 callers; vtable)
Populates `view_model_data.laptop_r_and_d_departments_container`. It makes
**exactly four literal** `Career_Construct_RnDDeptCard` (0x141c7aac0) calls with
compile-time `(lang_key, icon, enum)` triples:

| slot | lang key | icon (`rnd_dept_icons_small.*`) | enum | colour |
|---|---|---|---|---|
| 0 | `lng_rnd_dept_aero`        | `aero`        | 0 | 0xffeead00 blue   |
| 1 | `lng_rnd_dept_chassis`     | `chassis`     | 1 | 0xff3ec58d green  |
| 2 | `lng_rnd_dept_engine`      | `engine`      | 3 | 0xff7a2aed pink   |
| 3 | `lng_rnd_dept_maintenance` | `maintenance` | 2 | 0xff16deff yellow |

(`engine` = Powertrain, `maintenance` = Durability.) Plus two non-dept panel
cards (`tab_rnd_upgrade_purchase_panel`, `tab_rnd_perks`). The card vector is
sized to **exactly 6 pointers (0x30)** and truncated. Initial selection =
`DAT_145f93c08` clamped `<4` against the 4-entry `k_Career_RnDDeptNameTable`
(0x145755e58). The card constructor reads the colour from
`k_Career_RnDDeptColourTable` (0x145755e78) with bound `<6`; slots **[4] and [5]
are 0xffffffff white spares**.

### Pie + overview — `Career_Build_RnDPotentialPie` @0x141f41d60
Loops `k_Career_UpgradeDeptCodeTable` (0x145735d60 = `00 01 02 03 04 05 …`) but
is **gated `if (code < 4)` and `if (4 < i) break`**, with a per-case `switch`
mapping `code -> lng_rnd_dept_%s / _overview_description` and **inline hardcoded
colours** (same 4; default white + lang `error_department`). Pie sectors are
pre-authored VM nodes driven by `init_pie_sector` / `highlight_pie_sector`
(`Career_Build_RnDPieSector` @0x141f45d30) — **4 sector nodes only**.

### Why the 5th box is excluded
The UI never consults the department data model; it builds 4 cards from
compile-time constants (box grid = 4 unrolled calls; pie = table loop gated
`<4`). There is no code path that creates a 5th card, and no UIDF node for a
5th box or 5th pie sector.

### Smallest correct lever for a genuine 5th box+slice (deferred rework)
A multi-site native + UIDF + asset change:
1. `Career_Build_RnDDeptBoxGrid` (0x141c85f70): grow the card vector 6 -> 7
   (change the `0x30` truncation), inject a 5th `Career_Construct_RnDDeptCard`
   for Tyres with `(lng_rnd_dept_tyres, rnd_dept_icons_small.tyres, 4)`, and
   relax the `<4` selection clamp.
2. `Career_Build_RnDPotentialPie` (0x141f41d60): relax `code<4 -> <5` and
   `4<i -> 5<i`, add a `case 4:` colour.
3. Colour: write a distinct ABGR at `k_Career_RnDDeptColourTable[4]`
   (0x145755e78, currently white).
4. Assets: `lng_rnd_dept_tyres` + `lng_rnd_dept_tyres_overview_description`, a 5th
   `tyres` frame in `rnd_dept_icons_small`.
5. UIDF (`laptop_r_and_d_departments.ui`): a 5th box layout node and a 5th
   pie-sector node (the box/pie geometry is authored, not computed).

Several sites write into fixed-stride per-entry arrays (0x220 stride in the pie
builder); mis-sizing risks a crash, and the pie physically cannot show a 5th
slice without the extra UIDF sector node — so this is **not** safely
implementable blind and is scoped as a following iteration.
