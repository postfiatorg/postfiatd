//! Generate Orchard test bundles
//!
//! This tool generates valid Orchard bundles for testing purposes.
//! It outputs the serialized bundle as hex, which can be used in C++ tests.

use orchard::{
    builder::Builder,
    bundle::Flags,
    keys::{FullViewingKey, Scope, SpendingKey},
    tree::MerkleHashOrchard,
    value::NoteValue,
    Address, Anchor,
};
use rand::rngs::OsRng;
use std::env;

/// Generate a deterministic spending key for testing
fn generate_test_spending_key(seed_byte: u8) -> SpendingKey {
    let mut seed = [0u8; 32];
    seed[0] = seed_byte;
    SpendingKey::from_bytes(seed).expect("Valid seed for test key")
}

/// Generate a recipient address from a spending key
fn get_address_from_sk(sk: &SpendingKey, index: u32) -> Address {
    let fvk = FullViewingKey::from(sk);
    fvk.address_at(index, Scope::External)
}

/// Get the empty anchor
fn get_empty_anchor() -> Anchor {
    Anchor::from(MerkleHashOrchard::empty_root(32.into()))
}

/// Create a transparent-to-shielded bundle
fn build_transparent_to_shielded(
    amount_drops: u64,
    recipient: Address,
    anchor: Anchor,
) -> Result<Vec<u8>, String> {
    let flags = Flags::from_parts(true, true);
    let mut builder = Builder::new(flags, anchor);

    builder
        .add_output(None, recipient, NoteValue::from_raw(amount_drops), None)
        .map_err(|e| format!("Failed to add output: {:?}", e))?;

    let mut rng = OsRng;
    let unproven = builder
        .build(&mut rng)
        .map_err(|e| format!("Failed to build bundle: {:?}", e))?;

    match unproven {
        Some(unproven_bundle) => {
            let pk = orchard::circuit::ProvingKey::build();
            let proven = unproven_bundle
                .create_proof(&pk, &mut rng)
                .map_err(|e| format!("Failed to create proof: {:?}", e))?;

            let dummy_sighash = [0u8; 32];
            let authorized = proven
                .apply_signatures(&mut rng, dummy_sighash, &[])
                .map_err(|e| format!("Failed to apply signatures: {:?}", e))?;

            let mut bundle_bytes = Vec::new();
            zcash_primitives::transaction::components::orchard::write_v5_bundle(
                Some(&authorized),
                &mut bundle_bytes,
            )
            .map_err(|e| format!("Failed to serialize bundle: {:?}", e))?;

            Ok(bundle_bytes)
        }
        None => Err("Builder produced empty bundle".to_string()),
    }
}

fn main() {
    let args: Vec<String> = env::args().collect();

    if args.len() < 2 {
        eprintln!("Usage: {} <amount_in_drops> [recipient_seed]", args[0]);
        eprintln!();
        eprintln!("Examples:");
        eprintln!("  {} 1000000000  # Generate bundle for 1000 XRP (1 billion drops)", args[0]);
        eprintln!("  {} 100 42      # Generate bundle for 100 drops to recipient from seed 42", args[0]);
        std::process::exit(1);
    }

    let amount: u64 = args[1].parse().expect("Amount must be a valid number");
    let recipient_seed: u8 = args
        .get(2)
        .map(|s| s.parse().expect("Recipient seed must be 0-255"))
        .unwrap_or(42);

    eprintln!("Generating Orchard bundle...");
    eprintln!("  Amount: {} drops", amount);
    eprintln!("  Recipient seed: {}", recipient_seed);
    eprintln!();
    eprintln!("This will take ~5-10 seconds for proof generation...");

    let recipient_sk = generate_test_spending_key(recipient_seed);
    let recipient_addr = get_address_from_sk(&recipient_sk, 0);
    let anchor = get_empty_anchor();

    match build_transparent_to_shielded(amount, recipient_addr, anchor) {
        Ok(bundle_bytes) => {
            eprintln!();
            eprintln!("Bundle generated successfully!");
            eprintln!("  Size: {} bytes", bundle_bytes.len());
            eprintln!();
            eprintln!("Hex-encoded bundle:");
            println!("{}", hex::encode(&bundle_bytes));
        }
        Err(e) => {
            eprintln!("Error generating bundle: {}", e);
            std::process::exit(1);
        }
    }
}
