use std::path::Path;

const NAME: &str = "Swofty Launcher";

#[cfg(windows)]
pub fn create(root: &Path, exe: &Path, desktop: bool) {
    let link = |dir: &Path| -> Result<(), String> {
        std::fs::create_dir_all(dir).map_err(|e| e.to_string())?;
        let mut link = mslnk::ShellLink::new(exe).map_err(|e| e.to_string())?;
        link.set_working_dir(Some(root.display().to_string()));
        link.create_lnk(dir.join(format!("{NAME}.lnk")))
            .map_err(|e| e.to_string())
    };
    if let Some(data) = dirs::data_dir() {
        link(&data.join(r"Microsoft\Windows\Start Menu\Programs")).ok();
    }
    if desktop && let Some(dir) = dirs::desktop_dir() {
        link(&dir).ok();
    }
}

#[cfg(not(windows))]
pub fn create(root: &Path, exe: &Path, desktop: bool) {
    use std::os::unix::fs::PermissionsExt;

    let icon = root.join("share/icons/hicolor/256x256/apps/net.swofty.ModLauncher.png");
    let icon = if icon.is_file() {
        icon.display().to_string()
    } else {
        "net.swofty.ModLauncher".into()
    };
    let entry = format!(
        "[Desktop Entry]\nType=Application\nName={NAME} for F1 25\nComment=Mods and tracks for F1 25\n\
         Exec={}\nPath={}\nIcon={icon}\nTerminal=false\nCategories=Game;\nStartupWMClass=net.swofty.ModLauncher\n",
        quote(&exe.display().to_string()),
        root.display(),
    );
    let write = |dir: &Path| -> std::io::Result<()> {
        std::fs::create_dir_all(dir)?;
        let file = dir.join("net.swofty.ModLauncher.desktop");
        std::fs::write(&file, &entry)?;
        std::fs::set_permissions(&file, std::fs::Permissions::from_mode(0o755))
    };
    if let Some(data) = dirs::data_dir() {
        write(&data.join("applications")).ok();
    }
    let fallback = || dirs::home_dir().map(|h| h.join("Desktop")).filter(|d| d.is_dir());
    if desktop && let Some(dir) = dirs::desktop_dir().or_else(fallback) {
        write(&dir).ok();
    }
}

#[cfg(not(windows))]
fn quote(arg: &str) -> String {
    let mut out = String::from("\"");
    for c in arg.chars() {
        match c {
            '"' | '`' | '$' | '\\' => {
                out.push('\\');
                out.push(c);
            }
            '%' => out.push_str("%%"),
            c => out.push(c),
        }
    }
    out.push('"');
    out
}
