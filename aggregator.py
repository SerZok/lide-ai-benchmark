#!/usr/bin/env python3
"""Aggregate lide-ai-benchmark JSON Lines logs into paper-ready tables."""

import argparse
import csv
import json
from collections import Counter, defaultdict
from pathlib import Path

RESOURCE_CATEGORIES = {"resource_violation"}
COMPILATION_CATEGORIES = {"compilation_error"}


def load_records(path):
    records = []
    with path.open(encoding="utf-8") as source:
        for line_number, line in enumerate(source, 1):
            if not line.strip():
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError as error:
                raise ValueError(f"{path}:{line_number}: invalid JSON: {error}") from error
            if record.get("recordType") != "result":
                continue
            if not record.get("model") or not record.get("taskId"):
                raise ValueError(f"{path}:{line_number}: result is missing model or taskId")
            records.append(record)
    if not records:
        raise ValueError(f"{path}: no result records found")
    return records


def aggregate(records):
    grouped = defaultdict(list)
    for record in records:
        grouped[record["model"]].append(record)

    summaries = []
    for model in sorted(grouped):
        attempts = grouped[model]
        first_by_task = {}
        for record in sorted(attempts, key=lambda item: int(item.get("attempt", 1))):
            if int(record.get("attempt", 1)) == 1:
                first_by_task.setdefault(record["taskId"], record)
        first_runs = list(first_by_task.values())
        if not first_runs:
            raise ValueError(f"{model}: no attempt-1 records found")

        errors = Counter(
            record.get("errorCategory", "unknown")
            for record in attempts
            if record.get("status") != "passed"
        )
        passed = sum(record.get("status") == "passed" for record in first_runs)
        compilation_errors = sum(
            record.get("errorCategory") in COMPILATION_CATEGORIES
            for record in first_runs
        )
        resource_violations = sum(
            record.get("errorCategory") in RESOURCE_CATEGORIES
            for record in first_runs
        )
        summaries.append(
            {
                "model": model,
                "tasks": len(first_runs),
                "pass_at_1": passed / len(first_runs),
                "cer": compilation_errors / len(first_runs),
                "rvr": resource_violations / len(first_runs),
                "attempts": len(attempts),
                "errors": errors,
            }
        )
    return summaries


def render_markdown(summaries):
    lines = [
        "| Model | Tasks | pass@1 | CER | RVR | Attempts |",
        "|---|---:|---:|---:|---:|---:|",
    ]
    for item in summaries:
        lines.append(
            f"| {item['model']} | {item['tasks']} | {item['pass_at_1']:.4f} "
            f"| {item['cer']:.4f} | {item['rvr']:.4f} | {item['attempts']} |"
        )
    lines.extend(["", "Error category distribution (all attempts):", "",
                  "| Model | Category | Count | Share of attempts |",
                  "|---|---|---:|---:|"])
    for item in summaries:
        categories = item["errors"] or Counter({"none": 0})
        for category, count in sorted(categories.items()):
            share = count / item["attempts"] if item["attempts"] else 0
            lines.append(f"| {item['model']} | {category} | {count} | {share:.4f} |")
    return "\n".join(lines) + "\n"


def write_csv(path, summaries):
    with path.open("w", encoding="utf-8-sig", newline="") as output:
        writer = csv.DictWriter(
            output,
            fieldnames=[
                "model", "tasks", "pass_at_1", "cer", "rvr", "attempts",
                "error_category", "error_count", "error_share",
            ],
        )
        writer.writeheader()
        for item in summaries:
            categories = item["errors"] or Counter({"none": 0})
            for category, count in sorted(categories.items()):
                writer.writerow(
                    {
                        "model": item["model"],
                        "tasks": item["tasks"],
                        "pass_at_1": f"{item['pass_at_1']:.6f}",
                        "cer": f"{item['cer']:.6f}",
                        "rvr": f"{item['rvr']:.6f}",
                        "attempts": item["attempts"],
                        "error_category": category,
                        "error_count": count,
                        "error_share": f"{count / item['attempts']:.6f}",
                    }
                )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("jsonl", type=Path, help="JSON Lines file produced by the CLI")
    parser.add_argument("--csv", dest="csv_path", type=Path)
    parser.add_argument("--markdown", dest="markdown_path", type=Path)
    args = parser.parse_args()

    summaries = aggregate(load_records(args.jsonl))
    markdown = render_markdown(summaries)
    csv_path = args.csv_path or args.jsonl.with_suffix(".csv")
    markdown_path = args.markdown_path or args.jsonl.with_suffix(".md")
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    markdown_path.parent.mkdir(parents=True, exist_ok=True)
    write_csv(csv_path, summaries)
    markdown_path.write_text(markdown, encoding="utf-8")
    print(markdown, end="")
    print(f"\nCSV: {csv_path}\nMarkdown: {markdown_path}")


if __name__ == "__main__":
    main()
