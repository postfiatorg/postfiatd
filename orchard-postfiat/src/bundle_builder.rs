//! Orchard Bundle Builder for Testing
//!
//! This module provides functionality to CREATE Orchard bundles,
//! which is necessary for testing and wallet functionality.
//!
//! WARNING: This is for TESTING only. Production wallets should use
//! more sophisticated key management and note selection.

use orchard::{
    builder::{Builder, BundleType},
    keys::{FullViewingKey, Scope, SpendingKey, PreparedIncomingViewingKey},
    tree::MerkleHashOrchard,
    value::NoteValue,
    Address, Anchor, Bundle,
};
use incrementalmerkletree::Hashable;
use rand::rngs::OsRng;
use zcash_note_encryption::try_note_decryption;
use zcash_protocol::value::ZatBalance;

/// Generate a deterministic spending key for testing
///
/// WARNING: This uses a fixed seed! Only for testing!
pub fn generate_test_spending_key(seed_byte: u8) -> SpendingKey {
    // For testing, use a deterministic key based on seed_byte
    let mut seed = [0u8; 32];
    seed[0] = seed_byte;
    SpendingKey::from_bytes(seed).expect("Valid seed for test key")
}

/// Generate a recipient address from a spending key
pub fn get_address_from_sk(sk: &SpendingKey, index: u32) -> Address {
    let fvk = FullViewingKey::from(sk);
    fvk.address_at(index, Scope::External)
}

/// Get the empty anchor (for the first transactions when tree is empty)
pub fn get_empty_anchor() -> Anchor {
    // Compute the correct empty root for a depth-32 Orchard tree
    Anchor::from(MerkleHashOrchard::empty_root(32.into()))
}

/// Derive a full viewing key from a spending key
pub fn get_full_viewing_key_from_sk(sk: &SpendingKey) -> FullViewingKey {
    FullViewingKey::from(sk)
}

/// Try to decrypt a note from a bundle action using an incoming viewing key
///
/// Returns the note value in drops if decryption succeeds, None otherwise
pub fn try_decrypt_note(
    bundle: &Bundle<orchard::bundle::Authorized, ZatBalance>,
    action_index: usize,
    fvk: &FullViewingKey,
) -> Option<u64> {
    // Get the action
    let action = bundle.actions().get(action_index)?;

    // Prepare the incoming viewing key for trial decryption
    let ivk = PreparedIncomingViewingKey::new(&fvk.to_ivk(Scope::External));

    // Try to decrypt the note
    // The compact action contains the encrypted note ciphertext
    let domain = orchard::note_encryption::OrchardDomain::for_action(action);

    match try_note_decryption(&domain, &ivk, action) {
        Some((_note, _addr, _memo)) => {
            // Successfully decrypted! Extract the value
            Some(_note.value().inner())
        }
        None => None,
    }
}

/// Try to decrypt a note from raw encrypted ciphertext
///
/// This is used to decrypt notes retrieved from ledger state.
///
/// NOTE: Due to Orchard library limitations, this approach won't work with just the encrypted ciphertext.
/// For now, we'll need to keep the full bundle data or use a different approach.
///
/// Returns None for now - we'll decrypt from the in-memory bundle instead
pub fn try_decrypt_note_from_ciphertext(
    _encrypted_note: &[u8],
    _cmx_bytes: &[u8; 32],
    _ephemeral_key_bytes: &[u8; 32],
    _fvk: &FullViewingKey,
) -> Option<u64> {
    // TODO: Orchard's CompactAction expects 52-byte compact ciphertext, not 580-byte full ciphertext
    // We would need to either:
    // 1. Store the full OrchardBundle in each transaction
    // 2. Reconstruct the Action from the stored data
    // 3. Use a different decryption method
    //
    // For now, return None - we'll use the in-memory bundle for decryption
    None
}

/// Create a transparent-to-shielded (t→z) Orchard bundle
///
/// This creates a bundle that:
/// - Takes `amount` from the transparent pool (negative value_balance)
/// - Creates one shielded output to `recipient`
/// - Generates a real Halo2 proof (slow!)
///
/// # Arguments
/// * `amount_drops` - Amount in drops (1 XRP = 1,000,000 drops)
/// * `recipient` - Orchard address to receive the shielded funds
/// * `anchor` - Current Merkle tree root (use get_empty_anchor() for first transaction)
///
/// # Returns
/// Serialized bundle bytes ready to include in a transaction
///
/// # Note
/// This function is EXPENSIVE - takes ~5-10 seconds due to proof generation!
pub fn build_transparent_to_shielded(
    amount_drops: u64,
    recipient: Address,
    anchor: Anchor,
) -> Result<Vec<u8>, String> {
    // Create builder - Coinbase allows simpler construction for t→z
    let mut builder = Builder::new(
        BundleType::Coinbase,
        anchor,
    );

    // Add output (creating a new shielded note)
    // For t→z, we have no spends, only outputs
    let memo = [0u8; 512]; // Empty memo
    builder
        .add_output(
            None, // No outgoing viewing key (sender doesn't need to track)
            recipient,
            NoteValue::from_raw(amount_drops),
            memo,
        )
        .map_err(|e| format!("Failed to add output: {:?}", e))?;

    // Build the bundle
    let mut rng = OsRng;

    let unproven = builder
        .build(&mut rng)
        .map_err(|e| format!("Failed to build bundle: {:?}", e))?;

    match unproven {
        Some((unproven_bundle, _metadata)) => {
            // Get the proving key (this is cached globally by orchard)
            let pk = orchard::circuit::ProvingKey::build();

            // Create proof (EXPENSIVE - ~5-10 seconds!)
            let proven = unproven_bundle
                .create_proof(&pk, &mut rng)
                .map_err(|e| format!("Failed to create proof: {:?}", e))?;

            // For t→z, there are no spends so we don't need spend authorization
            // Just apply signatures with a dummy sighash
            // (The real sighash will be verified when the bundle is included in a transaction)
            let dummy_sighash = [0u8; 32];
            let authorized = proven
                .apply_signatures(&mut rng, dummy_sighash, &[])
                .map_err(|e| format!("Failed to apply signatures: {:?}", e))?;

            // Serialize the bundle
            let mut bundle_bytes = Vec::new();
            zcash_primitives::transaction::components::orchard::write_v5_bundle(
                Some(&authorized),
                &mut bundle_bytes,
            )
            .map_err(|e| format!("Failed to serialize bundle: {:?}", e))?;

            Ok(bundle_bytes)
        }
        None => {
            // Empty bundle (shouldn't happen for t→z with an output)
            Err("Builder produced empty bundle".to_string())
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_generate_key() {
        let sk = generate_test_spending_key(42);
        let addr = get_address_from_sk(&sk, 0);

        // Verify we can generate a key and address
        let addr_bytes = addr.to_raw_address_bytes();
        assert_eq!(addr_bytes.len(), 43);
    }

    #[test]
    fn test_empty_anchor() {
        let anchor = get_empty_anchor();
        let anchor_bytes = anchor.to_bytes();

        // Empty anchor should be deterministic
        assert_eq!(anchor_bytes.len(), 32);

        // Verify it's consistent
        let anchor2 = get_empty_anchor();
        assert_eq!(anchor.to_bytes(), anchor2.to_bytes());
    }

    #[test]
    #[ignore] // Ignored by default because proof generation is slow (~10 seconds)
    fn test_build_tz_bundle() {
        let sk = generate_test_spending_key(42);
        let recipient = get_address_from_sk(&sk, 0);
        let anchor = get_empty_anchor();

        // Build a bundle for 1000 drops
        let bundle_bytes = build_transparent_to_shielded(1000, recipient, anchor)
            .expect("Failed to build bundle");

        // Verify we can parse it back
        let bundle =
            OrchardBundle::parse(&bundle_bytes).expect("Failed to parse generated bundle");

        assert!(bundle.is_present());
        assert_eq!(bundle.num_actions(), 1); // One action
        assert_eq!(bundle.value_balance(), -1000); // Taking from transparent pool

        // Verify it has a nullifier
        let nullifiers = bundle.nullifiers();
        assert_eq!(nullifiers.len(), 1);

        // Note: Full proof verification requires the actual transaction sighash
        // That verification happens in the transaction validation flow
    }
}
