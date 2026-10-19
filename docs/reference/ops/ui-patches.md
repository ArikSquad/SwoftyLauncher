---
title: UI patches
description: The planned UI patch format for operations.ui — targets, node paths and screen edits. The build skips these files today.
---

# UI patches

UI patches describe changes to menus, labels and routes, one kind of [operation](/reference/data/operations). Save them as `*.pkgpatch.json` and list them in `operations.ui` in your [manifest](/reference/addons/manifest#operations).

::: warning Not built yet
The build skips `operations.ui` and prints `<addon>: <n> UI operation file(s) skipped; that stage is not built yet`. This is the format for the planned build step. To change a menu today, ship an edited scene with `replace-resource` ([Editing menus](/reference/ui/editing-menus)), or edit a `!pkg` resource with [`patch-pkg`](/reference/ops/erp#editing-pkg-resources).
:::

## Example

```json
{
  "schema_version": 1,
  "targets": [
    { "erp": "2025_asset_groups/ui_package/flow_frontend.erp",
      "resource": "eaid://ui_package/screens/fe_home.ui", "fragment": 0 },
    { "erp": "2025_asset_groups/ui_package/screens/fe_home.erp",
      "resource": "eaid://ui_package/screens/fe_home.ui", "fragment": 0 }
  ],
  "root": "Screens[0]:fe_home",
  "edits": [
    { "op": "set-text", "node": "contextual_panels.offline_contextual_panel.subtitle",
      "value": "Launched with Swofty Launcher, press", "localise": false }
  ]
}
```

`targets` lists the resources to change. The home screen has two copies, so both are listed. Node paths are relative to `root`. Edits run in order.

## Screen edits

| Edit | Purpose |
|---|---|
| `clone-node` | Copy `src` to a sibling named `as`; optionally change properties with `set` |
| `set-text` | Set a label's `node`, `value` and `localise` flag |
| `set-props` | Change properties on `node` using `set` |
| `add-signal` | Add a `signal` to a node's `event` |
| `remove-signal` | Remove signals from a node's `event` to a target `to` |

Signals name a target node (`to`) and pin (`pin`). Optional fields are `from` and `attrs`.

## Routes and UI databases

For `.uir` and `.uidb` files, use `clone-entry` or `set-value`. Paths use dots for keys and `[field=value]` to select an array entry.

## Text and prompts

Use `set-text` with `"localise": false` for literal text. To change a string in every menu that shows it, use `lang.set` ([In-game text](/reference/data/text)).

Keyboard and controller icons come from entries in `tool_tips.uidb`. See [`!pkg` resources](/reference/pkg) for the file format.
