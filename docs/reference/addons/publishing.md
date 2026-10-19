---
title: Packaging and publishing
description: Build an addon without touching the game, test it in game, read the logs, package it as a .swoftyaddon and publish a release.
---

# Packaging and publishing

Once your addon has its operations, code and settings, build it, test it in game, then package it as a `.swoftyaddon` file to share. These steps are the same for data addons and code addons.

## 1. Build {#build}

Install the addon folder, enable it in a modded profile, then build the profile. The build checks every operation without touching the game folder:

```sh
swofty addons install ./my-addon
swofty addons enable yourname.my-addon
swofty build
```

`swofty build` applies the profile's operations and writes the result to `$DATA/cache/overlay/<profile>/<buildhash>/`. That folder holds `plan.json`, listing the game files the build replaces and adds with their sources, and the session's `swofty/settings.json`.

Read every line of output before you launch. It prints every `warn` message, each owned unlock row, skipped operations and each conflict. A conflict means a later addon replaced a target that an earlier addon wrote. Fix the warnings, then build again. See [`swofty build`](/reference/cli#swofty-build).

Installing the folder again replaces the copy in `$DATA/addons/<id>/`, so reinstall after each change.

## 2. Test in game {#test-in-game}

```sh
swofty run --apply
```

`swofty run --apply` installs your profile, starts the game and follows the logs. When the game exits, the game folder returns to its stock files. If the game crashes, run `swofty restore` to put them back.

1. Test with only your addon enabled.
2. Test with the addons and mods players combine it with.
3. Try every value of each setting. Changing a `db` setting rebuilds the profile, so build and relaunch after changing one.

## 3. Read the logs {#logs}

| Log | Contents |
|---|---|
| `<game>/swofty/loader.log` | The in-game loader, your engine plugin's `swofty::log` lines, the runtime asset steps (`[assets]`) and the game's own warnings |
| `$DATA/sessions/<id>/` | One folder per session; `swofty logs` prints the latest summary |
| `$DATA/cache/addons/<id>/helper.log` | Your launcher plugin's stderr |

## 4. Package it {#package-it}

A `.swoftyaddon` is a zip of your addon folder's contents. `swofty.json` has to sit at the zip root, not inside a subfolder, so change into the folder before zipping it:

```sh
cd my-addon
zip -r ../yourname.my-addon.swoftyaddon .
```

Players drop the file on the launcher, or install and enable it from the command line:

```sh
swofty addons install yourname.my-addon.swoftyaddon
swofty addons enable yourname.my-addon
```

`swofty addons install` also takes a plain `.zip` with the same layout, or the folder itself. A `.swoftymod` file from an earlier version installs too.

Before you zip:

1. Set `id`, `name`, `version`, `author` and `description` in [`swofty.json`](./manifest).
2. Set `compat.game_builds` and `compat.seasons` to the builds and seasons your addon was made for.
3. Remove every file that none of your operations name. Leave out build folders such as `target/`.
4. Ship your compiled binaries: the engine plugin DLL and one launcher plugin binary per OS in `launcher.program`.
5. Include a README crediting each author whose files you ship, and get their permission to redistribute those files. Never include the game's own archives, database or executable.

## 5. Publish {#publish}

1. Bump `version` in `swofty.json` for each release.
2. Tell players which game build and season you tested on, and check that `compat.game_builds` and `compat.seasons` match.
3. Credit every author whose files you include.
4. Attach the `.swoftyaddon` to your release.

## Reference

- [Addon manifest](./manifest): every `swofty.json` field, including `compat`.
- [Command line](/reference/cli): `swofty build`, `swofty run`, `swofty logs`, `swofty restore` and `swofty addons`.
- [Logging](/reference/std/logging): writing to `swofty/loader.log` from an engine plugin.
- [`.serps` packages](/reference/mods/serps): sharing whole game files as a mod instead.
