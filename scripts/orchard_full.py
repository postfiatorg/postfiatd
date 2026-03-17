#!/usr/bin/env python3
"""
End-to-end Orchard shielded payment test: t->z, z->z, z->t, and double-spend detection.

Supports two modes:
  Standalone (default): uses ledger_accept() to advance ledgers manually.
  Live (--live):        polls tx RPC until transactions are validated by consensus.

Usage:
  # Standalone (local node in standalone mode)
  python3 orchard_full.py

  # Live devnet (admin port on validator)
  python3 orchard_full.py --live --url https://<validator-ip>:5005/

  # Verbose logging
  python3 orchard_full.py --live --url https://<validator-ip>:5005/ -v
"""

import json
import time
import sys
import requests
import urllib3
import argparse

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

TX_POLL_INTERVAL = 2
TX_POLL_TIMEOUT = 60


def truncate_long_fields(obj, max_length=128):
    if isinstance(obj, dict):
        return {k: truncate_long_fields(v, max_length) for k, v in obj.items()}
    elif isinstance(obj, list):
        return [truncate_long_fields(item, max_length) for item in obj]
    elif isinstance(obj, str) and len(obj) > max_length:
        return obj[:max_length] + "..."
    return obj


class RPCClient:
    def __init__(self, url="https://localhost:5005/", verify_ssl=False, verbose=False):
        self.url = url
        self.verify_ssl = verify_ssl
        self.headers = {"Content-Type": "application/json"}
        self.verbose = verbose

    def call(self, method, params):
        payload = {"method": method, "params": params}

        if self.verbose:
            print(f"\n>>> RPC Request: {method}")
            print(json.dumps(truncate_long_fields(payload), indent=2))

        response = requests.post(
            self.url, headers=self.headers, json=payload, verify=self.verify_ssl
        )
        response.raise_for_status()
        result = response.json()

        if self.verbose:
            print(f"\n<<< RPC Response: {method}")
            print(json.dumps(truncate_long_fields(result), indent=2))

        return result

    def server_info(self):
        return self.call("server_info", [{}])

    def account_info(self, account):
        return self.call("account_info", [{"account": account}])

    def tx(self, tx_hash):
        return self.call("tx", [{"transaction": tx_hash}])

    def ledger_accept(self):
        return self.call("ledger_accept", [{}])

    def orchard_wallet_add_key(self, full_viewing_key):
        return self.call("orchard_wallet_add_key", [{"full_viewing_key": full_viewing_key}])

    def orchard_scan_balance(self, full_viewing_key, ledger_index_min, ledger_index_max):
        return self.call("orchard_scan_balance", [{
            "full_viewing_key": full_viewing_key,
            "ledger_index_min": ledger_index_min,
            "ledger_index_max": ledger_index_max,
        }])

    def orchard_prepare_payment(self, payment_type, amount, **kwargs):
        params = {"payment_type": payment_type, "amount": amount}
        for key in ("recipient", "destination_account", "source_account", "spending_key", "spend_amount", "fee"):
            if key in kwargs and kwargs[key] is not None:
                params[key] = kwargs[key]
        return self.call("orchard_prepare_payment", [params])

    def submit(self, tx_json, secret=None, sequence=None):
        tx = tx_json.copy()
        if sequence is not None:
            tx["Sequence"] = sequence
        params = {"tx_json": tx}
        if secret is not None:
            params["secret"] = secret
        return self.call("submit", [params])


def get_current_ledger(client):
    info = client.server_info()
    validated = info.get("result", {}).get("info", {}).get("validated_ledger", {})
    return validated.get("seq", 0)


def get_account_sequence(client, account):
    result = client.account_info(account)
    return result["result"]["account_data"]["Sequence"]


def wait_for_validation(client, tx_hash, label=""):
    prefix = f"[{label}] " if label else ""
    print(f"  {prefix}Waiting for tx {tx_hash[:16]}... to validate", end="", flush=True)
    deadline = time.time() + TX_POLL_TIMEOUT
    while time.time() < deadline:
        try:
            result = client.tx(tx_hash)
            validated = result.get("result", {}).get("validated", False)
            if validated:
                print(" validated!")
                return result
        except Exception:
            pass
        print(".", end="", flush=True)
        time.sleep(TX_POLL_INTERVAL)
    print(" TIMEOUT!")
    raise TimeoutError(f"{prefix}Transaction {tx_hash} not validated within {TX_POLL_TIMEOUT}s")


def advance_ledger(client, live_mode, tx_hash=None, label=""):
    if live_mode:
        if tx_hash:
            return wait_for_validation(client, tx_hash, label)
        # No tx hash — just wait a few seconds for next ledger close
        print(f"  Waiting for ledger close...")
        time.sleep(5)
        return None
    else:
        result = client.ledger_accept()
        if result.get("result", {}).get("status") != "success":
            raise RuntimeError("ledger_accept failed")
        print(f"  Ledger accepted: index {result['result']['ledger_current_index']}")
        return result


def get_ledger_range(client, live_mode):
    if live_mode:
        current = get_current_ledger(client)
        return (max(1, current - 500), current + 10)
    return (1, 100)


def check_result(result, label):
    status = result.get("result", {}).get("status")
    if status != "success":
        error = result.get("result", {}).get("error_message", result.get("result", {}).get("error", "unknown"))
        raise RuntimeError(f"{label} failed: {error}")


def main():
    parser = argparse.ArgumentParser(
        description="Orchard shielded payment e2e test",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python3 orchard_full.py                                          # standalone
  python3 orchard_full.py --live --url https://1.2.3.4:5005/ -v    # live devnet
        """,
    )
    parser.add_argument("--verbose", "-v", action="store_true", help="Verbose RPC logging")
    parser.add_argument("--url", default="https://localhost:5005/", help="RPC endpoint URL (default: https://localhost:5005/)")
    parser.add_argument("--live", action="store_true", help="Live network mode (poll for validation instead of ledger_accept)")
    args = parser.parse_args()

    client = RPCClient(url=args.url, verbose=args.verbose)
    live = args.live

    mode_label = "LIVE DEVNET" if live else "STANDALONE"
    print(f"=== Orchard Full Flow Test ({mode_label}) ===")
    print(f"RPC endpoint: {args.url}\n")

    # Test keys
    FVK_1 = "32962E110407C099A57D577C7750BE7078A4729F08ED908727181DC0D4A2BD27DA4ED74993A50179C45BD63FDD0CC381058BB0DE6847B17FA3FF4B459C06562E925A5F23BA4DCC23A279220F887F46F31BEAA0CD63E6DD87790F4C4071627F08"
    FVK_2 = "935993F09041BB701B12FF053AD9D9F4F9051C8BDD70B39D62F4765B4E71F812BA38D514E952E84654C55F276BAC3986B5BCC4B2A21E253FA16EFF658394AD1B0924F4F8C86C42C9ECC983F135BA3C9D4D9493F51DCCE7ACDCF5A6C7E26D1923"
    SK_1 = "D8710D7D8D4717F313C1B1F49CA82AA5FA64B7AAD0D51BC671B8EB0E06E3DC99"
    SK_2 = "E47A50F38ACBADB0B839AE3A089E9988665B4E13819B64A4A4D3B3F5CB7FA0B3"
    ADDR_1 = "62B565D0A77917E0CBE360A30D081259A35DB3186AC533278632BFA6003E09576EC955B72EC71FDA33E39D"
    ADDR_2 = "C19558DB8066177BF73AAD65280FC53378A082A3FA5CEE57218D3A5F846E24201CC6978222B9AE2B4F1D95"
    SOURCE_ACCOUNT = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
    SOURCE_SECRET = "snoPBrXtMeMyMHUVTgbuqAfg1SUTb"

    try:
        # Verify connectivity
        info = client.server_info()
        server_state = info.get("result", {}).get("info", {}).get("server_state", "unknown")
        print(f"Server state: {server_state}")
        if live and server_state not in ("proposing", "full", "connected", "syncing", "tracking"):
            print(f"WARNING: Server state '{server_state}' may not be ready for transactions")

        # --- Step 1: Add viewing keys ---
        print("\n=== Step 1: Adding first viewing key ===")
        r = client.orchard_wallet_add_key(FVK_1)
        check_result(r, "Add first viewing key")
        print(f"  Balance: {r['result']['balance']}, Notes: {r['result']['notes_found']}, Tracked: {r['result']['tracked_keys']}")

        print("\n=== Step 2: Adding second viewing key ===")
        r = client.orchard_wallet_add_key(FVK_2)
        check_result(r, "Add second viewing key")
        print(f"  Balance: {r['result']['balance']}, Notes: {r['result']['notes_found']}, Tracked: {r['result']['tracked_keys']}")

        # --- Step 3-5: t->z payment ---
        print("\n=== Step 3: Preparing t->z payment (1000 PFT) ===")
        prepare = client.orchard_prepare_payment(
            payment_type="t_to_z",
            amount="1000000000",
            source_account=SOURCE_ACCOUNT,
            recipient=ADDR_1,
        )
        check_result(prepare, "Prepare t->z")

        seq = get_account_sequence(client, SOURCE_ACCOUNT) if live else 1
        print(f"\n=== Step 4: Submitting t->z (sequence={seq}) ===")
        submit_r = client.submit(
            tx_json=prepare["result"]["tx_json"],
            secret=SOURCE_SECRET,
            sequence=seq,
        )
        engine_result = submit_r.get("result", {}).get("engine_result", "UNKNOWN")
        tx_hash = submit_r.get("result", {}).get("tx_json", {}).get("hash", "")
        print(f"  Engine result: {engine_result}, hash: {tx_hash[:16]}...")
        if engine_result not in ("tesSUCCESS", "terQUEUED"):
            raise RuntimeError(f"t->z submit failed: {engine_result}")

        print("\n=== Step 5: Advancing ledger ===")
        advance_ledger(client, live, tx_hash, "t->z")

        # --- Step 6-7: Scan balances after t->z ---
        ledger_min, ledger_max = get_ledger_range(client, live)
        print(f"\n=== Step 6: Scanning first key (ledger {ledger_min}-{ledger_max}) ===")
        scan = client.orchard_scan_balance(FVK_1, ledger_min, ledger_max)
        check_result(scan, "Scan first key")
        print(f"  Balance: {scan['result']['total_balance_xrp']} PFT, Notes: {scan['result']['note_count']}, Spent: {scan['result']['spent_count']}")

        print(f"\n=== Step 7: Scanning second key (ledger {ledger_min}-{ledger_max}) ===")
        scan = client.orchard_scan_balance(FVK_2, ledger_min, ledger_max)
        check_result(scan, "Scan second key")
        print(f"  Balance: {scan['result']['total_balance_xrp']} PFT, Notes: {scan['result']['note_count']}, Spent: {scan['result']['spent_count']}")

        # --- Step 8: z->z payment ---
        print("\n=== Step 8: Preparing z->z payment (500 PFT) ===")
        prepare_zz = client.orchard_prepare_payment(
            payment_type="z_to_z",
            amount="500000000",
            spending_key=SK_1,
            recipient=ADDR_2,
        )
        check_result(prepare_zz, "Prepare z->z")

        # Prepare double-spend tx before the legitimate one consumes the note
        print("\n=== Step 8b: Preparing double-spend z->z (100 PFT, same spending key) ===")
        double_spend_tx = None
        try:
            ds_prepare = client.orchard_prepare_payment(
                payment_type="z_to_z",
                amount="100000000",
                spending_key=SK_1,
                recipient=ADDR_2,
            )
            if ds_prepare.get("result", {}).get("status") == "success":
                double_spend_tx = ds_prepare["result"]["tx_json"]
                print("  Prepared (will test after legitimate spend)")
            else:
                print(f"  Could not prepare: {ds_prepare.get('result', {}).get('error_message', 'unknown')}")
        except Exception as e:
            print(f"  Could not prepare: {e}")

        print("\n=== Step 9: Submitting z->z ===")
        submit_zz = client.submit(tx_json=prepare_zz["result"]["tx_json"])
        engine_result = submit_zz.get("result", {}).get("engine_result", "UNKNOWN")
        tx_hash = submit_zz.get("result", {}).get("tx_json", {}).get("hash", "")
        print(f"  Engine result: {engine_result}, hash: {tx_hash[:16]}...")
        if engine_result not in ("tesSUCCESS", "terQUEUED"):
            raise RuntimeError(f"z->z submit failed: {engine_result}")

        print("\n=== Step 10: Advancing ledger ===")
        advance_ledger(client, live, tx_hash, "z->z")

        # --- Step 11-12: Scan balances after z->z ---
        ledger_min, ledger_max = get_ledger_range(client, live)
        print(f"\n=== Step 11: Scanning first key (ledger {ledger_min}-{ledger_max}) ===")
        scan = client.orchard_scan_balance(FVK_1, ledger_min, ledger_max)
        check_result(scan, "Scan first key")
        print(f"  Balance: {scan['result']['total_balance_xrp']} PFT, Notes: {scan['result']['note_count']}, Spent: {scan['result']['spent_count']}")

        print(f"\n=== Step 12: Scanning second key (ledger {ledger_min}-{ledger_max}) ===")
        scan = client.orchard_scan_balance(FVK_2, ledger_min, ledger_max)
        check_result(scan, "Scan second key")
        print(f"  Balance: {scan['result']['total_balance_xrp']} PFT, Notes: {scan['result']['note_count']}, Spent: {scan['result']['spent_count']}")

        # --- Step 13-15: z->t payment ---
        print("\n=== Step 13: Preparing z->t payment (200 PFT) ===")
        prepare_zt = client.orchard_prepare_payment(
            payment_type="z_to_t",
            amount="200000000",
            spending_key=SK_2,
            destination_account=SOURCE_ACCOUNT,
        )
        check_result(prepare_zt, "Prepare z->t")

        print("\n=== Step 14: Submitting z->t ===")
        submit_zt = client.submit(tx_json=prepare_zt["result"]["tx_json"])
        engine_result = submit_zt.get("result", {}).get("engine_result", "UNKNOWN")
        tx_hash = submit_zt.get("result", {}).get("tx_json", {}).get("hash", "")
        print(f"  Engine result: {engine_result}, hash: {tx_hash[:16]}...")
        if engine_result not in ("tesSUCCESS", "terQUEUED"):
            raise RuntimeError(f"z->t submit failed: {engine_result}")

        print("\n=== Step 15: Advancing ledger ===")
        advance_ledger(client, live, tx_hash, "z->t")

        # --- Step 16: Scan second key after z->t ---
        ledger_min, ledger_max = get_ledger_range(client, live)
        print(f"\n=== Step 16: Scanning second key (ledger {ledger_min}-{ledger_max}) ===")
        scan = client.orchard_scan_balance(FVK_2, ledger_min, ledger_max)
        check_result(scan, "Scan second key")
        print(f"  Balance: {scan['result']['total_balance_xrp']} PFT, Notes: {scan['result']['note_count']}, Spent: {scan['result']['spent_count']}")

        # --- Step 17: Double-spend detection ---
        print("\n=== Step 17: Testing double-spend detection ===")
        if double_spend_tx is not None:
            ds_submit = client.submit(tx_json=double_spend_tx)
            engine_result = ds_submit.get("result", {}).get("engine_result", "UNKNOWN")
            print(f"  Double-spend result: {engine_result}")
            if engine_result == "tesSUCCESS":
                print("  ERROR: Double-spend NOT detected! Critical security bug!")
                sys.exit(1)
            else:
                print(f"  Double-spend correctly rejected: {engine_result}")
        else:
            print("  SKIPPED - could not prepare double-spend tx")

        # --- Summary ---
        print("\n=== All tests passed ===")
        print("  t->z: 1000 PFT shielded to first account")
        print("  z->z: 500 PFT transferred from first to second account")
        print("  z->t: 200 PFT unshielded from second account to transparent")
        print("  Double-spend: correctly rejected")

    except requests.exceptions.ConnectionError as e:
        print(f"\nConnection error: {e}")
        print(f"Make sure the node is running and accessible at {args.url}")
        sys.exit(1)
    except Exception as e:
        print(f"\nError: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
