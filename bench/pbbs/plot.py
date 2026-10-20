#!/usr/bin/env python3
# Plots taskparts' overhead relative to parlay's scheduler: for each
# benchmark and number of workers P, the median time of a taskparts
# variant divided by the median time of parlay at the same P.
#
# Writes OUT_DIR/<benchmark>.svg (one plot per benchmark),
# OUT_DIR/summary.csv, and OUT_DIR/report.html (all plots on one page).
#
# usage: plot.py RESULTS_CSV OUT_DIR

import csv
import html
import json
import math
import os
import statistics
import sys
from collections import defaultdict

SERIES = [  # (variant, label, css class)
    ("taskparts-main", "taskparts (main)", "s1"),
    ("taskparts-minimal", "taskparts (minimal)", "s2"),
]

STANDALONE_STYLE = """
  svg { font-family: -apple-system, 'Helvetica Neue', Arial, sans-serif; }
  .bg { fill: #ffffff; }
  .grid { stroke: #e4e6ea; stroke-width: 1; }
  .ref { stroke: #6b7079; stroke-width: 1.5; stroke-dasharray: 4 3; }
  .axis-label { fill: #5d626b; font-size: 11px; }
  .title { fill: #1d2025; font-size: 13px; font-weight: 600; }
  .s1 { stroke: #2a78d6; fill: #2a78d6; }
  .s2 { stroke: #eb6834; fill: #eb6834; }
  .line { fill: none; stroke-width: 2; }
  .dot { stroke: #ffffff; stroke-width: 2; }
  .legend { fill: #1d2025; font-size: 11px; }
"""


def load(path):
    times = defaultdict(list)
    for row in csv.DictReader(open(path)):
        if row["check"] == "error":
            continue
        times[(row["benchmark"], row["variant"], int(row["procs"]))].append(float(row["seconds"]))
    return {k: statistics.median(v) for k, v in times.items()}


def ratios(med):
    benches = sorted({b for (b, _, _) in med}, key=lambda b: b.lower())
    procs = sorted({p for (_, _, p) in med})
    out = {}
    for b in benches:
        out[b] = {}
        for v, _, _ in SERIES:
            pts = []
            for p in procs:
                base, t = med.get((b, "parlay", p)), med.get((b, v, p))
                if base and t:
                    pts.append((p, t / base))
            out[b][v] = pts
    return benches, procs, out


def geomean(xs):
    return math.exp(sum(math.log(x) for x in xs) / len(xs))


def summary(benches, procs, rat):
    """Geometric mean of the overhead across benchmarks, per P."""
    out = {}
    for v, _, _ in SERIES:
        pts = []
        for p in procs:
            xs = [r for b in benches for (q, r) in rat[b][v] if q == p]
            if len(xs) == len(benches):
                pts.append((p, geomean(xs)))
        out[v] = pts
    return out


def nice_log_ticks(lo, hi):
    cands = [0.25, 0.33, 0.5, 0.67, 0.75, 0.8, 0.9, 1.0, 1.1, 1.25, 1.5, 2, 3, 4, 6, 8, 12, 16]
    ticks = [t for t in cands if lo <= t <= hi]
    while len(ticks) > 7:
        ticks = [t for i, t in enumerate(ticks) if i % 2 == 0 or t == 1.0]
    if 1.0 not in ticks:
        ticks.append(1.0)
    return sorted(set(ticks))


def chart_svg(title, procs, series, standalone, chart_id=""):
    W, H = 360, 230
    ml, mr, mt, mb = 44, 14, 30, 34
    pw, ph = W - ml - mr, H - mt - mb
    ys = [r for pts in series.values() for (_, r) in pts] + [1.0]
    lo, hi = min(ys), max(ys)
    lo, hi = min(lo, 0.9), max(hi, 1.1)
    pad = (math.log(hi) - math.log(lo)) * 0.08
    llo, lhi = math.log(lo) - pad, math.log(hi) + pad
    pmin, pmax = min(procs), max(procs)

    def x(p):
        return ml + (p - pmin) / max(1, pmax - pmin) * pw

    def y(r):
        return mt + (lhi - math.log(r)) / (lhi - llo) * ph

    o = []
    if standalone:
        o.append(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}">')
        o.append(f"<style>{STANDALONE_STYLE}</style>")
        o.append(f'<rect class="bg" width="{W}" height="{H}"/>')
        o.append(f'<text class="title" x="{ml}" y="18">{html.escape(title)}</text>')
    else:
        o.append(f'<svg viewBox="0 0 {W} {H}" role="img" aria-label="{html.escape(title)}: '
                 f'time relative to parlay vs. number of processors" data-chart="{chart_id}">')
    for t in nice_log_ticks(math.exp(llo), math.exp(lhi)):
        cls = "ref" if t == 1.0 else "grid"
        o.append(f'<line class="{cls}" x1="{ml}" x2="{ml + pw}" y1="{y(t):.1f}" y2="{y(t):.1f}"/>')
        o.append(f'<text class="axis-label" x="{ml - 6}" y="{y(t) + 4:.1f}" text-anchor="end">{t:g}×</text>')
    for p in procs:
        o.append(f'<text class="axis-label" x="{x(p):.1f}" y="{mt + ph + 16}" text-anchor="middle">{p}</text>')
    o.append(f'<text class="axis-label" x="{ml + pw / 2:.1f}" y="{H - 3}" text-anchor="middle">processors</text>')
    for v, label, cls in SERIES:
        pts = series.get(v, [])
        if not pts:
            continue
        d = " ".join(f"{'M' if i == 0 else 'L'}{x(p):.1f},{y(r):.1f}" for i, (p, r) in enumerate(pts))
        o.append(f'<path class="line {cls}" d="{d}"/>')
        for p, r in pts:
            o.append(f'<circle class="dot {cls}" cx="{x(p):.1f}" cy="{y(r):.1f}" r="3.5">'
                     f"<title>{html.escape(label)}, P={p}: {r:.3f}× parlay</title></circle>")
    if standalone:
        lx = ml + pw - 130 * len(SERIES)
        for i, (v, label, cls) in enumerate(SERIES):
            xx = lx + i * 130
            o.append(f'<line class="line {cls}" x1="{xx}" x2="{xx + 16}" y1="14" y2="14"/>')
            o.append(f'<text class="legend" x="{xx + 21}" y="18">{html.escape(label)}</text>')
    else:
        o.append(f'<line class="crosshair" x1="0" x2="0" y1="{mt}" y2="{mt + ph}" visibility="hidden"/>')
        o.append(f'<rect class="hit" x="{ml}" y="{mt}" width="{pw}" height="{ph}" '
                 f'data-pmin="{pmin}" data-pmax="{pmax}" data-ml="{ml}" data-pw="{pw}"/>')
    o.append("</svg>")
    return "\n".join(o)


def main():
    results, out_dir = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)
    med = load(results)
    # plot only the taskparts variants that were run
    global SERIES
    SERIES = [s for s in SERIES if any(v == s[0] for (_, v, _) in med)]
    benches, procs, rat = ratios(med)
    summ = summary(benches, procs, rat)

    for b in benches:
        name = b.replace("/", "_")
        with open(os.path.join(out_dir, f"{name}.svg"), "w") as f:
            f.write(chart_svg(b, procs, rat[b], standalone=True))
    with open(os.path.join(out_dir, "geomean.svg"), "w") as f:
        f.write(chart_svg("geometric mean over all benchmarks", procs, summ, standalone=True))

    with open(os.path.join(out_dir, "summary.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["benchmark", "procs", "parlay_s", "taskparts_main_s", "taskparts_minimal_s",
                    "main_over_parlay", "minimal_over_parlay"])
        for b in benches:
            for p in procs:
                a = med.get((b, "parlay", p))
                m = med.get((b, "taskparts-main", p))
                n = med.get((b, "taskparts-minimal", p))
                w.writerow([b, p, a, m, n, m / a if a and m else "", n / a if a and n else ""])

    # data for the page's tooltips and table
    data = {"procs": procs, "series": [[v, label, "--" + cls] for v, label, cls in SERIES],
            "benches": {}}
    for i, b in enumerate(["geomean"] + benches):
        if b == "geomean":
            entry = {v: {p: r for p, r in summ[v]} for v, _, _ in SERIES}
        else:
            entry = {v: {p: r for p, r in rat[b][v]} for v, _, _ in SERIES}
            entry["seconds"] = {v: {p: med.get((b, v, p)) for p in procs}
                                for v in ["parlay"] + [s[0] for s in SERIES]}
        data["benches"][f"c{i}"] = {"name": b, **entry}
    charts = [chart_svg("geomean", procs, summ, standalone=False, chart_id="c0")]
    charts += [chart_svg(b, procs, rat[b], standalone=False, chart_id=f"c{i + 1}")
               for i, b in enumerate(benches)]
    template = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "report_template.html")).read()
    figs = []
    for i, (b, svg) in enumerate(zip(["geomean"] + benches, charts)):
        if i == 0:
            continue
        group, impl = b.split("/")
        figs.append(f'<figure class="chart"><figcaption><span class="bench">{html.escape(group)}</span>'
                    f'<span class="impl">{html.escape(impl)}</span></figcaption>{svg}</figure>')
    legend = "".join(f'<span class="key"><i class="{cls}"></i>{html.escape(label)}</span>'
                     for _, label, cls in SERIES)
    legend += '<span class="key"><i class="ref"></i>parlay = 1×</span>'
    page = (template.replace("{{SUMMARY_CHART}}", charts[0])
                    .replace("{{LEGEND}}", legend)
                    .replace("{{CHARTS}}", "\n".join(figs))
                    .replace("{{DATA}}", json.dumps(data))
                    .replace("{{NB_BENCHES}}", str(len(benches))))
    meta_path = os.path.join(os.path.dirname(os.path.abspath(results)), "meta.json")
    meta = json.load(open(meta_path)) if os.path.exists(meta_path) else {}
    page = page.replace("{{META}}", json.dumps(meta))
    with open(os.path.join(out_dir, "report.html"), "w") as f:
        f.write(page)
    print(f"wrote {len(benches)} plots to {out_dir}")


if __name__ == "__main__":
    main()
