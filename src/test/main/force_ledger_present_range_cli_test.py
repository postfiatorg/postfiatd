"""Offline CLI regression for --force_ledger_present_range.

Run with: python3 src/test/main/force_ledger_present_range_cli_test.py /path/to/postfiatd
The unrelated --trap_tx_hash error stops the process after argument validation
and before application startup, so the probe never opens a network listener.
"""

import pathlib
import subprocess
import sys
import tempfile
import unittest


class ForcedLedgerRangeCliTest(unittest.TestCase):
    def run_range(self, value):
        with tempfile.TemporaryDirectory(prefix="postfiatd-range-") as directory:
            root = pathlib.Path(directory)
            database = root / "db"
            config = root / "postfiatd.cfg"
            config.write_text(
                f"[database_path]\n{database}\n\n"
                f"[node_db]\ntype=NuDB\npath={database}\n"
            )
            result = subprocess.run(
                [
                    sys.argv[1],
                    "--conf",
                    str(config),
                    "--standalone",
                    "--force_ledger_present_range",
                    value,
                    "--trap_tx_hash",
                    "0",
                ],
                capture_output=True,
                text=True,
                timeout=10,
                check=False,
            )
            self.assertNotEqual(result.returncode, 0)
            return result.stdout + result.stderr

    def test_valid_range_reaches_later_non_starting_guard(self):
        self.assertIn(
            "Cannot use trap option without replay option", self.run_range("1,2")
        )

    def test_invalid_range_is_rejected_before_later_guard(self):
        for value in ("1junk,2", "-1,-1", "1,,2"):
            with self.subTest(value=value):
                output = self.run_range(value)
                self.assertIn("invalid 'force_ledger_present_range' parameter", output)
                self.assertNotIn("Cannot use trap option", output)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
