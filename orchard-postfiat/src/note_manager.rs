//! Note Manager for Production Orchard Transactions
//!
//! This module provides the infrastructure for:
//! - Storing received notes with full cryptographic details
//! - Maintaining the Merkle commitment tree
//! - Generating witness paths for spending
//! - Selecting notes for transactions

use orchard::{
    note::Note,
    tree::{MerkleHashOrchard, MerklePath},
    Anchor,
};
use incrementalmerkletree::{
    frontier::CommitmentTree,
    witness::IncrementalWitness,
    Hashable,
    Position,
};
use std::collections::HashMap;

/// A note with all the information needed to spend it
#[derive(Clone)]
pub struct SpendableNote {
    /// The full Orchard note object
    pub note: Note,
    /// Position in the commitment tree
    pub position: Position,
    /// Witness for generating merkle paths
    pub witness: IncrementalWitness<MerkleHashOrchard, 32>,
    /// The note commitment (cmx)
    pub cmx: [u8; 32],
    /// The nullifier for this note
    pub nullifier: [u8; 32],
    /// Amount in drops
    pub amount: u64,
    /// Ledger sequence where note was received
    pub ledger_seq: u32,
    /// Transaction hash
    pub tx_hash: [u8; 32],
}

/// Manages Orchard notes and the commitment tree
pub struct NoteManager {
    /// All unspent notes owned by this viewing key
    notes: HashMap<[u8; 32], SpendableNote>, // keyed by cmx
    /// The commitment tree (tracks all note commitments)
    tree: CommitmentTree<MerkleHashOrchard, 32>,
    /// Spent note commitments (for faster lookup)
    spent_notes: std::collections::HashSet<[u8; 32]>,
}

impl NoteManager {
    /// Create a new note manager
    pub fn new() -> Self {
        Self {
            notes: HashMap::new(),
            tree: CommitmentTree::empty(),
            spent_notes: std::collections::HashSet::new(),
        }
    }

    /// Add a received note to the manager
    ///
    /// This should be called when scanning the ledger and discovering a note
    /// that belongs to our viewing key.
    pub fn add_note(
        &mut self,
        note: Note,
        cmx: [u8; 32],
        nullifier: [u8; 32],
        ledger_seq: u32,
        tx_hash: [u8; 32],
    ) -> Result<(), String> {
        let amount = note.value().inner();

        // Add commitment to tree
        let cmx_hash = MerkleHashOrchard::from_bytes(&cmx)
            .into_option()
            .ok_or_else(|| "Invalid commitment bytes".to_string())?;

        // Create witness before appending
        let witness = IncrementalWitness::from_tree(self.tree.clone())
            .ok_or_else(|| "Failed to create witness from tree".to_string())?;

        // Append to tree and get position
        self.tree.append(cmx_hash)
            .map_err(|e| format!("Failed to add to tree: {:?}", e))?;

        let position = Position::from(self.tree.size() as u64 - 1);

        // Store the spendable note
        let spendable = SpendableNote {
            note,
            position,
            witness,
            cmx,
            nullifier,
            amount,
            ledger_seq,
            tx_hash,
        };

        self.notes.insert(cmx, spendable);
        Ok(())
    }

    /// Mark a note as spent by its nullifier
    pub fn mark_spent(&mut self, nullifier: &[u8; 32]) {
        // Find note with this nullifier and mark as spent
        if let Some((cmx, _)) = self.notes.iter()
            .find(|(_, note)| &note.nullifier == nullifier)
        {
            let cmx = *cmx;
            self.spent_notes.insert(cmx);
            self.notes.remove(&cmx);
        }
    }

    /// Get the current anchor (Merkle tree root)
    pub fn get_anchor(&self) -> Result<Anchor, String> {
        let root = self.tree.root();
        let anchor_bytes = root.to_bytes();

        Anchor::from_bytes(anchor_bytes)
            .into_option()
            .ok_or_else(|| "Failed to create anchor from tree root".to_string())
    }

    /// Generate a witness path for a specific note
    pub fn get_witness_path(&self, cmx: &[u8; 32]) -> Result<MerklePath, String> {
        let note = self.notes.get(cmx)
            .ok_or_else(|| "Note not found".to_string())?;

        // Generate merkle path from stored witness
        // The witness.path() returns the incrementalmerkletree MerklePath
        let inc_merkle_path = note.witness.path()
            .ok_or_else(|| "Failed to generate authentication path".to_string())?;

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

    /// Select notes to spend for a given amount
    ///
    /// Uses a simple greedy algorithm: pick smallest notes that sum to amount
    pub fn select_notes(&self, amount_needed: u64) -> Result<Vec<[u8; 32]>, String> {
        let mut available: Vec<_> = self.notes.values()
            .filter(|n| !self.spent_notes.contains(&n.cmx))
            .collect();

        // Sort by amount (smallest first for privacy)
        available.sort_by_key(|n| n.amount);

        let mut selected = Vec::new();
        let mut total = 0u64;

        for note in available {
            selected.push(note.cmx);
            total = total.checked_add(note.amount)
                .ok_or_else(|| "Amount overflow".to_string())?;

            if total >= amount_needed {
                return Ok(selected);
            }
        }

        Err(format!(
            "Insufficient balance: have {}, need {}",
            total, amount_needed
        ))
    }

    /// Get a note by its commitment
    pub fn get_note(&self, cmx: &[u8; 32]) -> Option<&SpendableNote> {
        self.notes.get(cmx)
    }

    /// Get total balance (unspent notes only)
    pub fn get_balance(&self) -> u64 {
        self.notes.values()
            .filter(|n| !self.spent_notes.contains(&n.cmx))
            .map(|n| n.amount)
            .sum()
    }

    /// Get number of unspent notes
    pub fn note_count(&self) -> usize {
        self.notes.len() - self.spent_notes.len()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use orchard::{
        keys::{FullViewingKey, Scope, SpendingKey},
        note::{RandomSeed, Rho},
        value::NoteValue,
    };

    #[test]
    fn test_note_manager_basic() {
        let mut manager = NoteManager::new();

        // Create a dummy spending key
        let sk_bytes = [1u8; 32];
        let sk = SpendingKey::from_bytes(sk_bytes).unwrap();
        let fvk = FullViewingKey::from(&sk);
        let addr = fvk.address_at(0, Scope::External);

        // Create a dummy note
        let rho = Rho::from_bytes(&[0u8; 32]).unwrap();
        let rseed = RandomSeed::from_bytes([2u8; 32], &rho).unwrap();
        let note = Note::from_parts(
            addr,
            NoteValue::from_raw(1000),
            rho,
            rseed,
        ).unwrap();

        let cmx = [3u8; 32];
        let nullifier = [4u8; 32];

        // Add note
        manager.add_note(note, cmx, nullifier, 100, [5u8; 32]).unwrap();

        // Check balance
        assert_eq!(manager.get_balance(), 1000);
        assert_eq!(manager.note_count(), 1);

        // Mark as spent
        manager.mark_spent(&nullifier);
        assert_eq!(manager.get_balance(), 0);
        assert_eq!(manager.note_count(), 0);
    }

    #[test]
    fn test_note_selection() {
        let mut manager = NoteManager::new();

        let sk_bytes = [1u8; 32];
        let sk = SpendingKey::from_bytes(sk_bytes).unwrap();
        let fvk = FullViewingKey::from(&sk);
        let addr = fvk.address_at(0, Scope::External);

        // Add multiple notes
        for i in 0..3 {
            let rho = Rho::from_bytes(&[i; 32]).unwrap();
            let rseed = RandomSeed::from_bytes([i + 10; 32], &rho).unwrap();
            let note = Note::from_parts(
                addr,
                NoteValue::from_raw((i as u64 + 1) * 1000),
                rho,
                rseed,
            ).unwrap();

            let mut cmx = [0u8; 32];
            cmx[0] = i;
            let mut nullifier = [0u8; 32];
            nullifier[0] = i + 100;

            manager.add_note(note, cmx, nullifier, 100, [i; 32]).unwrap();
        }

        // Select notes for 2500 (should pick 1000 + 2000)
        let selected = manager.select_notes(2500).unwrap();
        assert_eq!(selected.len(), 2);

        // Check total
        let total: u64 = selected.iter()
            .map(|cmx| manager.get_note(cmx).unwrap().amount)
            .sum();
        assert!(total >= 2500);
    }
}
