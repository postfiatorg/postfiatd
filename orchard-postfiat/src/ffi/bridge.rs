//! CXX Bridge definitions for Orchard operations

use crate::bundle_real::OrchardBundle;

/// Batch verifier for multiple Orchard bundles
pub struct OrchardBatchVerifier {
    // Placeholder for now
    bundles: Vec<(Box<OrchardBundle>, [u8; 32])>,
}

impl OrchardBatchVerifier {
    pub fn new() -> Self {
        Self {
            bundles: Vec::new(),
        }
    }

    pub fn add(&mut self, bundle: Box<OrchardBundle>, sighash: [u8; 32]) {
        self.bundles.push((bundle, sighash));
    }

    pub fn verify(self) -> bool {
        // Stub implementation - always return true for now
        // TODO: Implement actual batch verification
        true
    }
}

#[cxx::bridge]
pub mod ffi {
    /// Error type for Orchard operations
    #[derive(Debug)]
    pub struct OrchardError {
        pub message: String,
    }

    // Opaque Rust types exposed to C++
    extern "Rust" {
        type OrchardBundle;
        type OrchardBatchVerifier;

        // Bundle parsing and serialization
        fn orchard_bundle_parse(data: &[u8]) -> Result<Box<OrchardBundle>>;
        fn orchard_bundle_serialize(bundle: &OrchardBundle) -> Vec<u8>;
        fn orchard_bundle_box_clone(bundle: &OrchardBundle) -> Box<OrchardBundle>;

        // Bundle validation
        fn orchard_bundle_is_present(bundle: &OrchardBundle) -> bool;
        fn orchard_bundle_is_valid(bundle: &OrchardBundle) -> bool;

        // Bundle properties
        fn orchard_bundle_get_value_balance(bundle: &OrchardBundle) -> i64;
        fn orchard_bundle_get_anchor(bundle: &OrchardBundle) -> [u8; 32];
        fn orchard_bundle_get_nullifiers(bundle: &OrchardBundle) -> Vec<u8>;
        fn orchard_bundle_get_note_commitments(bundle: &OrchardBundle) -> Vec<u8>;
        fn orchard_bundle_get_encrypted_notes(bundle: &OrchardBundle) -> Vec<u8>;
        fn orchard_bundle_num_actions(bundle: &OrchardBundle) -> usize;

        // Proof verification
        fn orchard_verify_bundle_proof(
            bundle: &OrchardBundle,
            sighash: &[u8; 32]
        ) -> bool;

        // Batch verification
        fn orchard_batch_verify_init() -> Box<OrchardBatchVerifier>;
        fn orchard_batch_verify_add(
            verifier: &mut OrchardBatchVerifier,
            bundle: Box<OrchardBundle>,
            sighash: [u8; 32]
        );
        fn orchard_batch_verify_finalize(verifier: Box<OrchardBatchVerifier>) -> bool;

        // Bundle building (for testing)
        // WARNING: These are for TESTING only! Production use should have proper key management
        fn orchard_test_get_empty_anchor() -> [u8; 32];
        fn orchard_test_generate_spending_key(seed_byte: u8) -> Vec<u8>;
        fn orchard_test_get_address_from_sk(sk_bytes: &[u8]) -> Result<Vec<u8>>;
        fn orchard_test_build_transparent_to_shielded(
            amount_drops: u64,
            recipient_addr_bytes: &[u8],
            anchor: &[u8; 32]
        ) -> Result<Vec<u8>>;

        // Viewing key operations (for testing)
        fn orchard_test_get_full_viewing_key(sk_bytes: &[u8]) -> Result<Vec<u8>>;
        fn orchard_test_try_decrypt_note(
            bundle: &OrchardBundle,
            action_index: usize,
            fvk_bytes: &[u8]
        ) -> Result<u64>;
        fn orchard_test_try_decrypt_note_from_ciphertext(
            encrypted_note: &[u8],
            cmx_bytes: &[u8; 32],
            ephemeral_key_bytes: &[u8; 32],
            fvk_bytes: &[u8]
        ) -> Result<u64>;
    }
}

// Implementation of FFI functions

/// Parse an Orchard bundle from serialized bytes
pub fn orchard_bundle_parse(data: &[u8]) -> anyhow::Result<Box<OrchardBundle>> {
    OrchardBundle::parse(data)
        .map(Box::new)
        .map_err(|e| anyhow::anyhow!("Failed to parse Orchard bundle: {}", e))
}

/// Serialize an Orchard bundle to bytes
pub fn orchard_bundle_serialize(bundle: &OrchardBundle) -> Vec<u8> {
    bundle.serialize()
}

/// Clone an Orchard bundle
pub fn orchard_bundle_box_clone(bundle: &OrchardBundle) -> Box<OrchardBundle> {
    Box::new(bundle.clone())
}

/// Check if the bundle is present (not empty)
pub fn orchard_bundle_is_present(bundle: &OrchardBundle) -> bool {
    bundle.is_present()
}

/// Check if the bundle structure is valid
pub fn orchard_bundle_is_valid(bundle: &OrchardBundle) -> bool {
    bundle.is_valid()
}

/// Get the value balance (net flow in/out of shielded pool)
/// Positive = net outflow (z->t), Negative = net inflow (t->z)
pub fn orchard_bundle_get_value_balance(bundle: &OrchardBundle) -> i64 {
    bundle.value_balance()
}

/// Get the anchor (Merkle tree root) for this bundle
pub fn orchard_bundle_get_anchor(bundle: &OrchardBundle) -> [u8; 32] {
    bundle.anchor()
}

/// Get all nullifiers from the bundle (for double-spend checking)
/// Returns a flattened Vec<u8> with 32 bytes per nullifier
pub fn orchard_bundle_get_nullifiers(bundle: &OrchardBundle) -> Vec<u8> {
    bundle.nullifiers()
        .into_iter()
        .flat_map(|n| n.into_iter())
        .collect()
}

/// Get all note commitments from the bundle (for Merkle tree)
/// Returns a flattened Vec<u8> with 32 bytes per commitment
pub fn orchard_bundle_get_note_commitments(bundle: &OrchardBundle) -> Vec<u8> {
    bundle.note_commitments()
        .into_iter()
        .flat_map(|c| c.into_iter())
        .collect()
}

/// Get encrypted note data from the bundle
/// Returns flattened data: for each note (32 bytes cmx + 32 bytes epk + 580 bytes ciphertext)
/// Total: 644 bytes per note
pub fn orchard_bundle_get_encrypted_notes(bundle: &OrchardBundle) -> Vec<u8> {
    bundle.encrypted_notes()
        .into_iter()
        .flat_map(|(cmx, epk, ciphertext)| {
            cmx.into_iter()
                .chain(epk.into_iter())
                .chain(ciphertext.into_iter())
        })
        .collect()
}

/// Get the number of actions in this bundle
pub fn orchard_bundle_num_actions(bundle: &OrchardBundle) -> usize {
    bundle.num_actions()
}

/// Verify the Halo2 proof for this bundle
pub fn orchard_verify_bundle_proof(
    bundle: &OrchardBundle,
    sighash: &[u8; 32],
) -> bool {
    bundle.verify_proof(sighash)
}

/// Initialize a new batch verifier
pub fn orchard_batch_verify_init() -> Box<OrchardBatchVerifier> {
    Box::new(OrchardBatchVerifier::new())
}

/// Add a bundle to the batch verifier
pub fn orchard_batch_verify_add(
    verifier: &mut OrchardBatchVerifier,
    bundle: Box<OrchardBundle>,
    sighash: [u8; 32],
) {
    verifier.add(bundle, sighash);
}

/// Finalize and verify all bundles in the batch
pub fn orchard_batch_verify_finalize(verifier: Box<OrchardBatchVerifier>) -> bool {
    verifier.verify()
}

//------------------------------------------------------------------------------
// Bundle Building Functions (for Testing)
//------------------------------------------------------------------------------

/// Get the empty anchor (for first transactions when tree is empty)
pub fn orchard_test_get_empty_anchor() -> [u8; 32] {
    crate::bundle_builder::get_empty_anchor().to_bytes()
}

/// Generate a deterministic spending key for testing
/// WARNING: Uses a fixed seed! Only for testing!
pub fn orchard_test_generate_spending_key(seed_byte: u8) -> Vec<u8> {
    let sk = crate::bundle_builder::generate_test_spending_key(seed_byte);
    sk.to_bytes().to_vec()
}

/// Get an Orchard address from a spending key
pub fn orchard_test_get_address_from_sk(sk_bytes: &[u8]) -> anyhow::Result<Vec<u8>> {
    use orchard::keys::SpendingKey;

    let sk_array: [u8; 32] = sk_bytes
        .try_into()
        .map_err(|_| anyhow::anyhow!("Invalid spending key length, expected 32 bytes"))?;

    let sk = Option::from(SpendingKey::from_bytes(sk_array))
        .ok_or_else(|| anyhow::anyhow!("Invalid spending key"))?;

    let addr = crate::bundle_builder::get_address_from_sk(&sk, 0);
    Ok(addr.to_raw_address_bytes().to_vec())
}

/// Build a transparent-to-shielded (t→z) bundle for testing
///
/// This generates a REAL Orchard bundle with valid proofs.
/// WARNING: This is EXPENSIVE - takes ~5-10 seconds for proof generation!
///
/// # Arguments
/// * `amount_drops` - Amount in drops (1 XRP = 1,000,000 drops)
/// * `recipient_addr_bytes` - Raw Orchard address bytes (43 bytes)
/// * `anchor` - Current Merkle tree root (32 bytes)
///
/// # Returns
/// Serialized bundle bytes ready to include in a transaction
pub fn orchard_test_build_transparent_to_shielded(
    amount_drops: u64,
    recipient_addr_bytes: &[u8],
    anchor: &[u8; 32],
) -> anyhow::Result<Vec<u8>> {
    use orchard::{Address, Anchor};

    // Parse recipient address
    let addr_array: [u8; 43] = recipient_addr_bytes
        .try_into()
        .map_err(|_| anyhow::anyhow!("Invalid address length, expected 43 bytes"))?;

    let recipient = Option::from(Address::from_raw_address_bytes(&addr_array))
        .ok_or_else(|| anyhow::anyhow!("Invalid Orchard address"))?;

    // Parse anchor
    let anchor = Option::from(Anchor::from_bytes(*anchor))
        .ok_or_else(|| anyhow::anyhow!("Invalid anchor"))?;

    // Build the bundle
    crate::bundle_builder::build_transparent_to_shielded(amount_drops, recipient, anchor)
        .map_err(|e| anyhow::anyhow!("Failed to build bundle: {}", e))
}

/// Derive a full viewing key from a spending key
///
/// # Arguments
/// * `sk_bytes` - Spending key bytes (32 bytes)
///
/// # Returns
/// Full viewing key bytes (96 bytes)
pub fn orchard_test_get_full_viewing_key(sk_bytes: &[u8]) -> anyhow::Result<Vec<u8>> {
    use orchard::keys::SpendingKey;

    let sk_array: [u8; 32] = sk_bytes
        .try_into()
        .map_err(|_| anyhow::anyhow!("Invalid spending key length, expected 32 bytes"))?;

    let sk = Option::from(SpendingKey::from_bytes(sk_array))
        .ok_or_else(|| anyhow::anyhow!("Invalid spending key"))?;

    let fvk = crate::bundle_builder::get_full_viewing_key_from_sk(&sk);
    Ok(fvk.to_bytes().to_vec())
}

/// Try to decrypt a note from a bundle action using a full viewing key
///
/// # Arguments
/// * `bundle` - The Orchard bundle containing the note
/// * `action_index` - Index of the action to decrypt (0-based)
/// * `fvk_bytes` - Full viewing key bytes (96 bytes)
///
/// # Returns
/// Note value in drops if decryption succeeds
pub fn orchard_test_try_decrypt_note(
    bundle: &OrchardBundle,
    action_index: usize,
    fvk_bytes: &[u8],
) -> anyhow::Result<u64> {
    use orchard::keys::FullViewingKey;

    let fvk_array: [u8; 96] = fvk_bytes
        .try_into()
        .map_err(|_| anyhow::anyhow!("Invalid full viewing key length, expected 96 bytes"))?;

    let fvk = Option::from(FullViewingKey::from_bytes(&fvk_array))
        .ok_or_else(|| anyhow::anyhow!("Invalid full viewing key"))?;

    // Get the inner bundle
    let inner = bundle.inner()
        .ok_or_else(|| anyhow::anyhow!("Bundle is empty"))?;

    // Try to decrypt the note
    crate::bundle_builder::try_decrypt_note(inner, action_index, &fvk)
        .ok_or_else(|| anyhow::anyhow!("Failed to decrypt note - may not be for this viewing key"))
}

/// Try to decrypt a note from raw encrypted ciphertext
///
/// This is used to decrypt notes retrieved from ledger state.
///
/// # Arguments
/// * `encrypted_note` - The 580-byte encrypted note ciphertext
/// * `cmx_bytes` - The 32-byte note commitment
/// * `ephemeral_key_bytes` - The 32-byte ephemeral public key
/// * `fvk_bytes` - Full viewing key bytes (96 bytes)
///
/// # Returns
/// Note value in drops if decryption succeeds
pub fn orchard_test_try_decrypt_note_from_ciphertext(
    encrypted_note: &[u8],
    cmx_bytes: &[u8; 32],
    ephemeral_key_bytes: &[u8; 32],
    fvk_bytes: &[u8],
) -> anyhow::Result<u64> {
    use orchard::keys::FullViewingKey;

    let fvk_array: [u8; 96] = fvk_bytes
        .try_into()
        .map_err(|_| anyhow::anyhow!("Invalid full viewing key length, expected 96 bytes"))?;

    let fvk = Option::from(FullViewingKey::from_bytes(&fvk_array))
        .ok_or_else(|| anyhow::anyhow!("Invalid full viewing key"))?;

    // Try to decrypt
    crate::bundle_builder::try_decrypt_note_from_ciphertext(
        encrypted_note,
        cmx_bytes,
        ephemeral_key_bytes,
        &fvk,
    )
    .ok_or_else(|| anyhow::anyhow!("Failed to decrypt note - may not be for this viewing key"))
}
