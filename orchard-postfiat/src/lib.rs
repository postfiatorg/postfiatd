//! Orchard Privacy Integration for PostFiat
//!
//! This crate provides Orchard/Halo2 zero-knowledge proof functionality
//! for PostFiat's privacy features. It bridges Rust cryptographic operations
//! with the C++ PostFiat codebase.

// Use the real implementation for Phase 3
mod bundle_real;
pub use bundle_real::OrchardBundle;

// Bundle builder for testing and wallet functionality
// Always available for FFI test functions
pub mod bundle_builder;

// Keep old stub for reference (not used)
#[allow(dead_code)]
mod bundle_stub;

pub mod ffi;

// Re-export types for convenience
pub use orchard;
pub use halo2_proofs;
