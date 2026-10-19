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
