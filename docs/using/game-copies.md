---
title: Steam and EA app copies
description: Pick which copy of F1 25 Swofty Launcher plays when you have it on Steam and in the EA app, add a game folder, and set up the EA app copy on Linux.
---

# Steam and EA app copies

From 1.3.1, the launcher plays any copy of F1 25 you have: Steam's, the EA app's, or a game folder you add. **Play**, `swofty run`, `swofty apply` and `swofty build` use the active copy.

## Switch copies

The bottom-left of the launcher shows the active copy: its version, a **STEAM** or **EA APP** badge, and how it runs.

1. Select the game name next to the cover art.
2. Under **Play this copy**, select the copy you want. Each one shows its store, version and folder, and the active one has a check.

The launcher remembers your choice. If the active copy is uninstalled, the launcher plays the first copy it finds instead.

## Add a game folder

1. Select the game name, then **Add another folder…**. When no copy is found, select **Locate game** on the **Play** button instead.
2. Type the folder's path, or select **Browse…** and choose it. It's the folder with `F1_25.exe`.
3. Select **Add and play this copy**.

The folder becomes the active copy. A folder with `__Installer/installerdata.xml` gets the **EA APP** badge.

## Where the launcher finds copies

| Copy | Found from |
|---|---|
| Steam | The Steam library that has app 3059520 |
| EA app on Windows | `Install Dir` under `HKEY_LOCAL_MACHINE\SOFTWARE\Codemasters\F1_25`, then the Windows uninstall entries, then `Program Files\EA Games` |
| EA app on Linux | Added with **Add another folder…** |
| Any other folder | Added with **Add another folder…** |

An EA app copy needs `F1_25.exe` and `__Installer/installerdata.xml` in its folder.

## The store starts with the game

**Play** starts the store the active copy came from:

- **Steam copy:** the launcher starts Steam and waits for it to sign in. See [Steam](/reference/launch#steam).
- **EA app copy:** the launcher starts the EA app, installs your profile into the game folder, then asks the EA app to launch F1 25. See [EA app](/reference/launch#ea-app).

To turn this off, select the arrow next to **Play** and clear **Start Steam automatically** or **Start the EA app automatically**. One setting covers both stores.

## The EA app copy on Linux

On Linux, the launcher hands the EA app a launch link through a command you set. Set `ea_launch_command` under `"launch"` in `$DATA/config.json`, with `{url}` where the link goes:

```json
{
  "launch": {
    "ea_launch_command": "/path/to/ea-app.sh wine 'C:\\Program Files\\Electronic Arts\\EA Desktop\\EA Desktop\\EALauncher.exe' {url}",
    "ea_prefix": "/path/to/EAApp/pfx"
  }
}
```

- `ea_launch_command` runs with `sh -c`. It must reach the EA app that is already running when there is one, in the same Wine prefix.
- `ea_prefix` is the EA app's Wine prefix: the folder with `user.reg`, or the Proton folder that holds `pfx`. The launcher writes the in-game loader's DLL setting there.

Without `ea_launch_command`, **Play** on the EA app copy stops with a message naming the setting.

## From the command line

```sh
swofty game list
swofty game use steam
swofty game use ea
swofty game use "/games/F1 25"
swofty game add "/games/F1 25"
```

- `swofty game list` shows each copy's store, version and folder, with `*` next to the active one.
- `swofty game use` picks the active copy by store (`steam`, `ea`) or folder.
- `swofty game add` adds a folder and makes it the active copy.
- `--game <dir>` on any command plays that folder for one run.
- `swofty doctor` lists every copy and marks the active one.
