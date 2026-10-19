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
roots).

**Container RESOLVED (v0.4.1, live diagnostic).** `team_link` is the team's
**engine-supplier constructor** token, not the constructor and not a generic
`user_defined` hub: a McLaren (Mercedes PU) save opens
`upgrades.2026.trees.mercedes.fake` = `{deae0310dc31dce2}` (`_upgrade_instancecount_`
`{b2932bd32df83903}`, `upgrade_0._metadata_` `{cabec8fb14f94a7a}` — all three match
the diagnostic). The GVDB holds **46 real `.fake` hub containers**, each with the
`central_fake_upgrade` hub (`_upgrade_instancecount_ = 1`, identical 15-key
schema). 12 are `upgrades.2026.trees.<team>.fake` (`ferrari`, `force_india`,
`gm_cadillac`, `haas`, `mclaren`, `mercedes`, `redbull`, `renault`, `sauber`,
`tororosso`, `user_defined`, `williams`); the other 34 are additional
verified-identical fake hubs carried by hash. No 2025-season team trees exist, and
`engine_suppliers.trees.engine_supplier_upgrades_N.fake` (e.g. `{3c55d614fc76ce3e}`)
is empty (no hub) so it is not written. The four `user_defined.fake`-family hubs
the v0.4.0 note mislabelled (`{ae8319a01aa30478}` `{5927ed801bdb6765}`
`{75a045fd77ae52b9}` `{f71aa592926ce9d9}`) are four of these 46 and now carry the
corrected rows.

**Shipped data:** the 3 Tyres nodes are written as `upgrade_1/2/3` into all 46
containers (preserving `upgrade_0`), each `_upgrade_instancecount_` bumped 1→4,
every field a field-for-field clone of the stock `fake.upgrade_0` schema (15 keys,
correct type tags) with real `upgrade_type` (Tyre Wear / Brakes / Upgrade All
Durability) and `upgrade_department=Fake`. The DB-op `{hash}` selector continues FNV
through `.upgrade_N.field` (`fnv1a64_from(container_hash, ".upgrade_N.field")`;
verified against the GVDB keys and applied end-to-end through the engine — 0 stock
collisions, round-trip stable).

**Reachability RESOLVED (v0.4.1).** The whole R&D tree is one graph rooted at the
hub `default` (grid `0,0`), which links OUTWARD (child/unlock edges) to each
department's branch root by the target's `_metadata_` id. A node is reachable only
if a directed path from `default` reaches it, so the Tyres branch attaches the stock
way: `fake.upgrade_0.links` gains our root (`,tyres_thermal_1`, preserving the stock
branch targets) and `tyres_thermal_1` → `tyres_thermal_2` → `tyres_thermal_3` chain
outward. (The v0.4.0 rows linked child→hub and never attached the hub to the branch
→ unreachable.)

**Shipped native (scan `career.load_upgrade_rows_deptpath` @0x143539c84, default on,
read-only):** a hook (r8=team_link cstr, r9=deptname cstr, rdi=code) that logs each
code's resolved path + FNV container hash; the code-4 line should now equal
`{deae0310dc31dce2}` on the McLaren save (one of the 46 real hubs), confirming the
placement (and revealing the exact `{hash}` if a future build/save differs).
Remaining in-game-test items: the display-filter (ENUM A == card enum), confirm the
branch renders and is buyable, and the economy/rating integration.

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

## v0.5.0 — the 5th department SELECTION (flag `select_fifth_dept`, default on)

> **Superseded in v0.6.0 (dead end).** `DAT_145f93c08` is the R&D > Performance overview
> box-grid selection; a hardware watchpoint proved it never changes while the department
> tab pages are built, so it does not choose the tile page. The second clamp (TeamHQ VM
> command 0x55702d31, table slot 4 = `open_driver_ratings_rtt_panel`) is unrelated to R&D.
> v0.6.0 removes the TeamHQ relax, defaults `select_fifth_dept` off, and keeps only the
> box-grid name guard (always installed, a no-op for idx 0..3). The real blocker is in the
> v0.6.0 section below.

Makes the code-4 (`Fake`/Tyres) department SELECTABLE so it can be the active R&D
page. The tile data for code 4 is already loaded (both `Career_Load_UpgradeRows`
@0x143539bc0 and `Career_Load_UpgradeTreeNodes` @0x14353b030 loop codes 0..4, and
the v0.4.2 `diag_tree_rowcount` probe reports the code-4 row count). The blocker is
purely DISPLAY/SELECTION: the active-department index is the global
`DAT_145f93c08` (u8), and every non-pie reader clamps it `< 4`
(`cmp al,4 ; cmovae eax,0`), so the active dept can never be 4 and the Fake(4)
`@rows`/`@tiles` are never spawned.

### The crash trap (RESOLVED, verified against the exe)
`k_Career_RnDDeptNameTable` @0x145755e58 has only FOUR entries
(`{aero,chassis,maintenance,engine}`) and `k_Career_RnDDeptColourTable`
@0x145755e78 immediately follows it (+0x20). So reading `nameTable[4]` returns the
qword at 0x145755e78 = `colour[0]|colour[1]` = `0xff3ec58dffeead00`, which the box
grid then dereferences as a `char*` → crash. This is THE crash that a naive clamp
relax would hit on the first name read of dept 4.

### The two active-dept read/clamp sites (RIP-xref of 0x145f93c08; the four
`0x141f4xxxx` pie reads are a separate authored-art follow-up and are LEFT ALONE)
- **Box grid** `Career_Build_RnDDeptBoxGrid` @0x141c85f70, the name read (decomp
  141c85f70.c:488-493). Disassembly @0x141c86c7e:
  ```
  141c86c7e  0f b6 05 83 cf 30 04   movzx eax,[DAT_145f93c08]
  141c86c85  31 c9                  xor ecx,ecx
  141c86c87  3c 04                  cmp al,4            <- clamp immediate @0x141c86c88
  141c86c89  0f 43 c1               cmovae eax,ecx      (idx>=4 -> 0)
  141c86c8c  48 0f be c0            movsx rax,al
  141c86c90  48 8d 0d c1 f1 ac 03   lea rcx,[k_Career_RnDDeptNameTable]
  141c86c97  48 8b 14 c1            mov rdx,[rcx+rax*8]  <- the crash-trap read
  141c86c9b  48 c7 44 24 50 00..00  mov [rsp+0x50],0     <- MidHook site (rdx=name, rax=idx)
  ...
  141c86caf  e8 ..                  call Ne_Make_StrFromCStr(&out, rdx, 0, 0)
  ```
  Scan (`.xtls`, unique, count=1, hit=0x141c86c7e):
  `0F B6 05 83 CF 30 04 31 C9 3C 04 0F 43 C1 48 0F BE C0 48 8D 0D C1 F1 AC 03 48 8B 14 C1`
  → clamp immediate = hit+10 (0x141c86c88), MidHook site = hit+29 (0x141c86c9b).
- **Tile page** — a TeamHQ MyTeam career VM command dispatcher, func @0x142337ff0,
  the `cmp eax,0x55702d31`/`je` case @0x1423380e9 (not in the decompiled corpus).
  IDENTICAL clamp, but indexing a DIFFERENT 4-entry dept code-name table
  @0x145784d80 (`{aero,chassis,maintenance,engine}`) that is followed by unrelated
  panel-event strings, so `table[4]` = `"open_driver_ratings_rtt_panel"` — a VALID
  C-string. Relaxing this clamp therefore does NOT crash; it only lets the
  active-dept content resolver run for index 4. Disassembly @0x1423380e9:
  ```
  1423380e9  0f b6 05 18 bb c5 03   movzx eax,[DAT_145f93c08]
  1423380f0  31 c9                  xor ecx,ecx
  1423380f2  3c 04                  cmp al,4            <- clamp immediate @0x1423380f3
  1423380f4  0f 43 c1               cmovae eax,ecx
  1423380f7  48 0f be c0            movsx rax,al
  1423380fb  48 8d 0d 7e cc 44 03   lea rcx,[0x145784d80]
  142338102  48 8b 14 c1            mov rdx,[rcx+rax*8]
  ```
  Scan (`.xtls`, unique, count=1, hit=0x1423380e9):
  `0F B6 05 18 BB C5 03 31 C9 3C 04 0F 43 C1 48 0F BE C0 48 8D 0D 7E CC 44 03 48 8B 14 C1`
  → clamp immediate = hit+10 (0x1423380f3).

### What ships (own gate, flag `select_fifth_dept`, default on)
1. **Name-guard FIRST.** safetyhook MidHook at the box-grid name site (hit+29): for
   `rax == 4` it sets `rdx` to a static `"Tyres & Thermal Management"` C-string, so
   `nameTable[4]` = colour[0] is never dereferenced. Installed BEFORE any clamp is
   relaxed, and it protects index 4 for the life of the process (not runtime-gated
   off while a clamp is relaxed). No-op for idx 0..3.
2. **Box-grid clamp** `cmp al,4` → `cmp al,5` (one byte @ hit+10), byte-verified
   (must be 0x04) and VirtualProtect-guarded, applied ONLY after the name hook is
   live.
3. **Tile-page clamp** `cmp al,4` → `cmp al,5` (one byte @ hit+10), byte-verified +
   guarded. No name hook needed (table[4] is a valid string).

Both clamp relaxations are inert until the active dept is actually 4, so they
cannot misbehave unless the 5th department is genuinely selected. Independent of
Hooks A/B, the colour write, the box inject and the probes; on any scan miss /
mismatch / >1 match the feature installs nothing and the rest keep working. The
`select_fifth_dept` ON_CHANGE only logs (the clamp byte-patches are install-time).

### Confirmed-safe vs test-resolves
- **Safe (static-proven against the exe):** both scans are unique on this build; the
  name read returns a valid string for idx 4 (crash trap closed); the tile table[4]
  is a valid string; both clamp bytes are verified 0x04 before patching; every
  read/write is region- and value-guarded; relaxations are inert unless dept 4 is
  active. The box grid's name-hash dispatch (0xc239ed13/0xfb78dbfb/0x7c93fd0c) has no
  case for a 5th card, so with the Tyres name it falls to the default branch — no
  crash either way.
- **Test resolves:** whether, with dept 4 active, the non-decompiled `@rows`/`@tiles`
  spawner (`0x141808d90` populate / `Career_Bind_UpgradeCard` @0x1417f8090) actually
  renders the Fake(4) tiles, or the page is still empty (the content resolver may
  key off the wrong/short name), or the tile-page handler keys the content
  differently. The `diag_tree_rowcount` line + the `dept-name[4] overrun intercepted`
  line disambiguate the outcome.

### New sites (record into the KB later; NOT written to re/db here)
- box-grid active-dept name read + clamp: func @0x141c85f70, clamp imm @0x141c86c88,
  name MidHook @0x141c86c9b
- tile-page (TeamHQ VM cmd 0x55702d31) active-dept clamp: func @0x142337ff0, clamp
  imm @0x1423380f3
- crash trap: `k_Career_RnDDeptNameTable[4]` @0x145755e78 = `colour[0]` as char*

## v0.6.0 — why the Tyres page is empty: the dept-4 upgrade INSTANCES never get a real state

### The page path (live gdb trace, this build)
`Career_Build_RnDDeptPage` @0x141c7b880 (16 KB, from 0x141c7b3d0) builds one R&D department tab.
`rsi` = the 0x950 department card (`Career_Construct_RnDDeptCard`), dept = `byte [card+0x6d8]`
(Tyres card = 4), team = `dword [card+0x6bc]`, per-team model = `[card+0x688]`. At 0x141c7d8cf it
calls `Upgrades_QueryEnabled` @0x1435188d0`(U=[card+0x688], out=rsp+0xdf0, &team=rsp+0x8c, 0xffff)`
and keeps results whose `byte [u+0x72] == card dept`. Durability page: 95 results, by dept
`0:25 1:23 2:24 3:23 4:0`. Tyres page: the same 95, `4:0` → nothing to show. The page logic is
fine; our upgrades are not in the query result.

### The per-team model (`Upgrades`, 0x1b10 bytes, one per team)
Created by `Career_Add_TeamUpgrades` @0x14123ca40 (alloc 0x1b10): loads the record model
(@0x143539260 → `Career_Load_UpgradeRows`, tree nodes), then `Career_Instantiate_UpgradeTrees`
@0x14352cac0, then `Career_ResetAll_Upgrades` @0x14352e1c0.

| off | field |
|---|---|
| +0x08 | u8 mode (0 = single shared tree with owner cache, 1 = per team, 2 = per driver; this career: 0) |
| +0x0c | u32 team id |
| +0xe0 + k*0xc40 | tree k (k < 2): +0x08 allocator, +0x10/+0x18 instance vector (0xe0 stride), +0x30 inline pointer array (0x180 slots), +0xc30 count. So the array is `U+0x110+k*0xc40`, count `U+0xd10+k*0xc40` |
| +0x1960 | u64 number of trees |
| +0x1970 + k*0x58 | tree graph (nodes 0x40 stride: [0]=instance, +0x10/+0x18 neighbours, +0x30 grid x/y) |
| +0x1a60.. | robin-hood map owner → tree slot (mode 2) |
| +0x1a9c | u16[6] per-department counters (dept 0..5, so index 4 is in bounds) |
| +0x1aa8 | u32 owner cache (mode 0); 0x3fff0000 = none |

`Career_Instantiate_UpgradeTrees` copies EVERY 0x80-byte UpgradeRecord (no department filter)
into a 0xe0 instance, calls the instance init on the stack temp, moves it into the vector and
pushes the pointer (`param_1[(k+1)*0x188 + cnt - 0x166]`, count `param_1[(k+1)*0x188 + 0x1a]`).
That is the "no direct `[..+0xd10]` write" the static scan missed: the stores are indexed off
`param_1` with the slot folded into the index.

### The upgrade instance (0xe0) and its state history
| off | field |
|---|---|
| +0x08..+0x57 | SSO `upgrade_name` (+0x10 = chars) |
| +0x58 | u32 node hash = djb2(seed 0x1505) of `"%s_%d" % (_metadata_ label, row flag)` |
| +0x70/+0x71/+0x72 | type / size / **department (ENUM A)** |
| +0x74 | `unlock_at_facility_level` |
| +0x7c | row flag: 0xff = team tree row, else engine-supplier stat id |
| +0xb8 | allocator; +0xc0/+0xc8/+0xd0 = state entries begin/end/cap (0x10 each) |

State entry: +0 u32 owner (0x3fff0000 = none), +4 u16 day, +6 u16, +8 u8 state, +9 fitted,
+0xa/+0xb, **+0xc enabled**, +0xd facility-locked, +0xe. State names @0x14565cee0:
0 Unavailable, 1 Available, 2 InProgress, 3 Completed, 4 Failed, 5 Invalid, 6 PendingDevelopment,
7 InDevelopment, 8 PendingFitting. `Upgrades_QueryEnabled` keeps an instance only if the last
entry with `day <= mask` has **enabled != 0**.

### The exact skip mechanism (three layers, all on `byte [u+0x72]`)
1. **Instance init** `Career_Reset_UpgradeInstance` @0x143517d80 (KB name
   `Car_Reset_LastFittedUpgrade`; it is the per-instance reset called by instantiation, reset-all
   and the supplier refit): at **0x143517e0c `cmp byte [rcx+0x72],4 ; jne 0x143517e4c`**. A Fake (4)
   instance gets ONE sentinel entry `{owner 0x3fff0000, day 0xffff, state Invalid, enabled 0}` and
   returns, skipping the normal path (`PushState(owner, Unavailable, day 0)`, `SetFitted`,
   `SetEnabled(1)`, tail-call `SetFacilityLocked(unlock_level != 0)`). Enabled stays 0 → the query
   drops it. This is how the stock hub stays off every page; our three rows are
   `upgrade_department = Fake` too, so they get the same sentinel.
2. **Every state mutator bails on dept > 3** (`cmp byte [rcx+0x72],3 ; ja ret`):
   `Upgrade_PushState` @0x1435230e0 (guard 0x1435230f3, "Changing state from %s to %s"),
   `Upgrade_SetEnabled` @0x143523510 (0x143523520), `Upgrade_SetFacilityLocked` @0x143523730
   (0x143523740), `Upgrade_SetFitted` @0x1435157d0 (0x1435157e0), and the auto-fit pass
   @0x1412484e0 (0x1412486b8). So a dept-4 instance can never leave the sentinel.
3. **The availability graph search** `Upgrade_GraphSearch` @0x1435288f0 treats every dept-4 node
   as the tree hub (`cmp byte [rax+0x72],4 ; je` @0x143528a05): used by
   `Career_Refresh_UpgradeAvailability` @0x14352f300 (modes 1/2) and `Upgrade_EvalAvailability`
   @0x14352e720 (mode 0 owner switch `Upgrades_SetOwner` @0x14352e620, which writes the state byte
   of the last entry directly). Left alone, our nodes would act as hubs and unlock out of order.

No `>= 4` loop, 4-entry tree list or `.fake` container skip exists on the instantiation path; the
instances are all there (live check below). A data-only fix is impossible: the Tyres card filters
on `+0x72 == 4`, and the same byte must be <= 3 for layers 1-3.

Save games: `Upgrades_Deserialise` @0x143536000 → per-tree `@0x143524c20` finds each instance by
`+0x58` and REPLACES its state history (no dept guard). `@0x143523fd0` serialises every instance,
so saves written with v0.4/v0.5 carry our sentinel and would overwrite a fixed init on load.

### Live confirmation (read-only `/proc/<pid>/mem` scan, game not stopped, 2026-10-19)
21 `Upgrades` objects, all mode 0. Every team's tree contains the hub and our three:
`central_fake_upgrade` hash 0xbd057d45 (= djb2("default_255"), label `default`),
`tyres_wear_reduction` 0x27e41c13, `tyres_brake_cooling` 0x27f63494, `tyres_thermal_reliability`
0x28084d15 (= djb2("tyres_thermal_N_255")), row flag 0xff, ONE state entry each, enabled 0
(fresh trees: state 5 Invalid; trees restored from the save: state 0 from the mode-0 direct write).
155-instance trees: by dept `[25,23,52,51,4]`, enabled `[25,23,24,23,0]` = the 95 the page query
returned.

### The fix (flag `instantiate_tyres_upgrades`, default on, install-time)
Gate: one exactly-one `.xtls` scan of the init site
`C7 81 D8 00 00 00 00 00 00 00 48 8B 81 C0 00 00 00 48 89 81 C8 00 00 00 80 79 72 04 75 3A`
(hit must be 0x143517df4), then every other site byte-verified at its fixed RVA. Core sites are
all-or-nothing (rolled back on any create failure). Each hook is a safetyhook MidHook ON the
conditional jump, adjusting ZF in the saved rflags, and acts only on OUR instances: dept 4, row
flag 0xff, and `+0x58` in {0x27e41c13, 0x27f63494, 0x28084d15} or `upgrade_name` in our three.
Identification is by content, never address (the init runs on a stack temp).

| site | hook @ | effect for ours |
|---|---|---|
| init dept==4 branch | 0x143517e10 (`jne`) | clear ZF → normal init: Unavailable, enabled 1, locked 0 (`unlock_at_facility_level` = 0) |
| PushState guard | 0x1435230f7 (`ja`) | set ZF → state changes apply |
| SetEnabled guard | 0x143523524 | set ZF |
| SetFacilityLocked guard | 0x143523744 | set ZF |
| SetFitted guard | 0x1435157e4 | set ZF |
| auto-fit pass guard | 0x1412486bc | set ZF |
| graph-search hub test | 0x143528a09 (`je`) | clear ZF → regular node, not the hub |

The hub (`default`/`central_fake_upgrade`) never matches, so it keeps the stock sentinel, stays
disabled, stays out of every query, and still terminates the graph search as the root.

Sanitizer (heals saves that persisted the sentinel): for OUR instances whose states are empty or
whose last entry is still disabled, call the game's own init (now on the normal path), then mirror
`Career_ResetAll_Upgrades`: mode 0 → owner cache `+0x1aa8 = 0x3fff0000` so the next query runs
`Upgrades_SetOwner` and recomputes availability; modes 1/2 → `Career_Refresh_UpgradeAvailability
(U, 0)` (only at load). Runs at: load (`Upgrades_Deserialise` after the trees, 0x14353610f,
`rdi` = U), refresh entry (0x14352f300, `rcx` = U), and the R&D page just before the query
(0x141c7d8ba, `rcx` = U). Each optional site is byte-verified on its own.

Checked consumers of `+0x72` for index-by-department overruns once dept-4 upgrades become live:
department name/colour lookups in the upgrade panels are bounded (`cmp 3/5 ; ja default`), the
`+0x1a9c` counters are 6 wide, and cost/facility code compares the department against lists
(no match for 4 → no facility lock, stock row costs). Known cosmetic gap: the upgrade detail panel
shows the bounded fallback department label (`error_department`) for Tyres upgrades.

### Diagnostic (flag `diag_tyres_instances`, default on, read-only)
Own scan `48 8D 94 24 F0 0D 00 00 4C 8D 84 24 8C 00 00 00 66 41 B9 FF FF E8 FC AF 89 01 0F B6 8E D8 06 00 00`
(hit 0x141c7d8ba); MidHook at hit+0x1a = 0x141c7d8d4 (after the query; `rsi` = card, count at
`rsp+0x19f0`). Logs per page build (Tyres page up to 24 times, other pages once):
`instances: R&D page dept=N team=… query=… by dept 0:… 1:… 2:… 3:… 4:…`, the Tyres-listed names,
and every dept-4 instance's last state.

### New sites (record into the KB later; NOT written to re/db here)
- `Career_Reset_UpgradeInstance` 0x143517d80 (dept==4 branch 0x143517e0c)
- `Career_Instantiate_UpgradeTrees` 0x14352cac0, `Career_Add_TeamUpgrades` 0x14123ca40,
  `Career_ResetAll_Upgrades` 0x14352e1c0
- `Upgrade_PushState` 0x1435230e0, `Upgrade_SetEnabled` 0x143523510,
  `Upgrade_SetFacilityLocked` 0x143523730, `Upgrade_SetFitted` 0x1435157d0, auto-fit 0x1412484e0
- `Upgrade_GraphSearch` 0x1435288f0, `Career_Refresh_UpgradeAvailability` 0x14352f300,
  `Upgrades_SetOwner` 0x14352e620, `Upgrade_EvalAvailability` 0x14352e720,
  `Upgrade_EvalFacilityLock` 0x14352ea40
- `Upgrades_QueryEnabled` 0x1435188d0, `Career_Build_RnDDeptPage` 0x141c7b880 (query 0x141c7d8cf)
- `Upgrades_Deserialise` 0x143536000 (+ per-tree 0x143524c20), tree serialise 0x143523fd0

## v0.6.1 — the DEVELOP UPGRADE screen for the Tyres upgrades

### The screen
The detail screen is the departments container's purchase tab, `LaptopRnDUpgradePurchaseViewModel`
(card name `tab_rnd_upgrade_purchase_panel`, `begin[4]` of the box-grid card vector, vtable 0x145740ac0):
build `0x141c8a080` (vt+0x30; finds the instance by `upgrade_hash_name` via `Upgrades_QueryEnabled` and
copies `byte [u+0x72]` to panel+0x6e8), update `0x141c8d540` (cost, slots, timeline, comparison, card),
messages `0x141c8bcc0` (vt+0x50), option rows `0x141c8cbb0` / `0x141c8f0a0`, unlocks `0x141c8c430`, upgrade
card binder `0x141814320` (also `0x14180fc60`, `0x1418169f0`). Panel fields: +0x620 container, +0x680
career, +0x6a0 Upgrades, +0x6a8 instance, +0x6cc team, +0x6d0 cannot-develop, +0x6d1 insufficient points,
+0x6d2 research limit reached, +0x6e0 view, +0x6e8 department.

### Root cause of the Back trap
`back_pressed` (djb2 0x493fe92b) in `0x141c8bcc0` @0x141c8c150:
`name = dept < 4 ? k_Career_RnDDeptNameTable[dept] : "error_department"`, then container
`vt+0x80("open", name)`. The container's dispatch (`0x141c86fc0`, vt+0x50; open case @0x141c87032) maps the
name's djb2 to a card: aero 0x7c93fd0c, chassis 0xc239ed13, engine 0xfb78dbfb, maintenance 0x5c31ec68,
0x396ca87 (purchase), 0x10230e0a (perks); anything else takes `jne 0x141c875f5` and returns. The event is
consumed and no tab changes, so the player is stuck. Same dead end: the perks tab's back (`0x141c89730`,
reads `return_department_name`, which the department page sets with the same fallback @0x141c7fdd1) and the
department page's cross-department jump to a linked upgrade (@0x141c8041e).

### Second root cause: the 5th department row clobbered Aero (fixed in data)
`Career_Load_DepartmentEntries` parses each row's `department` with ENUM A (switch @0x14123412a: Chassis 1,
Durability 2, Engine 3, Fake 4, Invalid 5, anything else 0) and inserts per-department attribute pointers
into 19 maps keyed by that byte (`component+0x738 + N*0x48`, insert `0x1412ba3d0`; status 1 = key exists,
the value is still overwritten). Our row said `department = Tyres` → 0, so since v0.2.0 it overwrote AERO's
attributes (cost/time scales, slots, rush, dual development) with Durability's. The per-team modifier lists
(`{kind, dept, ?, active, value}`, built by listeners registered in `0x141238520` from those maps, read by
`0x1434fb4b0`) therefore had no department-4 entries at all: research slots 0 (→ research-limit flag), rush
locked, no cost/time scales. `db/departments.json` now says `department = Fake` (→ 4): Aero gets its own
attributes back and department 4 becomes a real economy department sharing Durability's attributes, so
every modifier consumer below works for Tyres with no code. Residual risk: code that iterates these maps and
indexes a 4-wide array by the key (none found on the R&D paths; watch the stock departments' slots/costs).

### Sweep: code that branches on the department byte (508 `byte [x+0x72]` accesses scanned)
Classified with `capstone` over a full `objdump` of `.xtls`; plus the panel/card copies (+0x6e8, +0x6d8).

| site | function | dept 4 behaviour | screen | status |
|---|---|---|---|---|
| 0x143517e0c | instance init 0x143517d80 | `==4` → disabled sentinel | page | v0.6.0 hook |
| 0x1435230f3 0x143523520 0x143523740 0x1435157e0 0x1412486b8 | push state / enabled / facility lock / fitted / auto-fit | `>3` → return | purchase | v0.6.0 hooks |
| 0x143528a05 | availability BFS 0x1435288f0 | `==4` → treated as hub | unlock chain | v0.6.0 hook |
| 0x141c8c150 | purchase messages 0x141c8bcc0 (back_pressed) | `>3` → "error_department" → open no-op | **yes** | **fixed: Back route** |
| 0x141c7fdd1 | dept page 0x141c7fc50 (perks) | return name "error_department" | Tyres page | fixed: Back route |
| 0x141c8041e | dept page 0x141c7fc50 (linked upgrade) | open "error_department" | Tyres page | fixed: Back route |
| 0x141c8703d..0x141c873c7 | container open dispatch 0x141c86fc0 | unknown name → ret | **yes** | **hooked @0x141c873c2** |
| 0x14181459c 0x14180fe7a 0x141816f48 | card binders | `lng_rnd_dept_error_department` (missing key) | **yes** (dept tag) | lang key added |
| 0x141815611 0x14181041d 0x1418185b5 | card binders | `rnd_dept_icons_small.error_department` (no frame) | **yes** | icon → `chassis` |
| 0x141814b95 0x14180ff10 0x1418174c6 | card binders | colour 0x14571bce0[4] = white | **yes** | [4] → Tyres orange |
| 0x141c8e1ed | panel update | colour 0x145755e78[4] | **yes** | Tyres orange since v0.2.0 |
| 0x141c8e28c | panel update | kind 0 (slots) for dept → 0 → +0x6d2 research limit | **yes** (blocks buy) | fixed: Fake row |
| 0x141c8de49 | panel update → dept summary 0x1416d53f0 | kinds 3/2/1/0xd → zeros | **yes** | fixed: Fake row |
| 0x143536f04 | rush available 0x143536e90 | kind 5 for dept → none → Rush locked | **yes** (Rush `-`) | fixed: Fake row |
| 0x14352f975 | failure chance 0x14352f8d0 | kind 0xe → base | **yes** | fixed: Fake row |
| 0x1412434ea | start research/develop 0x1412431a0 | kind 0x10 → none | purchase | fixed: Fake row |
| 0x143516b50 0x14351c4ff 0x14351c5d1 0x14351f1b8 0x14351f2a0 0x143521e90 0x143521f70 0x1435220f1 0x14352e95b 0x14352ea6b 0x14352fad3 0x1435301f3 0x1435302dc 0x143534796 0x1435348b6 0x1435349c8 0x143534a97 0x141c7dca8 | R&D simulation / availability / facility lock / dept page slots | modifier kinds 0x0-0x19 for dept → none | progression | fixed: Fake row |
| 0x141c8ebfc → 0x141890e60 | vehicle performance comparison | team base rating list (+0x1a38) has no dept 4 → empty bars | **yes** | alias hook @0x141c8ec4c |
| 0x141893f59 | per-team performance data 0x1418914f0 | `lng_rnd_dept_%s` / tables | pie | pie agent (`0x1418914f0`) |
| 0x1416b6d51 | timeline 0x1416b6cd0 | `rnd_goto_error_department_department` VM string | yes | harmless |
| 0x141c737ff 0x141c837a2 0x141c7db76 0x141c495e9 0x141c55188 0x141c70d8a 0x141c74da9 0x141c77eb2 | other R&D views | name `error_department` | no | lang key covers `lng_rnd_dept_%s` |
| 0x1418b0f95 0x1419bf58f 0x141c78850 0x141c84170 0x141f3dab5 | other views | colour tables `<6` → white | no | bounded, cosmetic |
| 0x14071e711 0x14071e99d 0x140777f12 | telemetry | ENUM A name table [4] = "Fake" | no | valid string |
| 0x140f5e300 0x140f5e820 | per-dept counters | `==0..5` chain | no | handles 4 |
| 0x14372f44d 0x143730424 | 0x14372ef80 | `>3` → skip | no | bounded |
| ENUM A switch (~27 inlined copies, e.g. 0x14353a700) | parse | "Fake" → 4 | load | data |
| card binders `rnd_part_image/_mask.<upgrade_name>` | 6 Ne_Format_String sites | no frame for our names → blank card/unlock art | **yes** | borrowed parts |
| 0x143525b60 | upgrade cost | formula (below) | **yes** | test price hook |

### What v0.6.1 installs (gate: one `.xtls` scan of the dispatch, hit must be 0x141c873ac; flag `tyres_upgrade_screen`, default on)
Every other site is byte-verified at its fixed RVA and installs on its own.
- **Back route** — MidHook @0x141c873c2 (`cmp eax,0x5c31ec68`, the last name compare of the open case). For
  name hash `tyres` (0x107666dc) or `error_department` (0x7e5c1342, only produced for department >= 4): find
  the department card (vtable 0x145740250, `+0x6d8 == 4`) that owns a container slot
  (`+0x548..+0x550`, `slotObj+0x20 == card+0x268`, `slotObj+8 != 0`), call `0x141c87620(&container, 0)` (the
  stock department-mode refresh: dept cards on, panels off, `show_shoulders`), enable the Tyres card with
  `0x14431f470(container+0x2d8, card+0x268, 1)`, set `rax = &cards[i]` and resume at the stock tail
  0x141c875a5 (`mov [rsi+0x7b8],card` → slot lookup → `jmp 0x14431f6b0` select). If no such card/slot
  exists, rewrite the hash to `maintenance` so the stock code opens Durability. Stock names never match.
- **Comparison alias** — MidHook @0x141c8ec4c (before `call 0x1418130b0`): the comparison's department byte
  at `[rsp+0x108]` is 4 only for our upgrades; Tyre Wear/Brakes → Chassis (1), Upgrade All Durability →
  Durability (2), which are the stock departments of those effect types.
- **Card art** — MidHooks on the `lea r9` before 9 `Ne_Format_String` calls (`[rsp+0x20]` = frame name):
  `rnd_part_image`/`rnd_part_mask` @0x141810122/0x1418103a2/0x1418154d0/0x14181558f/0x14181845c/0x14181852e
  (our names → `[om_xm_]tyre_blankets`, `calipers`, `turbo_charger_wear_1` + the state suffix; `om_xm_` when the
  pkg path contains it), and `rnd_dept_icons_small` @0x141810482/0x141815670/0x14181861b
  (`error_department` → `chassis`).
- **Card colour** — `0x14571bce0[4]` white → Tyres orange 0xff0078ff (table verified stock first).
- **Diagnostics** — back_pressed @0x141c8c172, option rows @0x141c8cbb0, purchase entry @0x1412414c0,
  state pushes in the v0.6.0 push guard.

### Test price (flag `tyres_test_price`, default on) — TEMPORARY
Cost = `0x143525b60(list, instance, team modifiers, option)`: base `cost` (+0x5c) or `cost_to_develop`
(+0x60, option & 6), team rows only; × modifier kind 3 (research) / 0x13 (development) cost scale; + kind 10 ×
upgrades owned; + `(base × instance+0xd8) / 100` (event-driven percent; −60 on the test save, which is why the
1000-point row showed 400). It is called by the affordability list `0x1435329b0`, the research and
development cost wrappers `0x14351dde0` / `0x14351df60` (`0x14351df60` is mislabelled
`Car_Read_VehicleAttribute` in the KB), the card and the panel. A MidHook at its entry (scan + 60-byte
verify) returns 5 for our three by resuming at a verified `ret` (0x14351df56). Restore: turn
`tyres_test_price` off (database `cost`/`cost_to_develop` stay 1000). Research/development times were also
shortened to 5/5 days for testing (database rows; real values 20/25).

### Names
Per-upgrade name key = `lng_rnd_<upgrade_name>`, description = `lng_rnd_<upgrade_name>_description` (card
binder `0x141814320`, desc `0x14385ad00`). The detail title is `lng_rnd_<size>` + `lng_rnd_<type>`
("Minor - Tyre Wear") and does not use the name. The department tag over the title is the Tyres card's own
name key (`lng_rnd_dept_tyres`, the same key the Tyres tab shows), now "Tyres".

## v0.7.0 — the five-sector development-potential pie (folded in)

The pie art and native half were prepared separately (`f1-modding/work/rnd-fifth-pie`) and merged here.

**Assets** (`erp/rnd_fifth_pie.json`, 60 ops, sources under `ui/`): per archive (`flow_career_hub.erp`,
`flow_team_hq.erp`) 3 `replace-resource` (`rnd_overview.idf` with 3+2 tiles, `laptop_r_and_d.ui` with
tile 5 bound to `@tyres` and a `dev_potential.tyres` group, `rnd_dev_potential_full.idf` pointing
`chart_1` at the new pie) and 27 `add-resource` (`rnd_dev_potential5.idf` five 72° sectors,
`rnd_dev_potential_sect72.idf` + its 3 proxies/models/meshes and 12 buffers, the five-spoke backdrop
material/texture). The replacements change size, so both archives take the rewritten-archive route
(launcher PR #138; the v1.0.4 CLI drops them). The research overview (`team_hq/research`) keeps the
stock four-sector pie.

**Native** (`src/rnd_fifth_pie.cpp`, `rnd_fifth_pie::install` right after `install_fifth_pie`; every piece
needs `render_fifth_pie`, now default on; slice and focus also need its patches live):

| option | scan gate | sites | for department code 4 |
|---|---|---|---|
| `pie_tyres_slice` | 0x141f48253 | 0x141f482a0, 0x141f485ab, 0x141f485b5, 0x141f48a64 | populate name `tyres`, description key, colour 0xff0078ff, wheel icon `maintenance` |
| `pie_tyres_focus` | 0x141f4aa21 | 0x141f4aa28 (resume 0x141f4aa4f) | `department_focused "tyres"` highlights the sector; `DAT_145f93c08` untouched |
| `pie_tyres_performance` | 0x1418945a5 | 0x1418945b6, 0x1418946d4, 0x141894702, 0x141894e03; data 0x14571bce0[4] | publisher `0x1418914f0` emits `department_performance.tyres` (derived current/potential) |

All sites were checked for in-function branch targets inside the patched bytes (none). The colour slot
0x14571bce0[4] is shared with the v0.6.1 card colour write; both accept an already-orange slot.
Expected log lines: `pie5 slice/perf/focus ... INSTALLED`, then on R&D > Performance
`pie5 slice: code 4 ...`, `pie5 perf: dept 4 published as department_performance.tyres`, and on the
Tyres tile `pie5 focus: department_focused "tyres" -> ...`.
