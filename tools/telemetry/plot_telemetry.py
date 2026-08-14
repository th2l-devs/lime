#!/usr/bin/env python3
"""Plot a lime telemetry CSV (time_s,cpu_ms,gpu_ms,frame_ms).

    python plot_telemetry.py telemetry.csv
    python plot_telemetry.py telemetry.csv --target 8.33 --out chart.png

Stalls (frames well over the target frame period) are shaded red so throttling
and freezes are visible at a glance.
"""

import argparse
import csv
import sys


def load(path):
    t, cpu, gpu, frame = [], [], [], []

    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            t.append(float(row["time_s"]))
            cpu.append(float(row["cpu_ms"]))
            g = float(row["gpu_ms"])
            gpu.append(None if g < 0 else g)
            frame.append(float(row["frame_ms"]))

    return t, cpu, gpu, frame


def find_stalls(t, frame, threshold):
    """Contiguous runs of frames over threshold -> list of (start, end) times."""
    spans, start = [], None

    for i, ms in enumerate(frame):
        if ms > threshold:
            if start is None:
                start = t[i]
        elif start is not None:
            spans.append((start, t[i]))
            start = None

    if start is not None:
        spans.append((start, t[-1]))

    return spans


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("--target", type=float, default=16.67,
                    help="target frame period in ms (16.67=60fps, 8.33=120fps)")
    ap.add_argument("--stall-factor", type=float, default=2.0,
                    help="frame time over target*factor counts as a stall")
    ap.add_argument("--out", help="write PNG instead of showing a window")
    ap.add_argument("--no-frame", action="store_true", help="hide the frame-time line")
    args = ap.parse_args()

    try:
        import matplotlib.pyplot as plt
    except ImportError:
        sys.exit("matplotlib is required:  pip install matplotlib")

    t, cpu, gpu, frame = load(args.csv)

    if not t:
        sys.exit("no samples in " + args.csv)

    threshold = args.target * args.stall_factor
    stalls = find_stalls(t, frame, threshold)

    fig, ax = plt.subplots(figsize=(20, 6))

    for start, end in stalls:
        ax.axvspan(start, end, color="red", alpha=0.12, lw=0)

    if not args.no_frame:
        ax.plot(t, frame, lw=0.6, color="#bbbbbb", label="Frame (wall clock)", zorder=1)

    ax.plot(t, cpu, lw=0.8, color="#1f77b4", label="CPU", zorder=3)

    if any(g is not None for g in gpu):
        ax.plot(t, [g if g is not None else float("nan") for g in gpu],
                lw=0.8, color="#2ca02c", label="GPU", zorder=2)

    ax.axhline(args.target, color="#888888", ls="--", lw=0.8,
               label="target %.2f ms" % args.target)

    ax.set_title("CPU + GPU Time")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Time (ms)")
    ax.set_xlim(t[0], t[-1])
    ax.set_ylim(bottom=0)
    ax.grid(alpha=0.3)
    ax.legend(loc="upper left")

    stalled = sum(1 for ms in frame if ms > threshold)
    print("samples      : %d over %.1fs" % (len(t), t[-1] - t[0]))
    print("cpu ms       : avg %.2f  max %.2f" % (sum(cpu) / len(cpu), max(cpu)))
    if any(g is not None for g in gpu):
        vals = [g for g in gpu if g is not None]
        print("gpu ms       : avg %.2f  max %.2f" % (sum(vals) / len(vals), max(vals)))
    print("frame ms     : avg %.2f  max %.2f" % (sum(frame) / len(frame), max(frame)))
    print("stalls       : %d frames over %.1fms, %d span(s)" % (stalled, threshold, len(stalls)))

    fig.tight_layout()

    if args.out:
        fig.savefig(args.out, dpi=110)
        print("wrote " + args.out)
    else:
        plt.show()


if __name__ == "__main__":
    main()
