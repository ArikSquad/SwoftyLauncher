# R&D: Tyres & Thermal Management

A Swofty Launcher addon for F1 25 that adds one brand-new top-level career R&D
department, **Tyres & Thermal Management**, alongside the stock four
(Aerodynamics, Chassis, Durability, Powertrain). A new department is not a pure
database edit: the department enum lives in `F1_25.exe`, so this addon pairs
database rows with a native engine mixin. See `native/HOOKS.md` for the
reverse-engineering verdict and the exact hook site.

## The department

Tyres & Thermal Management is a real engineering discipline currently split
between Chassis and Durability. Its upgrade nodes reuse existing
`upgrade_type` effects, so each node gets a real car-stat effect with no new
effect code:

- Tyre-wear reduction - `Tyre Wear`
- Brake cooling / performance - `Brakes`
- Cooling-driven reliability - `Upgrade All Durability` (general maintenance)

## Enum value: 4 (the "Tyres" spare)

The team-department enum in `F1_25.exe` is a single case-insensitive FNV-1a-64
switch that resolves `Powertrain=1`, `Chassis=2`, `Aero=3`, `Tyres=4`,
default/unknown `=0`. The token **Tyres (=4) is a defined-but-unused spare**:
reading the shipping database (742,857 records) finds no row whose `department`
value is `Tyres`, and no other `Tyres` department token anywhere. The four
shipping `UpgradeDepartmentEntry` rows carry `department` values `Engine`,
`Durability`, `Chassis`, `Aerodynamics`.

So the new department claims value 4 by writing `department = "Tyres"`: the
engine already resolves it natively, and it cannot collide with a shipped
department. No new enum value is minted.

## Build plan

### Data (`operations.database`)

1. `db/departments.json` - a new `UpgradeDepartmentEntry_4` row in the two
   containers the runtime department loader actually reads,
   `{5e21a18f9f59dbf6}` and `{1f304d36c3c2b947}` (each ships
   `UpgradeDepartmentEntry_0..3` = Aero/Chassis/Durability/Powertrain and
   `_UpgradeDepartmentEntry_instancecount_ = 4`). The row is cloned
   field-for-field (all 21 keys, same types) from the stock Durability row so
   every backing `MyTeam_RND_*` attribute reference still resolves (a missing key
   crashes the game). Only two fields change: `_metadata_` =
   `ModifierTyresThermal`, `department` = `Tyres`. The row count is **not** set in
   data - the native mixin bumps it to 5 at the source (see Code below), which is
   what makes the loader's `for i in 0..count-1` read our row `_4` as the fifth.

   (The static containers `{016caf2bb4d4f128}`/`{3121442cecc7bb08}` an earlier
   read-only pass used - rows at `_4..7`, no `_instancecount_` key - are a
   different, unread copy; the live `rnd-probe` loop proved the loader reads
   indices `0..3` from `{5e21…}`/`{1f30…}`.)
2. `db/tyres_tree.json` - a small three-node upgrade tree (Tyre Wear, Brakes,
   Upgrade All Durability), each node a field-for-field clone of the stock
   `fake.upgrade_0` hub schema. **Lookup RESOLVED (v0.4.0):** `Career_Load_UpgradeRows`
   (0x143539bc0) opens the tree container by dept NAME, `<team_link>.<deptname>`,
   with `k_Career_UpgradeDeptNameTable[4] = "fake"` - so the 5th department's tree
   is the `<team_link>.fake` container, a naming convention, not a data key on the
   dept row. Each record is classified by its own `upgrade_department` -> ENUM A,
   and the page shows records whose ENUM A matches its code, so the rows set
   `upgrade_department = "Fake"` (ENUM A = 4) - NOT "Tyres" (which resolves to 0
   and would collide with Aero).

   **Container RESOLVED (v0.4.1, live in-game diagnostic).** The code-4 tree is
   read per-save from `upgrades.2026.trees.<team_link>.fake`, where `team_link`
   resolves to the team's **engine-supplier constructor** token, NOT the
   constructor and NOT a generic `user_defined` hub. A McLaren (Mercedes PU) save
   opens `upgrades.2026.trees.mercedes.fake` = `{deae0310dc31dce2}`
   (`_upgrade_instancecount_` key `{b2932bd32df83903}`, `upgrade_0._metadata_` key
   `{cabec8fb14f94a7a}` - all three match the live diagnostic). The gamedb holds
   **46 real `.fake` hub containers** (each carries the shipped
   `fake.upgrade_0 = central_fake_upgrade` hub, `_upgrade_instancecount_ = 1`, same
   15-key schema). 12 are `upgrades.2026.trees.<team>.fake` for `ferrari`,
   `force_india`, `gm_cadillac`, `haas`, `mclaren`, `mercedes`, `redbull`,
   `renault`, `sauber`, `tororosso`, `user_defined`, `williams`; the other 34 are
   additional verified-identical fake hubs (per-save/variant copies) carried by
   hash. The 2025-season team trees do not exist, and the
   `engine_suppliers.trees.engine_supplier_upgrades_N.fake` containers (e.g.
   `{3c55d614fc76ce3e}`) are **empty** (no hub, no instancecount) so they are not
   written. The three `user_defined.fake` family hubs the v0.4.0 note mislabelled
   (`{ae8319a01aa30478}`, `{5927ed801bdb6765}`, `{75a045fd77ae52b9}`, plus the
   named `{f71aa592926ce9d9}` = `user_defined`) are four of these 46 real hubs and
   now carry the corrected rows (kept, not removed - they are real per-save
   targets).

   The 3 nodes ship as `upgrade_1/2/3` into **all 46** containers (the shipped
   `upgrade_0` hub is preserved, `_upgrade_instancecount_` bumped 1 -> 4), each a
   field-for-field clone of that container's own stock `fake.upgrade_0` schema (all
   15 keys, matching type tags). Keys use the per-container
   `{container_hash}.upgrade_N.<field>` scheme the addon DB-op engine resolves as
   `fnv1a64_from(container_hash, ".upgrade_N.<field>")`, verified against the real
   GVDB keys and applied end-to-end through the engine (0 stock collisions,
   gamedb round-trip stable).

   **Reachability RESOLVED (v0.4.1).** The whole career R&D tree is one graph
   rooted at the `fake` hub node `default` (`central_fake_upgrade`, grid `0,0`),
   which links OUTWARD (child/unlock edges) to each department's branch root by the
   target node's `_metadata_` id (e.g. `gearbox_durability_1`). A node is
   reachable/buyable only if a directed path from `default` reaches it. So the fix
   attaches the Tyres branch exactly as a stock department attaches to the hub: the
   hub's `links` list gains our root (`fake.upgrade_0.links` += `,tyres_thermal_1`,
   preserving the stock branch targets), then `tyres_thermal_1` -> `tyres_thermal_2`
   -> `tyres_thermal_3` chain outward. (The v0.4.0 rows linked backward - child to
   hub - and never attached the hub to the branch, so they were unreachable even
   in the wrong container.) The `attach_tyres_tree` native hook stays as-is
   (read-only logging). Remaining in-game-test item: confirm the branch renders and
   is buyable (link-gated vs. facility-gated buyability is the one thing static
   analysis cannot settle; the hub-link attach is the stock-faithful, strictly-safer
   choice either way).

### Text (`operations.assets`)

`assets/text.json` - `lang.set` strings for the department name and description
under several candidate keys. The exact key the R&D UI binds for a department
name is unverified (see Residual unknowns), so the strings cover the likely
conventions.

### Code (`operations.native`)

`native/rnd_departments.dll` - two independently-gated mid-function hooks:

- **Hook A (team-dept enum store)** `mov byte [rdi+0x340], cl`; signature
  `75 06 B1 03 EB 02 31 C9 88 8F 40 03 00 00`. When the token being resolved is
  `Tyres` it forces the stored enum byte to 4 (a confirm/activate of the spare,
  never a new value); every resolution is logged.
- **Hook B (department row count)** `mov [rsi+0x200], ecx` @`0x1445a28df` inside
  `Gdb_Init_TableCursor`; signature `89 8E 00 02 00 00 48 81 C4 20 01 00 00`
  (KB binding `gamedb.dept_count_store`). This is the single point where the
  `UpgradeDepartmentEntry` row count (from
  `<container>._UpgradeDepartmentEntry_instancecount_`, stock 4) is written into
  the cursor. The handler only acts when the cursor's row word (`cursor+0x100`)
  is `UpgradeDepartmentEntry` and the count is the stock 4, forcing it to 5, so
  every consumer that inits that table cursor agrees. The loader then reads
  `UpgradeDepartmentEntry_0..4`, i.e. our row `_4`.

Hooks A and B are gated on an exactly-one scan match - on 0 or >1 matches (or a
failed install) they log and install nothing, so the DLL stays benign.

The same DLL also carries the R&D > Performance UI rework:

- **Colour groundwork** (v0.2.0) - `k_Career_RnDDeptColourTable[4]`
  (`0x145755e88`, the stock white spare the fifth card reads via the `< 6` bound
  in the card constructor) is written to a distinct Tyres colour (`0xff0078ff`,
  orange). The write only happens after a layout probe confirms all six colour
  entries match the decompile on this build (`[0]=ffeead00 [1]=ff3ec58d
  [2]=ff16deff [3]=ff7a2aed [4]=[5]=ffffffff`); on any mismatch nothing is
  written.
- **The live 5th box** (v0.3.0) - a real fifth `Career_Construct_RnDDeptCard` for
  Tyres & Thermal Management is injected into `Career_Build_RnDDeptBoxGrid`
  (`0x141c85f70`) so the stock grid renders a fifth department box. Behind its own
  independent gate (one unique `.xtls` scan, hook site `0x141c86567`) and a
  feature flag (`render_fifth_box`, default on), separate from Hooks A/B and the
  colour write. The card vector (six pointers) is grown to seven, a 0x950 card is
  built the game's way with name `lng_rnd_dept_tyres`, a borrowed `aero` icon
  frame (no new texture) and enum 4 (colour slot [4]), and the builder's own
  attach loop binds it. The `< 4` selection clamp is left intact, so the box only
  renders; it is never the selected card. Full mechanism + addresses in
  `native/HOOKS.md`.

**Correction to the earlier scoping note.** The dept card's compile-time
`%s.%s.%s` argument is the **icon atlas path**
(`<pkg>.rnd_dept_icons_small.<frame>`), not a per-dept UIDF node path; the name is
a separate lang-key argument. So a dept card needs **no authored UIDF node** and
**no launcher UIDF/ERP pipeline** - it renders through the same card VM node the
stock four use. The v0.2.0 note claiming a fifth box needs an unbuilt UIDF node
was based on a misread of the card constructor arguments.

**The 5th pie slice (v0.4.0, partial, flag `render_fifth_pie`, default off).** The
live Performance-page pie is built by `Career_Build_RnDPieOverview` (`0x141f46eb0`,
+ populate `0x141f48030`) - NOT `Career_Build_RnDPotentialPie` (`0x141f41d60`,
dormant here). v0.4.0 grows its `0x900` (`0x80 + 4*0x220`) per-slice buffer to
`0xb20`, constructs a real 5th slot (`FUN_141f45650` @ buf+0x900), and relaxes the
`code < 4` populate guard so dept code 4 is populated - all faithful extensions of
the stock prologue (not blind). DEFERRED: the **5th pie-wedge authored in art**
(four 90-degree sector nodes) and the jump-table `case 4` name/colour; code 4 falls
to the default case (name `error_department`, white) and its sector node resolves
to an empty handle, so the slice is populated but no wedge draws yet. See
`native/HOOKS.md` for the exact sites.

## v0.4.0 status summary

| piece | flag | shipped | deferred |
|---|---|---|---|
| 5th box visible cell | `render_fifth_box_layout` | read-only slot-vector diagnostic (cause resolved: no authored +0x270 slot / scene cell) | authored/cloned 5th slot + scene rect |
| upgrade tree | `attach_tyres_tree` | 3 nodes in all 46 real `trees.<team_link>.fake` hubs (`upgrade_department=Fake`), instancecount 1->4, hub `links` attach + outward chain, + container-confirm hook | branch render/buyability confirm + rating integration (in-game test) |
| 5th pie slice | `render_fifth_pie` (off) | buffer grow + 5th slot + guard relax | authored wedge geometry + jump-table case 4 |

## Residual unknowns (the in-game test resolves these)

- **Does the game read the 5th department row?** Resolved by RE + the live
  `rnd-probe` trace: the count comes from the DB key
  `<container>._UpgradeDepartmentEntry_instancecount_` (= 4), read by
  `Gdb_Init_TableCursor` and used as the loader's `for i in 0..count-1` bound.
  The loader reads indices `0..3` from `{5e21a18f9f59dbf6}`/`{1f304d36c3c2b947}`.
  The native Hook B bumps that count to 5 so index `4` (our row) is read. **Open
  for the in-game test:** `department = Tyres` maps to the per-upgrade
  `upgrade_department` **ENUM A = 0** (default) in this loader's inline switch -
  the same slot as Aerodynamics - so the economy entry may collide with Aero's
  slot rather than form a distinct 5th entry. Whether a distinct 5th *tab* then
  renders (the tab list may enumerate via a different path) is the test question.
- **UI box visibility (v0.4.0 RESOLVED cause).** The v0.3.0 5th card is attached
  but invisible because the box grid's container (`builder+0x2d8`, a UITC tab VM)
  only shows departments that have an entry in the authored slot vector
  `builder+0x548..+0x550` (`container+0x270`), created at screen init from authored
  tab/scene nodes (a fixed four) - the per-card attach never adds a slot. A visible
  5th cell needs a 5th authored/cloned slot + a 5th `rnd_card_with_info` scene rect
  (the scene authors only four). The `render_fifth_box_layout` diagnostic captures
  the slot/card vectors in-game so that follow-up (external ERP scene patch, or a
  runtime slot clone) has ground truth. See `native/HOOKS.md`.
- **Upgrade-tree attachment (v0.4.1 RESOLVED).** `Career_Load_UpgradeRows` opens
  the tree by dept NAME, `upgrades.2026.trees.<team_link>.fake` for code 4, where
  `team_link` is the team's engine-supplier constructor token (McLaren save ->
  `mercedes` -> `{deae0310dc31dce2}`, matching the live diagnostic). The 3 nodes
  ship in all 46 real `trees.<team_link>.fake` hub containers as `upgrade_1/2/3`
  (`upgrade_department=Fake`, ENUM A=4), attached to the hub `default` via its
  `links` list and chained outward. Open: confirm the branch renders and is
  buyable, and the rating integration (in-game test).
- **Save tolerance** - whether a career save accepts the new row/enum value.
- **Enum overflow** - no fixed array keyed by the department byte was found
  (the loader's embedded sub-arrays have ~18 slots), but value 4 in a consumer
  that only expects 0-3 is unproven.
