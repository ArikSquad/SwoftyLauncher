//! The DB Editor's draft: the changes being edited, bound to a new addon or to
//! an installed DB Editor addon, autosaved to `<data>/dbedit/draft.json`.

use std::collections::{HashMap, HashSet};
use std::path::PathBuf;
use std::time::SystemTime;

use serde::{Deserialize, Serialize};

use super::package::{self, Package};
use super::{Catalog, Edit, Edits};
use crate::manifest::Category;
use crate::store::Store;
use crate::{Error, Result};

pub const FILE: &str = "draft.json";
pub const FORMAT: &str = "swofty-db-draft";

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct Draft {
    #[serde(default)]
    pub format: String,
    #[serde(default)]
    pub format_version: u32,
    /// The DB Editor addon the draft edits; `None` for a new addon.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub addon: Option<String>,
    /// A name for Save as Addon, such as the file an import came from.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub name: Option<String>,
    /// The last author typed, kept across drafts.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub author: Option<String>,
    /// The bound addon's `dbedit.json` time when `base` was read.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub stamp: Option<SystemTime>,
    /// The bound addon's saved edits.
    #[serde(default)]
    pub base: Vec<Edit>,
    #[serde(default)]
    pub edits: Edits,
}

/// What Save as Addon asks for.
#[derive(Debug, Clone, PartialEq)]
pub struct Details {
    pub name: String,
    pub author: String,
    pub category: Category,
    pub description: String,
}

pub fn path(store: &Store) -> PathBuf {
    store.root.join("dbedit").join(FILE)
}

fn same(a: &Edit, b: &Edit) -> bool {
    a.tag == b.tag && a.value == b.value
}

impl Draft {
    /// The saved draft; a missing file is an empty new draft.
    pub fn load(store: &Store) -> Result<Draft> {
        let file = path(store);
        if !file.is_file() {
            return Ok(Draft::default());
        }
        let mut d: Draft = crate::read_json(&file)?;
        if d.format != FORMAT {
            return Err(Error::Other(format!(
                "{}: format {:?} is not {FORMAT}",
                file.display(),
                d.format
            )));
        }
        d.edits.schema_version = 1;
        Ok(d)
    }

    /// Moves a draft file that does not read to `draft.damaged.json`.
    pub fn set_aside(store: &Store) -> Result<PathBuf> {
        let from = path(store);
        let to = from.with_file_name("draft.damaged.json");
        std::fs::rename(&from, &to).map_err(crate::io_err(&from))?;
        Ok(to)
    }

    pub fn save(&mut self, store: &Store) -> Result<()> {
        self.format = FORMAT.into();
        self.format_version = 1;
        self.edits.schema_version = 1;
        crate::write_json_shared(&path(store), self)
    }

    /// The draft of the installed addon `id`, keeping the last author.
    pub fn open(&mut self, store: &Store, id: &str) -> Result<()> {
        let stamp = package::modified(store, id);
        let p = Package::open(store, id)?;
        self.clear();
        self.addon = Some(id.to_owned());
        self.stamp = stamp;
        self.base = p.edits.edits.clone();
        self.edits = p.edits;
        Ok(())
    }

    /// An empty new draft, keeping the last author.
    pub fn clear(&mut self) {
        *self = Draft {
            author: self.author.take(),
            ..Draft::default()
        };
        self.edits.schema_version = 1;
    }

    /// How many keys differ from the bound addon's saved edits.
    pub fn unsaved(&self) -> usize {
        let base: HashMap<u64, &Edit> = self.base.iter().map(|e| (e.key, e)).collect();
        let mine: HashSet<u64> = self.edits.edits.iter().map(|e| e.key).collect();
        let changed = self
            .edits
            .edits
            .iter()
            .filter(|e| base.get(&e.key).is_none_or(|b| !same(b, e)))
            .count();
        changed + base.keys().filter(|k| !mine.contains(k)).count()
    }

    pub fn is_dirty(&self) -> bool {
        self.unsaved() > 0
    }

    /// Puts the edits back to the bound addon's saved ones, or none for a new addon.
    pub fn discard(&mut self) {
        self.edits.edits = self.base.clone();
    }

    /// Takes `base` as the addon's saved edits and keeps this draft's unsaved
    /// changes on top of it.
    pub fn rebase(&mut self, base: Vec<Edit>, stamp: Option<SystemTime>) {
        let old: HashMap<u64, &Edit> = self.base.iter().map(|e| (e.key, e)).collect();
        let mine: HashSet<u64> = self.edits.edits.iter().map(|e| e.key).collect();
        let reverted: HashSet<u64> = old.keys().filter(|k| !mine.contains(k)).copied().collect();
        let changed: Vec<Edit> = self
            .edits
            .edits
            .iter()
            .filter(|e| old.get(&e.key).is_none_or(|b| !same(b, e)))
            .cloned()
            .collect();
        let mut out = base.clone();
        out.retain(|e| !reverted.contains(&e.key));
        for c in changed {
            match out.iter_mut().find(|e| e.key == c.key) {
                Some(slot) => *slot = c,
                None => out.push(c),
            }
        }
        self.base = base;
        self.stamp = stamp;
        self.edits.edits = out;
    }

    /// Writes the draft as a new DB Editor addon and clears the draft.
    pub fn save_as_addon(&mut self, store: &Store, catalog: &Catalog, d: &Details) -> Result<Package> {
        let mut p = Package::create(store, &d.name)?;
        let made = p
            .set_details(&d.name, "1.0.0", &d.author, &d.description)
            .and_then(|()| {
                p.set_category(d.category);
                p.edits = self.edits.clone();
                p.save(store, catalog)
            });
        if let Err(e) = made {
            Package::delete(store, &p.id).ok();
            return Err(e);
        }
        if !d.author.trim().is_empty() {
            self.author = Some(d.author.trim().to_owned());
        }
        self.clear();
        Ok(p)
    }

    /// Writes the draft into its addon, on top of any change saved there since
    /// the draft read it, and clears the draft.
    pub fn save_addon(&mut self, store: &Store, catalog: &Catalog) -> Result<Package> {
        let id = self
            .addon
            .clone()
            .ok_or_else(|| Error::Other("this draft is not an addon yet; use Save as Addon".into()))?;
        let stamp = package::modified(store, &id);
        let mut p = Package::open(store, &id)?;
        if stamp != self.stamp {
            self.rebase(p.edits.edits.clone(), stamp);
        }
        p.edits = self.edits.clone();
        p.save(store, catalog)?;
        self.clear();
        Ok(p)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::dbedit::catalog::tests::sample;
    use swofty_formats::hash::fnv1a64;

    const NUM: &str = "entrants.drivers.driver_0.car_number";
    const NUM1: &str = "entrants.drivers.driver_1.car_number";
    const TEAM: &str = "entrants.teams.team_0.prestige";

    fn store(name: &str) -> Store {
        let root = std::env::temp_dir().join(format!("swofty-dbdraft-{name}-{}", std::process::id()));
        std::fs::remove_dir_all(&root).ok();
        Store::new(root)
    }

    fn details(name: &str) -> Details {
        Details {
            name: name.into(),
            author: "Max".into(),
            category: Category::Seasons,
            description: "Numbers".into(),
        }
    }

    #[test]
    fn saves_a_new_draft_as_an_addon_and_clears_it() {
        let s = store("new");
        let cat = Catalog::load(&sample()).unwrap();
        let mut d = Draft::load(&s).unwrap();
        assert_eq!(d, Draft::default());
        d.edits.set(&cat, fnv1a64(NUM), "33").unwrap();
        assert_eq!(d.unsaved(), 1);
        d.save(&s).unwrap();
        let again = Draft::load(&s).unwrap();
        assert_eq!((again.edits.len(), again.unsaved()), (1, 1));

        let p = d.save_as_addon(&s, &cat, &details("Fast grid")).unwrap();
        assert_eq!(p.id, "dbedit.fast-grid");
        assert_eq!(
            (d.addon.as_deref(), d.edits.len(), d.author.as_deref()),
            (None, 0, Some("Max"))
        );
        let m = &s.addons().unwrap()[&p.id];
        assert_eq!(m.category, Some(Category::Seasons));
        assert_eq!(
            m.origin.as_ref().map(|o| o.tool.as_str()),
            Some(package::ORIGIN_TOOL)
        );
        assert_eq!(
            (m.author.as_str(), m.description.as_deref()),
            ("Max", Some("Numbers"))
        );
        assert_eq!(Package::open(&s, &p.id).unwrap().edits.len(), 1);
        assert!(d.save_addon(&s, &cat).is_err());
        std::fs::remove_dir_all(&s.root).ok();
    }

    #[test]
    fn edits_an_addon_and_keeps_changes_saved_elsewhere() {
        let s = store("bound");
        let cat = Catalog::load(&sample()).unwrap();
        let mut d = Draft::default();
        d.edits.set(&cat, fnv1a64(NUM), "33").unwrap();
        d.edits.set(&cat, fnv1a64(TEAM), "20").unwrap();
        let id = d.save_as_addon(&s, &cat, &details("Grid")).unwrap().id;

        d.open(&s, &id).unwrap();
        assert_eq!(
            (d.addon.as_deref(), d.unsaved(), d.edits.len()),
            (Some(id.as_str()), 0, 2)
        );
        d.edits.set(&cat, fnv1a64(NUM), "44").unwrap();
        d.edits.revert(fnv1a64(TEAM));
        assert_eq!(d.unsaved(), 2);
        d.discard();
        assert_eq!(d.unsaved(), 0);
        d.edits.set(&cat, fnv1a64(NUM), "44").unwrap();
        d.edits.revert(fnv1a64(TEAM));

        std::thread::sleep(std::time::Duration::from_millis(20));
        let mut other = Package::open(&s, &id).unwrap();
        other.edits.set(&cat, fnv1a64(NUM1), "55").unwrap();
        other.edits.set(&cat, fnv1a64(NUM), "66").unwrap();
        other.save(&s, &cat).unwrap();

        d.save_addon(&s, &cat).unwrap();
        assert_eq!(d.addon, None);
        let saved = Package::open(&s, &id).unwrap().edits;
        assert_eq!(saved.value(&cat, fnv1a64(NUM)), Some("44"));
        assert_eq!(saved.value(&cat, fnv1a64(NUM1)), Some("55"));
        assert!(saved.get(fnv1a64(TEAM)).is_none());
        std::fs::remove_dir_all(&s.root).ok();
    }

    #[test]
    fn rebases_unsaved_changes_and_refuses_a_damaged_file() {
        let s = store("rebase");
        let cat = Catalog::load(&sample()).unwrap();
        let mut base = Edits::default();
        base.set(&cat, fnv1a64(NUM), "33").unwrap();
        let mut d = Draft {
            base: base.edits.clone(),
            edits: base.clone(),
            ..Draft::default()
        };
        d.rebase(Vec::new(), None);
        assert_eq!(d.edits.len(), 0);
        let mut newer = base.clone();
        newer.set(&cat, fnv1a64(NUM1), "7").unwrap();
        d.edits.set(&cat, fnv1a64(TEAM), "20").unwrap();
        d.rebase(newer.edits.clone(), None);
        let keys: Vec<u64> = d.edits.edits.iter().map(|e| e.key).collect();
        assert_eq!(keys, [fnv1a64(NUM), fnv1a64(NUM1), fnv1a64(TEAM)]);
        assert_eq!(d.unsaved(), 1);

        std::fs::create_dir_all(path(&s).parent().unwrap()).unwrap();
        std::fs::write(path(&s), b"{\"format\": \"swofty-db-dr").unwrap();
        assert!(Draft::load(&s).is_err());
        let aside = Draft::set_aside(&s).unwrap();
        assert!(aside.is_file() && !path(&s).exists());
        assert_eq!(Draft::load(&s).unwrap(), Draft::default());
        std::fs::remove_dir_all(&s.root).ok();
    }
}
