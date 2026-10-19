"""Unit tests for the serial-log acceptance verifier (not hardware tests)."""
import unittest

from test_rtl8188eu_hardware import STAGES, evaluate


class HardwareVerifierTests(unittest.TestCase):
    def test_empty_log_fails_every_hardware_stage(self):
        results, failures = evaluate("")
        self.assertEqual(len(results), len(STAGES))
        self.assertTrue(all(not passed for _, passed in results))
        self.assertEqual(failures, [])

    def test_all_evidence_markers_are_required(self):
        full_log = "\n".join(marker for _, marker in STAGES)
        results, failures = evaluate(full_log)
        self.assertTrue(all(passed for _, passed in results))
        self.assertEqual(failures, [])

    def test_driver_failure_marker_is_not_ignored(self):
        log = "\n".join(marker for _, marker in STAGES)
        log += "\nVANTAOS_RTL8188EU_FIRMWARE_LOAD_FAILED\n"
        results, failures = evaluate(log)
        self.assertTrue(all(passed for _, passed in results))
        self.assertIn("VANTAOS_RTL8188EU_FIRMWARE_LOAD_FAILED", failures)

    def test_mock_only_partial_progress_cannot_pass(self):
        log = "\n".join((
            "VANTAOS_RTL8188EU_USB_TRANSPORT_READY",
            "VANTAOS_RTL8188EU_FIRMWARE_READY",
            "VANTAOS_RTL8188EU_EFUSE_READY",
        ))
        results, _ = evaluate(log)
        self.assertTrue(results[0][1])
        self.assertTrue(results[1][1])
        self.assertTrue(results[2][1])
        self.assertTrue(any(not passed for _, passed in results[3:]))


if __name__ == "__main__":
    unittest.main()
