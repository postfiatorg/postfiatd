//! Build script for orchard-postfiat
//!
//! This script uses cxx-build to generate C++ bindings from the
//! Rust cxx bridge definitions.

fn main() {
    // Build the cxx bridge
    cxx_build::bridge("src/ffi/bridge.rs")
        .flag_if_supported("-std=c++17")
        .flag_if_supported("-Wall")
        .flag_if_supported("-Wextra")
        .compile("orchard-postfiat");

    // Tell cargo to rerun this build script if the bridge changes
    println!("cargo:rerun-if-changed=src/ffi/bridge.rs");
    println!("cargo:rerun-if-changed=src/bundle.rs");
    println!("cargo:rerun-if-changed=src/lib.rs");
}
