use std::collections::BTreeMap;

use serde::{Deserialize, Serialize};

pub const DEFAULT_WINEDEBUG: &str = "+timestamp,+pid,+tid,+seh,+loaddll,fixme-all";

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Session {
    pub schema: u32,
    pub ticket: String,
    pub expires: String,
    pub mode: String,
    pub game: GameRef,
    pub profile: String,
    pub overlay: Overlay,
    #[serde(default)]
    pub plugins: Vec<PluginPin>,
    #[serde(default)]
    pub options: BTreeMap<String, BTreeMap<String, serde_json::Value>>,
    #[serde(default)]
    pub ui_bridge: UiBridge,
    pub policy: Policy,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct GameRef {
    pub app_id: u32,
    pub build: String,
    pub exe_sha256: String,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq)]
pub struct Overlay {
    pub root: String,
    #[serde(default)]
    pub files: BTreeMap<String, String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct PluginPin {
    pub id: String,
    pub dll: String,
    pub sha256: String,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq)]
pub struct UiBridge {
    #[serde(default)]
    pub notify_map: BTreeMap<String, (String, String, serde_json::Value)>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Policy {
    pub offline: String,
    pub save_namespace: String,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq)]
pub struct Config {
    #[serde(default)]
    pub launch: LaunchConfig,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct LaunchConfig {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub cpu_affinity: Option<String>,
    #[serde(default)]
    pub env: BTreeMap<String, String>,
    #[serde(default = "default_winedebug")]
    pub winedebug: String,
}

impl Default for LaunchConfig {
    fn default() -> Self {
        Self {
            cpu_affinity: None,
            env: BTreeMap::new(),
            winedebug: default_winedebug(),
        }
    }
}

fn default_winedebug() -> String {
    DEFAULT_WINEDEBUG.into()
}

#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq)]
pub struct LaunchRequest {
    #[serde(default)]
    pub id: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub winedebug: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub profile: Option<String>,
}

pub fn parse_cpu_list(spec: &str) -> Option<Vec<usize>> {
    let mut out = Vec::new();
    for part in spec.split(',').map(str::trim).filter(|p| !p.is_empty()) {
        match part.split_once('-') {
            Some((a, b)) => out.extend(a.parse::<usize>().ok()?..=b.parse::<usize>().ok()?),
            None => out.push(part.parse().ok()?),
        }
    }
    (!out.is_empty()).then_some(out)
}

#[derive(Debug, Clone, PartialEq)]
pub struct SteamCommand {
    pub proton: Option<String>,
    pub verb: Option<String>,
    pub target: Option<usize>,
    pub argv: Vec<String>,
}

pub fn parse_steam_command(argv: &[String]) -> SteamCommand {
    let proton = argv.iter().position(|a| a.ends_with("/proton"));
    let verb = proton.and_then(|i| argv.get(i + 1)).cloned();
    let target = proton.map(|i| i + 2).filter(|&i| i < argv.len()).or_else(|| {
        argv.iter()
            .rposition(|a| a.to_ascii_lowercase().ends_with(".exe"))
    });
    SteamCommand {
        proton: proton.map(|i| argv[i].clone()),
        verb,
        target,
        argv: argv.to_vec(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cpu_lists() {
        assert_eq!(parse_cpu_list("0-2,5"), Some(vec![0, 1, 2, 5]));
        assert_eq!(parse_cpu_list("x"), None);
    }

    #[test]
    fn steam_command_shape() {
        let argv: Vec<String> = [
            "/steam/ubuntu12_32/reaper",
            "SteamLaunch",
            "AppId=3059520",
            "--",
            "/steam/SteamLinuxRuntime_sniper/_v2-entry-point",
            "--verb=waitforexitandrun",
            "--",
            "/steam/compatibilitytools.d/Proton/proton",
            "waitforexitandrun",
            "/games/F1 25/EAAntiCheat.GameServiceLauncher.exe",
        ]
        .map(String::from)
        .to_vec();
        let c = parse_steam_command(&argv);
        assert_eq!(c.verb.as_deref(), Some("waitforexitandrun"));
        assert_eq!(c.target, Some(9));
        assert!(c.proton.unwrap().ends_with("/proton"));
    }
}
