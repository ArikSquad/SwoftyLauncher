---
title: DB Editor
description: Make packages of changes to F1 25's game database in the launcher, turn them on per profile, stack them and share them as zips.
---

# DB Editor

The **DB Editor** page edits F1 25's game database, `gamedatabases.2025.bin`: drivers, teams, cars, line-ups, tracks, the calendar, weather, points, game modes and every other value the game reads from it. It's built into the launcher from 1.2.6.

## Packages

Your changes live in **packages**. A package is a named set of changes, and each package is a data addon:

- Turn a package on in a profile and its changes apply when you press **Play**. The **Addons** page lists it with the tag **Built from DB Editor**.
- Several packages can be on at once. When two change the same value, the one lower in the list wins. Move packages up and down on the **Packages** page.
- **Export ▸ Share package (.zip)** saves a package to share. **Import…** adds a package from a `.zip`, from a DB Editor export, from an EGO GameDatabases Editor export or from a modded game database. A package `.zip` also installs from the **Addons** page. Installing a package `.zip` from **Addons** makes a new package and never replaces one. When its id is taken, the new package gets a number after its name.
- The launcher writes the changes into the game folder when the game starts and rolls the folder back when it exits.

**Packages**, at the top of the list on the left, shows every package with its switch for the active profile, version, author, description and number of changes. **New package** creates one; **Details…** changes its name, version, author and description; **Duplicate** copies it; **Delete** removes it from every profile. **Turn off all packages** turns every package off in the active profile.

The package you're changing is named at the top of the page, under **Editing**. If you change a value without one, the DB Editor creates **My changes** and turns it on in the active profile. Packages can't be turned on in **Vanilla**.

## What changes apply to

Most of the database is read when a career, Grand Prix or other mode starts. A career copies its drivers, line-ups, ratings and calendar when you create it, so changes to those apply to careers you start afterwards. To change a career in progress, use the [Career Mode Editor](/using/career-editor). Pages with such a counterpart link to it.

## Find something

Type in the search box at the top of the page, or press `Ctrl+F` or `/`. It searches driver, team and track names, table names, key paths, values and the game's English text. Pick a result to open it.

## Pages

| Group | Pages |
|---|---|
| Drivers | Drivers, Driver looks, Entrants, Characters, Helmets, Suits, Gloves, Poses, Podium emotes, Driver heads, Retirements |
| Teams & Cars | Teams, Line-ups, Cars, Engines, Tyre compounds, Tyre allocations, Mandatory tyres, Garage order, Liveries |
| Tracks & Calendar | Tracks, Track settings, Calendar, Track lists, Season lengths, Weather |
| Rules & Modes | Points systems, Game modes, Mode settings, Features, Crowd |
| Advanced | All tables, Unnamed keys, Changes |

A page shows its rows in a table. Pages with a choice above the table, such as the season on **Track settings** or the grid on **Line-ups**, show the rows of that choice. Click a cell to change it, or a row to see every field in the panel beside the table. Changed values are highlighted, and the arrow next to one puts the game's value back. A value another package that's on also changes says so.

**Calendar**, **Garage order** and **Line-ups** have **Move up** and **Move down** on each row.

**Driver Manager** creates new drivers. The **Drivers** page links to it: it opens its page when it's installed, offers to turn it on when it's off, and opens its download page when it isn't installed.

## All tables

**All tables** browses the whole database the way the game stores it: containers, tables and their rows, and keys with their type, meaning, unit and range. A table opens as a grid of rows and fields. **Add key** adds a key the game's database doesn't have. The game reads only the keys it looks for, and a missing key it looks for crashes it.

**Unnamed keys** lists the values whose name isn't known yet, by key hash.

## Changes

**Changes** lists every change in the package you're editing, with the game's value and yours. Revert one change there, or every change in the package with **Revert all changes**, also next to the change count at the top of the page.

## Import from the EGO GameDatabases Editor

**Import…** turns an EGO GameDatabases Editor export of **Modified Records** into a new package. An EGO export that also adds records isn't imported: only its value changes could be, and those alone can crash the game.

## Import a modded game database

**Import…** also takes a modded `gamedatabases.2025.bin`, or a mod `.zip` that holds one and has no `swofty.json`. It compares the file with the game's stock database and makes a new package of the differences, named after the file. Changed values, added keys and removed keys all become changes in the package. A file that's the same as the stock database, or isn't a game database, isn't imported.

The import names the tables the file adds rows to. A package made this way that adds rows to a table doesn't stack with another package that adds rows to the same table.

## Share without the launcher

**Export ▸ Export game files (.zip)** saves a `.zip` for players who don't use the launcher. It applies the package you pick, or every package on in the active profile in their order, to the game's stock database. The zip holds the database at its place in the game folder, `game_package/gamedatabases/gamedatabases.2025.bin`, and a `README.txt`.

The README names the packages and their versions, and the game build and database the zip was made for. It tells players to install the zip by extracting it into the F1 25 folder and replacing the file, and to undo it with **Verify integrity of game files** in Steam.

The launcher refuses modded launches while a replaced database file is in the game folder. Players who use the launcher import the package `.zip` instead.

## When a change no longer applies

A change to a row that's gone, such as a row a game update removed, is skipped when the profile builds. The launch log names the package and the change.
