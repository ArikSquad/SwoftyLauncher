use std::collections::BTreeMap;
use std::path::PathBuf;

use swofty_core::ops::{DbOps, apply_db_ops};
use swofty_formats::gvdb::Gvdb;

#[test]
fn template_db_ops_resolve_against_the_real_database() {
    let Some(dir) = std::env::var_os("F1_25_DIR").map(PathBuf::from) else {
        eprintln!("F1_25_DIR is not set; skipping");
        return;
    };
    let Ok(data) = std::fs::read(dir.join("game_package/gamedatabases/gamedatabases.2025.bin")) else {
        eprintln!("no gamedatabases.2025.bin under F1_25_DIR; skipping");
        return;
    };
    let mut db = Gvdb::parse(&data).unwrap();
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../templates/mod/db/example.json");
    let ops: DbOps = serde_json::from_slice(&std::fs::read(root).unwrap()).unwrap();
    apply_db_ops(&mut db, &ops, &BTreeMap::<String, Vec<String>>::new()).unwrap();
    assert_eq!(
        db.get_text("rules.stop_go_penalty.penalty_duration").as_deref(),
        Some("5")
    );
    assert_eq!(db.records.len(), Gvdb::parse(&data).unwrap().records.len());
}
