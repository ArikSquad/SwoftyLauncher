//! Which addon overrides which in a profile's build: kept from the last
//! build and checked on request without installing anything.

use serde::{Deserialize, Serialize};

use crate::Result;
use crate::install::Game;
use crate::store::Store;

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct Report {
    pub profile: String,
    /// Seconds since the Unix epoch.
    pub built_at: f64,
    pub pairs: Vec<Pair>,
}

/// `winner` loads after `loser` and replaces what both write.
#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct Pair {
    pub winner: String,
    pub loser: String,
    pub database: usize,
    pub resources: usize,
    pub files: usize,
    /// Readable names of the overridden items, such as `tracks.Track_3.laps`.
    pub items: Vec<String>,
}

/// The conflicts of the profile's last build.
pub fn last(_store: &Store, _profile: &str) -> Option<Report> {
    todo!()
}

/// Plans the profile's build without installing it and returns its conflicts.
pub fn check(_store: &Store, _profile: &str, _game: &Game) -> Result<Report> {
    todo!()
}
