#!/usr/bin/env python3
"""Figures and complexity tables from the raw benchmark and work-count data.

Usage:  python3 scripts/analyze.py [results_dir]      (default: results)
Inputs:  <dir>/bench.json        Google Benchmark JSON (--benchmark_out)
         <dir>/work_counts.csv   output of build/work_counts
Outputs: <dir>/fig_scaling.pdf/.png, <dir>/complexity_table.md,
         <dir>/complexity_table.csv, <dir>/work_summary.csv,
         <dir>/per_n_means.csv

All times are per single operation (the benchmark already divides each
batch by its fixed size B). Fits use Google Benchmark's least-squares
formula: coef = sum(t*g)/sum(g^2), rms = sqrt(mean((t-coef*g)^2))/mean(t).

Three kinds of fit are reported:
  gb_*    the model Google Benchmark selected itself (Complexity(oAuto)), with
          its real-time coefficient from bench.json. NOTE: Google Benchmark
          selects the model on CPU time (complexity.cc, ComputeBigO) and its
          JSON "rms" field is the CPU-time RMS. With UseManualTime the CPU time
          also covers the untimed restoration, so the gb_rms reported here is
          recomputed on the real (manual, kernel-only) times.
  auto_*  the same automatic selection (same six models) redone on real time.
  supplied_* / affine_*  our hypotheses (c*model, and a + b*log2 N).
"""
import json
import math
import re
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import pandas as pd  # noqa: E402

RES = Path(sys.argv[1] if len(sys.argv) > 1 else "results")

KERNELS = {  # benchmark family -> (label, supplied model)
    "BM_Search": ("search", "lgN"),
    "BM_Insert": ("insert", "lgN"),
    "BM_Union": ("ordered_union", "lgN"),
    "BM_Split": ("split", "lgN"),
    "BM_SplitRebuild": ("split_rebuild (baseline)", "NlgN"),
}
MODELS = {
    "(1)": lambda n: np.ones_like(n, dtype=float),
    "lgN": lambda n: np.log2(n),
    "N": lambda n: n.astype(float),
    "NlgN": lambda n: n * np.log2(n),
    "N^2": lambda n: n.astype(float) ** 2,
    "N^3": lambda n: n.astype(float) ** 3,
}

# Reference categorical palette (slots 1-4), text and grid inks.
BLUE, ORANGE, AQUA, YELLOW = "#2a78d6", "#eb6834", "#1baf7a", "#eda100"
INK, INK2, GRID = "#0b0b0b", "#52514e", "#e4e3df"


def fit(n, t, model):
    g = MODELS[model](n)
    coef = float(np.sum(t * g) / np.sum(g * g))
    rms = float(np.sqrt(np.mean((t - coef * g) ** 2)) / np.mean(t))
    return coef, rms


def fit_affine(n, t):
    """Least squares t = a + b*log2(n); a supplied hypothesis with a constant."""
    g = np.log2(n)
    A = np.vstack([np.ones_like(g), g]).T
    (a, b), *_ = np.linalg.lstsq(A, t, rcond=None)
    rms = float(np.sqrt(np.mean((t - (a + b * g)) ** 2)) / np.mean(t))
    return float(a), float(b), rms


def load_bench():
    data = json.loads((RES / "bench.json").read_text())
    rows, bigo, rms = [], {}, {}
    for b in data["benchmarks"]:
        fam = b["name"].split("/")[0]
        if b["run_type"] == "iteration":
            m = re.search(r"/N:(\d+)/seed:(\d+)/", b["name"])
            if m:
                rows.append(dict(family=fam, N=int(m.group(1)),
                                 seed=int(m.group(2)),
                                 rep=b["repetition_index"],
                                 ns=b["real_time"]))
            else:
                rows.append(dict(family=fam, N=0, seed=0,
                                 rep=b["repetition_index"], ns=b["real_time"]))
        elif b.get("aggregate_name") == "BigO":
            bigo[fam] = (b["big_o"], b["real_coefficient"])
        elif b.get("aggregate_name") == "RMS":
            rms[fam] = b["rms"]
    return data["context"], pd.DataFrame(rows), bigo, rms


def main():
    ctx, df, bigo, gb_rms = load_bench()
    clock_ns = df[df.family == "BM_ClockOverhead"].ns.mean()
    df = df[df.family.isin(KERNELS)]

    # ---------------- complexity table ----------------
    table = []
    for fam, (label, supplied) in KERNELS.items():
        d = df[df.family == fam]
        n, t = d.N.to_numpy(), d.ns.to_numpy()
        gb_model, gb_coef = bigo[fam]
        # our formula reproduces Google Benchmark's real-time coefficient
        gb_chk_coef, gb_real_rms = fit(n, t, gb_model)
        assert math.isclose(gb_chk_coef, gb_coef, rel_tol=1e-6), fam
        # automatic selection redone on real time
        auto_model = min(MODELS, key=lambda m: fit(n, t, m)[1])
        auto_coef, auto_rms = fit(n, t, auto_model)
        coef, rms = fit(n, t, supplied)
        a, b, arms = fit_affine(n, t) if supplied == "lgN" else (math.nan,) * 3
        table.append(dict(kernel=label, points=len(d),
                          gb_model=gb_model, gb_coef_ns=gb_coef,
                          gb_rms=gb_real_rms, gb_cpu_rms_json=gb_rms[fam],
                          auto_model=auto_model, auto_coef_ns=auto_coef,
                          auto_rms=auto_rms, supplied_model=supplied,
                          supplied_coef_ns=coef, supplied_rms=rms,
                          affine_a_ns=a, affine_b_ns=b, affine_rms=arms))
    tab = pd.DataFrame(table)
    short = {"BM_Search": "search", "BM_Insert": "insert", "BM_Union": "union",
             "BM_Split": "split", "BM_SplitRebuild": "split_rebuild"}
    means = df.groupby(["family", "N"]).ns.agg(["mean", "median"]).reset_index()
    means["kernel"] = means.family.map(short)
    means.rename(columns={"mean": "ns"})[["kernel", "N", "ns", "median"]].to_csv(
        RES / "per_n_means.csv", index=False, float_format="%.2f")
    tab.to_csv(RES / "complexity_table.csv", index=False)

    # ---------------- work counts ----------------
    wc = pd.read_csv(RES / "work_counts.csv")
    cols = ["key_comparisons", "link_reads", "link_writes", "node_allocs",
            "node_frees"]
    for c in cols:
        wc[c] = wc[c] / wc.ops
    wsum = wc.groupby(["kernel", "N"])[cols].mean().reset_index()
    wsum["work"] = wsum.key_comparisons + wsum.link_reads + wsum.link_writes
    wsum.to_csv(RES / "work_summary.csv", index=False, float_format="%.2f")

    work_fit = {}
    for k, model in [("search", "lgN"), ("insert", "lgN"), ("union", "lgN"),
                     ("split", "lgN"), ("split_rebuild", "NlgN")]:
        d = wsum[wsum.kernel == k]
        work_fit[k] = fit(d.N.to_numpy(), d.work.to_numpy(), model)

    with open(RES / "complexity_table.md", "w") as f:
        f.write(f"Per-operation real time, {tab.points[0]} runs per kernel "
                "(6 sizes x 3 height seeds x 3 repetitions).\n")
        f.write(f"Clock-pair overhead (included in union/split): "
                f"{clock_ns:.1f} ns\n\n")
        f.write("GB = model chosen by Google Benchmark (on CPU time), RMS here on "
                "real time; auto = same selection redone on real time.\n\n")
        f.write("| kernel | GB model | coef (ns) | RMS | auto (real) model "
                "| coef (ns) | RMS | supplied model "
                "| coef (ns) | RMS | a + b·lgN: a, b (ns) | RMS |\n"
                "|---|---|---|---|---|---|---|---|---|---|---|---|\n")
        for r in table:
            f.write(f"| {r['kernel']} | {r['gb_model']} | "
                    f"{r['gb_coef_ns']:.4g} | {r['gb_rms']:.0%} | "
                    f"{r['auto_model']} | "
                    f"{r['auto_coef_ns']:.4g} | {r['auto_rms']:.0%} | "
                    f"{r['supplied_model']} | {r['supplied_coef_ns']:.4g} | "
                    f"{r['supplied_rms']:.0%} | "
                    + (f"{r['affine_a_ns']:.4g}, {r['affine_b_ns']:.4g} | "
                       f"{r['affine_rms']:.0%} |\n"
                       if not math.isnan(r['affine_rms']) else "n/a | n/a |\n"))
        f.write("\nLogical work per op (comparisons + link reads + link writes),"
                " mean of 10 height seeds:\n\n| kernel | ")
        ns = sorted(wsum.N.unique())
        f.write(" | ".join(f"2^{int(math.log2(x))}" for x in ns))
        f.write(" | fit | coef | RMS |\n|---|" + "---|" * (len(ns) + 3) + "\n")
        for k, (coef, rms) in work_fit.items():
            d = wsum[wsum.kernel == k].sort_values("N")
            model = "NlgN" if k == "split_rebuild" else "lgN"
            f.write(f"| {k} | " + " | ".join(f"{v:.4g}" for v in d.work)
                    + f" | {model} | {coef:.3g} | {rms:.1%} |\n")
        f.write("\nTime per unit of logical work (ns), timing mean / work mean:\n\n")
        for fam, k in [("BM_Search", "search"), ("BM_Insert", "insert"),
                       ("BM_Union", "union"), ("BM_Split", "split"),
                       ("BM_SplitRebuild", "split_rebuild")]:
            tm = df[df.family == fam].groupby("N").ns.mean()
            wk = wsum[wsum.kernel == k].set_index("N").work
            f.write(f"- {k}: " + ", ".join(
                f"2^{int(math.log2(x))}: {tm[x] / wk[x]:.2f}" for x in tm.index)
                + "\n")
        d = wsum[wsum.kernel == "split_rebuild"].sort_values("N")
        f.write("\nsplit_rebuild allocations/frees per op: "
                + ", ".join(f"{int(a)}/{int(b)}" for a, b in
                            zip(d.node_allocs, d.node_frees))
                + " (structural kernels: 0/0)\n")
        f.write("\nContext: " + ", ".join(
            f"{k}={ctx.get(k)}" for k in
            ["compiler", "cxx_flags", "google_benchmark", "mhz_per_cpu",
             "library_build_type"]) + "\n")
    print((RES / "complexity_table.md").read_text())

    # ---------------- figure ----------------
    plt.rcParams.update({
        "font.size": 8.5, "axes.edgecolor": INK2, "axes.labelcolor": INK,
        "xtick.color": INK2, "ytick.color": INK2, "axes.linewidth": 0.6,
        "xtick.major.width": 0.6, "ytick.major.width": 0.6,
        "axes.spines.top": False, "axes.spines.right": False,
        "legend.frameon": False, "legend.fontsize": 8,
    })
    fig, axes = plt.subplots(2, 3, figsize=(7.1, 5.0))
    ns_all = np.array(sorted(df.N.unique()))
    xt = [int(math.log2(x)) for x in ns_all]

    def style(ax, title, ylabel):
        ax.set_title(title, fontsize=9, color=INK, loc="left")
        ax.set_ylabel(ylabel)
        ax.grid(True, color=GRID, linewidth=0.5)
        ax.set_axisbelow(True)

    for ax, fam in zip(axes.flat[:4], ["BM_Search", "BM_Insert", "BM_Union",
                                       "BM_Split"]):
        label, supplied = KERNELS[fam]
        d = df[df.family == fam]
        per_seed = d.groupby(["N", "seed"]).ns.mean().reset_index()
        mean = d.groupby("N").ns.mean()
        ax.plot(np.log2(per_seed.N), per_seed.ns, "o", ms=3, color=BLUE,
                alpha=0.55, mec="white", mew=0.4, label="per-seed mean")
        ax.plot(np.log2(mean.index), mean.values, "-", lw=1.5, color=BLUE,
                label="mean over seeds")
        coef, rms = fit(d.N.to_numpy(), d.ns.to_numpy(), supplied)
        xs = np.linspace(ns_all.min(), ns_all.max(), 100)
        ax.plot(np.log2(xs), coef * np.log2(xs), "--", lw=1.2, color=ORANGE,
                label="supplied fit c·log₂N")
        ax.text(0.03, 0.97, f"c = {coef:.1f} ns, RMS {rms:.0%}",
                transform=ax.transAxes, ha="left", va="top", color=INK2,
                fontsize=8)
        ax.set_xticks(xt)
        ax.set_xlabel("log₂ N")
        ax.set_ylim(0, max(per_seed.ns.max(), coef * math.log2(ns_all.max()))
                    * 1.25)
        style(ax, f"({'abcd'[list(KERNELS).index(fam)]}) {label}", "ns / op")
        if fam == "BM_Search":
            ax.legend(loc="upper left", bbox_to_anchor=(0, 0.89))

    ax = axes.flat[4]
    for fam, color, lab in [("BM_Split", BLUE, "structural split"),
                            ("BM_SplitRebuild", ORANGE, "rebuilding split")]:
        d = df[df.family == fam]
        per_seed = d.groupby(["N", "seed"]).ns.mean().reset_index()
        mean = d.groupby("N").ns.mean()
        ax.plot(np.log2(per_seed.N), per_seed.ns, "o", ms=3, color=color,
                alpha=0.55, mec="white", mew=0.4)
        ax.plot(np.log2(mean.index), mean.values, "-", lw=1.5, color=color)
        below = fam == "BM_SplitRebuild"
        ax.annotate(lab, (np.log2(mean.index[2]), mean.values[2]),
                    xytext=(6, -8) if below else (4, 6),
                    textcoords="offset points", ha="left",
                    va="top" if below else "bottom", color=INK, fontsize=8)
    ax.set_yscale("log")
    ax.set_ylim(30, 3e9)
    ax.set_xticks(xt)
    ax.set_xlabel("log₂ N")
    style(ax, "(e) split vs rebuild", "ns / op (log)")

    ax = axes.flat[5]
    for k, color, lab in [("search", BLUE, "search"), ("insert", ORANGE, "insert"),
                          ("union", AQUA, "union"), ("split", YELLOW, "split")]:
        d = wsum[wsum.kernel == k].sort_values("N")
        ax.plot(np.log2(d.N), d.work, "-o", lw=1.5, ms=3.5, color=color,
                mec="white", mew=0.4, label=lab)
    ax.set_xticks(xt)
    ax.set_xlabel("log₂ N")
    ax.set_ylim(0, wsum.work[wsum.kernel != "split_rebuild"].max() * 1.3)
    style(ax, "(f) logical work", "cmp + link r/w per op")
    ax.legend(loc="upper left", ncol=2)

    fig.tight_layout(pad=0.4, w_pad=0.8, h_pad=0.8)
    fig.savefig(RES / "fig_scaling.pdf")
    fig.savefig(RES / "fig_scaling.png", dpi=200)
    print("wrote", RES / "fig_scaling.pdf")


if __name__ == "__main__":
    main()
