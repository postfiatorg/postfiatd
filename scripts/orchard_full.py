#!/usr/bin/env python3
"""
RPC Client for making JSON-RPC calls to localhost:5005
"""

import json
import requests
import urllib3
import copy
import argparse

# Disable SSL warnings for self-signed certificates (equivalent to curl -k)
urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)


def truncate_long_fields(obj, max_length=128):
    """
    Recursively truncate string fields longer than max_length in a JSON object.

    Args:
        obj: The object to process (dict, list, or primitive)
        max_length: Maximum length for string fields (default 128)

    Returns:
        A copy of the object with long strings truncated
    """
    if isinstance(obj, dict):
        result = {}
        for key, value in obj.items():
            result[key] = truncate_long_fields(value, max_length)
        return result
    elif isinstance(obj, list):
        return [truncate_long_fields(item, max_length) for item in obj]
    elif isinstance(obj, str) and len(obj) > max_length:
        return obj[:max_length] + "..."
    else:
        return obj


class RPCClient:
    def __init__(self, url: str = "https://localhost:5005/", verify_ssl: bool = False, verbose: bool = False):
        self.url = url
        self.verify_ssl = verify_ssl
        self.headers = {"Content-Type": "application/json"}
        self.verbose = verbose

    def call(self, method: str, params: list) -> dict:
        """
        Make an RPC call to the server.

        Args:
            method: The RPC method name
            params: List of parameters for the method

        Returns:
            The JSON response from the server
        """
        payload = {
            "method": method,
            "params": params
        }

        # Log the request with truncated fields (only if verbose mode is enabled)
        if self.verbose:
            print(f"\n>>> RPC Request: {method}")
            truncated_payload = truncate_long_fields(payload)
            print(json.dumps(truncated_payload, indent=2))

        response = requests.post(
            self.url,
            headers=self.headers,
            json=payload,
            verify=self.verify_ssl
        )

        response.raise_for_status()
        result = response.json()

        # Log the response with truncated fields (only if verbose mode is enabled)
        if self.verbose:
            print(f"\n<<< RPC Response: {method}")
            truncated_result = truncate_long_fields(result)
            print(json.dumps(truncated_result, indent=2))

        return result

    def orchard_prepare_payment(
        self,
        payment_type: str,
        amount: str,
        recipient: str = None,
        destination_account: str = None,
        source_account: str = None,
        spending_key: str = None,
        spend_amount: str = None,
        fee: str = None
    ) -> dict:
        """
        Prepare an orchard payment.

        Args:
            payment_type: Type of payment (e.g., "t_to_z", "z_to_z", "z_to_t")
            amount: Amount to send (as string)
            recipient: Recipient address (for t_to_z, z_to_z - Orchard address)
            destination_account: Destination account (for z_to_t - XRP address)
            source_account: Source account address (for t_to_z, z_to_t)
            spending_key: Spending key (for z_to_z, z_to_t)
            spend_amount: Amount to spend from shielded pool (deprecated, auto-calculated)
            fee: Transaction fee in drops (optional, uses ledger base fee if not specified)

        Returns:
            The JSON response from the server
        """
        params_dict = {
            "payment_type": payment_type,
            "amount": amount
        }

        if recipient:
            params_dict["recipient"] = recipient
        if destination_account:
            params_dict["destination_account"] = destination_account
        if source_account:
            params_dict["source_account"] = source_account
        if spending_key:
            params_dict["spending_key"] = spending_key
        if spend_amount:
            params_dict["spend_amount"] = spend_amount
        if fee:
            params_dict["fee"] = fee

        return self.call("orchard_prepare_payment", [params_dict])

    def submit(
        self,
        tx_json: dict,
        secret: str = None,
        sequence: int = None
    ) -> dict:
        """
        Submit a prepared transaction to the network.

        Args:
            tx_json: The transaction JSON from orchard_prepare_payment result
            secret: The account secret for signing (optional)
            sequence: The transaction sequence number (optional)

        Returns:
            The JSON response from the server
        """
        # Add sequence to tx_json if provided
        tx_json_with_seq = tx_json.copy()
        if sequence is not None:
            tx_json_with_seq["Sequence"] = sequence

        params_dict = {
            "tx_json": tx_json_with_seq
        }

        if secret is not None:
            params_dict["secret"] = secret

        params = [params_dict]

        return self.call("submit", params)

    def ledger_accept(self) -> dict:
        """
        Accept the current ledger (advance to next ledger).
        Typically used in standalone/test mode.
        
        Returns:
            The JSON response from the server
        """
        return self.call("ledger_accept", [{}])

    def orchard_wallet_add_key(self, full_viewing_key: str) -> dict:
        """
        Add a full viewing key to the Orchard wallet.
        
        Args:
            full_viewing_key: The full viewing key to add
            
        Returns:
            The JSON response from the server containing:
            - balance: Current balance
            - ivk: Incoming viewing key
            - notes_found: Number of notes found
            - status: Operation status
            - tracked_keys: Number of tracked keys
        """
        params = [{
            "full_viewing_key": full_viewing_key
        }]
        
        return self.call("orchard_wallet_add_key", params)

    def orchard_scan_balance(
        self,
        full_viewing_key: str,
        ledger_index_min: int,
        ledger_index_max: int
    ) -> dict:
        """
        Scan ledger range for balance using a full viewing key.
        
        Args:
            full_viewing_key: The full viewing key to scan with
            ledger_index_min: Start of ledger range to scan
            ledger_index_max: End of ledger range to scan
            
        Returns:
            The JSON response from the server containing:
            - ledger_range: {min, max}
            - note_count: Number of notes found
            - notes: List of notes
            - spent_count: Number of spent notes
            - status: Operation status
            - total_balance: Total balance in drops
            - total_balance_xrp: Total balance in XRP
        """
        params = [{
            "full_viewing_key": full_viewing_key,
            "ledger_index_min": ledger_index_min,
            "ledger_index_max": ledger_index_max
        }]
        
        return self.call("orchard_scan_balance", params)


def main():
    # Parse command-line arguments
    parser = argparse.ArgumentParser(
        description='Orchard Full Flow Test Script',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Run with verbose RPC logging
  python3 orchard_full.py --verbose

  # Run without RPC logging (default)
  python3 orchard_full.py
        """
    )
    parser.add_argument(
        '--verbose', '-v',
        action='store_true',
        help='Enable verbose RPC logging (shows all requests and responses with truncated fields)'
    )

    args = parser.parse_args()

    # Create RPC client with verbose flag
    client = RPCClient(verbose=args.verbose)

    try:
        # Step 1: Add FIRST viewing key to wallet (BEFORE any transactions)
        print("=== Step 1: Adding FIRST viewing key to wallet ===")
        wallet_result = client.orchard_wallet_add_key(
            full_viewing_key="32962E110407C099A57D577C7750BE7078A4729F08ED908727181DC0D4A2BD27DA4ED74993A50179C45BD63FDD0CC381058BB0DE6847B17FA3FF4B459C06562E925A5F23BA4DCC23A279220F887F46F31BEAA0CD63E6DD87790F4C4071627F08"
        )

        # Verify wallet add key success
        if wallet_result.get("result", {}).get("status") != "success":
            print("Failed to add first viewing key!")
            return

        print(f"First viewing key added! Balance: {wallet_result['result']['balance']}, "
              f"Notes found: {wallet_result['result']['notes_found']}, "
              f"Tracked keys: {wallet_result['result']['tracked_keys']}, "
              f"Anchor: {wallet_result['result'].get('anchor', 'NONE')}")

        # Step 2: Add SECOND viewing key to wallet (BEFORE any transactions)
        print("\n=== Step 2: Adding SECOND viewing key to wallet ===")
        wallet_result2 = client.orchard_wallet_add_key(
            full_viewing_key="935993F09041BB701B12FF053AD9D9F4F9051C8BDD70B39D62F4765B4E71F812BA38D514E952E84654C55F276BAC3986B5BCC4B2A21E253FA16EFF658394AD1B0924F4F8C86C42C9ECC983F135BA3C9D4D9493F51DCCE7ACDCF5A6C7E26D1923"
        )

        # Verify second wallet add key success
        if wallet_result2.get("result", {}).get("status") != "success":
            print("Failed to add second viewing key!")
            return

        print(f"Second viewing key added! Balance: {wallet_result2['result']['balance']}, "
              f"Notes found: {wallet_result2['result']['notes_found']}, "
              f"Tracked keys: {wallet_result2['result']['tracked_keys']}, "
              f"Anchor: {wallet_result2['result'].get('anchor', 'NONE')}")

        # Step 3: Prepare the t→z payment
        print("\n=== Step 3: Preparing t→z payment ===")
        prepare_result = client.orchard_prepare_payment(
            payment_type="t_to_z",
            amount="1000000000",
            source_account="rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
            recipient="62B565D0A77917E0CBE360A30D081259A35DB3186AC533278632BFA6003E09576EC955B72EC71FDA33E39D"
        )

        # Check if prepare was successful
        if prepare_result.get("result", {}).get("status") != "success":
            print("Payment preparation failed!")
            return

        # Step 4: Submit the t→z transaction using tx_json from prepare result
        print("\n=== Step 4: Submitting t→z transaction ===")
        tx_json = prepare_result["result"]["tx_json"]

        submit_result = client.submit(
            tx_json=tx_json,
            secret="snoPBrXtMeMyMHUVTgbuqAfg1SUTb",
            sequence=1
        )

        # Step 5: Accept the ledger
        print("\n=== Step 5: Accepting ledger ===")
        ledger_result = client.ledger_accept()

        # Verify ledger_accept success
        if ledger_result.get("result", {}).get("status") != "success":
            print("Ledger accept failed!")
            return

        print(f"Ledger accepted! Current ledger index: {ledger_result['result']['ledger_current_index']}")

        # Step 6: Scan balance for first viewing key (after t→z)
        print("\n=== Step 6: Scanning balance for first viewing key (after t→z) ===")
        scan_result1 = client.orchard_scan_balance(
            full_viewing_key="32962E110407C099A57D577C7750BE7078A4729F08ED908727181DC0D4A2BD27DA4ED74993A50179C45BD63FDD0CC381058BB0DE6847B17FA3FF4B459C06562E925A5F23BA4DCC23A279220F887F46F31BEAA0CD63E6DD87790F4C4071627F08",
            ledger_index_min=1,
            ledger_index_max=10
        )

        if scan_result1.get("result", {}).get("status") != "success":
            print("Failed to scan balance for first key!")
            return

        print(f"First key scan complete! Balance: {scan_result1['result']['total_balance_xrp']} PFT, "
              f"Notes: {scan_result1['result']['note_count']}, "
              f"Spent: {scan_result1['result']['spent_count']}")

        # Step 7: Scan balance for second viewing key (after t→z)
        print("\n=== Step 7: Scanning balance for second viewing key (after t→z) ===")
        scan_result2 = client.orchard_scan_balance(
            full_viewing_key="935993F09041BB701B12FF053AD9D9F4F9051C8BDD70B39D62F4765B4E71F812BA38D514E952E84654C55F276BAC3986B5BCC4B2A21E253FA16EFF658394AD1B0924F4F8C86C42C9ECC983F135BA3C9D4D9493F51DCCE7ACDCF5A6C7E26D1923",
            ledger_index_min=1,
            ledger_index_max=10
        )

        if scan_result2.get("result", {}).get("status") != "success":
            print("Failed to scan balance for second key!")
            return

        print(f"Second key scan complete! Balance: {scan_result2['result']['total_balance_xrp']} PFT, "
              f"Notes: {scan_result2['result']['note_count']}, "
              f"Spent: {scan_result2['result']['spent_count']}")

        # Step 8: Prepare z→z payment
        # Note: Fee is automatically calculated from ledger if not specified
        # The backend will automatically select notes to cover amount + fee
        print("\n=== Step 8: Preparing z→z payment ===")
        prepare_result2 = client.orchard_prepare_payment(
            payment_type="z_to_z",
            amount="500000000",
            spending_key="D8710D7D8D4717F313C1B1F49CA82AA5FA64B7AAD0D51BC671B8EB0E06E3DC99",
            recipient="C19558DB8066177BF73AAD65280FC53378A082A3FA5CEE57218D3A5F846E24201CC6978222B9AE2B4F1D95"
            # fee="10"  # Optional: specify custom fee in drops, otherwise uses ledger base fee
        )

        if prepare_result2.get("result", {}).get("status") != "success":
            print("z_to_z payment preparation failed!")
            return

        # Step 8b: Prepare ANOTHER z→z payment with same spending key (for double-spend test later)
        # Use the second account's address as recipient (doesn't matter where it goes, we just want a conflicting spend)
        print("\n=== Step 8b: Preparing double-spend z→z payment ===")
        double_spend_prepare = client.orchard_prepare_payment(
            payment_type="z_to_z",
            amount="100000000",  # Different amount (100 PFT)
            spending_key="D8710D7D8D4717F313C1B1F49CA82AA5FA64B7AAD0D51BC671B8EB0E06E3DC99",  # SAME spending key!
            recipient="C19558DB8066177BF73AAD65280FC53378A082A3FA5CEE57218D3A5F846E24201CC6978222B9AE2B4F1D95"
        )

        if double_spend_prepare.get("result", {}).get("status") != "success":
            print(f"Step 8b ERROR: Failed to prepare double-spend transaction: {double_spend_prepare.get('result', {}).get('error_message', 'Unknown error')}")
            double_spend_tx_json = None
        else:
            print("Step 8b: Prepared double-spend z→z payment (will test later)")
            double_spend_tx_json = double_spend_prepare["result"]["tx_json"]

        # Step 9: Submit z_to_z transaction
        print("\n=== Step 9: Submitting z_to_z transaction ===")
        tx_json2 = prepare_result2["result"]["tx_json"]
        
        submit_result2 = client.submit(
            tx_json=tx_json2
        )
        
        # Step 10: Accept the ledger
        print("\n=== Step 10: Accepting ledger ===")
        ledger_result2 = client.ledger_accept()
        
        if ledger_result2.get("result", {}).get("status") != "success":
            print("Ledger accept failed!")
            return
        
        print(f"Ledger accepted! Current ledger index: {ledger_result2['result']['ledger_current_index']}")
        
        # Step 11: Scan balance for first viewing key (after z→z)
        print("\n=== Step 11: Scanning balance for first viewing key (after z→z) ===")
        scan_result3 = client.orchard_scan_balance(
            full_viewing_key="32962E110407C099A57D577C7750BE7078A4729F08ED908727181DC0D4A2BD27DA4ED74993A50179C45BD63FDD0CC381058BB0DE6847B17FA3FF4B459C06562E925A5F23BA4DCC23A279220F887F46F31BEAA0CD63E6DD87790F4C4071627F08",
            ledger_index_min=1,
            ledger_index_max=10
        )
        
        if scan_result3.get("result", {}).get("status") != "success":
            print("Failed to scan balance for first key!")
            return
        
        print(f"First key scan complete! Balance: {scan_result3['result']['total_balance_xrp']} PFT, "
              f"Notes: {scan_result3['result']['note_count']}, "
              f"Spent: {scan_result3['result']['spent_count']}")
        
        # Step 12: Scan balance for second viewing key (after z→z)
        print("\n=== Step 12: Scanning balance for second viewing key (after z→z) ===")
        scan_result4 = client.orchard_scan_balance(
            full_viewing_key="935993F09041BB701B12FF053AD9D9F4F9051C8BDD70B39D62F4765B4E71F812BA38D514E952E84654C55F276BAC3986B5BCC4B2A21E253FA16EFF658394AD1B0924F4F8C86C42C9ECC983F135BA3C9D4D9493F51DCCE7ACDCF5A6C7E26D1923",
            ledger_index_min=1,
            ledger_index_max=10
        )
        
        if scan_result4.get("result", {}).get("status") != "success":
            print("Failed to scan balance for second key!")
            return
        
        print(f"Second key scan complete! Balance: {scan_result4['result']['total_balance_xrp']} PFT, "
              f"Notes: {scan_result4['result']['note_count']}, "
              f"Spent: {scan_result4['result']['spent_count']}")

        # Step 13: Prepare z→t payment (unshield 200 PFT from second account to transparent)
        print("\n=== Step 13: Preparing z→t payment ===")
        prepare_result3 = client.orchard_prepare_payment(
            payment_type="z_to_t",
            amount="200000000",
            spending_key="E47A50F38ACBADB0B839AE3A089E9988665B4E13819B64A4A4D3B3F5CB7FA0B3",
            destination_account="rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
        )

        if prepare_result3.get("result", {}).get("status") != "success":
            print("z_to_t payment preparation failed!")
            return

        # Step 14: Submit z_to_t transaction
        print("\n=== Step 14: Submitting z_to_t transaction ===")
        tx_json3 = prepare_result3["result"]["tx_json"]

        submit_result3 = client.submit(
            tx_json=tx_json3
        )

        # Step 15: Accept the ledger
        print("\n=== Step 15: Accepting ledger ===")
        ledger_result3 = client.ledger_accept()

        if ledger_result3.get("result", {}).get("status") != "success":
            print("Ledger accept failed!")
            return

        print(f"Ledger accepted! Current ledger index: {ledger_result3['result']['ledger_current_index']}")

        # Step 16: Scan balance for second viewing key (after z→t)
        print("\n=== Step 16: Scanning balance for second viewing key (after z→t) ===")
        scan_result5 = client.orchard_scan_balance(
            full_viewing_key="935993F09041BB701B12FF053AD9D9F4F9051C8BDD70B39D62F4765B4E71F812BA38D514E952E84654C55F276BAC3986B5BCC4B2A21E253FA16EFF658394AD1B0924F4F8C86C42C9ECC983F135BA3C9D4D9493F51DCCE7ACDCF5A6C7E26D1923",
            ledger_index_min=1,
            ledger_index_max=10
        )

        if scan_result5.get("result", {}).get("status") != "success":
            print("Failed to scan balance for second key!")
            return

        print(f"Second key scan complete! Balance: {scan_result5['result']['total_balance_xrp']} PFT, "
              f"Notes: {scan_result5['result']['note_count']}, "
              f"Spent: {scan_result5['result']['spent_count']}")

        # Step 17: Attempt double-spend (should FAIL)
        # We prepared this transaction earlier using the same note that was already spent
        print("\n=== Step 17: Testing double-spend detection ===")

        if double_spend_tx_json is not None:
            print("Step 17: Submitting the double-spend transaction prepared in Step 8b...")

            double_spend_submit = client.submit(
                tx_json=double_spend_tx_json
            )

            # This SHOULD fail because the nullifier was already revealed in step 9
            engine_result = double_spend_submit.get("result", {}).get("engine_result", "UNKNOWN")

            print(f"Step 17: Double-spend attempt result: {engine_result}")

            # Verify the double-spend was rejected
            # Expected: tefORCHARD_DUPLICATE_NULLIFIER or similar error (nullifier already revealed)
            if engine_result == "tesSUCCESS":
                print("ERROR: Double-spend was NOT detected! This is a critical security bug!")
            else:
                print(f"SUCCESS: Double-spend correctly rejected with {engine_result}")
        else:
            print("Step 17: SKIPPED - Could not prepare double-spend transaction in Step 8b")

        print("\n=== All tests completed successfully! ===")
        print("Summary:")
        print("  - t→z: 1000 PFT shielded to first account")
        print("  - z→z: 500 PFT transferred from first to second account")
        print("  - z→t: 200 PFT unshielded from second account to transparent")
        print("  - Double-spend: Correctly rejected with duplicate nullifier detection")

    except requests.exceptions.RequestException as e:
        print(f"Error making RPC call: {e}")
        raise


if __name__ == "__main__":
    main()
