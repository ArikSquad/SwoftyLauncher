# Uninstalling

Steam's **Verify integrity of game files** repairs game files, but leaves the launcher's additions in place. You'll need `swofty restore` to remove those:

1. Close F1 25.
2. Run `swofty restore`. If it reports files it couldn't restore, resolve those before you continue. See [Restoring the game](/using/profiles-and-restoring).
3. On Linux, open F1 25 → **Properties** → **General** in Steam and clear F1 25's **Launch options**.
4. If you used the installer, delete its shortcuts: **Swofty Launcher** in the Start menu (`%APPDATA%\Microsoft\Windows\Start Menu\Programs\Swofty Launcher.lnk`) and on the desktop on Windows, or `~/.local/share/applications/net.swofty.ModLauncher.desktop` and the desktop copy on Linux.
5. Copy out anything you want to keep, then delete the [install folder](/guide/installation#the-install-folder) and the [data folder](/guide/installation#where-your-files-are-stored). Deleting the data folder removes your installed addons, mods, profiles, backups and any saves stored there.
