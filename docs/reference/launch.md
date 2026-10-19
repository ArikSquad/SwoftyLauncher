---
title: Launching the game
description: How Swofty Launcher for F1 25 starts a modded session on Windows and Linux, where the logs go, and what happens when the game exits.
---

# Launching the game

Modded sessions run offline. To get set up, follow [Windows](/guide/windows) or [Linux and Steam Deck](/guide/linux).

## Linux: the Steam wrapper

Set Steam's launch option to `swofty run -- %command%` so its Play button starts Swofty Launcher for F1 25. The launcher uses your configured Proton version and records session logs.

To go back to Steam's normal launch, clear the option.

## Windows

Keep Steam running, then select **Play** in the launcher or run `swofty run`. To follow the logs in the launcher, select the arrow next to **Play** and choose **Play with console**.

## Logs

You'll find session logs in `$DATA/sessions/<id>/`. Run `swofty logs` for the latest summary, or use `swofty run` to follow the logs as you play.

If Proton can't create the game process, you'll see `Proton could not start the game process (CreateProcess error N)` instead of an exit code in `swofty run`, `swofty logs` and the launcher.

When the game exits, however it exits, the session puts the game folder back to vanilla. To check, run `swofty verify`. See [Restoring the game](/using/profiles-and-restoring) for the cases that need `swofty restore`.

## Next

- [Offline play](/reference/safety): what a modded session turns off.
- [Backups and recovery](/reference/restore): how the session rolls the game folder back.
