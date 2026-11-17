// Resources: res\mixlauncher.rc (icon, manifest, version info).
fn main() {
    let rc = "res/mixlauncher.rc";
    println!("cargo:rerun-if-changed={rc}");
    println!("cargo:rerun-if-changed=res/version.h");
    println!("cargo:rerun-if-changed=res/mixlauncher.manifest");
    println!("cargo:rerun-if-changed=res/mixlauncher.dev.manifest");
    // The version lives only in res/version.h: handed to the code as ML_VER_* (base.rs)
    let vh = std::fs::read_to_string("res/version.h").expect("res/version.h");
    for l in vh.lines() {
        let mut it = l.split_whitespace();
        if let (Some("#define"), Some(key), Some(val)) = (it.next(), it.next(), it.next()) {
            if key.starts_with("ML_VER_") {
                println!("cargo:rustc-env={key}={}", val.trim_matches('"'));
            }
        }
    }
    let dev = std::env::var_os("CARGO_FEATURE_DEV").is_some();
    let defs: &[&str] = if dev { &["ML_DEV_BUILD"] } else { &[] };
    embed_resource::compile(rc, defs).manifest_required().unwrap();
    // The C runtime: vcruntime linked in, the UCRT from Windows (ucrtbase.dll, part of Windows 10)
    println!("cargo:rustc-link-arg-bins=/NODEFAULTLIB:libucrt.lib");
    println!("cargo:rustc-link-arg-bins=ucrt.lib");
    println!("cargo:rustc-link-arg-bins=/SUBSYSTEM:WINDOWS");
    println!("cargo:rustc-link-arg-bins=/OPT:REF");
    println!("cargo:rustc-link-arg-bins=/OPT:ICF");
    for lib in ["user32", "gdi32", "shell32", "ole32", "oleaut32", "shlwapi", "advapi32", "dwmapi", "d3d11", "dxgi", "uuid", "winhttp"] {
        println!("cargo:rustc-link-lib={lib}");
    }
}
