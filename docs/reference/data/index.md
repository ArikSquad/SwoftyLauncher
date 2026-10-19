---
title: Data addons
description: Change game data with an addon that has only operations — the steps, which operation kind each change needs, and a first data addon to start from.
---

# Data addons

A data addon changes F1 25's content with **operations** only: no DLL and no launcher plugin. You list the changes in the `operations` block of its `swofty.json`, and the launcher applies them every time it builds a profile. Players install it on the **Addons** page. Content made for the Team Simplified suite, such as `.serps` packages and track zips, is a [mod](/reference/mods/) instead.

## Make one

1. Make a folder with a `swofty.json` at its root ([Your first data addon](#your-first-data-addon) below).
2. Decide what you change and find its operation kind in [the table below](#what-to-change).
3. Write the operation file and list it in `operations` ([Operations](./operations)).
4. Install the folder, enable it on a modded profile, and run `swofty build`; read every line of output ([Check what a build did](./operations#check-what-a-build-did)).
5. Test it in game with only your addon enabled ([Test in game](/reference/addons/publishing#test-in-game)).
6. Package it as a `.swoftyaddon` and publish it ([Package it](/reference/addons/publishing#package-it)).

## What to change

| Change | Operation kind | Page |
|---|---|---|
| Teams, cars, drivers, rules, unlocks | `database`: `set`, `add-row` | [Game database](./database) |
| Names and descriptions | `assets`: `lang.set`, `lang.table` | [In-game text](./text) |
| Textures, models, materials, worlds | `erp`: `replace-resource`, `copy-resource`, `patch-pkg`, ... | [Textures and models](./textures) |
| A special livery for one car | `database` and `erp` | [Adding a livery](./livery) |
| A track that ships with its own name | `assets`: `track.add`, `lang.set`, `db.set` | [A track with its own name](./track-names) |
| A menu layout | `erp`: `replace-resource` on a scene | [Editing menus](/reference/ui/editing-menus) |
| A new in-game screen | `screens` | [In-game UI](/reference/addons/in-game-ui) |
| A loose game file | `files` | [File operations](/reference/ops/files) |

[Operations](./operations) lists every kind, how each reaches the game and in what order they apply.

## What's in this section

| Page | Covers |
|---|---|
| [Operations](./operations) | What an operation is, every `operations` field, the prefixed kinds in `assets` files, build and runtime routes, order and conflicts |
| [Game database](./database) | Find a key, change a value, add a row, rename a track |
| [In-game text](./text) | Stock tags and your own strings |
| [Textures and models](./textures) | Inspect archives, replace and copy resources, mip files, `!pkg` edits |
| [Adding a livery](./livery) | A complete data addon: a special livery in the picker |
| [A track with its own name](./track-names) | Ship a community track in an addon and name it |

## Your first data addon

This addon makes a stop-go penalty last 10 seconds with one database operation:

```
my-addon/
├── swofty.json
└── db/
    └── changes.json
```

```json [swofty.json]
{
  "schema_version": 1,
  "id": "yourname.stop-go-10",
  "name": "10-second stop-go",
  "version": "1.0.0",
  "author": "Your Name",
  "description": "Stop-go penalties last 10 seconds.",
  "kind": ["database"],
  "compat": { "game": "f1_25", "game_builds": [">=25251167"] },
  "operations": {
    "database": ["db/changes.json"]
  }
}
```

```json [db/changes.json]
{
  "schema_version": 1,
  "ops": [
    { "op": "set", "select": "rules.stop_go_penalty", "field": "penalty_duration", "value": "10" }
  ]
}
```

Leave out `native` and `launcher`: those carry code. `kind` lists the categories the **Addons** page shows (`database`, `erp`, `ui`, `files`). See the [addon manifest](/reference/addons/manifest) for every field.

Install the folder, enable it on a modded profile, and build:

```sh
swofty addons install ./my-addon
swofty addons enable yourname.stop-go-10
swofty build
```

The `vanilla` profile takes no addons, so build a modded one. `swofty build` writes the result to `$DATA/cache/overlay/<profile>/<buildhash>/` and leaves the game folder alone. To play it, press **Play** in the launcher.
