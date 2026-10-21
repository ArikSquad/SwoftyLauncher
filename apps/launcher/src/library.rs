use std::sync::Arc;
use std::time::Instant;

use gpui_kit::{Image, ImageFormat};
use swofty_core::asset::{AssetOp, OpFile};
use swofty_core::launcher::Summary;
use swofty_core::manifest::AddonManifest;
use swofty_core::profile::{Profile, VANILLA};
use swofty_core::registry::{Host, Registry};
use swofty_core::store::Store;
use swofty_core::view::SectionSpec;

pub struct AddonItem {
    pub manifest: AddonManifest,
    pub enabled: bool,
    pub locked: bool,
    pub icon: Option<AddonIcon>,
    /// Only tracks (see [`swofty_core::tracks::is_track_addon`]): listed on the
    /// Tracks page while one is open in the profile.
    pub track: bool,
    /// Built from the DB Editor (see [`swofty_core::dbedit::package::is_package`]).
    pub package: bool,
    pub source: Source,
}

pub const DB_EDITOR_TOOL: &str = "db-editor";

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum Source {
    Builtin,
    DbEditor,
    Importer,
    Installed,
}

impl Source {
    pub const ALL: [Source; 4] = [Self::Builtin, Self::DbEditor, Self::Importer, Self::Installed];

    pub fn label(self) -> &'static str {
        match self {
            Self::Builtin => "Built in",
            Self::DbEditor => "Built from DB Editor",
            Self::Importer => "Imported by Mod Importer",
            Self::Installed => "Installed",
        }
    }

    pub fn tag(self) -> &'static str {
        match self {
            Self::Builtin => "Built in",
            Self::DbEditor => "DB Editor",
            Self::Importer => "Mod Importer",
            Self::Installed => "Installed",
        }
    }

    fn of(store: &Store, m: &AddonManifest, package: bool) -> Self {
        let tool = m.origin.as_ref().map(|o| o.tool.as_str());
        if store.is_builtin(&m.id) {
            Self::Builtin
        } else if package || tool == Some(DB_EDITOR_TOOL) {
            Self::DbEditor
        } else if tool == Some(swofty_core::managed::ID) {
            Self::Importer
        } else {
            Self::Installed
        }
    }
}

/// What an addon's op files change, counted by kind.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct Changes {
    pub database: usize,
    pub resources: usize,
    pub text: usize,
    pub files: usize,
    pub tracks: usize,
    pub screens: usize,
    pub plugins: usize,
    pub unreadable: Vec<String>,
}

impl Changes {
    pub fn read(store: &Store, item: &AddonItem) -> Self {
        let m = &item.manifest;
        let dir = store.addons_dir().join(&m.id);
        let mut c = Self {
            screens: m.operations.ui.len() + m.operations.screens.len(),
            plugins: m.operations.native.len(),
            ..Self::default()
        };
        let ops = m.operations.op_files().flat_map(|(kind, rel)| {
            let file = std::fs::read(dir.join(rel))
                .ok()
                .and_then(|b| serde_json::from_slice(&b).ok())
                .and_then(|v| OpFile::from_value(v, kind).ok());
            if file.is_none() {
                c.unreadable.push(rel.to_owned());
            }
            file.map(|f| f.ops).unwrap_or_default()
        });
        let ops: Vec<AssetOp> = ops.chain(m.operations.file_ops()).collect();
        for op in &ops {
            match op {
                AssetOp::Db(_) => c.database += 1,
                AssetOp::Erp(_) => c.resources += 1,
                AssetOp::Lang(_) => c.text += 1,
                AssetOp::File(_) => c.files += 1,
                AssetOp::Track(_) => c.tracks += 1,
                AssetOp::Group(_) | AssetOp::Item(_) | AssetOp::Warn { .. } => {}
            }
        }
        if item.track && c.tracks == 0 {
            c.tracks = 1;
        }
        c
    }

    /// Each non-zero count as "3 database values", in a fixed order.
    pub fn parts(&self) -> Vec<String> {
        [
            (self.database, "database value", "database values"),
            (self.resources, "archive resource", "archive resources"),
            (self.text, "text entry", "text entries"),
            (self.files, "file", "files"),
            (self.tracks, "track", "tracks"),
            (self.screens, "UI screen", "UI screens"),
            (self.plugins, "code plugin", "code plugins"),
        ]
        .into_iter()
        .filter(|(n, ..)| *n > 0)
        .map(|(n, one, many)| plural(n, one, many))
        .collect()
    }
}

#[derive(Clone)]
pub enum AddonIcon {
    Glyph(Arc<[u8]>),
    Image(Arc<Image>),
}

fn addon_icon(store: &Store, registry: &Registry, m: &AddonManifest) -> Option<AddonIcon> {
    let (bytes, format) = match registry.get(&m.id).and_then(|e| e.addon.icon()) {
        Some(bytes) if bytes.starts_with(b"\x89PNG") => (bytes.to_vec(), ImageFormat::Png),
        Some(bytes) => (bytes.to_vec(), ImageFormat::Svg),
        None => {
            let path = store.addons_dir().join(&m.id).join(m.icon.as_ref()?);
            let format = match path.extension()?.to_str()?.to_ascii_lowercase().as_str() {
                "svg" => ImageFormat::Svg,
                "png" => ImageFormat::Png,
                "jpg" | "jpeg" => ImageFormat::Jpeg,
                "webp" => ImageFormat::Webp,
                _ => return None,
            };
            (std::fs::read(path).ok()?, format)
        }
    };
    Some(match format {
        ImageFormat::Svg => AddonIcon::Glyph(bytes.into()),
        _ => AddonIcon::Image(Arc::new(Image::from_bytes(format, bytes))),
    })
}

pub const TRACKS_SECTION: &str = "tracks";

pub struct SectionItem {
    pub addon: String,
    pub spec: SectionSpec,
    /// `sections/<id>.svg` in the addon's folder, shown in the sidebar in place of the addon's icon.
    pub icon: Option<AddonIcon>,
}

fn section_icon(store: &Store, addon: &str, section: &str) -> Option<AddonIcon> {
    let path = store
        .addons_dir()
        .join(addon)
        .join("sections")
        .join(format!("{section}.svg"));
    Some(AddonIcon::Glyph(std::fs::read(path).ok()?.into()))
}

pub struct ProfileItem {
    pub name: String,
    pub addons: Vec<String>,
    pub summaries: Vec<Summary>,
}

impl ProfileItem {
    pub fn vanilla(&self) -> bool {
        self.name == VANILLA
    }

    pub fn summary(&self) -> String {
        if self.vanilla() {
            return "No addons".into();
        }
        plural(self.addons.len(), "addon", "addons")
    }
}

pub struct Library {
    pub profile: Profile,
    pub rebuild: Vec<String>,
    pub addons: Vec<AddonItem>,
    pub profiles: Vec<ProfileItem>,
    pub sections: Vec<SectionItem>,
    pub registry: Registry,
    /// The section an enabled addon offers with id `tracks`, which lists
    /// track packages and track addons together.
    pub tracks_page: Option<usize>,
    pub error: Option<String>,
    pub loaded: Instant,
}

impl Library {
    pub fn load(store: &Store) -> Self {
        let mut error = None;
        let mut note = |e: swofty_core::Error| error = Some(e.to_string());
        let profile = store.profile(&store.active_profile()).unwrap_or_else(|e| {
            note(e);
            Profile::vanilla()
        });
        let manifests = store.addons().unwrap_or_else(|e| {
            note(e);
            Default::default()
        });
        let registry = store.registry().unwrap_or_else(|e| {
            note(e);
            Registry::default()
        });
        let enabled: Vec<&str> = profile.enabled().collect();
        let mut addons: Vec<AddonItem> = manifests
            .values()
            .map(|m| {
                let package = !m.is_builtin() && swofty_core::dbedit::package::is_package(store, &m.id);
                AddonItem {
                    enabled: enabled.contains(&m.id.as_str()),
                    locked: registry.get(&m.id).is_some_and(|e| e.locked),
                    icon: addon_icon(store, &registry, m),
                    track: !m.is_builtin()
                        && swofty_core::tracks::is_track_addon(&store.addons_dir().join(&m.id), m),
                    package,
                    source: Source::of(store, m, package),
                    manifest: m.clone(),
                }
            })
            .collect();
        addons.sort_by_key(|m| {
            let declared = registry.get(&m.manifest.id).is_none_or(|e| e.declared);
            (!m.manifest.is_builtin(), declared, m.manifest.name.to_lowercase())
        });
        let profiles = store
            .profile_names()
            .unwrap_or_default()
            .into_iter()
            .map(|name| {
                let p = store.profile(&name).unwrap_or_else(|_| Profile::new(&name));
                ProfileItem {
                    addons: p
                        .enabled()
                        .filter_map(|id| manifests.get(id))
                        .map(|m| m.name.clone())
                        .collect(),
                    summaries: summaries(store, &registry, &p),
                    name,
                }
            })
            .collect();
        let sections: Vec<SectionItem> = registry
            .iter()
            .filter(|e| !e.declared && enabled.contains(&e.id()))
            .flat_map(|e| {
                e.addon.sections().into_iter().map(|spec| SectionItem {
                    addon: e.id().to_owned(),
                    icon: section_icon(store, e.id(), &spec.id),
                    spec,
                })
            })
            .collect();
        let tracks_page = sections.iter().position(|s| s.spec.id == TRACKS_SECTION);
        Self {
            tracks_page,
            rebuild: profile.rebuild_pending(&manifests),
            profile,
            addons,
            profiles,
            sections,
            registry,
            error,
            loaded: Instant::now(),
        }
    }

    /// The addons the Addons page lists: track addons are on the Tracks page
    /// while one is open in the profile.
    pub fn listed(&self) -> impl Iterator<Item = &AddonItem> {
        let tracks = self.tracks_page.is_some();
        self.addons.iter().filter(move |m| !(m.track && tracks))
    }

    pub fn active(&self) -> Option<&ProfileItem> {
        self.profiles.iter().find(|p| p.name == self.profile.name)
    }
}

fn summaries(store: &Store, registry: &Registry, profile: &Profile) -> Vec<Summary> {
    if profile.is_vanilla() {
        return Vec::new();
    }
    registry
        .iter()
        .filter(|e| !e.declared)
        .filter_map(|e| {
            let host = Host::new(store, e.id(), profile, None).ok()?;
            e.addon.summary(&host.cx())
        })
        .collect()
}

pub fn plural(n: usize, one: &str, many: &str) -> String {
    format!("{n} {}", if n == 1 { one } else { many })
}
