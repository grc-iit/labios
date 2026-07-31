#!/usr/bin/env python3
"""Predeclared analysis for the independent Prompt 08 replacement experiment."""

import csv
import json
import math
import random
import statistics
import sys
from collections import defaultdict
from pathlib import Path

PROFILES = ("small-hot-metadata", "large-sequential", "mixed-pipeline")
ARMS = ("baseline", "ablation", "informed")
EXPECTED_MEASURED = {
    "small-hot-metadata": 16,
    "large-sequential": 4,
    "mixed-pipeline": 1,
}
REPLICATES = 20
SEED = 2101


def percentile(values, probability):
    values = sorted(values)
    return values[max(0, math.ceil(probability * len(values)) - 1)]


def summarize(values):
    return {
        "n": len(values),
        "median_us": statistics.median(values),
        "p95_us": percentile(values, 0.95),
        "mean_us": statistics.fmean(values),
    }


def mann_whitney(left, right):
    pooled = sorted([(value, 0) for value in left] +
                    [(value, 1) for value in right])
    rank_sum = 0.0
    tie_term = 0
    index = 0
    while index < len(pooled):
        end = index + 1
        while end < len(pooled) and pooled[end][0] == pooled[index][0]:
            end += 1
        rank = (index + 1 + end) / 2
        rank_sum += rank * sum(group == 0 for _, group in pooled[index:end])
        tie = end - index
        tie_term += tie**3 - tie
        index = end
    n1, n2 = len(left), len(right)
    u1 = rank_sum - n1 * (n1 + 1) / 2
    mean = n1 * n2 / 2
    variance = n1 * n2 / 12 * (
        n1 + n2 + 1 - tie_term / ((n1 + n2) * (n1 + n2 - 1))
    )
    if variance == 0:
        return u1, 1.0
    correction = 0.5 if u1 > mean else (-0.5 if u1 < mean else 0.0)
    z = (u1 - mean - correction) / math.sqrt(variance)
    return u1, math.erfc(abs(z) / math.sqrt(2))


def cliffs_delta(left, right):
    greater = sum(a > b for a in left for b in right)
    less = sum(a < b for a in left for b in right)
    return (greater - less) / (len(left) * len(right))


def bootstrap(left, right, seed, samples=10_000):
    rng = random.Random(seed)
    differences, ratios = [], []
    for _ in range(samples):
        informed = [rng.choice(left) for _ in left]
        ablation = [rng.choice(right) for _ in right]
        informed_median = statistics.median(informed)
        ablation_median = statistics.median(ablation)
        differences.append(informed_median - ablation_median)
        ratios.append(informed_median / ablation_median
                      if ablation_median else math.inf)
    return {
        "median_difference_us_ci95": [
            percentile(differences, 0.025), percentile(differences, 0.975)
        ],
        "median_ratio_ci95": [
            percentile(ratios, 0.025), percentile(ratios, 0.975)
        ],
    }


def holm(pvalues):
    adjusted = {}
    running = 0.0
    for rank, (name, value) in enumerate(
            sorted(pvalues.items(), key=lambda item: item[1])):
        running = max(running, min(1.0, value * (len(pvalues) - rank)))
        adjusted[name] = running
    return adjusted


def load_rows(root):
    required = {
        "run_id", "profile", "arm", "phase", "repetition", "label_id",
        "resource", "submission_us", "completion_us", "worker_id", "attempt",
        "actual_queue_delay_us", "actual_service_time_us", "terminal_state",
        "failure", "verified",
    }
    rows = []
    for arm in ARMS:
        for profile in PROFILES:
            path = root / arm / profile / "raw.csv"
            with path.open(newline="") as source:
                reader = csv.DictReader(source)
                missing = required - set(reader.fieldnames or ())
                if missing:
                    raise SystemExit(f"{path}: missing columns {sorted(missing)}")
                cell_rows = list(reader)
            measured = [row for row in cell_rows if row["phase"] == "measured"]
            training = [row for row in cell_rows if row["phase"] == "training"]
            if len(measured) != REPLICATES * EXPECTED_MEASURED[profile]:
                raise SystemExit(f"{path}: wrong measured row count {len(measured)}")
            if len(training) != REPLICATES * 43:
                raise SystemExit(f"{path}: wrong training row count {len(training)}")
            for row in cell_rows:
                if row["arm"] != arm or row["verified"] != "1" or \
                        row["terminal_state"] != "complete" or row["failure"]:
                    raise SystemExit(f"{path}: invalid row {row['label_id']}")
                if int(row["worker_id"]) < 0 or int(row["attempt"]) < 1:
                    raise SystemExit(f"{path}: missing execution observation")
            repetitions = {int(row["repetition"]) for row in measured}
            if repetitions != set(range(REPLICATES)):
                raise SystemExit(f"{path}: repetitions are incomplete")
            rows.extend(measured)
    return rows


def main():
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} ARTIFACT_DIR")
    root = Path(sys.argv[1])
    manifest = list(csv.DictReader((root / "cells.csv").open(newline="")))
    expected_cells = len(ARMS) * len(PROFILES) * REPLICATES
    if len(manifest) != expected_cells or any(row["status"] != "passed"
                                               for row in manifest):
        raise SystemExit("independent cell manifest is incomplete")
    identities = {row["replicate_id"] for row in manifest}
    if len(identities) != expected_cells:
        raise SystemExit("replicate identities are not unique")

    rows = load_rows(root)
    raw = defaultdict(lambda: defaultdict(list))
    by_repetition = defaultdict(lambda: defaultdict(list))
    workers = defaultdict(list)
    for row in rows:
        key = (row["profile"], row["arm"])
        raw[key]["submission_us"].append(int(row["submission_us"]))
        raw[key]["completion_us"].append(int(row["completion_us"]))
        raw[key]["actual_queue_delay_us"].append(
            int(row["actual_queue_delay_us"]))
        raw[key]["actual_service_time_us"].append(
            int(row["actual_service_time_us"]))
        by_repetition[key][int(row["repetition"])].append(
            int(row["completion_us"]))
        logical_resource = (Path(row["resource"]).name
                            if row["profile"] != "mixed-pipeline" else "pipeline")
        workers[(row["profile"], row["arm"], int(row["repetition"]))].append(
            (logical_resource, int(row["worker_id"])))

    result = {
        "valid": True,
        "method": {
            "primary_unit": "median completion_us within each fresh-state replicate",
            "replicates_per_profile_arm": REPLICATES,
            "test": "two-sided Mann-Whitney U with tie and continuity corrections",
            "multiplicity": "Holm correction across three profiles",
            "effect": "Cliff's delta",
            "bootstrap": "10000 independent replicate resamples, seed 2101",
            "alpha": 0.05,
            "primary_comparison": "informed versus static-weight-matched ablation",
        },
        "profiles": {},
    }
    pvalues = {}
    for profile in PROFILES:
        entry = {"raw_label_distributions": {}, "replicate_distributions": {}}
        replicate_values = {}
        for arm in ARMS:
            entry["raw_label_distributions"][arm] = {
                metric: summarize(raw[(profile, arm)][metric])
                for metric in (
                    "submission_us", "completion_us", "actual_queue_delay_us",
                    "actual_service_time_us")
            }
            replicate_values[arm] = [
                statistics.median(by_repetition[(profile, arm)][repetition])
                for repetition in range(REPLICATES)
            ]
            entry["replicate_distributions"][arm] = summarize(
                replicate_values[arm])

        paired_differences = 0
        paired_total = 0
        for repetition in range(REPLICATES):
            ablation_workers = sorted(
                workers[(profile, "ablation", repetition)])
            informed_workers = sorted(
                workers[(profile, "informed", repetition)])
            if [key for key, _ in ablation_workers] != [
                    key for key, _ in informed_workers]:
                raise SystemExit(f"{profile}: unpaired placement rows")
            paired_differences += sum(
                a_worker != i_worker
                for (_, a_worker), (_, i_worker) in
                zip(ablation_workers, informed_workers))
            paired_total += len(ablation_workers)
        if paired_differences == 0:
            raise SystemExit(f"{profile}: no observed placement treatment")
        entry["placement_treatment"] = {
            "paired_rows": paired_total,
            "different_workers": paired_differences,
            "fraction_different": paired_differences / paired_total,
        }

        informed = replicate_values["informed"]
        ablation = replicate_values["ablation"]
        u_value, p_value = mann_whitney(informed, ablation)
        pvalues[profile] = p_value
        comparison = {
            "u": u_value,
            "p_unadjusted": p_value,
            "cliffs_delta_informed_vs_ablation": cliffs_delta(informed, ablation),
            "observed_median_difference_us":
                statistics.median(informed) - statistics.median(ablation),
            "observed_median_ratio":
                statistics.median(informed) / statistics.median(ablation),
        }
        comparison.update(bootstrap(informed, ablation, SEED))
        entry["primary_comparison"] = comparison
        result["profiles"][profile] = entry

    adjusted = holm(pvalues)
    for profile in PROFILES:
        comparison = result["profiles"][profile]["primary_comparison"]
        comparison["p_holm"] = adjusted[profile]
        ratio = comparison["observed_median_ratio"]
        low, high = comparison["median_ratio_ci95"]
        if adjusted[profile] < 0.05 and ratio < 1 and high < 1:
            comparison["classification"] = "favorable"
        elif adjusted[profile] < 0.05 and ratio > 1 and low > 1:
            comparison["classification"] = "adverse"
        else:
            comparison["classification"] = "null-or-inconclusive"

    (root / "analysis.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n")
    lines = [
        "# Prompt 08 independent-replicate result", "",
        "The primary unit is one fresh-state replicate median. Baseline is contextual.",
        "", "| Profile | Ablation median (µs) | Informed median (µs) | Ratio | Holm p | Cliff δ | Placement differences | Classification |",
        "|---|---:|---:|---:|---:|---:|---:|---|",
    ]
    for profile in PROFILES:
        item = result["profiles"][profile]
        comparison = item["primary_comparison"]
        placement = item["placement_treatment"]
        lines.append(
            f"| {profile} | "
            f"{item['replicate_distributions']['ablation']['median_us']:.1f} | "
            f"{item['replicate_distributions']['informed']['median_us']:.1f} | "
            f"{comparison['observed_median_ratio']:.4f} | "
            f"{comparison['p_holm']:.6g} | "
            f"{comparison['cliffs_delta_informed_vs_ablation']:.4f} | "
            f"{placement['different_workers']}/{placement['paired_rows']} | "
            f"{comparison['classification']} |"
        )
    (root / "RESULT.md").write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
