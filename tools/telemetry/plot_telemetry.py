#!/usr/bin/env python3
"""Plot and benchmark a lime telemetry CSV.

    python plot_telemetry.py telemetry.csv
    python plot_telemetry.py telemetry.csv --target 8.33 --out chart.png

Columns: time_s,frame_ms,cpu_ms,update_ms,render_ms,swap_ms,gpu_ms,mem_mb,section
(older 4-column captures still load).

Sections are shaded as coloured bands; stalls are shaded red.
"""

import argparse
import collections
import csv
import sys

PALETTE = ["#dbe9f6", "#fdebd0", "#e8dff5", "#dff0e4", "#fbe3e8", "#e6e6e6"]


def load(path):
    meta, rows = {}, []

    with open(path, newline="") as f:
        lines = []
        for line in f:
            if line.startswith("#"):
                if "=" in line:
                    k, v = line[1:].split("=", 1)
                    meta[k.strip()] = v.strip()
            else:
                lines.append(line)

        for r in csv.DictReader(lines):
            rows.append(r)

    if not rows:
        sys.exit("no samples in " + path)

    def col(name, default=None):
        if name not in rows[0]:
            return [default] * len(rows)
        out = []
        for r in rows:
            v = r[name]
            try:
                out.append(float(v))
            except (TypeError, ValueError):
                out.append(default)
        return out

    data = {
        "t": col("time_s", 0.0),
        "frame": col("frame_ms", 0.0),
        "cpu": col("cpu_ms", 0.0),
        "update": col("update_ms"),
        "render": col("render_ms"),
        "swap": col("swap_ms"),
        "gpu": [None if (g is None or g < 0) else g for g in col("gpu_ms")],
        "mem": col("mem_mb"),
        "section": [r.get("section", "") or "" for r in rows],
    }
    return meta, data


def spans(t, values, predicate):
    """Contiguous runs where predicate(value) -> [(start, end, value)]."""
    out, start, cur = [], None, None

    for i, v in enumerate(values):
        hit = predicate(v)
        if hit and start is None:
            start, cur = t[i], v
        elif not hit and start is not None:
            out.append((start, t[i], cur))
            start = None

    if start is not None:
        out.append((start, t[-1], cur))

    return out


def section_spans(t, sections):
    out, start, cur = [], t[0], sections[0]

    for i, s in enumerate(sections):
        if s != cur:
            out.append((start, t[i], cur))
            start, cur = t[i], s

    out.append((start, t[-1], cur))
    return [s for s in out if s[2]]


def low(sorted_frames, frac):
    n = max(1, int(len(sorted_frames) * frac))
    worst = sorted_frames[-n:]
    return 1000.0 / (sum(worst) / len(worst))


def report(meta, d, budget):
    f = d["frame"]
    s = sorted(f)
    n = len(f)
    avg = sum(f) / n

    def pct(p):
        return s[min(n - 1, int(n * p / 100))]

    print("===== lime telemetry =====")
    for k in ("gpu", "gl", "platform"):
        if k in meta:
            print("%-13s: %s" % (k, meta[k]))
    print("%-13s: %.1fs over %d frames" % ("duration", d["t"][-1] - d["t"][0], n))
    print("%-13s: %.3f ms (%.2f fps)" % ("average", avg, 1000 / avg))
    print("%-13s: %.3f ms (%.2f fps)" % ("frame p50", pct(50), 1000 / pct(50)))
    print("%-13s: %.3f ms" % ("frame p95", pct(95)))
    print("%-13s: %.3f ms" % ("frame p99", pct(99)))
    print("%-13s: %.3f ms" % ("frame max", s[-1]))
    print("%-13s: %.2f fps" % ("1% low", low(s, 0.01)))
    print("%-13s: %.2f fps" % ("0.1% low", low(s, 0.001)))
    print("%-13s: %.3f ms" % ("cpu avg", sum(d["cpu"]) / n))

    for key, label in (("update", "update avg"), ("render", "render avg"), ("swap", "swap avg")):
        vals = [v for v in d[key] if v is not None]
        if vals:
            print("%-13s: %.3f ms" % (label, sum(vals) / len(vals)))

    g = [v for v in d["gpu"] if v is not None]
    print("%-13s: %s" % ("gpu avg", "%.3f ms" % (sum(g) / len(g)) if g else "n/a"))

    mem = [v for v in d["mem"] if v is not None]
    if mem:
        print("%-13s: %.1f MB" % ("heap peak", max(mem)))

    overs = sum(1 for v in f if v > budget * 1.5)
    print("%-13s: %d frames > %.2f ms (%.1f%%)" % ("over budget", overs, budget * 1.5, 100.0 * overs / n))

    secs = [x for x in d["section"] if x]
    if secs:
        print("--- sections ---")
        by = collections.OrderedDict()
        for sec, fr in zip(d["section"], f):
            if sec:
                by.setdefault(sec, []).append(fr)
        for name, fr in by.items():
            ss = sorted(fr)
            print("  %-18s %6d frames  avg %6.2f fps  1%% low %6.2f fps"
                  % (name, len(fr), 1000 / (sum(fr) / len(fr)), low(ss, 0.01)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("--target", type=float, default=0,
                    help="target frame period in ms (default: median)")
    ap.add_argument("--stall-factor", type=float, default=2.0)
    ap.add_argument("--out")
    ap.add_argument("--no-sections", action="store_true")
    args = ap.parse_args()

    try:
        import matplotlib.pyplot as plt
    except ImportError:
        sys.exit("matplotlib is required:  pip install matplotlib")

    meta, d = load(args.csv)
    t, f = d["t"], d["frame"]
    budget = args.target if args.target > 0 else sorted(f)[len(f) // 2]

    report(meta, d, budget)

    has_mem = any(v is not None for v in d["mem"])
    rows = 3 if has_mem else 2
    fig, axes = plt.subplots(rows, 1, figsize=(20, 4 * rows), sharex=True,
                             gridspec_kw={"height_ratios": [3, 2] + ([1] if has_mem else [])})

    colours = {}
    if not args.no_sections:
        for start, end, name in section_spans(t, d["section"]):
            if name not in colours:
                colours[name] = PALETTE[len(colours) % len(PALETTE)]
            for ax in axes:
                ax.axvspan(start, end, color=colours[name], lw=0, zorder=0)
            axes[0].annotate(name, (start, 1.01), xycoords=("data", "axes fraction"),
                             fontsize=8, color="#555555")

    for start, end, _ in spans(t, f, lambda v: v > budget * args.stall_factor):
        for ax in axes:
            ax.axvspan(start, end, color="red", alpha=0.18, lw=0, zorder=1)

    ax = axes[0]
    ax.plot(t, f, lw=0.6, color="#999999", label="Frame (wall clock)", zorder=2)
    ax.plot(t, d["cpu"], lw=0.8, color="#1f77b4", label="CPU", zorder=4)
    if any(v is not None for v in d["gpu"]):
        ax.plot(t, [v if v is not None else float("nan") for v in d["gpu"]],
                lw=0.8, color="#2ca02c", label="GPU", zorder=3)
    ax.axhline(budget, color="#666666", ls="--", lw=0.8, label="target %.2f ms" % budget)
    ax.set_ylabel("Time (ms)")
    ax.set_title("CPU + GPU Time")
    ax.set_ylim(0, min(max(f) * 1.05, budget * 6))
    ax.legend(loc="upper left", fontsize=8)
    ax.grid(alpha=0.3)

    ax = axes[1]
    plotted = False
    for key, colour, label in (("update", "#9467bd", "Update"),
                               ("render", "#ff7f0e", "Render"),
                               ("swap", "#8c564b", "Swap / vsync wait")):
        if any(v is not None for v in d[key]):
            ax.plot(t, [v if v is not None else float("nan") for v in d[key]],
                    lw=0.7, color=colour, label=label, zorder=3)
            plotted = True
    ax.set_ylabel("Time (ms)")
    ax.set_title("CPU breakdown")
    if plotted:
        ax.legend(loc="upper left", fontsize=8)
    else:
        ax.text(0.5, 0.5, "no breakdown columns in this capture",
                ha="center", va="center", transform=ax.transAxes, color="#888888")
    ax.grid(alpha=0.3)

    if has_mem:
        ax = axes[2]
        ax.plot(t, [v if v is not None else float("nan") for v in d["mem"]],
                lw=0.9, color="#d62728", label="Heap", zorder=3)
        ax.set_ylabel("MB")
        ax.set_title("Heap usage")
        ax.legend(loc="upper left", fontsize=8)
        ax.grid(alpha=0.3)

    axes[-1].set_xlabel("Time (s)")
    axes[-1].set_xlim(t[0], t[-1])
    fig.tight_layout()

    if args.out:
        fig.savefig(args.out, dpi=110)
        print("wrote " + args.out)
    else:
        plt.show()


if __name__ == "__main__":
    main()
