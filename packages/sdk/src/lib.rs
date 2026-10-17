use std::ffi::{c_char, c_int};

pub const SWOFTY_API_VERSION: u32 = 1;
pub const INIT_SYMBOL: &str = "SwoftyModInit";
pub const HEADER: &str = include_str!("../include/swofty_api.h");

pub type LogFn = unsafe extern "C" fn(fmt: *const c_char, ...);
pub type ModuleBaseFn = unsafe extern "C" fn() -> usize;
pub type ScanFn =
    unsafe extern "C" fn(section: *const c_char, pattern: *const c_char, out: *mut usize) -> c_int;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SwoftyApi {
    pub version: u32,
    pub log: Option<LogFn>,
    pub module_base: Option<ModuleBaseFn>,
    pub scan: Option<ScanFn>,
}

pub type SwoftyModInitFn = unsafe extern "C" fn(api: *const SwoftyApi) -> c_int;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum Capability {
    Log,
    Scan,
    Hook,
    ReadDb,
    Options,
    OnFrame,
}

impl Capability {
    pub const ALL: [Capability; 6] = [
        Self::Log,
        Self::Scan,
        Self::Hook,
        Self::ReadDb,
        Self::Options,
        Self::OnFrame,
    ];

    pub fn as_str(self) -> &'static str {
        match self {
            Self::Log => "log",
            Self::Scan => "scan",
            Self::Hook => "hook",
            Self::ReadDb => "read-db",
            Self::Options => "options",
            Self::OnFrame => "on-frame",
        }
    }

    pub fn parse(s: &str) -> Option<Self> {
        Self::ALL.into_iter().find(|c| c.as_str() == s)
    }

    pub fn available_in(self, api_version: u32) -> bool {
        api_version >= 1 && matches!(self, Self::Log | Self::Scan)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn layout_matches_header() {
        assert_eq!(std::mem::size_of::<SwoftyApi>(), 32);
        assert_eq!(std::mem::offset_of!(SwoftyApi, log), 8);
        assert_eq!(std::mem::offset_of!(SwoftyApi, scan), 24);
        assert!(HEADER.contains(&format!("#define SWOFTY_API_VERSION {SWOFTY_API_VERSION}")));
        assert!(HEADER.contains("SwoftyModInitFn"));
    }

    #[test]
    fn template_header_matches() {
        assert_eq!(
            include_str!("../../../templates/mod/native/include/swofty_api.h"),
            HEADER
        );
    }

    #[test]
    fn capability_names_round_trip() {
        for c in Capability::ALL {
            assert_eq!(Capability::parse(c.as_str()), Some(c));
        }
    }
}
