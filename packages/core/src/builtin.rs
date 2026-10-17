use crate::manifest::ModManifest;

pub const CORE_ID: &str = "swofty.core";
pub const CORE_MANIFEST: &str = include_str!("../../../mods/swoftymod/swofty.json");

pub fn core_manifest() -> ModManifest {
    ModManifest::parse(CORE_MANIFEST).expect("the built-in manifest is valid")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn builtin_manifest_is_valid() {
        let m = core_manifest();
        assert_eq!(m.id, CORE_ID);
        assert!(m.is_builtin());
    }
}
