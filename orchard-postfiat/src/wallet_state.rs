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
    keys::{FullViewingKey, IncomingViewingKey, PreparedIncomingViewingKey},
    note::Note,
    note_encryption::OrchardDomain,
    tree::{MerkleHashOrchard, MerklePath},
    Anchor,
};
use incrementalmerkletree::{
    Position,
    Hashable,
};
use bridgetree::BridgeTree;
use zcash_note_encryption::try_note_decryption;
use std::collections::BTreeMap;

/// A decrypted note with metadata for spending
///
/// Following Zcash's design:
/// - Stores only the position, not the witness
/// - Witness is generated on-demand via tree.witness()
/// - Anchor is retrieved on-demand via tree.root()
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
    /// Position in commitment tree (used to generate witness on-demand)
    pub position: Position,
    /// Anchor (Merkle root) from the transaction that created this note
    /// For reference only - actual anchor for spending comes from tree.root()
    pub anchor: Anchor,
    /// Index of the IVK that decrypted this note (index into ivks vec)
    /// Used to filter notes when spending with a specific FVK
    pub ivk_index: usize,
}

/// Identifier for a note: (tx_hash, action_idx)
type NoteId = ([u8; 32], u32);

/// Orchard wallet state - all data needed for spending
///
/// Following Zcash's design, this structure:
/// - Lives entirely in Rust memory
/// - Serializes to a single blob for disk persistence
/// - Uses BridgeTree for automatic witness computation and checkpoint management
/// - Tracks notes by IVK, not FVK
/// - Stores only positions, not witnesses (witnesses generated on-demand)
pub struct OrchardWalletState {
    /// Registered incoming viewing keys
    /// We only track IVKs to match Zcash's design
    ivks: Vec<IncomingViewingKey>,

    /// Decrypted notes indexed by (tx_hash, action_idx)
    notes: BTreeMap<NoteId, DecryptedNote>,

    /// BridgeTree for commitment tracking with automatic witness generation
    /// - Depth: 32 (Orchard tree depth)
    /// - Max checkpoints: 100 (keep last 100 ledgers for reorg support)
    commitment_tree: BridgeTree<MerkleHashOrchard, u32, 32>,

    /// Nullifier tracking: nullifier -> note_id
    /// Used to mark notes as spent
    nullifiers: BTreeMap<[u8; 32], NoteId>,

    /// Spent notes (just the IDs)
    spent_notes: std::collections::HashSet<NoteId>,

    /// Last ledger sequence we've processed
    last_checkpoint: Option<u32>,

    /// Mapping of cmx -> position for commitments appended in current batch
    /// This is cleared after each bundle's notes are decrypted
    /// Needed because mark() always returns the last appended position
    cmx_to_position: BTreeMap<[u8; 32], Position>,
}

impl OrchardWalletState {
    /// Create a new empty wallet state
    pub fn new() -> Self {
        Self {
            ivks: Vec::new(),
            notes: BTreeMap::new(),
            commitment_tree: BridgeTree::new(100),  // Keep last 100 checkpoints for reorg support
            nullifiers: BTreeMap::new(),
            spent_notes: std::collections::HashSet::new(),
            last_checkpoint: None,
            cmx_to_position: BTreeMap::new(),
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
    ///
    /// Following Zcash's approach:
    /// - append() adds the commitment to the tree
    /// - mark() is called separately for our notes to record position
    pub fn append_commitment(&mut self, cmx: [u8; 32]) -> Result<(), String> {
        let cmx_hash = MerkleHashOrchard::from_bytes(&cmx)
            .into_option()
            .ok_or_else(|| "Invalid commitment bytes".to_string())?;

        let tree_root_before = self.commitment_tree.root(0);
        eprintln!("\n=== Appending commitment ===");
        eprintln!("CMX: {:?}", cmx);
        eprintln!("Tree root BEFORE: {:?}", tree_root_before.map(|r| r.to_bytes()));

        self.commitment_tree.append(cmx_hash)
            .then_some(())
            .ok_or_else(|| "Failed to append to tree (tree full)".to_string())?;

        // Record the position of this commitment using mark()
        // This is needed to track positions for multi-note bundles
        if let Some(position) = self.commitment_tree.mark() {
            self.cmx_to_position.insert(cmx, position);
            eprintln!("Recorded position {:?} for CMX {:?}", position, cmx);
        }

        let tree_root_after = self.commitment_tree.root(0);
        eprintln!("Tree root AFTER: {:?}", tree_root_after.map(|r| r.to_bytes()));

        Ok(())
    }

    /// Add a decrypted note to the wallet
    ///
    /// This should be called after successfully decrypting a note that belongs to us.
    /// The commitment must have been added to the tree first via append_commitment.
    ///
    /// Following Zcash's approach:
    /// - Call mark() to record the position of this note in the tree
    /// - Store only the position, not the witness
    /// - Witness will be generated on-demand when spending
    ///
    /// # Arguments
    /// * `anchor` - The anchor from the transaction that created this note.
    ///              For reference only - actual anchor comes from tree.root()
    pub fn add_note(
        &mut self,
        note: Note,
        cmx: [u8; 32],
        nullifier: [u8; 32],
        tx_hash: [u8; 32],
        action_idx: u32,
        ledger_seq: u32,
        anchor: Anchor,
        ivk_index: usize,
    ) -> Result<(), String> {
        let amount = note.value().inner();

        // Get the position from the mapping that was created during append_commitment
        // This ensures each note gets its correct individual position
        let position = self.cmx_to_position.get(&cmx)
            .copied()
            .ok_or_else(|| format!("Position not found for CMX {:?}. Did you forget to call append_commitment first?", cmx))?;

        eprintln!("wallet_state::add_note: Using position {:?} for note (from cmx mapping)", position);

        // Get current tree root for comparison
        if let Some(current_root) = self.commitment_tree.root(0) {
            eprintln!("wallet_state::add_note: Current tree root (depth 0): {:?}",
                      current_root.to_bytes());
            eprintln!("wallet_state::add_note: Bundle anchor (historical, stored for reference): {:?}",
                      anchor.to_bytes());
        }

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
            anchor,  // Store for reference only
            ivk_index,
        };

        // Store the note
        self.notes.insert(note_id, decrypted_note);

        // Track nullifier
        self.nullifiers.insert(nullifier, note_id);

        eprintln!("wallet_state::add_note: Successfully stored note with amount {} at position {:?}",
                  amount, position);

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
        let mut decrypted_count = 0;

        // Iterate over all actions in the bundle
        for (action_idx, action) in bundle.actions().iter().enumerate() {
            // Try each registered IVK
            for (ivk_index, ivk) in self.ivks.iter().enumerate() {
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

                    // CRITICAL: Store note with CURRENT tree anchor, matching Zcash approach
                    // In Zcash: AppendNoteCommitments() adds all commitments FIRST, then GetLatestAnchor()
                    // gets the current tree root to use for building the NEXT transaction
                    // (See zcash/src/wallet/gtest/test_orchard_wallet.cpp:119-132)
                    // The bundle's anchor is what was used to SPEND notes (input), but newly CREATED
                    // notes must record the tree state AFTER they were added (for future spends)
                    let current_tree_root = self.commitment_tree.root(0)
                        .ok_or_else(|| "Cannot get current tree root".to_string())?;

                    let current_anchor = Anchor::from_bytes(current_tree_root.to_bytes())
                        .into_option()
                        .ok_or_else(|| "Failed to create anchor from tree root".to_string())?;

                    // Add note to wallet with the current tree anchor
                    // Include ivk_index so we know which key owns this note
                    self.add_note(
                        note,
                        cmx,
                        nullifier,
                        tx_hash,
                        action_idx as u32,
                        ledger_seq,
                        current_anchor,  // Use CURRENT tree anchor (after all commitments added)
                        ivk_index,
                    )?;

                    decrypted_count += 1;

                    // Note decrypted, no need to try other IVKs for this action
                    break;
                }
            }
        }

        // Clear the cmx->position mapping now that we're done processing this bundle
        self.cmx_to_position.clear();

        Ok(decrypted_count)
    }

    /// Mark a note as spent by nullifier
    pub fn mark_spent(&mut self, nullifier: &[u8; 32]) {
        if let Some(note_id) = self.nullifiers.get(nullifier) {
            self.spent_notes.insert(*note_id);
        }
    }

    /// Get the current anchor (Merkle root) at depth 0 (current state)
    pub fn get_anchor(&self) -> Result<Anchor, String> {
        let root = self.commitment_tree.root(0)  // depth 0 = current state
            .ok_or_else(|| "Tree is empty, no anchor available".to_string())?;

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
    ///
    /// Generates the witness on-demand using BridgeTree.witness()
    /// Following Zcash's approach: witness(position, checkpoint_depth)
    ///
    /// IMPORTANT: The witness must be generated from the CURRENT tree (depth 0)
    /// but the ANCHOR comes from the previous checkpoint (depth 1).
    /// The witness authenticates the note's position in the tree.
    /// The anchor authenticates the tree state.
    pub fn get_merkle_path(&self, note: &DecryptedNote) -> Result<MerklePath, String> {
        // Generate witness on-demand from BridgeTree
        // checkpoint_depth = 0 means current tree state (includes all commitments)
        eprintln!("\n=== wallet_state::get_merkle_path ===");
        eprintln!("Generating witness for position {:?} at checkpoint depth 0", note.position);

        let auth_path_vec = self.commitment_tree.witness(note.position, 0)
            .map_err(|e| format!(
                "Failed to generate witness for position {:?} at checkpoint depth 0: {:?}",
                note.position, e
            ))?;

        eprintln!("Auth path length: {}", auth_path_vec.len());

        // Calculate what root this witness authenticates to
        // This is done by hashing up from the note's commitment through the auth path
        let witness_root = self.commitment_tree.root(0)
            .ok_or_else(|| "Cannot get tree root at depth 0".to_string())?;
        eprintln!("Witness authenticates to tree root (depth 0): {:?}", witness_root.to_bytes());

        // Convert position to u32 for Orchard's MerklePath
        let position_u32: u32 = u64::from(note.position).try_into()
            .map_err(|_| "Position too large for u32".to_string())?;

        // Convert to fixed-size array
        let mut auth_path = [MerkleHashOrchard::empty_leaf(); 32];
        for (i, elem) in auth_path_vec.iter().enumerate().take(32) {
            auth_path[i] = *elem;
        }

        // Convert to Orchard's MerklePath
        Ok(MerklePath::from_parts(position_u32, auth_path))
    }

    /// Get the anchor (Merkle root) that must be used when spending this note
    ///
    /// Following Zcash's approach (wallet.rs:698-700, 1428):
    /// - Always use checkpoint_depth = 0 (current tree state)
    /// - This gives the most recent tree root
    /// - This anchor MUST exist in the ledger's anchor table
    pub fn get_note_anchor(&self, note: &DecryptedNote) -> Result<Anchor, String> {
        eprintln!("\n=== wallet_state::get_note_anchor ===");
        eprintln!("Getting anchor for note at position {:?}", note.position);

        // Get anchor from CURRENT tree state (depth 0), matching Zcash
        let tree_root = self.commitment_tree.root(0)
            .ok_or_else(|| "Tree is empty, no anchor available".to_string())?;

        eprintln!("Tree root at depth 0 (current tree): {:?}", tree_root.to_bytes());
        eprintln!("Note's stored anchor (from tx that created it): {:?}", note.anchor.to_bytes());

        if tree_root.to_bytes() != note.anchor.to_bytes() {
            eprintln!("WARNING: Current tree root != note's stored anchor!");
            eprintln!("  This is expected if other notes were added after this note was created");
        }

        Anchor::from_bytes(tree_root.to_bytes())
            .into_option()
            .ok_or_else(|| "Failed to create anchor from tree root".to_string())
    }

    /// Select notes for spending a given amount
    ///
    /// Returns notes that sum to at least the target amount.
    /// Uses a greedy algorithm (smallest notes first).
    ///
    /// If `fvk` is provided, only selects notes belonging to that FVK.
    pub fn select_notes(&self, target_amount: u64, fvk: Option<&FullViewingKey>) -> Result<Vec<&DecryptedNote>, String> {
        let mut spendable = self.get_spendable_notes();

        // If FVK is provided, filter notes by ivk_index
        if let Some(fvk) = fvk {
            let ivk = fvk.to_ivk(orchard::keys::Scope::External);

            // Find which IVK index this matches
            let ivk_index = self.ivks.iter().position(|stored_ivk| stored_ivk == &ivk)
                .ok_or_else(|| "FVK not found in wallet".to_string())?;

            eprintln!("wallet_state::select_notes: Filtering notes for ivk_index={}", ivk_index);

            // Filter to only notes from this IVK
            spendable.retain(|note| note.ivk_index == ivk_index);
        }

        eprintln!("wallet_state::select_notes: Called with target_amount={}", target_amount);
        eprintln!("wallet_state::select_notes: Found {} spendable notes{}",
                  spendable.len(),
                  if fvk.is_some() { " (filtered by FVK)" } else { "" });

        let mut selected = Vec::new();
        let mut total = 0u64;

        for (idx, note) in spendable.into_iter().enumerate() {
            eprintln!("wallet_state::select_notes: Considering note {} - amount: {}, cmx: {:?}, ivk_index: {}",
                      idx, note.amount, note.cmx, note.ivk_index);
            selected.push(note);
            total = total.checked_add(note.amount)
                .ok_or_else(|| "Amount overflow".to_string())?;

            if total >= target_amount {
                eprintln!("wallet_state::select_notes: Selected {} notes with total={}", selected.len(), total);
                return Ok(selected);
            }
        }

        eprintln!("wallet_state::select_notes: Insufficient balance - have {}, need {}", total, target_amount);
        Err(format!(
            "Insufficient balance: have {}, need {}",
            total, target_amount
        ))
    }

    /// Checkpoint at a ledger sequence
    ///
    /// Following Zcash's approach:
    /// - Call tree.checkpoint() to save the current tree state
    /// - This creates a checkpoint that can be used for witness generation
    /// - Allows reorg support by keeping historical tree states
    pub fn checkpoint(&mut self, ledger_seq: u32) -> bool {
        let success = self.commitment_tree.checkpoint(ledger_seq);
        if success {
            self.last_checkpoint = Some(ledger_seq);
            eprintln!("wallet_state::checkpoint: Created checkpoint at ledger {}", ledger_seq);
        } else {
            eprintln!("wallet_state::checkpoint: WARNING - Failed to create checkpoint at ledger {}", ledger_seq);
        }
        success
    }

    /// Get last checkpoint
    pub fn last_checkpoint(&self) -> Option<u32> {
        self.last_checkpoint
    }

    /// Reset wallet state (for testing)
    pub fn reset(&mut self) {
        self.notes.clear();
        self.commitment_tree = BridgeTree::new(100);  // Keep last 100 checkpoints
        self.nullifiers.clear();
        self.spent_notes.clear();
        self.last_checkpoint = None;
        self.cmx_to_position.clear();
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
