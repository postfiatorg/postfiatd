//! Foreign Function Interface (FFI) module for C++ integration
//!
//! This module defines the cxx bridge between Rust and C++,
//! exposing Orchard functionality to the PostFiat C++ codebase.

pub mod bridge;

pub use bridge::*;
