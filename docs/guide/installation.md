# Installation

Install the launcher with the installer or by hand, then follow the setup steps for [Windows](/guide/windows) or [Linux and Steam Deck](/guide/linux). You'll play modded games in single-player, offline.

## Requirements

- F1 25 on Steam, with the F1 2026 season content installed.
- Windows 10 or 11 (x86-64), or Linux with Steam and Proton. Steam Deck, NixOS and Flatpak Steam work. macOS does not.
- Free disk space: your content changes copies of the game files, so you'll need room for those copies.

## Install with the installer

1. Download the installer for your platform:

   | Platform | Download |
   |---|---|
   | Windows | [SwoftyLauncherInstaller.exe](https://github.com/Swofty-Developments/SwoftyLauncher/raw/main/installer/SwoftyLauncherInstaller.exe) |
   | Linux and Steam Deck | [SwoftyLauncherInstaller-linux.zip](https://github.com/Swofty-Developments/SwoftyLauncher/raw/main/installer/SwoftyLauncherInstaller-linux.zip), then extract it |

2. Run `SwoftyLauncherInstaller.exe` on Windows, or `SwoftyLauncherInstaller` on Linux and Steam Deck. Windows may show **Windows protected your PC**; select **More info**, then **Run anyway**.
3. Choose a folder and select **Install**.

The installer downloads the latest release, checks it against the release's `SHA256SUMS` and extracts it into that folder. It adds **Swofty Launcher** to the Start menu on Windows or the app menu on Linux, adds a desktop shortcut if **Add a desktop shortcut** is ticked, then opens the launcher.

| Platform | Default folder |
|---|---|
| Windows | `%LOCALAPPDATA%\Programs\SwoftyLauncher` |
| Linux and Steam Deck | `~/SwoftyLauncher` |

With **Flatpak Steam**, choose a folder inside `~/.var/app/com.valvesoftware.Steam/`, such as `~/.var/app/com.valvesoftware.Steam/SwoftyLauncher`.

You only need the installer once. After that, open the launcher from the Start menu or app menu, and it [updates itself](#updating).

## Install by hand

1. Open [Releases](https://github.com/Swofty-Developments/SwoftyLauncher/releases).
2. Download the archive for your platform:

   | Platform | Archive |
   |---|---|
   | Windows | `swofty-launcher-<version>-windows-x86_64.zip` |
   | Linux and Steam Deck | `swofty-launcher-<version>-linux-x86_64.tar.gz` |

3. Extract it where you want to keep the launcher, such as `Documents\SwoftyLauncher` or `~/SwoftyLauncher`. With **Flatpak Steam**, extract it inside `~/.var/app/com.valvesoftware.Steam/`.

A hand install adds no menu entries or shortcuts; open the launcher from `bin/`.

## The install folder

Either way, the install folder holds:

| Path | Contents |
|---|---|
| `bin/swofty-launcher` | The launcher |
| `bin/swofty` | The command line |
| `bin/swofty-ui` | [Swofty UI](/reference/ui/swofty-ui) |
| `loader/` | The in-game loader; keep it next to `bin/` |

On Windows, the files in `bin/` end in `.exe`. The setup guides call this folder the install folder.

## Updating

When you open the launcher, it checks for a new release, at most once every 6 hours. If there's one available, you'll see its version in a bar above **Play**, with **Update**, **Release notes** and **Skip this version**.

1. Close F1 25 first: the launcher won't update while the game runs.
2. Select **Update**. It downloads the archive, checks it against the release's `SHA256SUMS`, and replaces `bin/`, `loader/` and, on Linux, `share/`.
3. Select **Restart**.

If a step fails, you'll see the error and the old files stay in place. An update interrupted partway is rolled back the next time you start the launcher.

Running the installer again on the same folder also installs the latest release.

If you built from source or installed through Nix, you'll see **Download** instead; it opens the release page. From a terminal, `swofty update` does the same.

## Where your files are stored

When these docs refer to `$DATA`, they mean the folder below for your platform:

| Platform | Folder |
|---|---|
| Linux | `$XDG_DATA_HOME/swofty`, usually `~/.local/share/swofty` |
| Flatpak Steam | `~/.var/app/com.valvesoftware.Steam/data/swofty` |
| Windows | `%LOCALAPPDATA%\Swofty` |

Your addons, mods, profiles, backups and session logs live here, apart from the install folder. Because it holds your backups, keep this folder until you've [restored the game to stock](/using/profiles-and-restoring).
