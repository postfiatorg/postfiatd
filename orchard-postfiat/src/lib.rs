//! Orchard Privacy Integration for PostFiat
//!
//! This crate provides Orchard/Halo2 zero-knowledge proof functionality
//! for PostFiat's privacy features. It bridges Rust cryptographic operations
//! with the C++ PostFiat codebase.

mod bundle;
pub mod ffi;

pub use bundle::OrchardBundle;

// Re-export types for convenience
pub use orchard;
pub use halo2_proofs;
