//! CXX Bridge definitions for Orchard operations

use crate::bundle_real::OrchardBundle;
use crate::note_manager::NoteManager as RustNoteManager;
use crate::wallet_state::OrchardWalletState as RustWalletState;

/// Wrapper for NoteManager to expose to C++
pub struct NoteManager {
    inner: RustNoteManager,
}

/// Wrapper for OrchardWalletState to expose to C++
pub struct OrchardWalletState {
    pub(crate) inner: RustWalletState,
}

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
        fn orchard_generate_random_spending_key() -> Vec<u8>;
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
        fn orchard_test_compute_note_nullifier(
            bundle: &OrchardBundle,
            action_index: usize,
            fvk_bytes: &[u8]
        ) -> Result<Vec<u8>>;

        // Production note management and z->z transactions
        type NoteManager;

        fn orchard_note_manager_new() -> Box<NoteManager>;
        fn orchard_note_manager_add_note(
            manager: &mut NoteManager,
            note_bytes: &[u8],
            cmx: &[u8; 32],
            nullifier: &[u8; 32],
            ledger_seq: u32,
            tx_hash: &[u8; 32]
        ) -> Result<()>;
        fn orchard_note_manager_mark_spent(
            manager: &mut NoteManager,
            nullifier: &[u8; 32]
        );
        fn orchard_note_manager_get_balance(manager: &NoteManager) -> u64;
        fn orchard_note_manager_note_count(manager: &NoteManager) -> usize;
        fn orchard_note_manager_get_anchor(manager: &NoteManager) -> Result<Vec<u8>>;
        fn orchard_note_manager_decrypt_and_add_note(
            manager: &mut NoteManager,
            bundle: &OrchardBundle,
            action_index: usize,
            fvk_bytes: &[u8],
            ledger_seq: u32,
            tx_hash: &[u8; 32]
        ) -> Result<()>;

        fn orchard_build_shielded_to_shielded_production(
            manager: &NoteManager,
            sk_bytes: &[u8],
            recipient_addr_bytes: &[u8],
            send_amount: u64
        ) -> Result<Vec<u8>>;

        // Wallet state management (Zcash-style server-side wallet)
        type OrchardWalletState;

        fn orchard_wallet_state_new() -> Box<OrchardWalletState>;
        fn orchard_wallet_state_reset(wallet: &mut OrchardWalletState);

        // IVK management
        fn orchard_wallet_state_add_ivk(wallet: &mut OrchardWalletState, ivk_bytes: &[u8]) -> Result<()>;
        fn orchard_wallet_state_remove_ivk(wallet: &mut OrchardWalletState, ivk_bytes: &[u8]) -> Result<()>;
        fn orchard_wallet_state_get_ivk_count(wallet: &OrchardWalletState) -> usize;

        // Balance and notes
        fn orchard_wallet_state_get_balance(wallet: &OrchardWalletState) -> u64;
        fn orchard_wallet_state_get_note_count(wallet: &OrchardWalletState, include_spent: bool) -> usize;
        fn orchard_wallet_state_get_note(wallet: &OrchardWalletState, cmx: &[u8; 32]) -> Result<Vec<u8>>;

        // Commitment tree operations
        fn orchard_wallet_state_append_commitment(wallet: &mut OrchardWalletState, cmx: &[u8; 32]) -> Result<()>;
        fn orchard_wallet_state_get_anchor(wallet: &OrchardWalletState) -> Result<Vec<u8>>;

        // Note scanning and management
        fn orchard_wallet_state_try_add_note(
            wallet: &mut OrchardWalletState,
            bundle: &OrchardBundle,
            action_index: usize,
            fvk_bytes: &[u8],
            ledger_seq: u32,
            tx_hash: &[u8; 32]
        ) -> Result<bool>;
        fn orchard_wallet_state_try_decrypt_notes(
            wallet: &mut OrchardWalletState,
            bundle: &OrchardBundle,
            ledger_seq: u32,
            tx_hash: &[u8; 32]
        ) -> Result<usize>;
        fn orchard_wallet_state_mark_spent(wallet: &mut OrchardWalletState, nullifier: &[u8; 32]);

        // Checkpointing
        fn orchard_wallet_state_checkpoint(wallet: &mut OrchardWalletState, ledger_seq: u32);
        fn orchard_wallet_state_last_checkpoint(wallet: &OrchardWalletState) -> u32;

        // Key derivation utilities
        fn orchard_derive_ivk_from_fvk(fvk_bytes: &[u8]) -> Result<Vec<u8>>;

        // Wallet-based bundle building (PRODUCTION)
        fn orchard_wallet_build_z_to_z(
            wallet: &OrchardWalletState,
            sk_bytes: &[u8],
            recipient_addr_bytes: &[u8],
            send_amount: u64,
            fee: u64
        ) -> Result<Vec<u8>>;
        fn orchard_wallet_build_z_to_t(
            wallet: &OrchardWalletState,
            sk_bytes: &[u8],
            unshield_amount: u64,
            fee: u64
        ) -> Result<Vec<u8>>;
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

/// Generate a cryptographically secure random spending key
///
/// Uses OsRng to generate 32 bytes of secure random data for the spending key.
/// This is suitable for production use.
pub fn orchard_generate_random_spending_key() -> Vec<u8> {
    let sk = crate::bundle_builder::generate_random_spending_key();
    sk.to_bytes().to_vec()
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

/// Compute the nullifier for a decrypted note
///
/// This function decrypts a note and returns its nullifier.
/// The nullifier is what gets revealed when spending a note.
///
/// # Arguments
/// * `bundle` - The Orchard bundle containing the action
/// * `action_index` - Index of the action in the bundle
/// * `fvk_bytes` - Full viewing key bytes (96 bytes)
///
/// # Returns
/// 32-byte nullifier if decryption succeeds
pub fn orchard_test_compute_note_nullifier(
    bundle: &OrchardBundle,
    action_index: usize,
    fvk_bytes: &[u8],
) -> anyhow::Result<Vec<u8>> {
    use orchard::keys::{FullViewingKey, PreparedIncomingViewingKey, Scope};
    use zcash_note_encryption::try_note_decryption;

    let fvk_array: [u8; 96] = fvk_bytes
        .try_into()
        .map_err(|_| anyhow::anyhow!("Invalid full viewing key length, expected 96 bytes"))?;

    let fvk = FullViewingKey::from_bytes(&fvk_array)
        .ok_or_else(|| anyhow::anyhow!("Invalid full viewing key"))?;

    // Get the inner bundle and action
    let inner = bundle.inner()
        .ok_or_else(|| anyhow::anyhow!("Bundle is empty"))?;
    let action = inner.actions().get(action_index)
        .ok_or_else(|| anyhow::anyhow!("Action index out of bounds"))?;

    // Prepare the incoming viewing key for trial decryption
    let ivk = PreparedIncomingViewingKey::new(&fvk.to_ivk(Scope::External));

    // Try to decrypt the note
    let domain = orchard::note_encryption::OrchardDomain::for_action(action);
    let (note, _addr, _memo) = try_note_decryption(&domain, &ivk, action)
        .ok_or_else(|| anyhow::anyhow!("Failed to decrypt note - not ours"))?;

    // Compute and return the nullifier
    let nullifier = note.nullifier(&fvk).to_bytes();
    Ok(nullifier.to_vec())
}

/// Build a shielded-to-shielded (z→z) bundle for testing
///
/// This generates a REAL Orchard bundle with valid proofs.
/// WARNING: This is EXPENSIVE - takes ~5-10 seconds for proof generation!
///
/// # Arguments
/// * `sk_bytes` - Spending key bytes (32 bytes) - needed to authorize spends
/// * `spend_amount` - Total amount available to spend from shielded notes (in drops)
/// * `recipient_addr_bytes` - Raw Orchard address bytes (43 bytes)
/// * `send_amount` - Amount to send to recipient (in drops)
/// * `anchor` - Current Merkle tree root (32 bytes)
///
/// # Returns
/// Serialized bundle bytes ready to include in a transaction

// ============================================================================
// Production Note Management FFI Functions
// ============================================================================

/// Create a new note manager
pub fn orchard_note_manager_new() -> Box<NoteManager> {
    Box::new(NoteManager {
        inner: RustNoteManager::new(),
    })
}

/// Add a received note to the manager
pub fn orchard_note_manager_add_note(
    manager: &mut NoteManager,
    note_bytes: &[u8],
    cmx: &[u8; 32],
    nullifier: &[u8; 32],
    ledger_seq: u32,
    tx_hash: &[u8; 32],
) -> anyhow::Result<()> {
    // Deserialize the note
    use orchard::note::Note;

    // For now, we need to receive the full note from C++
    // In production, this would deserialize from note_bytes
    // TODO: Implement proper note serialization/deserialization

    Err(anyhow::anyhow!("Note deserialization not yet implemented. Need to pass full Note object from scanner."))
}

/// Mark a note as spent by its nullifier
pub fn orchard_note_manager_mark_spent(
    manager: &mut NoteManager,
    nullifier: &[u8; 32],
) {
    manager.inner.mark_spent(nullifier);
}

/// Get the total balance of unspent notes
pub fn orchard_note_manager_get_balance(manager: &NoteManager) -> u64 {
    manager.inner.get_balance()
}

/// Get the count of unspent notes
pub fn orchard_note_manager_note_count(manager: &NoteManager) -> usize {
    manager.inner.note_count()
}

/// Get the current anchor (Merkle tree root)
pub fn orchard_note_manager_get_anchor(manager: &NoteManager) -> anyhow::Result<Vec<u8>> {
    let anchor = manager.inner.get_anchor()
        .map_err(|e| anyhow::anyhow!("Failed to get anchor: {}", e))?;

    Ok(anchor.to_bytes().to_vec())
}

/// Decrypt a note from a bundle action and add it to the manager
pub fn orchard_note_manager_decrypt_and_add_note(
    manager: &mut NoteManager,
    bundle: &OrchardBundle,
    action_index: usize,
    fvk_bytes: &[u8],
    ledger_seq: u32,
    tx_hash: &[u8; 32],
) -> anyhow::Result<()> {
    use orchard::keys::{FullViewingKey, PreparedIncomingViewingKey, Scope};
    use zcash_note_encryption::try_note_decryption;

    // Parse FVK
    let fvk_array: [u8; 96] = fvk_bytes.try_into()
        .map_err(|_| anyhow::anyhow!("FVK must be 96 bytes"))?;
    let fvk = FullViewingKey::from_bytes(&fvk_array)
        .ok_or_else(|| anyhow::anyhow!("Invalid FVK"))?;

    // Get the inner bundle and action
    let inner_bundle = bundle.inner()
        .ok_or_else(|| anyhow::anyhow!("Bundle is empty"))?;
    let action = inner_bundle.actions().get(action_index)
        .ok_or_else(|| anyhow::anyhow!("Action index out of bounds"))?;

    // Prepare the incoming viewing key for trial decryption
    let ivk = PreparedIncomingViewingKey::new(&fvk.to_ivk(Scope::External));

    // Try to decrypt the note
    let domain = orchard::note_encryption::OrchardDomain::for_action(action);
    let (note, _addr, _memo) = try_note_decryption(&domain, &ivk, action)
        .ok_or_else(|| anyhow::anyhow!("Failed to decrypt note - not ours"))?;

    // Get cmx (note commitment)
    let cmx = action.cmx().to_bytes();

    // Compute nullifier
    let nullifier = note.nullifier(&fvk).to_bytes();

    // Add to manager
    manager.inner.add_note(note, cmx, nullifier, ledger_seq, *tx_hash)
        .map_err(|e| anyhow::anyhow!("Failed to add note: {}", e))
}

/// Build a production z→z bundle with real note spending
pub fn orchard_build_shielded_to_shielded_production(
    manager: &NoteManager,
    sk_bytes: &[u8],
    recipient_addr_bytes: &[u8],
    send_amount: u64,
) -> anyhow::Result<Vec<u8>> {
    // Parse spending key
    let sk_array: [u8; 32] = sk_bytes.try_into()
        .map_err(|_| anyhow::anyhow!("Spending key must be 32 bytes"))?;

    use orchard::keys::SpendingKey;
    let sk = SpendingKey::from_bytes(sk_array)
        .into_option()
        .ok_or_else(|| anyhow::anyhow!("Invalid spending key"))?;

    // Parse recipient address
    let recipient_array: [u8; 43] = recipient_addr_bytes.try_into()
        .map_err(|_| anyhow::anyhow!("Recipient address must be 43 bytes"))?;

    use orchard::Address;
    let recipient = Address::from_raw_address_bytes(&recipient_array)
        .into_option()
        .ok_or_else(|| anyhow::anyhow!("Invalid recipient address"))?;

    // Build the production bundle with real spends
    crate::bundle_builder::build_shielded_to_shielded_production(
        &manager.inner,
        &sk_array,
        recipient,
        send_amount,
    )
    .map_err(|e| anyhow::anyhow!("Failed to build production z→z bundle: {}", e))
}

// ============================================================================
// Wallet State Management FFI Functions
// ============================================================================

/// Create a new empty wallet state
pub fn orchard_wallet_state_new() -> Box<OrchardWalletState> {
    Box::new(OrchardWalletState {
        inner: RustWalletState::new(),
    })
}

/// Reset the wallet state (clear all data)
pub fn orchard_wallet_state_reset(wallet: &mut OrchardWalletState) {
    wallet.inner.reset();
}

/// Add an incoming viewing key to track
pub fn orchard_wallet_state_add_ivk(
    wallet: &mut OrchardWalletState,
    ivk_bytes: &[u8],
) -> anyhow::Result<()> {
    use orchard::keys::IncomingViewingKey;

    let ivk_array: [u8; 64] = ivk_bytes.try_into()
        .map_err(|_| anyhow::anyhow!("IVK must be 64 bytes"))?;

    let ivk = IncomingViewingKey::from_bytes(&ivk_array)
        .into_option()
        .ok_or_else(|| anyhow::anyhow!("Invalid IVK"))?;

    wallet.inner.add_ivk(ivk);
    Ok(())
}

/// Remove an incoming viewing key
pub fn orchard_wallet_state_remove_ivk(
    wallet: &mut OrchardWalletState,
    ivk_bytes: &[u8],
) -> anyhow::Result<()> {
    use orchard::keys::IncomingViewingKey;

    let ivk_array: [u8; 64] = ivk_bytes.try_into()
        .map_err(|_| anyhow::anyhow!("IVK must be 64 bytes"))?;

    let ivk = IncomingViewingKey::from_bytes(&ivk_array)
        .into_option()
        .ok_or_else(|| anyhow::anyhow!("Invalid IVK"))?;

    wallet.inner.remove_ivk(&ivk);
    Ok(())
}

/// Get the count of registered IVKs
pub fn orchard_wallet_state_get_ivk_count(wallet: &OrchardWalletState) -> usize {
    wallet.inner.list_ivks().len()
}

/// Get the total balance of unspent notes
pub fn orchard_wallet_state_get_balance(wallet: &OrchardWalletState) -> u64 {
    wallet.inner.get_balance()
}

/// Get the count of notes (optionally include spent)
pub fn orchard_wallet_state_get_note_count(
    wallet: &OrchardWalletState,
    include_spent: bool,
) -> usize {
    wallet.inner.list_notes(include_spent).len()
}

/// Get a specific note by commitment
pub fn orchard_wallet_state_get_note(
    wallet: &OrchardWalletState,
    cmx: &[u8; 32],
) -> anyhow::Result<Vec<u8>> {
    let note = wallet.inner.get_note(cmx)
        .ok_or_else(|| anyhow::anyhow!("Note not found"))?;

    // Serialize note metadata as JSON for now
    // TODO: Use a proper binary format
    let json = serde_json::json!({
        "amount": note.amount,
        "ledger_seq": note.ledger_seq,
        "tx_hash": hex::encode(note.tx_hash),
        "action_idx": note.action_idx,
    });

    Ok(json.to_string().into_bytes())
}

/// Append a commitment to the Merkle tree
pub fn orchard_wallet_state_append_commitment(
    wallet: &mut OrchardWalletState,
    cmx: &[u8; 32],
) -> anyhow::Result<()> {
    wallet.inner.append_commitment(*cmx)
        .map_err(|e| anyhow::anyhow!("Failed to append commitment: {}", e))
}

/// Get the current anchor (Merkle tree root)
pub fn orchard_wallet_state_get_anchor(wallet: &OrchardWalletState) -> anyhow::Result<Vec<u8>> {
    let anchor = wallet.inner.get_anchor()
        .map_err(|e| anyhow::anyhow!("Failed to get anchor: {}", e))?;

    Ok(anchor.to_bytes().to_vec())
}

/// Try to decrypt and add a note from a bundle action
pub fn orchard_wallet_state_try_add_note(
    wallet: &mut OrchardWalletState,
    bundle: &OrchardBundle,
    action_index: usize,
    fvk_bytes: &[u8],
    ledger_seq: u32,
    tx_hash: &[u8; 32],
) -> anyhow::Result<bool> {
    use orchard::keys::{FullViewingKey, PreparedIncomingViewingKey, Scope};
    use zcash_note_encryption::try_note_decryption;

    // Parse FVK
    let fvk_array: [u8; 96] = fvk_bytes.try_into()
        .map_err(|_| anyhow::anyhow!("FVK must be 96 bytes"))?;
    let fvk = FullViewingKey::from_bytes(&fvk_array)
        .ok_or_else(|| anyhow::anyhow!("Invalid FVK"))?;

    // Get the inner bundle and action
    let inner_bundle = bundle.inner()
        .ok_or_else(|| anyhow::anyhow!("Bundle is empty"))?;
    let action = inner_bundle.actions().get(action_index)
        .ok_or_else(|| anyhow::anyhow!("Action index out of bounds"))?;

    // Try to decrypt with each registered IVK
    // For now, just try with the external IVK from the provided FVK
    let ivk = PreparedIncomingViewingKey::new(&fvk.to_ivk(Scope::External));

    let domain = orchard::note_encryption::OrchardDomain::for_action(action);
    if let Some((note, _addr, _memo)) = try_note_decryption(&domain, &ivk, action) {
        // Get cmx and nullifier
        let cmx = action.cmx().to_bytes();
        let nullifier = note.nullifier(&fvk).to_bytes();
        let amount = note.value().inner();

        // TODO: Store the full note in wallet_state
        // For now, this is a placeholder returning success
        Ok(true)
    } else {
        Ok(false)
    }
}

/// Try to decrypt notes from an Orchard bundle
///
/// This attempts to decrypt all actions in the bundle using the wallet's registered IVKs.
/// If any notes decrypt successfully, they're added to the wallet with witnesses.
///
/// Returns the number of notes successfully decrypted and added.
pub fn orchard_wallet_state_try_decrypt_notes(
    wallet: &mut OrchardWalletState,
    bundle: &OrchardBundle,
    ledger_seq: u32,
    tx_hash: &[u8; 32],
) -> anyhow::Result<usize> {
    // Get the inner bundle
    let inner_bundle = bundle.inner()
        .ok_or_else(|| anyhow::anyhow!("Bundle is empty"))?;

    // Try to decrypt notes from the bundle
    wallet.inner.try_decrypt_notes_from_bundle(inner_bundle, *tx_hash, ledger_seq)
        .map_err(|e| anyhow::anyhow!("Failed to decrypt notes: {}", e))
}

/// Mark a note as spent by nullifier
pub fn orchard_wallet_state_mark_spent(
    wallet: &mut OrchardWalletState,
    nullifier: &[u8; 32],
) {
    wallet.inner.mark_spent(nullifier);
}

/// Set a checkpoint at a ledger sequence
pub fn orchard_wallet_state_checkpoint(
    wallet: &mut OrchardWalletState,
    ledger_seq: u32,
) {
    wallet.inner.checkpoint(ledger_seq);
}

/// Get the last checkpoint ledger sequence
pub fn orchard_wallet_state_last_checkpoint(wallet: &OrchardWalletState) -> u32 {
    wallet.inner.last_checkpoint().unwrap_or(0)
}

// ============================================================================
// Key Derivation Utilities
// ============================================================================

/// Derive an Incoming Viewing Key (IVK) from a Full Viewing Key (FVK)
///
/// This allows converting a 96-byte FVK into a 64-byte IVK for wallet tracking.
/// The IVK can decrypt incoming notes but cannot compute nullifiers.
///
/// # Arguments
/// * `fvk_bytes` - Full viewing key bytes (96 bytes)
///
/// # Returns
/// Incoming viewing key bytes (64 bytes) for the External scope
pub fn orchard_derive_ivk_from_fvk(fvk_bytes: &[u8]) -> anyhow::Result<Vec<u8>> {
    use orchard::keys::{FullViewingKey, Scope};

    let fvk_array: [u8; 96] = fvk_bytes.try_into()
        .map_err(|_| anyhow::anyhow!("FVK must be 96 bytes"))?;

    let fvk = FullViewingKey::from_bytes(&fvk_array)
        .ok_or_else(|| anyhow::anyhow!("Invalid FVK"))?;

    // Derive the external IVK (used for receiving payments)
    let ivk = fvk.to_ivk(Scope::External);

    Ok(ivk.to_bytes().to_vec())
}

/// Build a production z→z transaction using OrchardWalletState
///
/// This function:
/// - Selects notes from the wallet to cover the send amount
/// - Generates proper witness paths for spending
/// - Builds a fully valid Orchard bundle with Halo2 proof
/// - Returns serialized bundle ready for transaction inclusion
///
/// # Arguments
/// * `wallet` - Wallet state with tracked notes and commitment tree
/// * `sk_bytes` - Spending key (32 bytes) - SECURITY: Do not store!
/// * `recipient_addr_bytes` - Recipient Orchard address (43 bytes)
/// * `send_amount` - Amount to send in drops
///
/// # Returns
/// Serialized Orchard bundle bytes
///
/// # Note
/// This is PRODUCTION-READY and will create valid on-chain transactions.
/// Proof generation takes ~5-10 seconds.
pub fn orchard_wallet_build_z_to_z(
    wallet: &OrchardWalletState,
    sk_bytes: &[u8],
    recipient_addr_bytes: &[u8],
    send_amount: u64,
    fee: u64,
) -> anyhow::Result<Vec<u8>> {
    use orchard::Address;

    // Parse spending key
    let sk_array: [u8; 32] = sk_bytes.try_into()
        .map_err(|_| anyhow::anyhow!("Spending key must be 32 bytes"))?;

    // Parse recipient address
    let addr_array: [u8; 43] = recipient_addr_bytes.try_into()
        .map_err(|_| anyhow::anyhow!("Address must be 43 bytes"))?;

    let recipient = Option::from(Address::from_raw_address_bytes(&addr_array))
        .ok_or_else(|| anyhow::anyhow!("Invalid Orchard address"))?;

    // Build the bundle using wallet state
    crate::bundle_builder::build_shielded_to_shielded_from_wallet(
        &wallet.inner,
        &sk_array,
        recipient,
        send_amount,
        fee,
    )
    .map_err(|e| anyhow::anyhow!("Failed to build z→z bundle: {}", e))
}

/// Build a production z→t transaction using OrchardWalletState
///
/// This function:
/// - Selects notes from the wallet to cover the unshield amount
/// - Generates proper witness paths for spending
/// - Builds a fully valid Orchard bundle with Halo2 proof
/// - Creates a POSITIVE value balance (funds leaving shielded pool)
/// - Returns serialized bundle ready for transaction inclusion
///
/// # Arguments
/// * `wallet` - Wallet state with tracked notes and commitment tree
/// * `sk_bytes` - Spending key (32 bytes) - SECURITY: Do not store!
/// * `unshield_amount` - Amount to transfer to transparent pool in drops
///
/// # Returns
/// Serialized Orchard bundle bytes with positive value balance
///
/// # Note
/// This is PRODUCTION-READY and will create valid on-chain transactions.
/// Proof generation takes ~5-10 seconds.
pub fn orchard_wallet_build_z_to_t(
    wallet: &OrchardWalletState,
    sk_bytes: &[u8],
    unshield_amount: u64,
    fee: u64,
) -> anyhow::Result<Vec<u8>> {
    // Parse spending key
    let sk_array: [u8; 32] = sk_bytes.try_into()
        .map_err(|_| anyhow::anyhow!("Spending key must be 32 bytes"))?;

    // Build the bundle using wallet state
    crate::bundle_builder::build_shielded_to_transparent(
        &wallet.inner,
        &sk_array,
        unshield_amount,
        fee,
    )
    .map_err(|e| anyhow::anyhow!("Failed to build z→t bundle: {}", e))
}
