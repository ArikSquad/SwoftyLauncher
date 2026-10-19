# hud: in-race HUD (view-model / logic side)

Owner: see `subsystems.tsv`. Rows: `kb.py find '\thud\t'`. The `render` lane (owner `overlay`) owns the D3D12 present path and immediate-mode draw; this lane is the game's own HUD **view-models**: what data the HUD widgets publish and when they open/close.

## Map
- HUD widgets are ordinary NeUI view-models. Each publishes under a `view_model_data.hud_*` registry node and writes values with the same `Vm_Set_*` / `Vm_NodeSet_*` / `Vm_Holder_SetCStr` calls the front end uses, resolving its node with `Vm_Resolve_RegistryNode` (vm lane).
- `Hud_Open_TyrePerformance` (0x14152b5f0, L2, `HudTyrePerformanceViewModel::setToOpen`, vtable slot 0x1456ed290): the worked example. It is perf-counter throttled (interval at +0x760, min count at +0x770, last tick +0x750), then publishes a bool via `Vm_NodeSet_Bool` and a label via `Vm_Holder_SetCStr`, resolving `#view_model_data.hud_spectator_2.*`. Related: `TyrePerformanceRuleManager::onSectorCrossed` (0x14152a9f0, game lane) drives it.

## HUD view-model vocabulary (class names in the exe, addresses not yet resolved)
These are referenced as data/component names (and a few pretty-functions); their functions live behind vtables near the tyre-performance VM (abs-ptr region ~0x1456exxxx). Enumeration is future work (scan the vtables, or the `view_model_data.hud_*` registry children at runtime):
`HudScreenViewModel`, `HudDynamicComponentBaseViewModel` (base), `HudTyrePerformanceViewModel`, `HudDynamicMFDViewModel`, `HudMinimapViewModel`, `HudTimingPositionViewModel`, `HudDynamicLapCounterViewModel`, `HudSessionTimerViewModel`, `HudDynamicTimingPillsViewModel`, `HudDynamicSplitBoxesViewModel`, `HudDriverStandingsPillsViewModel`, `HudDriverTagsViewModel`, `HudProximityArrowsViewModel`, `HudRearViewMirrorViewModel`, `HudSpeedTrapViewModel`, `HudPitWindowViewModel`, `HudPitStopMinigameViewModel`, `HudPPTargetsViewModel`, `HudDynamicRaceObjectivesViewModel`, `HudDynamicPracticeStrategyDataViewModel`, `HudDynamicFavouriteGoalsViewModel`, `HudGridBoxIndicatorViewModel`, `HudRaceStartAnalysisViewModel`, `HudWrongWayViewModel`, `HudEventTRViewModel`, `HudEvent{Status,Update}EliminationViewModel`, `HudSpectatorScreenViewModel`, `HudNotificationsDynamicViewModel`, `HudDynamicDialViewModel` (`HudDialUpdateMessage`), `HudCustomisationViewModel`/`HudCustomisationSystem`, `HudTutorialNotificationsSystem`.

## Hooking
- To change a HUD value: resolve its `view_model_data.hud_<widget>` node and `Vm_Set_*` it (same as the MOD SETTINGS label path).
- To force-show/throttle a dynamic widget: wrap its `setToOpen`-style vtable slot (as `Hud_Open_TyrePerformance`).

## Open
- Only `HudTyrePerformanceViewModel` is at a resolved address so far; the rest are L0 class names. Next: abs-ptr scan of the vtable block around 0x1456ed290 to name `HudDynamicComponentBaseViewModel` and the dynamic component VMs, and a runtime dump of `view_model_data.hud_*`.

## Inbox
