use std::path::Path;
use std::process::{Child, Stdio};
use std::time::{Duration, Instant};

use anyhow::{Result, anyhow, bail};
use swofty_core::ea_app::{self, Handoff};
use swofty_core::install::Source;
use swofty_core::process::{game_processes, stop_process};
use swofty_core::steam_client;
use swofty_core::store::Store;

use crate::run::{StageFile, ending, say};

const APP_START_TIMEOUT: Duration = Duration::from_secs(90);
const HANDOFF_TIMEOUT: Duration = Duration::from_secs(180);
const PRESS_PLAY_TIMEOUT: Duration = Duration::from_secs(600);
const HANDOFF_LOG: &str = "ea_handoff.log";

/// Which store Play brings up first for a copy of the game.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum StoreStart {
    Steam,
    EaApp,
}

pub fn store_start(source: Source, direct: bool) -> StoreStart {
    match source {
        Source::EaApp if !direct => StoreStart::EaApp,
        _ => StoreStart::Steam,
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Policy {
    NoHandoff,
    Running,
    Start,
    Off,
}

pub fn policy(handoff: bool, on: bool, running: bool) -> Policy {
    match (handoff, running, on) {
        (false, _, _) => Policy::NoHandoff,
        (true, true, _) => Policy::Running,
        (true, false, true) => Policy::Start,
        (true, false, false) => Policy::Off,
    }
}

fn handoff(store: &Store) -> Option<Handoff> {
    ea_app::handoff(store.config().launch.ea_launch_command.as_deref())
}

/// An EA app copy needs the EA app: start it when it is closed and wait for
/// it, as Steam is for a Steam copy. A link-only command starts it with the
/// launch link instead.
pub fn ensure(store: &Store, opted_out: bool) -> Result<()> {
    let h = handoff(store);
    let on = store.config().launch.start_store && !opted_out;
    match policy(h.is_some(), on, ea_app::running()) {
        Policy::NoHandoff => bail!("{}", ea_app::no_handoff()),
        Policy::Running => say("ea app", "detected: running"),
        Policy::Off => bail!(
            "the EA app is closed and starting it is turned off (launch.start_store or --no-start-store); \
             open the EA app and press Play again"
        ),
        Policy::Start => match h.as_ref().and_then(ea_app::opener) {
            Some(mut open) => {
                say("ea app", "detected: closed; starting the EA app");
                let stage = StageFile(store.root.join(steam_client::STAGE_FILE));
                stage.set("Starting the EA app…");
                let _child = detach(&mut open).map_err(|e| {
                    anyhow!("could not start the EA app ({e}); open the EA app and press Play again")
                })?;
                let since = Instant::now();
                while !ea_app::running() {
                    if since.elapsed() >= APP_START_TIMEOUT {
                        bail!(
                            "the EA app didn't start within {} s; open the EA app and press Play again",
                            APP_START_TIMEOUT.as_secs()
                        );
                    }
                    std::thread::sleep(steam_client::POLL_INTERVAL);
                }
                say(
                    "ea app",
                    &format!("running after {:.1} s", since.elapsed().as_secs_f64()),
                );
            }
            None => say("ea app", "detected: closed; the launch link starts it"),
        },
    }
    Ok(())
}

fn detach(c: &mut std::process::Command) -> std::io::Result<Child> {
    c.stdin(Stdio::null()).stdout(Stdio::null()).stderr(Stdio::null());
    #[cfg(unix)]
    std::os::unix::process::CommandExt::process_group(c, 0);
    #[cfg(windows)]
    std::os::windows::process::CommandExt::creation_flags(c, 0x0000_0008 | 0x0000_0200);
    c.spawn()
}

fn new_copy(before: &[u32]) -> Option<u32> {
    game_processes()
        .into_iter()
        .map(|g| g.pid)
        .find(|p| !before.contains(p))
}

fn wait_for_game(before: &[u32], timeout: Duration, handoff: &mut Option<Child>) -> Result<Option<u32>> {
    let since = Instant::now();
    loop {
        if let Some(pid) = new_copy(before) {
            return Ok(Some(pid));
        }
        if ending() {
            bail!("asked to stop before F1 25 started");
        }
        if let Some(c) = handoff.as_mut()
            && let Ok(Some(exit)) = c.try_wait()
        {
            *handoff = None;
            if !exit.success() {
                say("ea app", &format!("the hand-off command exited with {exit}"));
                return Ok(None);
            }
        }
        if since.elapsed() >= timeout {
            return Ok(None);
        }
        std::thread::sleep(Duration::from_millis(500));
    }
}

/// The session's game, launched by the EA app: hand it the launch link and
/// wait for F1 25 to start. The game files are staged before this.
pub fn start(store: &Store, game: &Path, args: &[String], session: &Path, before: &[u32]) -> Result<Started> {
    let url = ea_app::install_url(game, args).map_err(|e| anyhow!(e))?;
    let h = handoff(store).ok_or_else(|| anyhow!("{}", ea_app::no_handoff()))?;
    let stage = StageFile(store.root.join(steam_client::STAGE_FILE));
    stage.set("Starting F1 25 through the EA app…");
    say("ea app", &format!("asking the EA app to launch F1 25: {url}"));
    let log = std::fs::File::create(session.join(HANDOFF_LOG))?;
    let mut c = h.command(&url);
    c.stdin(Stdio::null()).stdout(log.try_clone()?).stderr(log);
    #[cfg(unix)]
    std::os::unix::process::CommandExt::process_group(&mut c, 0);
    let mut handoff = match c.spawn() {
        Ok(child) => Some(child),
        Err(e) => {
            say("ea app", &format!("could not run {h}: {e}"));
            None
        }
    };
    let pid = match wait_for_game(before, HANDOFF_TIMEOUT, &mut handoff)? {
        Some(pid) => pid,
        None => {
            if let Some(mut open) = ea_app::opener(&h) {
                detach(&mut open).ok();
            }
            say(
                "ea app",
                "F1 25 did not start from the launch link; press Play on F1 25 in the EA app",
            );
            stage.set("Press Play on F1 25 in the EA app");
            wait_for_game(before, PRESS_PLAY_TIMEOUT, &mut None)?.ok_or_else(|| {
                anyhow!(
                    "F1 25 was not started from the EA app within {} min; the game files are rolled back",
                    PRESS_PLAY_TIMEOUT.as_secs() / 60
                )
            })?
        }
    };
    say("ea app", &format!("F1 25 started (pid {pid})"));
    Ok(Started { handoff })
}

pub struct Started {
    handoff: Option<Child>,
}

/// Until every copy of F1 25 started since the launch has closed.
pub fn wait(before: &[u32], started: Started, mut tick: impl FnMut()) -> i32 {
    let mut asked = false;
    loop {
        let running: Vec<u32> = game_processes()
            .into_iter()
            .map(|g| g.pid)
            .filter(|p| !before.contains(p))
            .collect();
        if running.is_empty() {
            break;
        }
        tick();
        if !asked && ending() {
            asked = true;
            say(
                "info",
                "asked to stop: closing F1 25, then rolling its game files back",
            );
            for pid in running {
                stop_process(pid).ok();
            }
        }
        std::thread::sleep(Duration::from_millis(200));
    }
    if let Some(mut c) = started.handoff {
        std::thread::spawn(move || c.wait().ok());
    }
    0
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_active_copy_picks_the_store_to_start() {
        assert_eq!(store_start(Source::Steam, false), StoreStart::Steam);
        assert_eq!(store_start(Source::EaApp, false), StoreStart::EaApp);
        assert_eq!(store_start(Source::EaApp, true), StoreStart::Steam);
        assert_eq!(store_start(Source::Unknown, false), StoreStart::Steam);
    }

    #[test]
    fn starts_the_ea_app_only_when_it_can_and_may() {
        assert_eq!(policy(false, true, true), Policy::NoHandoff);
        assert_eq!(policy(true, false, true), Policy::Running);
        assert_eq!(policy(true, true, false), Policy::Start);
        assert_eq!(policy(true, false, false), Policy::Off);
    }

    #[cfg(unix)]
    #[test]
    fn hands_the_link_to_the_configured_command() {
        let root = std::env::temp_dir().join(format!("swofty-ea-handoff-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let game = root.join("F1 25");
        std::fs::create_dir_all(game.join("__Installer")).unwrap();
        std::fs::write(
            game.join(ea_app::MARKER),
            "<contentIDs><contentID>16425845</contentID><contentID>16425845_oa</contentID></contentIDs>",
        )
        .unwrap();
        let store = Store::new(root.join("data"));
        std::fs::create_dir_all(&store.root).unwrap();
        let got = root.join("url.txt");
        let mut config = store.config();
        config.launch.ea_launch_command = Some(format!("printf %s {{url}} > '{}'", got.display()));
        store.save_config(&config).unwrap();
        let url = ea_app::install_url(&game, &["-vr".into()]).unwrap();
        let mut c = handoff(&store).unwrap().command(&url);
        assert!(c.status().unwrap().success());
        assert_eq!(
            std::fs::read_to_string(&got).unwrap(),
            "origin2://game/launch?offerIds=16425845,16425845_oa&autoDownload=1&cmdParams=-vr"
        );
        std::fs::remove_dir_all(&root).ok();
    }
}
