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

1. `db/departments.json` - a new `UpgradeDepartmentEntry_8` row in both season
   families (`{016caf2bb4d4f128}`, `{3121442cecc7bb08}`), cloned field-for-field
   (all 21 keys, same types) from the stock Durability row so every backing
   `MyTeam_RND_*` attribute reference still resolves (a missing key crashes the
   game). Only two fields change: `_metadata_` = `ModifierTyresThermal`,
   `department` = `Tyres`. Each family's `UpgradeDepartmentEntry` instance count
   is set to 5.
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

`native/rnd_departments.dll` - one mid-function hook at the team-department enum
resolver's store (`mov byte [rdi+0x340], cl`; signature
`75 06 B1 03 EB 02 31 C9 88 8F 40 03 00 00`). When the token being resolved is
`Tyres` it forces the stored enum byte to 4; every resolution is logged. Gated
on an exactly-one scan match - on 0 or >1 matches it logs and installs nothing.
Forcing 4 is a confirm/activate of the spare, never a new value.

## Residual unknowns (the in-game test resolves these)

- **Does the game read the 5th department row?** The `UpgradeDepartmentEntry`
  table has no discoverable `_instancecount_` record; the count (4) is built at
  runtime, and the rows sit at indices 4-7. The row is added at index 8 with the
  count set to 5. If the game ignores it, the tab simply will not appear (no
  crash) and a native count-bump becomes the next step.
- **UI tab rendering** - whether the departments container spawns a 5th tab from
  the collection, and which lang key it binds for the name.
- **Upgrade-tree attachment** - which container the Tyres tab loads.
- **Save tolerance** - whether a career save accepts the new row/enum value.
- **Enum overflow** - no fixed array keyed by the department byte was found
  (the loader's embedded sub-arrays have ~18 slots), but value 4 in a consumer
  that only expects 0-3 is unproven.
