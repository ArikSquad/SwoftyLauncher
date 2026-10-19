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

## v0.3.0 — the LIVE 5th BOX, shipped (box only; pie deferred)

A genuine 5th department box now renders on R&D > Performance behind its own
independent gate. Two earlier assumptions were **wrong** and are corrected here.

### Correction 1 — the dept card does NOT bind a per-dept UIDF node
`Career_Construct_RnDDeptCard` (0x141c7aac0) takes
`(card, ctx, builder, allocator, name_cstr, icon_cstr, enum_byte)` — confirmed by
the call-site disassembly at 0x141c861bc-0x141c861e3 (5th arg [rsp+0x20] = name,
6th arg [rsp+0x28] = icon, 7th arg [rsp+0x30] = enum). The exe string bytes are:
- 5th arg = the **name lang key**: `lng_rnd_dept_aero` (0x1457407d6),
  `lng_rnd_dept_chassis`, `lng_rnd_dept_engine`, `lng_rnd_dept_maintenance`.
- 6th arg = the **icon atlas path**, formatted `%s.%s.%s` = `<pkg>` (from
  `*(lVar8+0x660)`) `.` `rnd_dept_icons_small` (0x14570bcda) `.` `<frame>`
  (`aero`/`chassis`/`engine`/`maintenance`). So the `%s.%s.%s` suffix is the icon
  **frame name in the `rnd_dept_icons_small` atlas**, NOT a view-model/UIDF node
  path. ctor stores icon at card+0x818 (param_1[0x103]), name at card+0x830
  (param_1[0x106]), enum at card+0x6d8 (param_1[0xdb]), colour at card+0x948
  (param_1[0x129]) = `k_Career_RnDDeptColourTable[enum]` (bound `<6`).

Consequence: a dept card needs **no per-dept authored UIDF node** and **no
launcher UIDF/ERP pipeline**. It renders through the same card VM node
(card+0x268) the other four use, attached to the container `builder+0x2d8` by the
builder's own attach loop. This removes the blocker the v0.2.0 notes recorded.

### The box injection (implemented)
- **Gate (independent of Hooks A/B + the colour write):** one `.xtls` scan of
  `48 C7 40 18 00 00 00 00 48 8B 8E 98 02 00 00` (unique, count=1; match =
  0x141c8655f). Hook site = **match+8 = 0x141c86567**, the `mov rcx,[rsi+0x298]`
  that starts the purchase-panel alloc, right after the 4th dept card is stored
  and before the panels are built. safetyhook MidHook. Feature flag
  `render_fifth_box` (toggle, default on). On any scan miss / layout mismatch /
  >1 match it logs and injects nothing.
- **At the site:** `rsi` = builder (param_1), `rdi` = the icon-prefix object
  (lVar8, non-volatile, set at 0x141c8601e, clobbered at 0x141c8657f, so live
  here); begin[0..3] = the four dept cards; panels not yet built.
- **Grow:** the card-pointer vector (begin `[self+0x280]`, end `[+0x288]`, cap
  `[+0x290]` in pointer units, allocator `[+0x278]`) is sized to exactly 6
  (end-begin==0x30). If cap<7, reallocate to 8 via the allocator vtable (alloc =
  `vt+0x18(allocator,bytes,align)`, free = `vt+0x38(allocator,ptr)`), copy the 6
  pointers, free the old buffer, set cap=8. Set `end = begin + 0x38` (7 slots).
  The resumed stock code then fills begin[4]/begin[5] (panels) off a freshly-read
  begin, leaving **begin[6] = the Tyres card**.
- **Construct:** allocate a 0x950 card via the card allocator `[self+0x298]`;
  build the icon NeStr with `Ne_Make_StrFromCStr(&s,"",0,0)` (0x1441974e0) +
  `Ne_Format_String(&s,"%s.%s.%s", *(char**)(lVar8+0x660), "rnd_dept_icons_small",
  "aero")` (0x1441630f0); call `Career_Construct_RnDDeptCard(card, *(self+0x28),
  self, cardAllocator, "lng_rnd_dept_tyres", s->chars, 4)`; store begin[6]=card;
  release the temp NeStr through `g_Ne_DefaultAllocator` (0x146ca54a8, free =
  vt+0x38). NeStr chars at +9, refcount (u16) at +4.
- **Icon:** BORROWED — frame `aero` of `rnd_dept_icons_small` (no new texture).
  Logged as `icon-borrowed="<pkg>.rnd_dept_icons_small.aero"`.
- **Colour:** reuses `k_Career_RnDDeptColourTable[4]` (0x145755e88) = Tyres orange
  0xff0078ff, written by the v0.2.0 colour groundwork; the card ctor reads it via
  its `<6` bound at construction time (after load).
- **Name:** our own `"lng_rnd_dept_tyres"` literal (the loc system resolves it at
  display time; confirmed resolving live).
- **Selection clamp NOT relaxed.** `DAT_145f93c08` stays clamped `<4` against the
  4-entry `k_Career_RnDDeptNameTable` (0x145755e58). A saved index 4 would read
  `name[4]` which overlaps `colour[0]` (the colour table immediately follows the
  name table at 0x145755e78) → a colour read as a char* → crash. So the 5th box
  only RENDERS; it is never the selected card this round.

Deviation from the original spec note ("grow at the allocation/truncation site"):
growing after the 4th card (where lVar8 is live and all four cards exist) is
cleaner and leaves the stock grow/truncate and the panel stores untouched. The
builder's own attach loop (begin..end) then binds all seven slots identically.

### Confirmed-safe vs the in-game test
- **Safe (static-proven):** the scan is unique on this build; the card is built by
  the real constructor identically to a stock dept card, so the attach loop
  (`card+0x260` method + `FUN_14431d720(builder+0x2d8, card+0x268,0,0)`) cannot
  fault on it; selection stays `<4`; every read/write is region-guarded; the two
  panels still land in begin[4]/begin[5].
- **The test resolves:** whether the container `builder+0x2d8` lays the 5th card
  out visibly (it may be a fixed 4-slot 2x2 grid — 5th invisible/overlapping — or
  an auto-flow that shows a 3rd row). This is the one thing static analysis cannot
  settle; the injection is faithful either way.

### Pie path — RESOLVED (next iteration), the LIVE builder is NOT 0x141f41d60
The Performance-page pie did not fire 0x141f41d60 because there are **two parallel
R&D view-model classes**:
- Class A (research-overview/graph screen, vtable 0x14575e000): pie builder
  `0x141f41d60` (+0x98), sector handler `0x141f45d30` (+0x50). NOT this page.
- **Class B (LIVE, vtable 0x14575e300)**: pie builder
  **`Career_Build_RnDPieOverview` @0x141f46eb0** (+0x98), sector handler
  `0x141f4ac40` (+0x50). Built by `0x141c43290` — the **same 0x141c4xxxx screen
  family that owns the box grid `Career_Build_RnDDeptBoxGrid` @0x141c85f70**.
  `0x141f46eb0` has no E8 callers; it is reached via VM dispatch (0x14431d720)
  from vtable-B slot 0x14575e398, binds `onTabActivated`, and emits
  `init_pie_sector` / `rnd_overview_activated`.

Mechanism: `0x141f46eb0` allocs a **0x900** sector buffer (`0x80 + 4*0x220`) at
0x141f46f75, constructs 4 slots via `FUN_141f45650` at buf+0x80/+0x2a0/+0x4c0/
+0x6e0 (unrolled, stride 0x220), then populates via helper **`0x141f48030`**:
loops `k_Career_UpgradeDeptCodeTable`, guard `if(code<4)` (`cmp r14d,3/ja`
@0x141f48218), index cap `if(4<written)` (`cmp r13d,4/ja` @0x141f4821e, already
permits 5), a 4-entry jump table (bound `cmp r14d,3` @0x141f4822b, table
@0x141f48231); **colours are an INLINE switch** (aero 0xffeead00 @0x141f4858e,
chassis 0xff3ec58d, maintenance 0xff16deff, engine 0xff7a2aed, default white — the
pie does NOT read the colour table), names formatted `lng_rnd_dept_%s` /
`lng_rnd_dept_%s_overview_description` from the matched icon name; sector VM nodes
resolved by name-hash via `Vm_GetOrCreate_ChildNode` under `rnd_wheel_icon`
(empty handle on miss); highlight clamp `if(3<DAT_145f93c08)`.

5th-slice plan (LIVE path — patch 0x141f46eb0 + 0x141f48030, **NOT** 0x141f41d60):
1. Buffer grow @0x141f46f75: `mov edx,0x900` (`ba 00 09 00 00`) → `ba 20 0b 00 00`
   (**0xb20** = 0x80 + 5*0x220). Mandatory before relaxing the guard.
2. Construct a 5th slot: inject `lea rcx,[rbx+0x900]; call FUN_141f45650` (the four
   are unrolled at 0x141f46fdf..0x141f4700a; no inline room → code cave/detour).
3. 0x141f48030 code guard @0x141f48218 `cmp r14d,3`→`4`; jump-table bound
   @0x141f4822b `cmp r14d,3`→`4` and extend the rel32 table @0x141f48231 to 5
   entries with a `case 4`; index cap @0x141f4821e already permits 5.
4. Add a `case 4` colour (e.g. 0xff0078ff) + icon-name `tyres` so
   `lng_rnd_dept_tyres` / `_overview_description` resolve.
5. Highlight clamp (line ~254, `3 < DAT_145f93c08`) → `4`.
6. Assets: a 5th pie-sector node under `rnd_wheel_icon` (resolved by name-hash) +
   the pie-wheel **geometry reworked for 5 wedges** (authored, not computed), and
   the two lang keys. This is why the pie slice is deferred — unlike the box, the
   wedge is authored art.

Decompiles: `re/decomp/141f46eb0.c` (live builder), `re/decomp/141f48030.c`
(populate loop), plus class-A `141f41d60.c`/`141f45d30.c` for contrast.

## v0.4.0 — three independently-gated pieces (box visibility, tree, pie slice)

Three pieces, each behind its OWN exactly-one `.xtls` scan + feature flag,
independent of Hooks A/B, the colour write and the v0.3.0 box inject. On any scan
miss / layout mismatch / >1 match each logs `[rnd-tyres]` and does nothing.

### The data model that ties it together (RESOLVED)
The whole R&D UI (box grid, pie, upgrade trees) is keyed by the per-upgrade
**ENUM A** (`upgrade_department`), not the team-dept ENUM B:
`0=aero 1=chassis 2=durability 3=engine 4=Fake` (switch @0x14353a6dd). Value **4
("Fake") is the unused spare** that card enum 4 / `k_Career_RnDDeptColourTable[4]`
and `k_Career_UpgradeDeptNameTable[4]="fake"` all line up on. So the "5th
department" is the code-4/"Fake" slot made real. (The team `department`=Tyres /
Hook A / ENUM B=4 is a separate axis driving the team's setup, not the R&D grid.)

### Piece 1 — the 5th box is invisible: CAUSE RESOLVED, visible cell DEFERRED
`Career_Build_RnDDeptBoxGrid`'s container (builder+0x2d8) is an embedded **UITC
tab-container VM**. A department is visible/selectable only if it has an entry in
the authored **slot vector at container+0x270..+0x278 (= builder+0x548..+0x550)**,
0x10 per entry: entry+8 = slotObj, slotObj+0x20 = bound card node, slotObj+8 =
inner content node. Those slots are created at **screen init** from the authored
tab/VM nodes (`view_model_data.laptop_r_and_d_departments_container`) + the
authored `rnd_card_with_info` scene instances in `frontend_overlay.idf`, keyed by
node id — a **fixed four**. The per-card attach `FUN_14431d720` only registers a
child (container+0x28/+0x378); it **never writes +0x270**. So the v0.3.0 5th card
is parented but has no slot and no authored scene cell → invisible (matches the
live result).
A genuine 5th cell needs a 5th +0x270 slot **and** a 5th `rnd_card_with_info`
scene instance with a real cell rect. The slot-register routine (UITC `vt[0x48]` /
screen builder 0x141c43290) is not in the decompiled corpus and the 5th cell rect
lives in the un-dumped `frontend_overlay.idf` scene; cloning a slot + fabricating
a rect blind risks a crash, and the declarative UIDF/ERP pipeline is not built — so
**no visible-cell lever is safely shippable from the addon this round**.
**Shipped (flag `render_fifth_box_layout`, default on, scan
`career.rnd_box_layout_probe` @0x141c86d86, read-only):** a one-shot diagnostic
that dumps the live slot-vector vs card-vector + every authored slot's bound/inner
node, so the follow-up (an external f1-modding ERP scene patch adding a 5th
authored cell, or a runtime slot+scene-node clone using the captured pointers) has
exact ground truth.

### Piece 2 — the upgrade tree (flag `attach_tyres_tree`)
`Career_Load_UpgradeRows` (0x143539bc0) loops codes 0..4 and opens
`<team_link>.<deptname>` (`"%s.%s"`, deptname from `k_Career_UpgradeDeptNameTable`;
[4]="fake"), reading `upgrade_<N>` rows into one vector. Each record stores only
its **ENUM-A byte**, so the department page shows records whose ENUM A == its code.
Therefore the Tyres (code-4) page shows records with `upgrade_department="Fake"`
(ENUM A=4). `db/tyres_tree.json` sets `upgrade_department="Fake"` — the flagged
**economy-collision fix** ("Tyres"→0 would collide with Aero; "Fake"→4 is the
distinct spare).
The "fake" container is NOT empty — it is the tree's **central hub**
(`fake.upgrade_0 = central_fake_upgrade`, instancecount 1, linking the branch
roots). MyTeam's tree is **`<handle>.trees.user_defined`** (engine-supplier choice
is irrelevant), with **four shipped `user_defined.fake` containers** (2025/2026 ×
a duplicate copy), FNV container hashes
`{ae8319a01aa30478}` `{5927ed801bdb6765}` `{75a045fd77ae52b9}` `{f71aa592926ce9d9}`.
**Shipped data:** the 3 Tyres nodes are written as `upgrade_1/2/3` into all four
(preserving `upgrade_0`), each `_upgrade_instancecount_` bumped 1→4, every field a
field-for-field clone of the stock `fake.upgrade_0` schema (15 keys, correct type
tags) with real `upgrade_type` (Tyre Wear / Brakes / Upgrade All Durability) and
`upgrade_department=Fake`. The DB-op `{hash}` selector continues FNV through
`.upgrade_N.field` (verified against the GVDB keys).
**Shipped native (scan `career.load_upgrade_rows_deptpath` @0x143539c84, default on,
read-only):** a hook (r8=team_link cstr, r9=deptname cstr, rdi=code) that logs each
code's resolved path + FNV container hash; the code-4 line should equal one of the
four hashes above, confirming the placement on the player's save (and revealing the
exact `{hash}` if a future build/save differs). Remaining in-game-test items: the
display-filter (ENUM A == card enum), tree reachability/linkage of the new nodes
(their `links` connect to the hub via `_metadata_` "default"; whether the hub must
also list them is the open question), and the economy/rating integration.

### Piece 3 — the development-potential pie 5th slice (flag `render_fifth_pie`, default OFF)
Live builder `Career_Build_RnDPieOverview` (0x141f46eb0) + populate
`Career_Populate_RnDPieSlices` (0x141f48030). Scan `career.rnd_pie_buf_alloc`
@0x141f46f6b (`mov rbx,[rsi+0x270]; mov rax,[rbx]; mov edx,0x900; …`), all sites by
fixed RVA + region/value verified, patches ordered so the guard is relaxed LAST:
1. buffer `0x900→0xb20` immediate @0x141f46f76 (= 0x80 + 5*0x220).
2. MidHook @0x141f47012 (rbx=buffer, after the 4th `FUN_141f45650`): construct the
   5th slot `FUN_141f45650(buf+0x900)` (slot i @ buf+0x80+i*0x220).
3. relax the populate `code<4` guard `cmp r14d,3 → 4` @0x141f4821b. (The index
   guard `cmp r13d,4` @0x141f4821e already permits 5 writes; left intact.)
These are faithful extensions of the stock prologue, not blind writes. **DEFERRED
(authored art):** the pie **wedge geometry** (four pre-authored 90° sectors) and
the jump-table `case 4` name/colour. With the guard relaxed, code 4 takes the
populate DEFAULT jump-table case → name token `error_department`, colour white, and
its sector node resolves by name-hash under `rnd_wheel_icon` to an EMPTY handle
(benign, node-guarded). So the 5th slice is **populated but no wedge draws** until a
5th authored sector node + a `case 4` are added. Extending the 4-entry rel32 jump
table @0x141f48231 needs a code cave within ±2GB and would be a blind crash risk,
so it is not done. Default OFF because it has no visible payoff yet and is the
riskiest piece; toggling at runtime logs that the (install-time) code patches need
a relaunch.

### New KB signatures (build d873a1c8…, sigcheck green)
- `career.rnd_pie_buf_alloc` → `site_Career_RnDPieBufAlloc` @0x141f46f6b
- `career.rnd_box_layout_probe` → `site_Career_RnDDeptBoxLayoutProbe` @0x141c86d86
- `career.load_upgrade_rows_deptpath` → `site_Career_LoadUpgradeRows_DeptPath` @0x143539c84
