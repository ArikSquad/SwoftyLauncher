---
title: Addons
description: Install addons, find them with search and filters, turn them on per profile, set their load order, check their conflicts and remove them on the Addons page.
---

# Addons

Everything the launcher loads is an addon: a folder with a `swofty.json`. A data addon changes the game's database, text, archives and files; a code addon adds an engine plugin, a launcher page or both. Mods you download for the game folder become addons through the [Mod Importer](/using/mods), and changes you make in the [DB Editor](/using/db-editor) save as addons.

## Install an addon

The two cards at the top of the **Addons** page are the ways in:

- **Installing a Swofty Addon?** Drop a `.swoftyaddon`, a zip of an addon or an addon folder on the card, or click it to choose a file. **Choose a folder…** picks an addon folder. You can also drop an addon anywhere on the launcher window.
- **Installing a mod?** **Go to Mod Importer** opens the [Mod Importer](/using/mods), which turns the mod into an addon.

The launcher copies the addon to `$DATA/addons/<id>/`, opens **Addons** and highlights it. Installing an addon with an id that's already installed replaces it. A new addon starts off in every profile.

## Find an addon

Type in the search box on the left to search names, ids, authors and descriptions. Under it, the filters narrow the list:

| Filter | Values |
|---|---|
| Category | What the addon is for: Tracks, Liveries, Cars, Drivers & teams, Seasons, Database, Gameplay, UI, Audio, Tools, Other, or Uncategorised |
| Type | What it changes: Database, Archives, Text & UI, Files, Code |
| Source | Built in, Built from DB Editor, Imported by Mod Importer, Installed |
| State | On, Off, Has conflicts, Update available |

Each value shows how many addons match. **Clear** removes every filter.

## Turn addons on or off

Each row has a switch for the active [profile](/using/profiles). The next time you press **Play**, the launcher builds the profile with the change.

Some addons, such as Driver Manager, change what your careers store. Turning one of those off asks first: careers you played with it may not load without it. Select **Turn off anyway** to go ahead, or **Cancel** to leave it on.

**Turn off all** turns off every addon except those that show **Always on** and those that would ask about your saves; the launcher names those.

In the `vanilla` profile the list is read-only. Switch to a modded profile to turn addons on.

## Load order

Addons that are on are listed first, in load order. When two addons change the same thing, the one lower in the list wins. Drag a row, or use the arrows in its panel, to move it.

## Check an addon

Select a row to open its panel: description, author, version, source, the game builds it was made for, what it changes (database values, archive resources, text, files, tracks), its settings and its conflicts.

**Conflicts** lists the addons this one overrides and the ones that override it, with what they both change. The list comes from the profile's last build. **Check conflicts** builds the profile again without starting the game and refreshes the list. The list says it is out of date after an addon is turned on, turned off or moved, or after the addon itself changes. Play waits while a check runs.

An addon from the DB Editor has **Edit in DB Editor**. An addon from the Mod Importer has **Open in Mod Importer**.

## Change its settings

Change a value under **Settings** in the addon's panel. **Reset to defaults** puts every setting back. Settings save to the active profile; you can change them in game too, see [In-game addon settings](/using/swofty-mod).

## Remove an addon

Select **Remove** in the addon's panel and confirm. Removing an addon deletes its files and takes it out of every profile. Built-in addons can't be removed.

## Built-in addons

| Addon | What it does |
|---|---|
| **SwoftyMod** | Loads addons in game and adds **ADDON SETTINGS**. It's always on in modded profiles |
| **Simplified for Swofty** | Team Simplified's addon: the [Mod Importer](/using/mods). The launcher installs it, [keeps it up to date](/using/updating#simplified-for-swofty) and keeps it on |

Each release also installs a few addons switched off, such as xanpavle's F1 25 RE-Optimization; see [Addons that ship with the launcher](/using/bundled-addons).

## From a terminal

| Command | Does |
|---|---|
| `swofty addons list` | List installed addons and whether each is on in the profile |
| `swofty addons install <path>` | Install a folder, `.swoftyaddon` or zip |
| `swofty addons enable <id>` | Turn an addon on |
| `swofty addons disable <id>` | Turn an addon off |
| `swofty addons remove <id>` | Remove an addon and take it out of every profile |
| `swofty conflicts [--check]` | Show the profile's conflicts from its last build, or build it again first |
| `swofty settings list [id]` | List addon settings with the profile's values |
| `swofty settings set <id> <setting> <value>` | Change a setting |
| `swofty settings reset <id>` | Put an addon's settings back to their defaults |

Add `--profile <name>` to work on a profile other than the active one. For an addon that has affected your saves, `disable` and `remove` print the warning and ask before going ahead; `--yes` skips the question and is required outside a terminal.

## Next

- [Mod Importer](/using/mods): turn a mod into an addon.
- [DB Editor](/using/db-editor): change the game database and save it as an addon.
- [Profiles](/using/profiles): keep different sets of addons apart.
- To make an addon, see [Addons](/reference/addons/) in the developer docs.
