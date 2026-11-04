#!/usr/bin/env python3
"""HTML report of the P4 software-performance results (docs/performance.rst).

Inputs (any subset):
  --bench DIR   grid/run-p4-bench.sh output (profile.json, stream.json, cold.jsonl)
  --ab DIR      grid/run-p4-ab.sh output (A = before, B = after)
  --soak DIR    soak series: DIR/{native,liquid}-RATE/summary.json from sim-soak.py

Every chart has a table with the same numbers. Simulation results only.
"""
import argparse
import glob
import html
import json
import os
import statistics
from pathlib import Path

CSS = """
.viz-root { color-scheme: light; --surface-1: #fcfcfb; --surface-2: #f3f2ef; --text-primary: #0b0b0b;
  --text-secondary: #52514e; --text-muted: #8a8984; --grid: #e4e3df;
  --s1: #2a78d6; --s2: #eb6834; --s3: #1baf7a; --s4: #eda100; --s5: #e87ba4; --s6: #008300; --s7: #6250d6; --s8: #e34948;
  --good: #008300; --bad: #c62a2a;
  font: 14px/1.45 system-ui, -apple-system, Segoe UI, sans-serif; background: var(--surface-1); color: var(--text-primary);
  max-width: 1180px; margin: 0 auto; padding: 24px; }
@media (prefers-color-scheme: dark) { :root:where(:not([data-theme="light"])) .viz-root { color-scheme: dark;
  --surface-1: #1a1a19; --surface-2: #242423; --text-primary: #f0efec; --text-secondary: #c3c2b7; --text-muted: #8f8e86; --grid: #33332f;
  --s1: #3987e5; --s2: #d95926; --s3: #199e70; --s4: #c98500; --s5: #d55181; --s6: #008300; --s7: #9085e9; --s8: #e66767;
  --good: #3fae3f; --bad: #e66767; } }
.viz-root h1 { font-size: 22px; } .viz-root h2 { font-size: 18px; margin-top: 32px; } .viz-root h3 { font-size: 15px; }
.viz-root p, .viz-root li { color: var(--text-secondary); max-width: 900px; }
.viz-root table { border-collapse: collapse; margin: 8px 0 16px; font-variant-numeric: tabular-nums; }
.viz-root th, .viz-root td { padding: 3px 10px; border-bottom: 1px solid var(--grid); text-align: right; }
.viz-root th:first-child, .viz-root td:first-child { text-align: left; }
.viz-root th { color: var(--text-secondary); font-weight: 600; }
.viz-root .pass { color: var(--good); font-weight: 600; } .viz-root .fail { color: var(--bad); font-weight: 600; }
.viz-root svg text { fill: var(--text-secondary); font-size: 11px; }
.viz-root .legend { display: flex; gap: 14px; flex-wrap: wrap; margin: 4px 0; color: var(--text-secondary); font-size: 12px; }
.viz-root .legend span::before { content: ""; display: inline-block; width: 10px; height: 10px; border-radius: 2px;
  margin-right: 5px; vertical-align: -1px; background: var(--c); }
.viz-root details { margin: 6px 0 14px; } .viz-root summary { color: var(--text-secondary); cursor: pointer; }
.viz-root .mark:hover { opacity: 0.8; }
"""

STAGES = ['acquisition/detection', 'FFT and equalization', 'demapping', 'FEC', 'CRC', 'Chase search',
          'memory (memcpy/memmove/memset/malloc)', 'other']
STAGE_LABEL = {'memory (memcpy/memmove/memset/malloc)': 'memory'}


def esc(s):
    return html.escape(str(s))


def fold_stages(stages):
    out = {s: 0.0 for s in STAGES}
    for k, v in stages.items():
        out[k if k in out else 'other'] += v
    return out


def table(headers, rows):
    h = ''.join(f'<th>{esc(x)}</th>' for x in headers)
    body = ''.join('<tr>' + ''.join(f'<td>{x}</td>' for x in r) + '</tr>' for r in rows)
    return f'<table><thead><tr>{h}</tr></thead><tbody>{body}</tbody></table>'


def legend(items):
    return '<div class="legend">' + ''.join(f'<span style="--c: var({c})">{esc(n)}</span>' for n, c in items) + '</div>'


def stacked_bars(rows, width=900):
    """rows: [(label, {stage: share})]; 100% stacked horizontal bars."""
    bar, gap, left = 18, 8, 290
    h = len(rows) * (bar + gap) + 24
    parts = [f'<svg width="{width}" height="{h}" role="img" aria-label="stage shares">']
    plot = width - left - 20
    for x in (0, 0.25, 0.5, 0.75, 1.0):
        xx = left + x * plot
        parts.append(f'<line x1="{xx}" x2="{xx}" y1="0" y2="{h - 20}" stroke="var(--grid)"/>'
                     f'<text x="{xx}" y="{h - 6}" text-anchor="middle">{int(x * 100)}%</text>')
    for i, (label, shares) in enumerate(rows):
        y = i * (bar + gap)
        parts.append(f'<text x="{left - 8}" y="{y + bar - 5}" text-anchor="end">{esc(label)}</text>')
        x = left
        for k, s in enumerate(STAGES):
            w = shares.get(s, 0) * plot
            if w <= 0:
                continue
            parts.append(f'<rect class="mark" x="{x:.1f}" y="{y}" width="{max(w - 2, 0.5):.1f}" height="{bar}" rx="2" '
                         f'fill="var(--s{k + 1})"><title>{esc(label)}: {esc(STAGE_LABEL.get(s, s))} '
                         f'{100 * shares[s]:.1f}%</title></rect>')
            x += w
    parts.append('</svg>')
    return ''.join(parts)


def hbars(rows, unit, width=900, series=(('native', '--s1'), ('liquid', '--s2'))):
    """rows: [(label, {series: value})]; grouped horizontal bars from zero."""
    bar, gap, left = 11, 10, 290
    n = len(series)
    h = len(rows) * (n * bar + gap) + 24
    vmax = max((v for _, d in rows for v in d.values() if v is not None), default=1) * 1.08
    plot = width - left - 60
    parts = [f'<svg width="{width}" height="{h}" role="img" aria-label="{esc(unit)}">']
    for t in range(0, 6):
        x = left + t / 5 * plot
        parts.append(f'<line x1="{x}" x2="{x}" y1="0" y2="{h - 20}" stroke="var(--grid)"/>'
                     f'<text x="{x}" y="{h - 6}" text-anchor="middle">{vmax * t / 5:.0f}</text>')
    for i, (label, d) in enumerate(rows):
        y0 = i * (n * bar + gap)
        parts.append(f'<text x="{left - 8}" y="{y0 + n * bar / 2 + 4}" text-anchor="end">{esc(label)}</text>')
        for j, (name, color) in enumerate(series):
            v = d.get(name)
            if v is None:
                continue
            w = v / vmax * plot
            y = y0 + j * bar
            parts.append(f'<rect class="mark" x="{left}" y="{y}" width="{max(w, 1):.1f}" height="{bar - 2}" rx="2" fill="var({color})">'
                         f'<title>{esc(label)}, {esc(name)}: {v:.2f} {esc(unit)}</title></rect>'
                         f'<text x="{left + w + 4:.1f}" y="{y + bar - 3}">{v:.1f}</text>')
    parts.append(f'<text x="{left + plot}" y="{h - 6}" text-anchor="end" dx="50">{esc(unit)}</text></svg>')
    return ''.join(parts)


def lines(series, xlabel, ylabel, width=420, height=220):
    """series: [(name, color, [(x, y)])]."""
    pts = [p for _, _, s in series for p in s]
    if not pts:
        return ''
    x0, x1 = min(p[0] for p in pts), max(p[0] for p in pts)
    y0, y1 = 0, max(p[1] for p in pts) * 1.1
    left, bottom = 46, 30
    pw, ph = width - left - 10, height - bottom - 10
    sx = lambda x: left + (x - x0) / ((x1 - x0) or 1) * pw
    sy = lambda y: 10 + ph - (y - y0) / ((y1 - y0) or 1) * ph
    parts = [f'<svg width="{width}" height="{height}" role="img" aria-label="{esc(ylabel)}">']
    for t in range(0, 5):
        yv = y0 + (y1 - y0) * t / 4
        parts.append(f'<line x1="{left}" x2="{left + pw}" y1="{sy(yv):.1f}" y2="{sy(yv):.1f}" stroke="var(--grid)"/>'
                     f'<text x="{left - 4}" y="{sy(yv) + 4:.1f}" text-anchor="end">{yv:.0f}</text>')
    parts.append(f'<text x="{left + pw / 2}" y="{height - 4}" text-anchor="middle">{esc(xlabel)}</text>'
                 f'<text x="10" y="{10 + ph / 2}" transform="rotate(-90 10 {10 + ph / 2})" text-anchor="middle">{esc(ylabel)}</text>')
    for name, color, s in series:
        d = ' '.join(f'{sx(x):.1f},{sy(y):.1f}' for x, y in s)
        parts.append(f'<polyline points="{d}" fill="none" stroke="var({color})" stroke-width="2"><title>{esc(name)}</title></polyline>')
        for x, y in s[::max(1, len(s) // 40)]:
            parts.append(f'<circle cx="{sx(x):.1f}" cy="{sy(y):.1f}" r="5" fill="transparent"><title>{esc(name)}: {y:.1f} at {x:.0f} s</title></circle>')
    parts.append('</svg>')
    return ''.join(parts)


def bench_section(d):
    out = ['<h2>Stage profile through the production PHY classes</h2>']
    prof = json.loads((d / 'profile.json').read_text())
    info = (d / 'run-info.txt').read_text() if (d / 'run-info.txt').exists() else ''
    out.append(f'<p>One pinned core, {prof["packets"]} packets of {prof["payload"]} bytes per point, '
               f'{prof["chunk"]}-sample chunks. Shares from the in-process CPU sampler (perf unavailable). '
               f'Run: <code>{esc(info.splitlines()[0] if info else "")}</code></p>')
    rows, trows = [], []
    for r in prof['results']:
        for l in r['links']:
            label = f'{r["config"]}, {l["link"]} {l["snr_db"]:.1f} dB'
            sh = fold_stages(l['stages'])
            rows.append((label, sh))
            trows.append([esc(label), f'{100 * l["per"]:.1f}%', f'{l["cpu_us_per_attempt"]:.1f}', f'{l["cpu_us_per_verified"]:.1f}',
                          f'{l["frame_cpu_us_per_attempt"]:.1f}'] + [f'{100 * sh[s]:.0f}' for s in STAGES])
    out.append(legend([(STAGE_LABEL.get(s, s), f'--s{i + 1}') for i, s in enumerate(STAGES)]))
    out.append(stacked_bars(rows))
    out.append('<details open><summary>Table</summary>' + table(
        ['Configuration and link', 'PER', 'CPU µs/attempt', 'CPU µs/verified', 'frame-only µs/attempt']
        + [STAGE_LABEL.get(s, s) + ' %' for s in STAGES], trows) + '</details>')
    out.append('<h3>Idle cost and modulation</h3>')
    out.append(table(['Configuration', 'idle ns/sample', 'idle MS/s one core', 'modulation µs/packet', 'frame samples'],
                     [[esc(r['config']), f'{r["idle"]["cpu_ns_per_sample"]:.2f}', f'{r["idle"]["msps_one_core"]:.1f}',
                       f'{r["modulation"]["cpu_us_per_packet"]:.1f}', f'{r["frame_samples"]:.0f}'] for r in prof['results']]))
    if (d / 'cold.jsonl').exists():
        cold = [json.loads(l) for l in open(d / 'cold.jsonl') if l.strip()]
        out.append('<h3>Cold construction (fresh process, median of runs)</h3>')
        keys = ['construct_us', 'first_modulator_us', 'first_demodulator_us', 'warm_construct_us', 'warm_modulator_us',
                'warm_demodulator_us', 'getModulatedSize_us']
        trs = []
        for phy in ('native', 'liquid'):
            rs = [c for c in cold if c['phy'] == phy]
            if rs:
                trs.append([phy, str(len(rs))] + [f'{statistics.median(c[k] for c in rs):.0f}' for k in keys])
        out.append(table(['PHY', 'runs'] + [k.replace('_us', ' µs') for k in keys], trs))
    if (d / 'stream.json').exists():
        out.append(stream_section(json.loads((d / 'stream.json').read_text())['results']))
    return ''.join(out)


def stream_section(results):
    out = ['<h2>Streaming receive throughput (one core per demodulator)</h2>',
           '<p>MS/s of capture each demodulator sustains on one core; headroom at R MS/s is this divided by R. '
           'PHY demodulator only (no channelizer, MAC or I/O).</p>']
    rows = []
    by = {}
    for r in results:
        by.setdefault((r['config'], r['pattern']), {})[(r['chunk'], r['demodulators'])] = r
    for (c, p), v in by.items():
        if (2048, 1) in v:
            rows.append((f'{c}, {p}', {'native' if c.startswith('native') else 'liquid': v[(2048, 1)]['msps_per_demod_cpu']}))
    out.append(legend([('native', '--s1'), ('liquid', '--s2')]))
    out.append(hbars(rows, 'MS/s per core (2048-sample chunks, one demodulator)'))
    chunks = sorted({r['chunk'] for r in results})
    trows = []
    for (c, p), v in by.items():
        trows.append([esc(c), esc(p)] + [f'{v[(ch, 1)]["msps_per_demod_cpu"]:.1f}' if (ch, 1) in v else '' for ch in chunks]
                     + [f'{v[(2048, 2)]["msps_per_demod_cpu"]:.1f}' if (2048, 2) in v else '',
                        f'{v[(2048, 1)]["delivered_first_pass"]}/{v[(2048, 1)]["frames"]}' if (2048, 1) in v else ''])
    out.append('<details open><summary>Table: MS/s per demodulator by chunk size</summary>' + table(
        ['Configuration', 'capture'] + [f'chunk {ch}' for ch in chunks] + ['2 demods (2048)', 'delivered'], trows) + '</details>')
    return ''.join(out)


def ab_section(d):
    out = ['<h2>Before and after the native hot-path changes (paired, randomized blocks)</h2>']
    info = (d / 'run-info.txt').read_text()
    out.append(f'<pre>{esc(info)}</pre>')
    def load(prefix, v):
        return [json.loads(Path(p).read_text()) for p in sorted(glob.glob(str(d / f'{prefix}-{v}-*.json')))]
    A, B = load('profile', 'A'), load('profile', 'B')
    if A and B:
        trows = []
        for i, r in enumerate(A[0]['results']):
            for j, l in enumerate(r['links']):
                a = statistics.mean(x['results'][i]['links'][j]['cpu_us_per_attempt'] for x in A)
                b = statistics.mean(x['results'][i]['links'][j]['cpu_us_per_attempt'] for x in B)
                trows.append([esc(f'{r["config"]}, {l["link"]}'), f'{a:.1f}', f'{b:.1f}', f'{b / a:.2f}'])
            a = statistics.mean(x['results'][i]['idle']['cpu_ns_per_sample'] for x in A)
            b = statistics.mean(x['results'][i]['idle']['cpu_ns_per_sample'] for x in B)
            trows.append([esc(f'{r["config"]}, idle (ns/sample)'), f'{a:.2f}', f'{b:.2f}', f'{b / a:.2f}'])
        out.append('<h3>Demodulation CPU per attempt (µs), mean over blocks</h3>'
                   + table(['Configuration', 'before', 'after', 'after/before'], trows))
    SA, SB = load('stream', 'A'), load('stream', 'B')
    if SA and SB:
        trows = []
        for i, r in enumerate(SA[0]['results']):
            if r['chunk'] != 2048:
                continue
            a = statistics.mean(x['results'][i]['msps_per_demod_cpu'] for x in SA)
            b = statistics.mean(x['results'][i]['msps_per_demod_cpu'] for x in SB)
            trows.append([esc(f'{r["config"]}, {r["pattern"]}, {r["demodulators"]} demod'), f'{a:.2f}', f'{b:.2f}', f'{b / a:.2f}'])
        out.append('<h3>Streaming MS/s per demodulator (2048-sample chunks)</h3>'
                   + table(['Configuration', 'before', 'after', 'after/before'], trows))
    return ''.join(out)


def ms(x):
    return '' if x is None else f'{1e3 * x:.2f}'


def soak_section(d):
    runs = {}
    for p in sorted(glob.glob(str(d / '*' / 'summary.json'))):
        s = json.loads(Path(p).read_text())
        runs[(s['phy'], s['rate_msps'])] = s
    if not runs:
        return ''
    out = ['<h2>Real-time soak through the full stack (simulated device, wall-clock paced)</h2>',
           '<p>Two production radios, TDMA (35 ms slots, 10 ms guard), rs8 QPSK, ARQ, tail-drop queue, saturating '
           'bidirectional UDP. Load per radio thread: work meters for the spinning workers, /proc CPU for the rest; '
           'simulated-device rendering excluded. Gate: busiest thread ≤ 50% (headroom ≥ 2x), no device late/underflow/'
           'overflow/drop during traffic, queue occupancy bounded, RSS slope &lt; 100 kB/min.</p>']
    rates = sorted({r for _, r in runs})
    out.append(legend([('native', '--s1'), ('liquid', '--s2')]))
    out.append(hbars([(f'{r:g} MS/s', {phy: runs[(phy, r)]['nodes']['1']['headroom'] for phy in ('native', 'liquid')
                                         if (phy, r) in runs and runs[(phy, r)]['nodes'].get('1', {}).get('headroom')})
                      for r in rates], 'headroom (x), node 1'))
    trows = []
    for (phy, rate), s in sorted(runs.items(), key=lambda kv: (kv[0][1], kv[0][0])):
        for n, node in sorted(s['nodes'].items()):
            rec = next(iter(node['received'].values()), {})
            dev = node['device_during_traffic']
            sv = node.get('service', {})
            qd = node.get('queue_depth_packets') or {}
            trows.append([f'{phy} {rate:g} MS/s node {n}', f'{s["duration_s"] / 60:.0f}',
                          f'{node["headroom"]:.2f}' if node.get('headroom') else '', esc(node.get('busiest_radio_thread')),
                          f'{node.get("radio_work_cpus", 0):.2f}', f'{node.get("spinning_cpus", 0):.2f}',
                          f'{100 * node["sim_device_busy"].get("dr-sim-render", 0):.0f}%',
                          f'{rec.get("unique", 0)}', ms(rec.get('latency_p50')), ms(rec.get('latency_p99')), ms(rec.get('latency_p999')),
                          ms((sv.get('demod_latency_s') or {}).get('p99')), ms((sv.get('demod_latency_s') or {}).get('p999')),
                          ms((sv.get('mod_latency_s') or {}).get('p99')),
                          f'{qd.get("p99", "")}', f'{dev["tx_late"]}/{dev["tx_underflow"]}/{dev["rx_late"]}/{dev["rx_overflows"]}/{dev["rx_queue_drops"]}',
                          f'{node.get("rss_hwm_kb", 0) / 1024:.0f}',
                          f'{node["rss_slope_kb_per_min_second_half"]:.1f}' if node.get('rss_slope_kb_per_min_second_half') is not None else '',
                          f'{node["heap_slope_kb_per_min_second_half"]:.1f}' if node.get('heap_slope_kb_per_min_second_half') is not None else '',
                          f'<span class="{"pass" if s["passed"] else "fail"}">{"pass" if s["passed"] else "FAIL"}</span>'])
    out.append('<details open><summary>Table</summary>' + table(
        ['Run', 'min', 'headroom x', 'busiest thread', 'work CPUs', 'spinning CPUs', 'device model', 'packets', 'latency p50 ms',
         'p99', 'p99.9', 'demod p99 ms', 'demod p99.9', 'mod p99 ms', 'queue p99 pkts', 'late/uf/rx-late/ovf/drop', 'RSS HWM MB',
         'RSS slope kB/min', 'heap slope kB/min', 'gate'], trows) + '</details>')
    for (phy, rate), s in sorted(runs.items(), key=lambda kv: (kv[0][1], kv[0][0])):
        fails = [c['check'] for c in s['checks'] if not c['passed']]
        if fails:
            out.append(f'<p><b>{esc(phy)} {rate:g} MS/s failed checks:</b> ' + '; '.join(esc(f) for f in fails) + '</p>')
    out.append('<h3>RSS over time (node 1, MB)</h3><div style="display:flex;flex-wrap:wrap;gap:12px">')
    for r in rates:
        ser = []
        for phy, color in (('native', '--s1'), ('liquid', '--s2')):
            s = runs.get((phy, r))
            if s and '1' in s['nodes']:
                ser.append((f'{phy} {r:g} MS/s', color, [(t, kb / 1024) for t, kb in s['nodes']['1']['rss_kb']]))
        out.append(f'<div><div style="color:var(--text-secondary)">{r:g} MS/s</div>{lines(ser, "seconds since launch", "RSS MB")}</div>')
    out.append('</div>')
    if any('heap_kb' in s['nodes'].get('1', {}) for s in runs.values()):
        out.append('<h3>Heap in use over time (node 1, MB; glibc mallinfo2)</h3><div style="display:flex;flex-wrap:wrap;gap:12px">')
        for r in rates:
            ser = []
            for phy, color in (('native', '--s1'), ('liquid', '--s2')):
                s = runs.get((phy, r))
                if s and 'heap_kb' in s['nodes'].get('1', {}):
                    ser.append((f'{phy} {r:g} MS/s', color, [(t, kb / 1024) for t, kb in s['nodes']['1']['heap_kb']]))
            out.append(f'<div><div style="color:var(--text-secondary)">{r:g} MS/s</div>{lines(ser, "seconds since launch", "heap MB")}</div>')
        out.append('</div>')
    return ''.join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--bench')
    ap.add_argument('--ab')
    ap.add_argument('--soak')
    ap.add_argument('--output', required=True)
    args = ap.parse_args()
    body = ['<h1>DragonRadio P4: software performance (simulation, gridgpu01)</h1>',
            '<p>No radio was opened and nothing was transmitted. All results come from the production code over a '
            'simulated device or captured streams. See docs/performance.rst for methods and limits.</p>']
    if args.soak:
        body.append(soak_section(Path(args.soak)))
    if args.bench:
        body.append(bench_section(Path(args.bench)))
    if args.ab:
        body.append(ab_section(Path(args.ab)))
    Path(args.output).write_text('<!doctype html><html><head><meta charset="utf-8"><title>DragonRadio P4 performance</title>'
                                 f'<style>{CSS}</style></head><body class="viz-root">{"".join(body)}</body></html>\n')
    print(f'wrote {args.output}')


if __name__ == '__main__':
    raise SystemExit(main())
