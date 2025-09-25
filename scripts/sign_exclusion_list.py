#!/usr/bin/env python3
"""
Sign an exclusion list JSON file for use with RemoteExclusionListFetcher

This script takes a secret key in hex format and an exclusion list JSON file,
and adds a signature to the JSON that can be verified by the RemoteExclusionListFetcher.

Usage:
    python3 sign_exclusion_list.py --secret-key <hex_secret_key> --input exclusions.json --output signed_exclusions.json
"""

import json
import hashlib
import argparse
import sys
from typing import Dict, List, Any
import base58
import nacl.signing
import nacl.encoding

def decode_xrpl_address(address: str) -> bytes:
    """Decode an XRPL address to bytes"""
    # XRPL addresses use base58 with specific alphabet
    return base58.b58decode(address)

def create_signing_message(exclusion_data: Dict[str, Any]) -> str:
    """
    Create the canonical message for signing, matching the C++ implementation
    """
    # Extract addresses from exclusions
    addresses = []
    if "exclusions" in exclusion_data and isinstance(exclusion_data["exclusions"], list):
        for entry in exclusion_data["exclusions"]:
            if "address" in entry:
                addresses.append(entry["address"])

    # Sort addresses to ensure consistent ordering
    addresses.sort()

    # Create message as concatenated sorted addresses
    message_parts = []
    for addr in addresses:
        message_parts.append(addr)
        message_parts.append("\n")

    address_string = "".join(message_parts)

    # Get version, timestamp, and issuer_address
    version = exclusion_data.get("version", "1.0")
    timestamp = exclusion_data.get("timestamp", "")
    issuer_address = exclusion_data.get("issuer_address", "")

    # Create final message matching C++ format:
    # "v1:" + version + ":" + timestamp + ":" + issuer_address + ":" + address_string
    message = f"v1:{version}:{timestamp}:{issuer_address}:{address_string}"

    return message

def sign_message_ed25519(message: str, secret_key_hex: str) -> str:
    """
    Sign a message using Ed25519 and return hex signature
    """
    # Convert hex secret key to bytes
    secret_key_bytes = bytes.fromhex(secret_key_hex)

    # Ed25519 secret keys in NaCl are 32 bytes
    if len(secret_key_bytes) != 32:
        raise ValueError(f"Ed25519 secret key must be 32 bytes, got {len(secret_key_bytes)}")

    # Create signing key
    signing_key = nacl.signing.SigningKey(secret_key_bytes)

    # Sign the message
    signed = signing_key.sign(message.encode('utf-8'))

    # Return just the signature part (64 bytes) as hex
    signature_hex = signed.signature.hex()

    return signature_hex

def derive_public_key_ed25519(secret_key_hex: str) -> str:
    """
    Derive Ed25519 public key from secret key and return as base58 node public key
    """
    # Convert hex secret key to bytes
    secret_key_bytes = bytes.fromhex(secret_key_hex)

    # Create signing key
    signing_key = nacl.signing.SigningKey(secret_key_bytes)

    # Get public key
    public_key_bytes = bytes(signing_key.verify_key)

    # For XRPL node public keys, we need to add the prefix
    # ED prefix is 0xED (237 in decimal) for Ed25519 keys
    prefixed_key = b'\xed' + public_key_bytes

    # Calculate double SHA-256 checksum
    hash1 = hashlib.sha256(prefixed_key).digest()
    hash2 = hashlib.sha256(hash1).digest()
    checksum = hash2[:4]

    # Combine prefix + key + checksum
    full_key = prefixed_key + checksum

    # Convert to base58
    return base58.b58encode(full_key).decode('ascii')

def sign_exclusion_list(exclusion_data: Dict[str, Any], secret_key_hex: str, algorithm: str = "ed25519") -> Dict[str, Any]:
    """
    Sign an exclusion list and add the signature to the JSON
    """
    # Create the message to sign
    message = create_signing_message(exclusion_data)

    print(f"Message to sign:\n{message}")
    print(f"Message length: {len(message)} bytes")

    if algorithm == "ed25519":
        # Sign the message
        signature_hex = sign_message_ed25519(message, secret_key_hex)

        # Derive public key
        public_key_base58 = derive_public_key_ed25519(secret_key_hex)

        print(f"Public key (base58): {public_key_base58}")
        print(f"Signature (hex): {signature_hex}")

        # Add signature to the JSON
        exclusion_data["signature"] = {
            "algorithm": "ed25519",
            "public_key": public_key_base58,
            "signature": signature_hex
        }
    else:
        raise ValueError(f"Unsupported algorithm: {algorithm}")

    return exclusion_data

def main():
    parser = argparse.ArgumentParser(description="Sign an exclusion list JSON file")
    parser.add_argument("--secret-key", required=True, help="Secret key in hex format")
    parser.add_argument("--input", required=True, help="Input JSON file path")
    parser.add_argument("--output", required=True, help="Output signed JSON file path")
    parser.add_argument("--algorithm", default="ed25519", choices=["ed25519"], help="Signing algorithm (currently only ed25519 supported)")

    args = parser.parse_args()

    # Read input JSON
    try:
        with open(args.input, 'r') as f:
            exclusion_data = json.load(f)
    except Exception as e:
        print(f"Error reading input file: {e}", file=sys.stderr)
        sys.exit(1)

    # Sign the exclusion list
    try:
        signed_data = sign_exclusion_list(exclusion_data, args.secret_key, args.algorithm)
    except Exception as e:
        print(f"Error signing exclusion list: {e}", file=sys.stderr)
        sys.exit(1)

    # Write output JSON
    try:
        with open(args.output, 'w') as f:
            json.dump(signed_data, f, indent=2)
        print(f"Successfully signed exclusion list and wrote to {args.output}")
    except Exception as e:
        print(f"Error writing output file: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()