#!/usr/bin/env python3
"""Predeclared Prompt 08 analysis; Python standard library only."""

import argparse
import csv
import json
import math
import random
import statistics
from collections import defaultdict
from pathlib import Path

PROFILES = ("small-hot-metadata", "large-sequential", "mixed-pipeline")
ARMS = ("baseline", "ablation", "informed")


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
    pooled = [(value, 0) for value in left] + [(value, 1) for value in right]
    pooled.sort()
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
        a = [rng.choice(left) for _ in left]
        b = [rng.choice(right) for _ in right]
        ma, mb = statistics.median(a), statistics.median(b)
        differences.append(ma - mb)
        ratios.append(ma / mb if mb else math.inf)
    differences.sort()
    ratios.sort()
    return {
        "median_difference_us_ci95": [
            percentile(differences, 0.025),
            percentile(differences, 0.975),
        ],
        "median_ratio_ci95": [
            percentile(ratios, 0.025),
            percentile(ratios, 0.975),
        ],
    }


def holm(pvalues):
    adjusted = {}
    running = 0.0
    for rank, (name, value) in enumerate(sorted(pvalues.items(), key=lambda x: x[1])):
        running = max(running, min(1.0, value * (len(pvalues) - rank)))
        adjusted[name] = running
    return adjusted


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("artifact_dir", type=Path)
    parser.add_argument(
        "--invalid-diagnostic",
        action="store_true",
        help="acknowledge that sequential repetitions do not support inference",
    )
    args = parser.parse_args()
    if not args.invalid_diagnostic:
        raise SystemExit(
            "refusing formal Prompt 08 inference: this retained method has "
            "non-independent repetitions; pass --invalid-diagnostic only to "
            "reproduce the rejected calculation"
        )
    rows = []
    required = {
        "run_id", "profile", "arm", "repetition", "label_id", "resource",
        "submission_us", "completion_us", "worker_id", "terminal_state",
        "failure", "verified",
    }
    for arm in ARMS:
        path = args.artifact_dir / arm / "raw.csv"
        with path.open(newline="") as source:
            reader = csv.DictReader(source)
            missing = required - set(reader.fieldnames or ())
            if missing:
                raise SystemExit(f"{path}: missing columns {sorted(missing)}")
            arm_rows = list(reader)
        if len(arm_rows) != 420:
            raise SystemExit(f"{path}: expected 420 rows, found {len(arm_rows)}")
        for row in arm_rows:
            if row["arm"] != arm or row["verified"] != "1" or \
                    row["terminal_state"] != "complete" or row["failure"]:
                raise SystemExit(f"{path}: invalid correctness row {row['label_id']}")
        rows.extend(arm_rows)

    raw = defaultdict(lambda: defaultdict(list))
    repetitions = defaultdict(lambda: defaultdict(lambda: defaultdict(list)))
    for row in rows:
        profile, arm = row["profile"], row["arm"]
        for metric in ("submission_us", "completion_us"):
            value = int(row[metric])
            raw[(profile, arm)][metric].append(value)
            repetitions[(profile, arm)][int(row["repetition"])][metric].append(value)

    result = {"valid": False, "invalid_reason":
        "sequential stateful repetitions are not independent; service/queue "
        "columns are scheduling EWMAs and untimed verification trains traces",
        "method": {
        "primary_unit": "per-repetition median completion_us",
        "test": "two-sided Mann-Whitney U with tie correction and continuity correction",
        "multiplicity": "Holm correction across three profiles",
        "effect": "Cliff's delta",
        "bootstrap": "10000 independent resamples, seed 2101",
        "alpha": 0.05,
    }, "profiles": {}}
    pvalues = {}
    for profile in PROFILES:
        entry = {"raw_label_distributions": {}, "repetition_distributions": {}}
        rep_values = {}
        for arm in ARMS:
            entry["raw_label_distributions"][arm] = {
                metric: summarize(raw[(profile, arm)][metric])
                for metric in ("submission_us", "completion_us")
            }
            rep_values[arm] = [
                statistics.median(values["completion_us"])
                for _, values in sorted(repetitions[(profile, arm)].items())
            ]
            if len(rep_values[arm]) != 20:
                raise SystemExit(f"{profile}/{arm}: expected 20 repetitions")
            entry["repetition_distributions"][arm] = summarize(rep_values[arm])
        informed, ablation = rep_values["informed"], rep_values["ablation"]
        u, p = mann_whitney(informed, ablation)
        pvalues[profile] = p
        comparison = {
            "u": u,
            "p_unadjusted": p,
            "cliffs_delta_informed_vs_ablation": cliffs_delta(informed, ablation),
            "observed_median_difference_us":
                statistics.median(informed) - statistics.median(ablation),
            "observed_median_ratio":
                statistics.median(informed) / statistics.median(ablation),
        }
        comparison.update(bootstrap(informed, ablation, 2101))
        entry["primary_comparison"] = comparison
        result["profiles"][profile] = entry

    adjusted = holm(pvalues)
    for profile in PROFILES:
        comparison = result["profiles"][profile]["primary_comparison"]
        comparison["p_holm"] = adjusted[profile]
        ratio = comparison["observed_median_ratio"]
        low, high = comparison["median_ratio_ci95"]
        if adjusted[profile] < 0.05 and ratio < 1 and high < 1:
            classification = "favorable"
        elif adjusted[profile] < 0.05 and ratio > 1 and low > 1:
            classification = "adverse"
        else:
            classification = "null-or-inconclusive"
        comparison["classification"] = classification

    output = args.artifact_dir / "analysis.json"
    output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    lines = [
        "# INVALID Prompt 08 diagnostic", "",
        "**Do not cite this table as performance evidence.** Repetitions are "
        "serially dependent and the service/queue measurement contract is not "
        "met.", "",
        "| Profile | Ablation median (µs) | Informed median (µs) | Ratio | "
        "Holm p | Cliff δ | Classification |",
        "|---|---:|---:|---:|---:|---:|---|",
    ]
    for profile in PROFILES:
        item = result["profiles"][profile]
        comparison = item["primary_comparison"]
        a = item["repetition_distributions"]["ablation"]["median_us"]
        i = item["repetition_distributions"]["informed"]["median_us"]
        lines.append(
            f"| {profile} | {a:.1f} | {i:.1f} | "
            f"{comparison['observed_median_ratio']:.4f} | "
            f"{comparison['p_holm']:.6g} | "
            f"{comparison['cliffs_delta_informed_vs_ablation']:.4f} | "
            f"{comparison['classification']} |"
        )
    (args.artifact_dir / "RESULT.md").write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
