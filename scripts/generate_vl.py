#!/usr/bin/env python3
"""
Generate a signed Validator List (VL) JSON file for PostFiat networks.

A VL is a cryptographically signed JSON document that postfiatd nodes fetch
to determine which validators to trust. Each node's [validator_list_keys]
config holds the publisher's master public key, and the node verifies
the VL signature against it before accepting the list.

Architecture:
    Publisher key pair (generated once, stored securely):
        - Master key (Ed25519): identifies the publisher in node configs
        - Ephemeral signing key (secp256k1): signs the VL blob
        - Manifest: binds master key to signing key, signed by both
        - Token: contains the manifest + ephemeral secret key

    The VL JSON contains:
        - The publisher's manifest (so nodes can extract the signing key)
        - A base64-encoded blob (JSON with sequence, expiration, validators)
        - A hex-encoded signature over the raw blob bytes

    Signing process (must match what postfiatd verifies):
        1. Build inner blob as a JSON string
        2. Sign the raw JSON bytes with SHA-512-Half + secp256k1 ECDSA
        3. Base64-encode the same raw bytes for the output
        4. Hex-encode the DER signature

Prerequisites:
    pip3 install ecdsa

Usage:
    1. Create a config file (see example below)
    2. Run: python3 scripts/generate_vl.py config.json -o testnet_vl.json
    3. Upload the output to the URL in [validator_list_sites]

Config file format:
    {
        "publisher_token": "<base64 from 'validator-keys create_token --keyfile publisher-keys.json'>",
        "sequence": 1,
        "expiration": "2027-06-01",
        "validator_tokens": [
            "<base64 token for validator 1>",
            "<base64 token for validator 2>",
            "<base64 token for validator 3>",
            "<base64 token for validator 4>",
            "<base64 token for validator 5>"
        ]
    }

    - publisher_token: the token generated from the publisher key pair
    - sequence: must be higher than the previous VL's sequence (nodes reject <= current)
    - expiration: date (YYYY-MM-DD) when this VL expires (nodes reject expired VLs)
    - validator_tokens: tokens from each validator (generated with 'validator-keys create_token')

Generating keys:
    Publisher key pair (one-time, store securely):
        validator-keys create_keys --keyfile publisher-keys.json
        validator-keys create_token --keyfile publisher-keys.json

    Validator key pair (per validator):
        validator-keys create_keys
        validator-keys create_token

    The master public key from publisher-keys.json (converted to hex with
    scripts/base58_to_hex.py) goes into [validator_list_keys] in validators-*.txt.

Verification:
    Use --decode to inspect an existing VL without generating anything:
        python3 scripts/generate_vl.py --decode testnet_vl.json
"""

import argparse
import base64
import hashlib
import json
import sys
from datetime import datetime, timezone

try:
    from ecdsa import SigningKey, SECP256k1, util as ecdsa_util
except ImportError:
    sys.exit(
        "Missing dependency: 'ecdsa' package is required.\n"
        "Install with: pip3 install ecdsa"
    )

RIPPLE_EPOCH = 946684800  # Jan 1, 2000 00:00:00 UTC


def sha512_half(data: bytes) -> bytes:
    """XRPL SHA-512-Half: first 32 bytes of SHA-512."""
    return hashlib.sha512(data).digest()[:32]


def to_ripple_epoch(date_str: str) -> int:
    """Convert YYYY-MM-DD to XRPL epoch (seconds since Jan 1, 2000 UTC)."""
    dt = datetime.strptime(date_str, "%Y-%m-%d").replace(tzinfo=timezone.utc)
    unix_ts = int(dt.timestamp())
    ripple_ts = unix_ts - RIPPLE_EPOCH
    if ripple_ts <= 0:
        sys.exit(f"Error: expiration date {date_str} is before the XRPL epoch (2000-01-01)")
    return ripple_ts


def from_ripple_epoch(ripple_ts: int) -> str:
    """Convert XRPL epoch timestamp to human-readable UTC date string."""
    unix_ts = ripple_ts + RIPPLE_EPOCH
    return datetime.fromtimestamp(unix_ts, tz=timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")


def clean_token(token_str: str) -> str:
    """Strip [validator_token] header, whitespace, and newlines from a token string."""
    lines = token_str.strip().splitlines()
    return "".join(
        line.strip() for line in lines
        if line.strip() and not line.strip().startswith("[")
    )


def decode_token(token_str: str) -> dict:
    """Decode a validator token (base64 JSON with manifest + signing key)."""
    cleaned = clean_token(token_str)
    try:
        return json.loads(base64.b64decode(cleaned))
    except Exception as e:
        sys.exit(f"Error decoding token: {e}\nToken starts with: {cleaned[:40]}...")


def parse_manifest(manifest_b64: str) -> dict:
    """
    Parse an XRPL manifest (serialized STObject binary) to extract:
        - master_public_key: hex 33-byte Ed25519 master key (sfPublicKey)
        - signing_public_key: hex 33-byte ephemeral key (sfSigningPubKey)
        - signing_key_type: 'ed25519' or 'secp256k1'

    XRPL STObject wire format:
        Each field starts with a type/field identifier byte:
            high nibble = type code, low nibble = field code
            if either is 0, the next byte(s) contain the extended code
        Field types used in manifests:
            2 = uint32 (4 bytes, big-endian)
            7 = blob (variable-length: 1-byte length if < 193, else 2-byte)
    """
    data = base64.b64decode(manifest_b64)
    result = {}
    i = 0

    while i < len(data):
        byte = data[i]
        i += 1

        type_code = (byte >> 4) & 0x0F
        field_code = byte & 0x0F

        if type_code == 0:
            if i >= len(data):
                break
            type_code = data[i]
            i += 1
        if field_code == 0:
            if i >= len(data):
                break
            field_code = data[i]
            i += 1

        if type_code == 1:  # uint16
            i += 2
        elif type_code == 2:  # uint32
            i += 4
        elif type_code == 7:  # blob (variable length)
            if i >= len(data):
                break
            length = data[i]
            i += 1
            if length > 192:
                if i >= len(data):
                    break
                length = 193 + ((length - 193) * 256) + data[i]
                i += 1

            blob = data[i : i + length]

            if field_code == 1:  # sfPublicKey (master public key)
                result["master_public_key"] = blob.hex().upper()
            elif field_code == 3:  # sfSigningPubKey (ephemeral signing key)
                result["signing_public_key"] = blob.hex().upper()
                if blob[0] == 0xED:
                    result["signing_key_type"] = "ed25519"
                elif blob[0] in (0x02, 0x03):
                    result["signing_key_type"] = "secp256k1"
                else:
                    sys.exit(f"Error: unknown key type prefix 0x{blob[0]:02X}")

            i += length
        else:
            break

    if "master_public_key" not in result:
        sys.exit("Error: could not extract master public key from manifest")

    return result


def sign_secp256k1(secret_key_hex: str, data: bytes) -> str:
    """
    Sign data with secp256k1 ECDSA using XRPL's SHA-512-Half digest.
    Returns hex-encoded DER signature with canonical low-S value.
    """
    digest = sha512_half(data)
    sk = SigningKey.from_string(bytes.fromhex(secret_key_hex), curve=SECP256k1)
    sig = sk.sign_digest(digest, sigencode=ecdsa_util.sigencode_der_canonize)
    return sig.hex().upper()


def sign_ed25519(secret_key_hex: str, data: bytes) -> str:
    """
    Sign data with Ed25519 directly on raw bytes (no hashing).
    Returns hex-encoded 64-byte signature.
    """
    try:
        import nacl.signing
    except ImportError:
        sys.exit(
            "Missing dependency: 'PyNaCl' package is required for Ed25519 signing.\n"
            "Install with: pip3 install pynacl"
        )
    sk = nacl.signing.SigningKey(bytes.fromhex(secret_key_hex))
    return sk.sign(data).signature.hex().upper()


def sign_blob(data: bytes, secret_key_hex: str, key_type: str) -> str:
    """Sign raw blob bytes using the appropriate algorithm for the key type."""
    if key_type == "secp256k1":
        return sign_secp256k1(secret_key_hex, data)
    elif key_type == "ed25519":
        return sign_ed25519(secret_key_hex, data)
    else:
        sys.exit(f"Error: unsupported signing key type '{key_type}'")


def decode_existing_vl(path: str):
    """Decode and print the contents of an existing VL JSON file."""
    with open(path) as f:
        vl = json.load(f)

    print(f"Version: {vl.get('version', 'unknown')}")
    print(f"Publisher key: {vl.get('public_key', 'N/A')}")

    manifest_fields = parse_manifest(vl["manifest"])
    print(f"Master key (from manifest): {manifest_fields.get('master_public_key', 'N/A')}")
    print(f"Signing key type: {manifest_fields.get('signing_key_type', 'N/A')}")
    print(f"Signing key: {manifest_fields.get('signing_public_key', 'N/A')}")

    blobs = []
    if "blobs_v2" in vl:
        blobs = [(b["blob"], b["signature"]) for b in vl["blobs_v2"]]
    elif "blob" in vl:
        blobs = [(vl["blob"], vl["signature"])]

    for i, (blob_b64, sig) in enumerate(blobs):
        blob_json = base64.b64decode(blob_b64).decode("utf-8")
        blob = json.loads(blob_json)
        print(f"\nBlob {i + 1}:")
        print(f"  Sequence: {blob['sequence']}")
        print(f"  Expiration: {from_ripple_epoch(blob['expiration'])}")
        if "effective" in blob:
            print(f"  Effective: {from_ripple_epoch(blob['effective'])}")
        print(f"  Signature: {sig[:40]}...")
        print(f"  Validators ({len(blob['validators'])}):")
        for j, v in enumerate(blob["validators"]):
            print(f"    {j + 1}. {v['validation_public_key']}")


def generate_vl(config_path: str, output_path: str, version: int):
    """Generate a signed VL JSON file from a config file."""
    with open(config_path) as f:
        config = json.load(f)

    required = ["publisher_token", "sequence", "expiration", "validator_tokens"]
    for key in required:
        if key not in config:
            sys.exit(f"Error: missing required field '{key}' in config")

    # Parse publisher token
    publisher = decode_token(config["publisher_token"])
    publisher_manifest_b64 = publisher["manifest"]
    publisher_fields = parse_manifest(publisher_manifest_b64)
    publisher_secret = publisher["validation_secret_key"]
    key_type = publisher_fields.get("signing_key_type")

    if not key_type:
        sys.exit("Error: could not determine signing key type from publisher manifest")

    print(f"Publisher master key: {publisher_fields['master_public_key']}")
    print(f"Signing key type: {key_type}")

    # Parse validator tokens and extract their manifests + public keys
    validators = []
    for i, vtoken_str in enumerate(config["validator_tokens"]):
        vtoken = decode_token(vtoken_str)
        vfields = parse_manifest(vtoken["manifest"])
        validators.append({
            "validation_public_key": vfields["master_public_key"],
            "manifest": vtoken["manifest"],
        })
        print(f"Validator {i + 1}: {vfields['master_public_key']}")

    # Build inner blob JSON (key order and compact format must match XRPL expectations)
    sequence = config["sequence"]
    expiration = to_ripple_epoch(config["expiration"])
    now = to_ripple_epoch(datetime.now(timezone.utc).strftime("%Y-%m-%d"))

    if expiration <= now:
        sys.exit(f"Error: expiration date {config['expiration']} is in the past")

    blob_obj = {"sequence": sequence, "expiration": expiration}
    if "effective" in config:
        blob_obj["effective"] = to_ripple_epoch(config["effective"])
    blob_obj["validators"] = validators

    # Compact JSON with no whitespace (matches XRPL C++ construction)
    blob_json = json.dumps(blob_obj, separators=(",", ":"))
    blob_bytes = blob_json.encode("utf-8")

    # Sign the raw JSON bytes (NOT the base64-encoded version)
    signature = sign_blob(blob_bytes, publisher_secret, key_type)

    # Base64-encode the blob for the output
    blob_b64 = base64.b64encode(blob_bytes).decode("ascii")

    # Assemble outer VL JSON
    if version == 1:
        vl = {
            "public_key": publisher_fields["master_public_key"],
            "manifest": publisher_manifest_b64,
            "blob": blob_b64,
            "signature": signature,
            "version": 1,
        }
    else:
        vl = {
            "public_key": publisher_fields["master_public_key"],
            "manifest": publisher_manifest_b64,
            "blobs_v2": [{"signature": signature, "blob": blob_b64}],
            "version": 2,
        }

    with open(output_path, "w") as f:
        json.dump(vl, f, separators=(",", ":"))

    print(f"\nVL written to {output_path}")
    print(f"  Format: v{version}")
    print(f"  Sequence: {sequence}")
    print(f"  Expiration: {config['expiration']} ({from_ripple_epoch(expiration)})")
    print(f"  Validators: {len(validators)}")
    print(f"\n[validator_list_keys] value for node configs:")
    print(f"  {publisher_fields['master_public_key']}")


def main():
    parser = argparse.ArgumentParser(
        description="Generate or decode a signed Validator List (VL) JSON file."
    )
    parser.add_argument(
        "config",
        help="Path to JSON config file (generation mode) or VL JSON file (decode mode)",
    )
    parser.add_argument(
        "-o", "--output",
        default="vl.json",
        help="Output file path (default: vl.json)",
    )
    parser.add_argument(
        "--version",
        type=int,
        choices=[1, 2],
        default=2,
        help="VL format version (default: 2)",
    )
    parser.add_argument(
        "--decode",
        action="store_true",
        help="Decode and display an existing VL file instead of generating one",
    )
    args = parser.parse_args()

    if args.decode:
        decode_existing_vl(args.config)
    else:
        generate_vl(args.config, args.output, args.version)


if __name__ == "__main__":
    main()
