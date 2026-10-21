---
title: Tools
description: The Tools page lists the tools that make or edit Swofty addons, with a button to open or get each one.
---

# Tools

**Tools**, in the sidebar, lists the tools that make or edit Swofty addons. Each card shows the tool's name, who made it and what it does, and a button:

| Button | What it does |
|---|---|
| **Open** | Opens the tool: a launcher page, or a program that ships with the launcher |
| **Turn on** | Turns the tool's addon on in the active profile, when it's installed but off |
| **Get** | Opens the tool's download or docs page in your browser |

## The tools

| Tool | Made by | Open | Get |
|---|---|---|---|
| DB Editor | Swofty | The [DB Editor](/using/db-editor) page | |
| Mod Importer | Team Simplified | The **Mod Importer** page | |
| Swofty UI | Swofty | Starts `bin/swofty-ui`, which opens in your browser | [Swofty UI](/reference/ui/swofty-ui) |
| Career Mode Editor | Swofty | Its **Career Editor** page, when the addon is on | [Career Mode Editor](/using/career-editor) |
| Driver Manager | Swofty | Its **Driver Manager** page, when the addon is on | [Overtake](https://www.overtake.gg/downloads/driver-manager-addon.87734/) |
| Simplified Suite | Team Simplified | | [GitHub](https://github.com/TeamSimplified/simplified-suite) |
| EGO GameDatabases Editor | PavelGames | | [Overtake](https://www.overtake.gg/downloads/ego-gamedatabases-editor.84903/) |

## Adding a tool

The list is `apps/launcher/assets/tools.json` in the launcher's repository. Each entry has:

| Field | Value |
|---|---|
| `name` | The tool's name |
| `maker` | Who made it |
| `icon` | One of `database`, `folder-input`, `app-window`, `user-pen`, `users`, `package`, `table`, `map`, `palette`, `code` |
| `about` | One sentence on what it does |
| `open` | Optional. `page:database`, `page:importer` or `page:addons` opens that page; `addon:<id>` opens the addon's first page; `program:<name>` starts `bin/<name>` |
| `get` | Optional. The download or docs URL, used when `open` can't open anything |
| `more` | Optional. A second link, as `{ "label": …, "url": … }` |
