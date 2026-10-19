fn main() {
    println!("cargo:rerun-if-changed=../apps/launcher/assets/icon.ico");
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("windows") {
        let mut res = winresource::WindowsResource::new();
        res.set_icon("../apps/launcher/assets/icon.ico");
        res.compile().expect("failed to embed the installer icon");
    }
}
