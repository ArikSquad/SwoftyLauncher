use std::collections::{BTreeMap, BTreeSet, HashMap};

use driver_manager::model::Driver;
use driver_manager::ops;
use serde_json::{Value, json};
use swofty_addon::asset::{Features, Kind, Op, Origin};
use swofty_addon::helper::Built;
use swofty_addon::manifest::AddonManifest;
use swofty_core::planner::Planner;
use swofty_core::profile::Profile;
use swofty_formats::gvdb::{Gvdb, Record};
use swofty_formats::hash::fnv1a64;

const DM: &str = "swofty.driver-manager";
const MOD: &str = "mod.old-team";

struct Rows(Vec<Record>);

impl Rows {
    fn put(&mut self, path: &str, tag: u8, value: &str) {
        self.0.push(Record::new(path, tag, value));
    }

    fn row(&mut self, table: &str, row: &str, i: usize, label: &str, set: &[(&str, &str)]) {
        let fields = ops::fields();
        let key = format!("{table}.{row}");
        let names = fields
            .as_object()
            .unwrap()
            .iter()
            .find(|(k, _)| {
                **k == key || k.ends_with(&format!("].{row}")) && key.ends_with(&format!(".{row}"))
            })
            .map(|(_, v)| v.as_array().unwrap().clone())
            .unwrap();
        for name in names {
            let name = name.as_str().unwrap();
            let (tag, value) = match set.iter().find(|(k, _)| *k == name) {
                Some((_, v)) => (4, (*v).to_owned()),
                None if name == "_metadata_" => (4, label.to_owned()),
                None if name.ends_with("_instancecount_") => (0, "0".to_owned()),
                None => (2, String::new()),
            };
            self.put(&format!("{table}.{row}_{i}.{name}"), tag, &value);
        }
    }

    fn count(&mut self, table: &str, row: &str, n: usize) {
        self.put(&format!("{table}._{row}_instancecount_"), 0, &n.to_string());
    }
}

const LOOK_TABLE: &str = "entrants.driver_customisations.folder_0";
const LISTS: &str = "entrants.entrant_lists";

fn database(lists: &[&str], new_rows: bool) -> Gvdb {
    let mut r = Rows(Vec::new());
    let extra = usize::from(new_rows);
    r.count("entrants.drivers", "driver", 1 + extra);
    r.row("entrants.drivers", "driver", 0, "jarno_opmeer", &[]);
    r.count("entrants.driver_customisations", "folder", 1);
    r.put(&format!("{LOOK_TABLE}._metadata_"), 4, "ai_driver_customisations");
    r.count(LOOK_TABLE, "preloaded_driver_customisation_properties", 1);
    r.row(
        LOOK_TABLE,
        "preloaded_driver_customisation_properties",
        0,
        "official_driver_customisation_for_driver_pool",
        &[],
    );
    r.count(
        "driver_statistics.driver_statistics",
        "DriverStatisticsHolder",
        1 + extra,
    );
    r.row(
        "driver_statistics.driver_statistics",
        "DriverStatisticsHolder",
        0,
        "jarno_opmeer",
        &[("driver", "jarno_opmeer")],
    );
    let entrants = format!("{LISTS}.entrants");
    r.count(&entrants, "entrant", 1 + extra);
    r.row(
        &entrants,
        "entrant",
        0,
        "jarno_opmeer",
        &[("driver", "jarno_opmeer")],
    );
    if new_rows {
        r.row("entrants.drivers", "driver", 1, "new_rookie", &[]);
        r.row(
            "driver_statistics.driver_statistics",
            "DriverStatisticsHolder",
            1,
            "new_rookie",
            &[("driver", "new_rookie")],
        );
        r.row(&entrants, "entrant", 1, "new_rookie", &[("driver", "new_rookie")]);
    }
    r.count(LISTS, "entrant_list", lists.len());
    for (i, label) in lists.iter().enumerate() {
        let list = format!("{LISTS}.entrant_list_{i}");
        r.put(&format!("{list}._metadata_"), 4, label);
        let (row, reference) = if label.ends_with("_driver_pool") {
            ("opmeer", "jarno_opmeer")
        } else {
            ("seat_1", "jarno_opmeer")
        };
        r.count(&list, "entrant", 1);
        r.row(&list, "entrant", 0, row, &[("entrant_reference", reference)]);
    }
    Gvdb { records: r.0 }
}

const NEW_LISTS: [&str; 4] = [
    "f1_career_2026_driver_pool",
    "f1_2025_driver_pool",
    "f1_career_2026",
    "f1_2026_driver_pool",
];
const OLD_LISTS: [&str; 3] = ["f1_2025_driver_pool", "f1_career_2026", "f1_2026_driver_pool"];

fn snapshot_mod(stock: &Gvdb, modded: &Gvdb) -> Vec<Value> {
    let before: HashMap<u64, &Record> = stock.records.iter().map(|r| (r.key, r)).collect();
    modded
        .records
        .iter()
        .filter(|r| {
            before
                .get(&r.key)
                .is_none_or(|s| s.value != r.value || s.tag != r.tag)
        })
        .map(|r| {
            let hex: String = r.value.iter().map(|b| format!("{b:02x}")).collect();
            json!({ "op": "db.put-record", "key": format!("0x{:016x}", r.key), "type": r.tag, "value": hex })
        })
        .collect()
}

fn driver() -> Driver {
    let mut d = Driver::blank(None, 21);
    d.first = "Jacob".into();
    d.last = "Swofty".into();
    d.code = "SWO".into();
    d.stat_id = 208;
    d.preset_id = 366;
    d
}

fn build(order: &[&str]) -> Gvdb {
    let stock = database(&NEW_LISTS, true);
    let mod_ops = snapshot_mod(&stock, &database(&OLD_LISTS, false));
    let reply: Built =
        serde_json::from_value(json!({ "ops": ops::driver_ops(&driver()), "fields": ops::fields() }))
            .unwrap();
    let mod_reply: Built = serde_json::from_value(json!({ "ops": mod_ops })).unwrap();
    let features = Features {
        stable: BTreeSet::from([Kind::Db, Kind::Lang]),
        experimental: BTreeSet::new(),
    };
    let opt_in = BTreeSet::new();
    let mut planner = Planner::new(&features, &opt_in);
    planner.add_fields(DM, reply.fields.clone());
    for id in order {
        let ops = if *id == DM { &reply.ops } else { &mod_reply.ops };
        for op in ops {
            planner.push(Op {
                op: op.clone(),
                origin: Origin {
                    addon: (*id).into(),
                    item: None,
                },
            });
        }
    }
    let mut db = stock;
    planner.apply_db(&mut db).unwrap();
    db
}

struct Read(HashMap<u64, String>);

impl Read {
    fn new(db: &Gvdb) -> Read {
        Read(
            db.records
                .iter()
                .map(|r| (r.key, r.text().into_owned()))
                .collect(),
        )
    }

    fn text(&self, path: &str) -> String {
        self.0.get(&fnv1a64(path)).cloned().unwrap_or_default()
    }

    fn rows(&self, table: &str, row: &str) -> Vec<String> {
        let n: usize = self
            .text(&format!("{table}._{row}_instancecount_"))
            .parse()
            .unwrap_or(0);
        (0..n).map(|i| format!("{table}.{row}_{i}")).collect()
    }

    fn find(&self, table: &str, row: &str, label: &str) -> Option<String> {
        self.rows(table, row)
            .into_iter()
            .find(|r| self.text(&format!("{r}._metadata_")) == label)
    }

    fn dangling(&self) -> Vec<String> {
        let mut out = Vec::new();
        let entrants = format!("{LISTS}.entrants");
        for list in self.rows(LISTS, "entrant_list") {
            for e in self.rows(&list, "entrant") {
                let r = self.text(&format!("{e}.entrant_reference"));
                if !r.is_empty() && self.find(&entrants, "entrant", &r).is_none() {
                    out.push(format!("{e} -> entrant {r}"));
                }
            }
        }
        for e in self.rows(&entrants, "entrant") {
            let d = self.text(&format!("{e}.driver"));
            if !d.is_empty() && self.find("entrants.drivers", "driver", &d).is_none() {
                out.push(format!("{e} -> driver {d}"));
            }
            let look = self.text(&format!("{e}.driver_customisation"));
            if !look.is_empty()
                && self
                    .find(LOOK_TABLE, "preloaded_driver_customisation_properties", &look)
                    .is_none()
            {
                out.push(format!("{e} -> look {look}"));
            }
        }
        out
    }

    fn pool(&self, label: &str) -> Vec<String> {
        let list = self.find(LISTS, "entrant_list", label).unwrap();
        self.rows(&list, "entrant")
            .iter()
            .map(|e| self.text(&format!("{e}.entrant_reference")))
            .collect()
    }
}

#[test]
fn an_older_team_mod_built_after_the_drivers_strands_the_pool_row() {
    let db = Read::new(&build(&[DM, MOD]));
    assert_eq!(
        db.dangling(),
        vec![format!("{LISTS}.entrant_list_1.entrant_1 -> entrant dm_208")]
    );
    assert_eq!(
        db.text(&format!("{LISTS}.entrant_list_1._metadata_")),
        "f1_career_2026"
    );
    assert_eq!(db.pool("f1_2025_driver_pool"), vec!["jarno_opmeer"]);
}

#[test]
fn drivers_built_after_an_older_team_mod_resolve_every_reference() {
    let db = Read::new(&build(&[MOD, DM]));
    assert_eq!(db.dangling(), Vec::<String>::new());
    assert_eq!(db.pool("f1_2025_driver_pool"), vec!["jarno_opmeer", "dm_208"]);
    assert_eq!(db.pool("f1_2026_driver_pool"), vec!["jarno_opmeer", "dm_208"]);
    assert_eq!(db.pool("f1_career_2026"), vec!["jarno_opmeer"]);
    let entrant = db
        .find(&format!("{LISTS}.entrants"), "entrant", "dm_208")
        .unwrap();
    assert_eq!(entrant, format!("{LISTS}.entrants.entrant_1"));
    assert_eq!(db.text(&format!("{entrant}.driver_customisation")), "dm_208_look");
    assert!(db.find("entrants.drivers", "driver", "dm_208").is_some());
    assert!(db.find("entrants.drivers", "driver", "new_rookie").is_none());
}

#[test]
fn without_mods_the_drivers_land_where_they_always_did() {
    let db = Read::new(&build(&[DM]));
    assert_eq!(db.dangling(), Vec::<String>::new());
    assert_eq!(
        db.find(&format!("{LISTS}.entrants"), "entrant", "dm_208")
            .unwrap(),
        format!("{LISTS}.entrants.entrant_2")
    );
    assert_eq!(db.pool("f1_2025_driver_pool"), vec!["jarno_opmeer", "dm_208"]);
    assert_eq!(db.pool("f1_career_2026_driver_pool"), vec!["jarno_opmeer"]);
}

fn manifest(id: &str, extra: &str) -> AddonManifest {
    AddonManifest::parse(&format!(
        r#"{{"schema_version":1,"id":"{id}","name":"{id}","version":"1.0.0","author":"t",
        "compat":{{"game":"f1_25"}}{extra}}}"#
    ))
    .unwrap()
}

#[test]
fn driver_manager_builds_after_every_other_addon() {
    let ours = AddonManifest::parse(include_str!("../../swofty.json")).unwrap();
    let mut installed = BTreeMap::new();
    for m in [
        manifest(swofty_core::builtin::CORE_ID, ""),
        manifest(MOD, ""),
        manifest("other.livery", ""),
        ours,
    ] {
        installed.insert(m.id.clone(), m);
    }
    let mut profile = Profile::new("t");
    for id in [swofty_core::builtin::CORE_ID, DM, MOD, "other.livery"] {
        profile.set_enabled(id, true).unwrap();
    }
    let plan = swofty_core::resolver::resolve(&profile, &installed).unwrap();
    assert_eq!(plan.order.last().map(String::as_str), Some(DM));
}
