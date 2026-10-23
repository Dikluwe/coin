use std::{env, path::PathBuf, process::Command};
fn checked(command: &mut Command) {
    let status = command.status().expect("start native build command");
    assert!(status.success(), "native build command failed: {command:?}");
}
fn main() {
    assert_eq!(env::var("CARGO_CFG_TARGET_OS").unwrap(), "linux", "Linux/Xlib only");
    let out = PathBuf::from(env::var_os("OUT_DIR").unwrap());
    let source = PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").unwrap()).join("src/x11_shim.cpp");
    let object = out.join("b95_x11.o");
    checked(Command::new(env::var("CXX").unwrap_or_else(|_| "c++".into()))
        .args(["-std=c++17", "-O2", "-fPIC", "-Wall", "-Wextra", "-c"])
        .arg(&source).arg("-o").arg(&object));
    checked(Command::new(env::var("AR").unwrap_or_else(|_| "ar".into()))
        .arg("crs").arg(out.join("libb95_x11.a")).arg(&object));
    println!("cargo:rustc-link-search=native={}", out.display());
    println!("cargo:rustc-link-lib=static=b95_x11");
    println!("cargo:rustc-link-lib=dylib=X11");
    println!("cargo:rustc-link-lib=dylib=stdc++");
    println!("cargo:rerun-if-changed=src/x11_shim.cpp");
    println!("cargo:rerun-if-env-changed=CXX");
    println!("cargo:rerun-if-env-changed=AR");
}
