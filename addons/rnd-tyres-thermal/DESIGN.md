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
   Upgrade All Durability) in a dedicated container, each node a valid clone of
   the stock durability node schema with `upgrade_department = Tyres`. This is
   ready data; **attaching the container to the Tyres tab is the next
   iteration** (the department->tree-container lookup is unresolved by the
   read-only RE).

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

**Why the 5th pie slice is NOT shipped yet.** The live Performance-page pie is
built by `Career_Build_RnDPieOverview` (`0x141f46eb0`, + populate helper
`0x141f48030`) - NOT the canonical `Career_Build_RnDPotentialPie` (`0x141f41d60`),
which belongs to a parallel research-overview screen that does not open here. The
live pie allocates its per-sector array at exactly `0x900` (`0x80 + 4*0x220`) and
constructs exactly four slots, with a `code < 4` loop guard. A fifth slice needs
the buffer grown to `0xb20`, a fifth constructed slot, the guards relaxed, a
`case 4` colour/name - and, unlike the box, a **5th pie-wedge authored in art**
(the wedge geometry is authored, not computed). That rework is the next
iteration; see `native/HOOKS.md` for the exact multi-site patch spec against the
live builder.

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
- **UI box rendering** - the R&D > Performance department box grid is **hardcoded
  to 4 in native code** and does **not** enumerate the `UpgradeDepartmentEntry`
  data model, so the loader-count bump alone does **not** add a 5th box. v0.3.0
  therefore injects a real fifth card natively into
  `Career_Build_RnDDeptBoxGrid` (`0x141c85f70`) - see the v0.3.0 section above and
  `native/HOOKS.md`. **Open for the in-game test:** whether the box container lays
  the fifth card out visibly (it may be a fixed 4-slot 2x2 grid, or an auto-flow)
  - the injection is faithful either way; this is the one thing static analysis
  cannot settle. The 5th pie slice is deferred (authored wedge art); the live pie
  builder is `0x141f46eb0` (not `0x141f41d60`).
- **Upgrade-tree attachment** - which container the Tyres tab loads.
- **Save tolerance** - whether a career save accepts the new row/enum value.
- **Enum overflow** - no fixed array keyed by the department byte was found
  (the loader's embedded sub-arrays have ~18 slots), but value 4 in a consumer
  that only expects 0-3 is unproven.
