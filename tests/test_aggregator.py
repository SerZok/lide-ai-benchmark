import unittest
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from aggregator import aggregate, render_markdown


class AggregatorTests(unittest.TestCase):
    def test_metrics_use_first_attempt_and_category_distribution_uses_all_attempts(self):
        records = [
            {"model": "m", "taskId": "a", "attempt": 1, "status": "passed"},
            {"model": "m", "taskId": "a", "attempt": 2, "status": "failed",
             "errorCategory": "incorrect_result"},
            {"model": "m", "taskId": "b", "attempt": 1, "status": "compilation-error",
             "errorCategory": "compilation_error"},
            {"model": "m", "taskId": "b", "attempt": 2, "status": "timeout",
             "errorCategory": "resource_violation"},
        ]

        summary = aggregate(records)[0]

        self.assertEqual(summary["tasks"], 2)
        self.assertEqual(summary["pass_at_1"], 0.5)
        self.assertEqual(summary["cer"], 0.5)
        self.assertEqual(summary["rvr"], 0.0)
        self.assertEqual(summary["errors"]["resource_violation"], 1)

    def test_markdown_includes_required_metrics(self):
        markdown = render_markdown(aggregate([
            {"model": "m", "taskId": "a", "attempt": 1, "status": "passed"}
        ]))

        self.assertIn("pass@1", markdown)
        self.assertIn("CER", markdown)
        self.assertIn("RVR", markdown)


if __name__ == "__main__":
    unittest.main()
