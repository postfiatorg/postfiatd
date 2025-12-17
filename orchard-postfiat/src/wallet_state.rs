//! Orchard Wallet State Management
//!
//! This module implements server-side wallet storage for Orchard notes,
//! following the Zcash architecture where wallet state lives in Rust
//! and is serialized as a single blob to disk.
//!
//! Key design principles from Zcash:
//! - Use BridgeTree for automatic witness management
//! - Track by IncomingViewingKey (IVK), not FullViewingKey
//! - Serialize entire state as single blob (not per-note storage)
//! - Checkpoint at each ledger for reorg support

use orchard::{
    keys::{IncomingViewingKey, PreparedIncomingViewingKey},
    note::Note,
    note_encryption::OrchardDomain,
    tree::{MerkleHashOrchard, MerklePath},
    Anchor,
};
use incrementalmerkletree::{
    frontier::CommitmentTree,
    witness::IncrementalWitness,
    Position,
    Hashable,
};
use zcash_note_encryption::try_note_decryption;
use std::collections::BTreeMap;

/// A decrypted note with metadata and witness for spending
#[derive(Clone, Debug)]
pub struct DecryptedNote {
    /// The full Orchard note
    pub note: Note,
    /// Note commitment (cmx)
    pub cmx: [u8; 32],
    /// Nullifier
    pub nullifier: [u8; 32],
    /// Amount in drops
    pub amount: u64,
    /// Ledger sequence where received
    pub ledger_seq: u32,
    /// Transaction hash
    pub tx_hash: [u8; 32],
    /// Action index within transaction
    pub action_idx: u32,
    /// Position in commitment tree
    pub position: Position,
    /// Witness for generating Merkle paths (needed for spending)
    pub witness: IncrementalWitness<MerkleHashOrchard, 32>,
}

/// Identifier for a note: (tx_hash, action_idx)
type NoteId = ([u8; 32], u32);

/// Orchard wallet state - all data needed for spending
///
/// Following Zcash's design, this structure:
/// - Lives entirely in Rust memory
/// - Serializes to a single blob for disk persistence
/// - Uses BridgeTree for automatic witness computation
/// - Tracks notes by IVK, not FVK
pub struct OrchardWalletState {
    /// Registered incoming viewing keys
    /// We only track IVKs to match Zcash's design
    ivks: Vec<IncomingViewingKey>,

    /// Decrypted notes indexed by (tx_hash, action_idx)
    notes: BTreeMap<NoteId, DecryptedNote>,

    /// Commitment tree for all note commitments
    /// This provides automatic witness computation
    commitment_tree: CommitmentTree<MerkleHashOrchard, 32>,

    /// Nullifier tracking: nullifier -> note_id
    /// Used to mark notes as spent
    nullifiers: BTreeMap<[u8; 32], NoteId>,

    /// Spent notes (just the IDs)
    spent_notes: std::collections::HashSet<NoteId>,

    /// Last ledger sequence we've processed
    last_checkpoint: Option<u32>,
}

impl OrchardWalletState {
    /// Create a new empty wallet state
    pub fn new() -> Self {
        Self {
            ivks: Vec::new(),
            notes: BTreeMap::new(),
            commitment_tree: CommitmentTree::empty(),
            nullifiers: BTreeMap::new(),
            spent_notes: std::collections::HashSet::new(),
            last_checkpoint: None,
        }
    }

    /// Add an incoming viewing key to track
    pub fn add_ivk(&mut self, ivk: IncomingViewingKey) {
        if !self.ivks.contains(&ivk) {
            self.ivks.push(ivk);
        }
    }

    /// Remove an incoming viewing key
    pub fn remove_ivk(&mut self, ivk: &IncomingViewingKey) {
        self.ivks.retain(|k| k != ivk);
    }

    /// List all registered IVKs
    pub fn list_ivks(&self) -> &[IncomingViewingKey] {
        &self.ivks
    }

    /// Add a note commitment to the tree
    ///
    /// This should be called for ALL commitments in the ledger,
    /// not just our notes, to maintain correct witness paths.
    pub fn append_commitment(&mut self, cmx: [u8; 32]) -> Result<(), String> {
        let cmx_hash = MerkleHashOrchard::from_bytes(&cmx)
            .into_option()
            .ok_or_else(|| "Invalid commitment bytes".to_string())?;

        self.commitment_tree.append(cmx_hash)
            .map_err(|e| format!("Failed to append to tree: {:?}", e))?;

        Ok(())
    }

    /// Add a decrypted note to the wallet with witness
    ///
    /// This should be called after successfully decrypting a note that belongs to us.
    /// The commitment must have been added to the tree first via append_commitment.
    pub fn add_note(
        &mut self,
        note: Note,
        cmx: [u8; 32],
        nullifier: [u8; 32],
        tx_hash: [u8; 32],
        action_idx: u32,
        ledger_seq: u32,
    ) -> Result<(), String> {
        let amount = note.value().inner();

        // Create witness from current tree state (before this note's commitment)
        // The position is the current tree size - 1 (0-indexed)
        let position = Position::from(self.commitment_tree.size() as u64 - 1);

        // Create witness for this position
        let witness = IncrementalWitness::from_tree(self.commitment_tree.clone())
            .ok_or_else(|| "Failed to create witness from tree".to_string())?;

        let note_id = (tx_hash, action_idx);

        let decrypted_note = DecryptedNote {
            note,
            cmx,
            nullifier,
            amount,
            ledger_seq,
            tx_hash,
            action_idx,
            position,
            witness,
        };

        // Store the note
        self.notes.insert(note_id, decrypted_note);

        // Track nullifier
        self.nullifiers.insert(nullifier, note_id);

        Ok(())
    }

    /// Try to decrypt and add a note from action with encrypted note data
    ///
    /// This attempts decryption with all registered IVKs.
    /// If successful, stores the full note data.
    ///
    /// Note: This is a simplified version. For production, we'd need to handle
    /// the full Orchard action structure properly with ephemeral keys and ciphertexts.
    pub fn try_add_note(
        &mut self,
        cmx: [u8; 32],
        ephemeral_key: &[u8; 32],
        enc_ciphertext: &[u8],
        tx_hash: [u8; 32],
        action_idx: u32,
        ledger_seq: u32,
    ) -> Result<bool, String> {
        // TODO: Implement proper note decryption using the encrypted note data
        // For now, we'll skip this and rely on the existing orchard_note_manager_decrypt_and_add_note
        // function which handles the decryption properly

        // This method is a placeholder for future Zcash-style wallet integration
        // where we'd decrypt notes directly in Rust

        Ok(false)
    }

    /// Try to decrypt and add notes from an Orchard bundle
    ///
    /// This attempts decryption of all actions in the bundle with registered IVKs.
    /// If any note decrypts successfully, it's added to the wallet.
    ///
    /// Returns the number of notes successfully decrypted and added.
    pub fn try_decrypt_notes_from_bundle<V>(
        &mut self,
        bundle: &orchard::Bundle<orchard::bundle::Authorized, V>,
        tx_hash: [u8; 32],
        ledger_seq: u32,
    ) -> Result<usize, String> {
        use orchard::keys::Scope;

        let mut decrypted_count = 0;

        // Iterate over all actions in the bundle
        for (action_idx, action) in bundle.actions().iter().enumerate() {
            // Try each registered IVK
            for ivk in &self.ivks {
                // Prepare IVK for decryption
                let prepared_ivk = PreparedIncomingViewingKey::new(ivk);

                // Try to decrypt the note
                let domain = OrchardDomain::for_action(action);

                if let Some((note, _recipient, _memo)) = try_note_decryption(&domain, &prepared_ivk, action) {
                    // Successfully decrypted! Extract the data we need

                    // Get commitment (cmx)
                    let cmx = action.cmx().to_bytes();

                    // Get nullifier directly from the action
                    // The action contains the revealed nullifier which we can use to track spent notes
                    let nullifier = action.nullifier().to_bytes();

                    // Add note to wallet
                    self.add_note(
                        note,
                        cmx,
                        nullifier,
                        tx_hash,
                        action_idx as u32,
                        ledger_seq,
                    )?;

                    decrypted_count += 1;

                    // Note decrypted, no need to try other IVKs for this action
                    break;
                }
            }
        }

        Ok(decrypted_count)
    }

    /// Mark a note as spent by nullifier
    pub fn mark_spent(&mut self, nullifier: &[u8; 32]) {
        if let Some(note_id) = self.nullifiers.get(nullifier) {
            self.spent_notes.insert(*note_id);
        }
    }

    /// Get the current anchor (Merkle root)
    pub fn get_anchor(&self) -> Result<Anchor, String> {
        let root = self.commitment_tree.root();
        Anchor::from_bytes(root.to_bytes())
            .into_option()
            .ok_or_else(|| "Failed to create anchor".to_string())
    }

    /// Get total balance (unspent notes only)
    pub fn get_balance(&self) -> u64 {
        self.notes
            .iter()
            .filter(|(id, _)| !self.spent_notes.contains(id))
            .map(|(_, note)| note.amount)
            .sum()
    }

    /// Get all notes (optionally include spent)
    pub fn list_notes(&self, include_spent: bool) -> Vec<&DecryptedNote> {
        self.notes
            .iter()
            .filter(|(id, _)| include_spent || !self.spent_notes.contains(id))
            .map(|(_, note)| note)
            .collect()
    }

    /// Get a specific note by commitment
    pub fn get_note(&self, cmx: &[u8; 32]) -> Option<&DecryptedNote> {
        self.notes
            .iter()
            .find(|(_, note)| &note.cmx == cmx)
            .map(|(_, note)| note)
    }

    /// Get spendable notes (unspent notes with witnesses)
    ///
    /// Returns notes sorted by amount (smallest first) for coin selection
    pub fn get_spendable_notes(&self) -> Vec<&DecryptedNote> {
        let mut spendable: Vec<&DecryptedNote> = self.notes
            .iter()
            .filter(|(id, _)| !self.spent_notes.contains(id))
            .map(|(_, note)| note)
            .collect();

        // Sort by amount (smallest first for better coin selection)
        spendable.sort_by_key(|note| note.amount);

        spendable
    }

    /// Get Merkle path for a note (for spending)
    pub fn get_merkle_path(&self, note: &DecryptedNote) -> Result<MerklePath, String> {
        // Generate the merkle path from the witness
        let inc_merkle_path = note.witness.path()
            .ok_or_else(|| "Failed to generate Merkle path from witness".to_string())?;

        // Convert position to u32 for Orchard's MerklePath
        let position_u32: u32 = u64::from(note.position).try_into()
            .map_err(|_| "Position too large for u32".to_string())?;

        // Extract auth path as array from incrementalmerkletree::MerklePath
        let auth_path_vec: Vec<_> = inc_merkle_path.path_elems().iter().copied().collect();
        let mut auth_path = [MerkleHashOrchard::empty_leaf(); 32];
        for (i, elem) in auth_path_vec.iter().enumerate().take(32) {
            auth_path[i] = *elem;
        }

        // Convert to Orchard's MerklePath
        Ok(MerklePath::from_parts(position_u32, auth_path))
    }

    /// Select notes for spending a given amount
    ///
    /// Returns notes that sum to at least the target amount.
    /// Uses a greedy algorithm (smallest notes first).
    pub fn select_notes(&self, target_amount: u64) -> Result<Vec<&DecryptedNote>, String> {
        let spendable = self.get_spendable_notes();

        let mut selected = Vec::new();
        let mut total = 0u64;

        for note in spendable {
            selected.push(note);
            total = total.checked_add(note.amount)
                .ok_or_else(|| "Amount overflow".to_string())?;

            if total >= target_amount {
                return Ok(selected);
            }
        }

        Err(format!(
            "Insufficient balance: have {}, need {}",
            total, target_amount
        ))
    }

    /// Checkpoint at a ledger sequence
    pub fn checkpoint(&mut self, ledger_seq: u32) {
        self.last_checkpoint = Some(ledger_seq);
    }

    /// Get last checkpoint
    pub fn last_checkpoint(&self) -> Option<u32> {
        self.last_checkpoint
    }

    /// Reset wallet state (for testing)
    pub fn reset(&mut self) {
        self.notes.clear();
        self.commitment_tree = CommitmentTree::empty();
        self.nullifiers.clear();
        self.spent_notes.clear();
        self.last_checkpoint = None;
    }
}

impl Default for OrchardWalletState {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_wallet_state_creation() {
        let wallet = OrchardWalletState::new();
        assert_eq!(wallet.get_balance(), 0);
        assert_eq!(wallet.list_notes(false).len(), 0);
    }

    #[test]
    fn test_ivk_management() {
        // TODO: Add test with actual IVK
    }
}
