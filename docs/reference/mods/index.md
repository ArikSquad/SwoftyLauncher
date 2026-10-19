---
title: Mods
description: Make content for the Team Simplified suite — .serps packages, game files laid out like the game folder, and community track zips — and import it on the Mods page.
---

# Mods

A mod is content for the Team Simplified suite: the files players drop on the launcher's **Mods** page, which the Simplified for Swofty addon adds. A mod has no `swofty.json`. It carries game files in the game folder's layout, and the importer compares them with the game's own files and merges only what changed. To describe changes as operations you write yourself, make a [data addon](/reference/data/) instead.

## Mod types

| Type | What it holds | Made with | Players install it |
|---|---|---|---|
| `.serps` package | `mod.serps` and an `F1 25/` folder of game files: the database, language files, `.erp` archives, other files | Simplified Studio, or by hand | Drop it on the **Mods** page, or `swofty mods import` |
| Game files in a zip, 7z, RAR or folder | `.erp` archives and other game files at their game paths, with no `mod.serps` | Any tool | The same |
| Track zip | `manifest.json`, the track's `.erp`, menu pictures and mip files, in the EGO GameDatabases Editor format | Track tools for the EGO editor | The same |

A single `.erp` file isn't a mod on its own: put it at its game path, such as `F1 25/2025_asset_groups/ui_package/<name>.erp`, and zip the folder.

## Make one

1. Pick the type from the table above.
2. Build it: a `.serps` package or game files as in [`.serps` packages](./serps), a track as in [Track packages](./tracks).
3. Import it into a test profile, turn it on and build: `swofty mods import <file>`, `swofty mods enable <id>`, `swofty build`. Read every line of output ([Import and build](./serps#_3-import-and-build)).
4. Test it in game with only your mod turned on, then with the mods players combine it with.
5. Publish the file. Players drop it on the **Mods** page ([Mods and tracks](/using/mods)).

## What's in this section

| Page | Covers |
|---|---|
| [`.serps` packages](./serps) | The package layout, `mod.serps`, what the importer reads, compares and merges |
| [Track packages](./tracks) | The EGO track zip, its `manifest.json` and the track's name |
| [How tracks are installed](/reference/installing-tracks) | What activating a track changes in the archives, database, menus and ERS |

## How mods load

Every profile shares one library under `$DATA/addon-data/teamsimplified.mods/library/` and keeps its own order and switches. At each build, the addon's build hook turns every enabled mod into [operations](/reference/data/operations), in profile order: `db.put-record` for database records, `erp.put-resource` for archive resources, `lang.set` for text, `file.put` for other files, and `track.add` for each track. Where two mods change the same thing, the one lower in the list wins.

The `.serps` format and the importer come from Simplified Suite by Team Simplified, [github.com/TeamSimplified/simplified-suite](https://github.com/TeamSimplified/simplified-suite).
