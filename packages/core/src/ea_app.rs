//! The EA app's copy of F1 25: where Windows has it installed, the link that
//! asks the EA app to launch it, and the command that hands the link over.

use std::path::{Path, PathBuf};
use std::process::Command;

use crate::GAME_EXE;
use crate::compat::parse_reg_query;

pub const MARKER: &str = "__Installer/installerdata.xml";
pub const PROCESS: &str = "EADesktop.exe";
pub const LAUNCHER_EXE: &str = "EALauncher.exe";

/// The game's own installerdata.xml names the first key as its install check.
pub const INSTALL_KEYS: &[&str] = &[
    r"HKLM\SOFTWARE\Codemasters\F1_25",
    r"HKLM\SOFTWARE\WOW6432Node\Codemasters\F1_25",
    r"HKLM\SOFTWARE\EA Games\F1 25",
    r"HKLM\SOFTWARE\WOW6432Node\EA Games\F1 25",
    r"HKLM\SOFTWARE\EA Sports\F1 25",
    r"HKLM\SOFTWARE\WOW6432Node\EA Sports\F1 25",
];
pub const UNINSTALL_KEYS: &[&str] = &[
    r"HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall",
    r"HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall",
];
pub const PROTOCOL_KEY: &str = r"HKCR\origin2\shell\open\command";
pub const DESKTOP_KEYS: &[&str] = &[
    r"HKLM\SOFTWARE\Electronic Arts\EA Desktop",
    r"HKLM\SOFTWARE\WOW6432Node\Electronic Arts\EA Desktop",
];

pub fn is_install(dir: &Path) -> bool {
    dir.join(GAME_EXE).is_file() && dir.join(MARKER).is_file()
}

/// The install folders a `reg query` lists, by any of the value names EA
/// and Windows use for one.
pub fn install_dirs(reg_out: &str) -> Vec<PathBuf> {
    parse_reg_query(reg_out)
        .into_iter()
        .filter(|(name, _)| {
            ["Install Dir", "InstallDir", "InstallLocation"]
                .iter()
                .any(|n| name.eq_ignore_ascii_case(n))
        })
        .map(|(_, data)| data.trim().trim_matches('"').to_owned())
        .filter(|d| !d.is_empty())
        .map(PathBuf::from)
        .collect()
}

/// `reg query <key> /s` output split into each subkey's values.
pub fn reg_tree(out: &str) -> Vec<(String, String)> {
    let mut blocks: Vec<(String, String)> = Vec::new();
    for line in out.lines() {
        let line = line.trim_end_matches('\r');
        if line.starts_with("HKEY_") {
            blocks.push((line.to_owned(), String::new()));
        } else if let Some((_, values)) = blocks.last_mut() {
            values.push_str(line);
            values.push('\n');
        }
    }
    blocks
}

/// Every F1 25 install folder Windows records for the EA app, checked with
/// `is_install`. `reg` runs `reg query` with the given arguments.
pub fn find_installs(
    reg: impl Fn(&[&str]) -> Option<String>,
    defaults: &[PathBuf],
    is_install: impl Fn(&Path) -> bool,
) -> Vec<PathBuf> {
    let keyed = INSTALL_KEYS
        .iter()
        .filter_map(|k| reg(&[k]))
        .flat_map(|out| install_dirs(&out));
    let uninstall = UNINSTALL_KEYS
        .iter()
        .filter_map(|k| reg(&[k, "/s"]))
        .flat_map(|out| reg_tree(&out))
        .flat_map(|(_, values)| install_dirs(&values));
    let folders = defaults.iter().flat_map(|d| {
        std::fs::read_dir(d)
            .into_iter()
            .flatten()
            .flatten()
            .map(|e| e.path())
    });
    let mut out: Vec<PathBuf> = Vec::new();
    for dir in keyed.chain(uninstall).chain(folders) {
        if is_install(&dir) && !out.iter().any(|o| crate::copies::same_dir(o, &dir)) {
            out.push(dir);
        }
    }
    out
}

#[cfg(windows)]
fn reg_exe(args: &[&str]) -> Option<String> {
    let mut all = vec!["query"];
    all.extend_from_slice(args);
    crate::compat::windows_tool("reg", &all).filter(|o| !o.trim().is_empty())
}

#[cfg(windows)]
fn program_files() -> Vec<PathBuf> {
    ["ProgramFiles", "ProgramW6432", "ProgramFiles(x86)"]
        .iter()
        .filter_map(std::env::var_os)
        .map(PathBuf::from)
        .collect()
}

#[cfg(windows)]
pub fn installs() -> Vec<PathBuf> {
    let defaults: Vec<PathBuf> = program_files().iter().map(|p| p.join("EA Games")).collect();
    find_installs(reg_exe, &defaults, is_install)
}

/// Linux has no standard place for the EA app's games; players add the
/// folder in the launcher.
#[cfg(not(windows))]
pub fn installs() -> Vec<PathBuf> {
    Vec::new()
}

/// The `<contentID>` values in the install's installerdata.xml.
pub fn content_ids(installerdata: &str) -> Vec<String> {
    let mut out: Vec<String> = Vec::new();
    let mut rest = installerdata;
    while let Some(start) = rest.find("<contentID>") {
        rest = &rest[start + "<contentID>".len()..];
        let Some(end) = rest.find("</contentID>") else {
            break;
        };
        let id = rest[..end].trim();
        if !id.is_empty() && !out.iter().any(|o| o == id) {
            out.push(id.to_owned());
        }
        rest = &rest[end..];
    }
    out
}

fn encode(text: &str) -> String {
    text.bytes()
        .map(|b| match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'.' | b'_' | b'~' => (b as char).to_string(),
            _ => format!("%{b:02X}"),
        })
        .collect()
}

/// The link that asks the EA app to launch the game, with `args` for it.
pub fn launch_url(ids: &[String], args: &[String]) -> Option<String> {
    if ids.is_empty() {
        return None;
    }
    let offers: Vec<String> = ids.iter().map(|i| encode(i)).collect();
    let mut url = format!(
        "origin2://game/launch?offerIds={}&autoDownload=1",
        offers.join(",")
    );
    if !args.is_empty() {
        url.push_str("&cmdParams=");
        url.push_str(&encode(&args.join(" ")));
    }
    Some(url)
}

pub fn install_url(dir: &Path, args: &[String]) -> Result<String, String> {
    let xml = std::fs::read_to_string(dir.join(MARKER))
        .map_err(|e| format!("could not read {}: {e}", dir.join(MARKER).display()))?;
    launch_url(&content_ids(&xml), args)
        .ok_or_else(|| format!("{} names no content ID to launch", dir.join(MARKER).display()))
}

/// A command line split into words, honouring double quotes.
pub fn split_command(line: &str) -> Vec<String> {
    let mut words = Vec::new();
    let mut word = String::new();
    let (mut quoted, mut any) = (false, false);
    for c in line.chars() {
        match c {
            '"' => {
                quoted = !quoted;
                any = true;
            }
            c if c.is_whitespace() && !quoted => {
                if any || !word.is_empty() {
                    words.push(std::mem::take(&mut word));
                }
                any = false;
            }
            c => word.push(c),
        }
    }
    if any || !word.is_empty() {
        words.push(word);
    }
    words
}

/// How a launch link reaches the EA app.
#[derive(Debug, Clone, PartialEq)]
pub enum Handoff {
    /// A program and its arguments, with `%1` or `{url}` where the link goes.
    Program(Vec<String>),
    /// A shell command line with `{url}` where the link goes.
    Shell(String),
}

impl Handoff {
    /// The `origin2` handler Windows registered, from `reg query <key> /ve`.
    pub fn from_protocol(reg_out: &str) -> Option<Self> {
        let (_, command) = parse_reg_query(reg_out).into_iter().next()?;
        let words = split_command(&command);
        (!words.is_empty()).then_some(Self::Program(words))
    }

    pub fn from_setting(command: &str) -> Option<Self> {
        let command = command.trim();
        if command.is_empty() {
            None
        } else if cfg!(windows) {
            Some(Self::Program(split_command(command)))
        } else {
            Some(Self::Shell(command.to_owned()))
        }
    }

    /// The words to run for `url`; the link is appended when the command
    /// has no placeholder.
    pub fn argv(&self, url: &str) -> Vec<String> {
        match self {
            Self::Program(words) => {
                let mut placed = false;
                let mut out: Vec<String> = words
                    .iter()
                    .map(|w| {
                        if w.contains("%1") || w.contains("{url}") {
                            placed = true;
                            w.replace("%1", url).replace("{url}", url)
                        } else {
                            w.clone()
                        }
                    })
                    .collect();
                if !placed {
                    out.push(url.to_owned());
                }
                out
            }
            Self::Shell(line) => {
                let quoted = format!("'{}'", url.replace('\'', r"'\''"));
                let line = if line.contains("{url}") {
                    line.replace("{url}", &quoted)
                } else {
                    format!("{line} {quoted}")
                };
                vec!["sh".into(), "-c".into(), line]
            }
        }
    }

    pub fn command(&self, url: &str) -> Command {
        let argv = self.argv(url);
        let mut c = Command::new(&argv[0]);
        c.args(&argv[1..]);
        c
    }
}

impl std::fmt::Display for Handoff {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Program(words) => f.write_str(&words.join(" ")),
            Self::Shell(line) => f.write_str(line),
        }
    }
}

/// EALauncher.exe from the EA Desktop key's values, else its default folder.
pub fn launcher_exe(desktop_values: &str, defaults: &[PathBuf]) -> Option<PathBuf> {
    let values = parse_reg_query(desktop_values);
    let value = |name: &str| {
        values
            .iter()
            .find(|(n, d)| n.eq_ignore_ascii_case(name) && !d.trim().is_empty())
            .map(|(_, d)| PathBuf::from(d.trim().trim_matches('"')))
    };
    value("LauncherAppPath")
        .or_else(|| value("InstallLocation").map(|d| d.join(LAUNCHER_EXE)))
        .or_else(|| value("DesktopAppPath").and_then(|d| Some(d.parent()?.join(LAUNCHER_EXE))))
        .into_iter()
        .chain(defaults.iter().cloned())
        .find(|p| p.is_file())
}

/// How this computer hands a launch link to the EA app: the setting when
/// set, else on Windows the `origin2` handler the EA app registered. The
/// handler is what Windows runs for EA's own links, so it follows the EA
/// app wherever it is installed and shows up front whether it is; running
/// it directly instead of through `start` keeps the link's `&` away from
/// cmd.exe. EALauncher.exe from the EA Desktop key is the fallback.
pub fn handoff(setting: Option<&str>) -> Option<Handoff> {
    if let Some(h) = setting.and_then(Handoff::from_setting) {
        return Some(h);
    }
    windows_handoff()
}

#[cfg(windows)]
fn windows_handoff() -> Option<Handoff> {
    if let Some(h) = reg_exe(&[PROTOCOL_KEY, "/ve"]).and_then(|o| Handoff::from_protocol(&o)) {
        return Some(h);
    }
    let desktop: String = DESKTOP_KEYS.iter().filter_map(|k| reg_exe(&[k])).collect();
    let defaults: Vec<PathBuf> = program_files()
        .iter()
        .map(|p| {
            p.join(r"Electronic Arts\EA Desktop\EA Desktop")
                .join(LAUNCHER_EXE)
        })
        .collect();
    launcher_exe(&desktop, &defaults).map(|exe| Handoff::Program(vec![exe.display().to_string()]))
}

#[cfg(not(windows))]
fn windows_handoff() -> Option<Handoff> {
    None
}

/// EALauncher.exe on its own, which opens the EA app.
pub fn opener(handoff: &Handoff) -> Option<Command> {
    match handoff {
        Handoff::Program(words) if words[0].to_ascii_lowercase().ends_with(".exe") => {
            Some(Command::new(&words[0]))
        }
        _ => None,
    }
}

pub fn running() -> bool {
    !crate::process::named(PROCESS).is_empty()
}

/// The `user.reg` of the EA app's Wine prefix from the `launch.ea_prefix`
/// setting: the prefix itself, or Proton's compatdata folder around it.
pub fn prefix_user_reg(setting: &Path) -> Option<PathBuf> {
    [setting.join("user.reg"), setting.join("pfx/user.reg")]
        .into_iter()
        .find(|p| p.is_file())
}

pub const NO_HANDOFF_LINUX: &str = "this is the EA app's copy of F1 25, and on Linux Swofty starts it through the EA app with \
     the launch.ea_launch_command setting: a command that hands {url} to the EA app in its Wine prefix. Set it in \
     config.json (see Playing the EA app copy in the docs), or switch to another copy";
pub const NO_HANDOFF_WINDOWS: &str =
    "the EA app was not found on this computer; install or repair the EA app, then press Play again";

pub fn no_handoff() -> &'static str {
    if cfg!(windows) {
        NO_HANDOFF_WINDOWS
    } else {
        NO_HANDOFF_LINUX
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const INSTALLERDATA: &str = r#"<?xml version="1.0" encoding="utf-8"?>
<DiPManifest version="4.0">
  <buildMetaData><gameVersion version="1.0.141.7865"/></buildMetaData>
  <contentIDs>
    <contentID>16425845</contentID>
    <contentID>16425845_oa</contentID>
    <contentID>16425845</contentID>
  </contentIDs>
  <runtime>
    <launcher uid="launcher_1">
      <filePath>[HKEY_LOCAL_MACHINE\SOFTWARE\Codemasters\F1_25\Install Dir]EAAntiCheat.GameServiceLauncher.exe</filePath>
    </launcher>
  </runtime>
</DiPManifest>"#;

    #[test]
    fn reads_the_content_ids_once_each() {
        assert_eq!(content_ids(INSTALLERDATA), ["16425845", "16425845_oa"]);
        assert!(content_ids("<DiPManifest/>").is_empty());
        assert_eq!(content_ids("<contentID> 1 </contentID><contentID>"), ["1"]);
    }

    #[test]
    fn builds_the_launch_link() {
        let ids = content_ids(INSTALLERDATA);
        assert_eq!(
            launch_url(&ids, &[]).unwrap(),
            "origin2://game/launch?offerIds=16425845,16425845_oa&autoDownload=1"
        );
        assert_eq!(
            launch_url(&ids, &["-vr".into()]).unwrap(),
            "origin2://game/launch?offerIds=16425845,16425845_oa&autoDownload=1&cmdParams=-vr"
        );
        assert_eq!(
            launch_url(&ids, &["-vr".into(), "-x=a&b".into()]).unwrap(),
            "origin2://game/launch?offerIds=16425845,16425845_oa&autoDownload=1&cmdParams=-vr%20-x%3Da%26b"
        );
        assert_eq!(launch_url(&[], &[]), None);
    }

    #[test]
    fn reads_install_folders_from_the_registry() {
        let out = "\r\nHKEY_LOCAL_MACHINE\\SOFTWARE\\Codemasters\\F1_25\r\n    Install Dir    REG_SZ    D:\\EA Games\\F1 25\\\r\n    Locale    REG_SZ    en_US\r\n\r\n";
        assert_eq!(install_dirs(out), [PathBuf::from("D:\\EA Games\\F1 25\\")]);
        let tree = "\r\nHKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{A}\r\n    DisplayName    REG_SZ    Other\r\n    InstallLocation    REG_SZ    C:\\Other\r\n\r\nHKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{B}\r\n    DisplayName    REG_SZ    F1® 25\r\n    InstallLocation    REG_SZ    \"E:\\Games\\F1 25\"\r\n";
        let blocks = reg_tree(tree);
        assert_eq!(blocks.len(), 2);
        let dirs: Vec<PathBuf> = blocks.iter().flat_map(|(_, v)| install_dirs(v)).collect();
        assert_eq!(
            dirs,
            [PathBuf::from("C:\\Other"), PathBuf::from("E:\\Games\\F1 25")]
        );
    }

    #[test]
    fn finds_installs_by_key_uninstall_entry_and_default_folder() {
        let root = std::env::temp_dir().join(format!("swofty-ea-find-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        let make = |rel: &str, marker: bool| {
            let d = root.join(rel);
            std::fs::create_dir_all(d.join("__Installer")).unwrap();
            std::fs::write(d.join(GAME_EXE), b"MZ").unwrap();
            if marker {
                std::fs::write(d.join(MARKER), INSTALLERDATA).unwrap();
            }
            d
        };
        let keyed = make("keyed/F1 25", true);
        let listed = make("listed/F1 25", true);
        let default = make("EA Games/F1 25", true);
        let steamish = make("steamish/F1 25", false);
        let reg = |args: &[&str]| -> Option<String> {
            match args {
                [k] if *k == INSTALL_KEYS[0] => Some(format!(
                    "\r\n{k}\r\n    Install Dir    REG_SZ    {}\r\n",
                    keyed.display()
                )),
                [k] if *k == INSTALL_KEYS[1] => Some(format!(
                    "\r\n{k}\r\n    Install Dir    REG_SZ    {}\r\n",
                    keyed.display()
                )),
                [k, "/s"] if *k == UNINSTALL_KEYS[0] => Some(format!(
                    "\r\nHKEY_X\\{{1}}\r\n    InstallLocation    REG_SZ    {}\r\n\r\nHKEY_X\\{{2}}\r\n    InstallLocation    REG_SZ    {}\r\n",
                    listed.display(),
                    steamish.display()
                )),
                _ => None,
            }
        };
        let found = find_installs(reg, &[root.join("EA Games")], is_install);
        assert_eq!(found, [keyed, listed, default]);
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn hands_the_link_to_the_registered_handler() {
        let out = "\r\nHKEY_CLASSES_ROOT\\origin2\\shell\\open\\command\r\n    (Default)    REG_SZ    \"C:\\Program Files\\Electronic Arts\\EA Desktop\\EA Desktop\\EALauncher.exe\" \"%1\"\r\n";
        let h = Handoff::from_protocol(out).unwrap();
        assert_eq!(
            h.argv("origin2://game/launch?offerIds=1&autoDownload=1"),
            [
                r"C:\Program Files\Electronic Arts\EA Desktop\EA Desktop\EALauncher.exe",
                "origin2://game/launch?offerIds=1&autoDownload=1"
            ]
        );
        assert_eq!(
            opener(&h).map(|c| c.get_program().to_owned()),
            Some(r"C:\Program Files\Electronic Arts\EA Desktop\EA Desktop\EALauncher.exe".into())
        );
        let bare = Handoff::Program(vec!["EALauncher.exe".into()]);
        assert_eq!(bare.argv("u"), ["EALauncher.exe", "u"]);
    }

    #[test]
    fn a_shell_setting_gets_the_link_quoted() {
        let h = Handoff::Shell("/games/ea-app.sh wine start {url}".into());
        assert_eq!(
            h.argv("origin2://game/launch?offerIds=1&autoDownload=1"),
            [
                "sh",
                "-c",
                "/games/ea-app.sh wine start 'origin2://game/launch?offerIds=1&autoDownload=1'"
            ]
        );
        assert_eq!(Handoff::Shell("open".into()).argv("a'b")[2], r"open 'a'\''b'");
        assert_eq!(Handoff::from_setting("  "), None);
        assert!(opener(&h).is_none());
    }

    #[test]
    fn splits_quoted_commands() {
        assert_eq!(
            split_command(r#""C:\A B\x.exe" --flag "%1" """#),
            [r"C:\A B\x.exe", "--flag", "%1", ""]
        );
    }

    #[test]
    fn finds_the_launcher_from_the_desktop_key() {
        let root = std::env::temp_dir().join(format!("swofty-ea-launcher-{}", std::process::id()));
        std::fs::create_dir_all(&root).unwrap();
        let exe = root.join(LAUNCHER_EXE);
        std::fs::write(&exe, b"MZ").unwrap();
        let values = format!(
            "\r\nHKEY_X\r\n    InstallLocation    REG_SZ    {}\r\n",
            root.display()
        );
        assert_eq!(launcher_exe(&values, &[]), Some(exe.clone()));
        assert_eq!(launcher_exe("", std::slice::from_ref(&exe)), Some(exe));
        assert_eq!(launcher_exe("", &[root.join("missing.exe")]), None);
        std::fs::remove_dir_all(&root).ok();
    }
}
