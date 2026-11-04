#!/usr/bin/env python3
"""Coded PER report (P3): measured PER of native OFDM and liquid OFDM through
the production PHY classes over AWGN (bench/fec_per.cc via
grid/run-fec-benchmark.sh), against ideal theoretical references.

    report-fec.py --run RUN_DIR --output report.html [--json summary.json]

Every number on the page is computed here from RUN_DIR/fec_per.csv.

Theory (AWGN, ideal synchronization and channel knowledge, independent bit
errors after interleaving, payload section only; the header is excluded):
  * uncoded QPSK / Gray 16-QAM: exact bit error probability, frame success
    (1 - p)^bits over payload + CRC-32;
  * rs8: liquid's blocking of the payload + CRC into shortened RS(255,223)
    codewords correcting 16 byte errors, byte error 1 - (1 - p)^8;
  * v27 (K=7, (133,171) octal, r1/2): first-event union bound with the
    standard distance spectrum, soft (Q(sqrt(2 d Ec/N0))) or hard
    (binomial P_d), PER <= 1 - (1 - P_event)^(info bits).
v29p78 has no published spectrum here and gets no theory curve.
SNR is per complex sample over the frame. Per-tone Es/N0 = SNR * FFT / active
tones (all active tones carry equal power in both PHYs).
"""
import argparse
import csv
import html
import json
import math
from pathlib import Path

FFT = 48
ACTIVE = 38          # liquid default 48-tone mask: 34 data + 4 pilots
EXT_HEADER = 8       # sizeof(ExtendedHeader): frame payload = data + 8 bytes

# Rate 1/2 K=7 (133,171): event weights a_d and information weights B_d
V27_A = {10: 11, 12: 38, 14: 193, 16: 1331, 18: 7275, 20: 40406, 22: 234969}


def qfunc(x):
    return 0.5 * math.erfc(x / math.sqrt(2))


def log_binom_tail(n, p, t):
    """P(more than t of n Bernoulli(p) events)."""
    if p <= 0:
        return 0.0
    if p >= 1:
        return 1.0
    total = 0.0
    lp, lq = math.log(p), math.log1p(-p)
    for i in range(t + 1, n + 1):
        lc = math.lgamma(n + 1) - math.lgamma(i + 1) - math.lgamma(n - i + 1)
        total += math.exp(lc + i * lp + (n - i) * lq)
    return min(1.0, total)


def ber_uncoded(es_n0, bps):
    if bps == 2:
        return qfunc(math.sqrt(es_n0))           # Eb/N0 = Es/N0 / 2 -> Q(sqrt(2 Eb/N0))
    x = math.sqrt(es_n0 / 5)
    return 0.25 * (3 * qfunc(x) + 2 * qfunc(3 * x) - qfunc(5 * x))


def per_uncoded(es_n0, bps, nbytes):
    p = ber_uncoded(es_n0, bps)
    return 1 - (1 - p) ** (8 * (nbytes + 4))


def per_rs8(es_n0, bps, nbytes):
    p = ber_uncoded(es_n0, bps)
    ps = 1 - (1 - p) ** 8
    n = nbytes + 4
    blocks = -(-n // 223)
    dec = -(-n // blocks)
    fail = log_binom_tail(dec + 32, ps, 16)
    return 1 - (1 - fail) ** blocks


def per_v27(es_n0, nbytes, soft):
    ec_n0 = es_n0 / 2                              # QPSK: 2 coded bits per symbol
    if soft:
        pev = sum(a * qfunc(math.sqrt(2 * d * ec_n0)) for d, a in V27_A.items())
    else:
        p = qfunc(math.sqrt(2 * ec_n0))
        pev = 0.0
        for d, a in V27_A.items():
            pd = sum(math.comb(d, k) * p ** k * (1 - p) ** (d - k) for k in range(d // 2 + 1, d + 1))
            pd += 0.5 * math.comb(d, d // 2) * p ** (d // 2) * (1 - p) ** (d // 2)
            pev += a * pd
    pev = min(1.0, pev)
    return 1 - (1 - pev) ** (8 * (nbytes + 4))


def theory(kind, snr_db, nbytes):
    es_n0 = 10 ** (snr_db / 10) * FFT / ACTIVE
    return {
        'qpsk uncoded': lambda: per_uncoded(es_n0, 2, nbytes),
        'qam16 uncoded': lambda: per_uncoded(es_n0, 4, nbytes),
        'qpsk rs8': lambda: per_rs8(es_n0, 2, nbytes),
        'qam16 rs8': lambda: per_rs8(es_n0, 4, nbytes),
        'qpsk v27 soft': lambda: per_v27(es_n0, nbytes, True),
        'qpsk v27 hard': lambda: per_v27(es_n0, nbytes, False),
    }.get(kind, lambda: None)()


def load(run):
    rows = []
    with open(Path(run) / 'fec_per.csv') as f:
        for r in csv.DictReader(f):
            for k in ('snr_db', 'measured_snr_db', 'per', 'demod_us', 'mod_us', 'frame_samples'):
                r[k] = float(r[k])
            for k in ('sent', 'delivered', 'payload_fail', 'header_fail', 'corrupt', 'payload_bytes'):
                r[k] = int(r[k])
            rows.append(r)
    return rows


def snr_at(points, target=0.01, floor=1e-6):
    """Lowest SNR at which PER falls to target (log-linear interpolation).
    PER 0 is floored (measured points: half a packet) so the log is defined."""
    pts = sorted((s, max(p, floor)) for s, p in points)
    for (s0, p0), (s1, p1) in zip(pts, pts[1:]):
        if p0 > target >= p1:
            return s0 + (s1 - s0) * (math.log(p0) - math.log(target)) / (math.log(p0) - math.log(p1))
    if pts and pts[0][1] <= target:
        return pts[0][0]
    return None


def theory_kind(config):
    """'native+v29hdr qpsk v27 soft' -> 'qpsk v27 soft'; chase settings dropped."""
    parts = [w for w in config.split(' ')[1:] if not w.startswith('chase')]
    return ' '.join(parts)


# --------------------------------------------------------------- rendering

SERIES = [('native+v29hdr', 'Native, coded header (v29)', 'var(--s1)', ''),
          ('native', 'Native, uncoded header', 'var(--s2)', ''),
          ('liquid', 'liquid OFDM (header v29p78 BPSK)', 'var(--s3)', ''),
          ('theory', 'Theory, payload only (ideal)', 'var(--ink-2)', '6 5')]


def chart(title, families, rows, kind, nbytes, xrange):
    W, H, L, R, T, B = 960, 340, 64, 16, 16, 44
    x0, x1 = xrange
    ymin = 1e-3
    sx = lambda s: L + (s - x0) / (x1 - x0) * (W - L - R)
    sy = lambda p: T + (0 - math.log10(max(p, ymin))) / (0 - math.log10(ymin)) * (H - T - B)
    out = [f'<figure class="chart"><figcaption>{html.escape(title)}</figcaption>',
           f'<svg viewBox="0 0 {W} {H}" role="img" aria-label="{html.escape(title)}">']
    for e in range(0, 4):
        y = sy(10 ** -e)
        out.append(f'<line class="grid" x1="{L}" x2="{W-R}" y1="{y:.1f}" y2="{y:.1f}"/>'
                   f'<text class="tick" x="{L-8}" y="{y+4:.1f}" text-anchor="end">{["1","0.1","0.01","0.001"][e]}</text>')
    for s in range(int(x0), int(x1) + 1, 2):
        out.append(f'<line class="grid" x1="{sx(s):.1f}" x2="{sx(s):.1f}" y1="{T}" y2="{H-B}"/>'
                   f'<text class="tick" x="{sx(s):.1f}" y="{H-B+18}" text-anchor="middle">{s}</text>')
    out.append(f'<text class="axis-label" x="{(L+W-R)/2}" y="{H-6}" text-anchor="middle">SNR per sample (dB)</text>'
               f'<text class="axis-label" x="14" y="{(T+H-B)/2}" text-anchor="middle" transform="rotate(-90 14 {(T+H-B)/2})">PER</text>')
    legend = []
    for key, label, color, dash in SERIES:
        if key == 'theory':
            pts = [(s / 4, theory(kind, s / 4, nbytes)) for s in range(int(4 * x0), int(4 * x1) + 1)]
            pts = [(s, p) for s, p in pts if p is not None]
        else:
            name = families.get(key)
            if not name:
                continue
            pts = sorted((r['snr_db'], r['per']) for r in rows if r['config'] == name)
        if not pts:
            continue
        d = ' '.join(('M' if i == 0 else 'L') + f'{sx(s):.1f},{sy(p):.1f}' for i, (s, p) in enumerate(pts) if s <= x1 + 1e-9)
        out.append(f'<path d="{d}" fill="none" stroke="{color}" stroke-width="2" stroke-dasharray="{dash}" stroke-linejoin="round"/>')
        if key != 'theory':
            for s, p in pts:
                r = next(r for r in rows if r['config'] == families[key] and r['snr_db'] == s)
                tip = (f'{label}: {s:g} dB, PER {p:.4f} ({r["delivered"]}/{r["sent"]} delivered; '
                       f'{r["header_fail"]} header/acquisition, {r["payload_fail"]} payload failures)')
                out.append(f'<circle cx="{sx(s):.1f}" cy="{sy(p):.1f}" r="4" fill="{color}" stroke="var(--surface)" stroke-width="2">'
                           f'<title>{html.escape(tip)}</title></circle>')
        legend.append(f'<span class="key"><i style="border-color:{color};border-style:{"dashed" if dash else "solid"}"></i>{html.escape(label)}</span>')
    out.append('</svg>')
    out.append('<div class="legend">' + ''.join(legend) + '</div></figure>')
    return '\n'.join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--run', required=True)
    ap.add_argument('--output', required=True)
    ap.add_argument('--json')
    args = ap.parse_args()
    rows = load(args.run)
    info = (Path(args.run) / 'run-info.txt').read_text() if (Path(args.run) / 'run-info.txt').exists() else ''
    nbytes = rows[0]['payload_bytes'] + EXT_HEADER
    configs = sorted({r['config'] for r in rows})

    def series(name):
        return [(r['snr_db'], r['per']) for r in rows if r['config'] == name]

    half_packet = 0.5 / rows[0]['sent']

    summary = {}
    for c in configs:
        pts = series(c)
        hi = max(r['snr_db'] for r in rows if r['config'] == c)
        cpu = next(r for r in rows if r['config'] == c and r['snr_db'] == hi)
        kind = theory_kind(c)
        th = [(s / 4, theory(kind, s / 4, nbytes)) for s in range(-20, 120)]
        th = [(s, p) for s, p in th if p is not None]
        s_meas = snr_at(pts, floor=half_packet)
        s_th = snr_at(th) if th else None
        summary[c] = dict(snr_at_1pct=s_meas, theory_snr_at_1pct=s_th,
                          gap_db=(s_meas - s_th) if s_meas is not None and s_th is not None else None,
                          demod_us_high_snr=cpu['demod_us'], mod_us=cpu['mod_us'], frame_samples=cpu['frame_samples'],
                          corrupt=sum(r['corrupt'] for r in rows if r['config'] == c), theory_kind=kind if th else None)

    if args.json:
        Path(args.json).write_text(json.dumps(summary, indent=1))

    fam = lambda code: {'native+v29hdr': f'native+v29hdr qpsk {code}', 'native': f'native qpsk {code}', 'liquid': f'liquid qpsk {code}'}
    charts = [
        ('Uncoded QPSK', {'native+v29hdr': 'native+v29hdr qpsk uncoded chase0', 'native': 'native qpsk uncoded chase0', 'liquid': 'liquid qpsk uncoded'}, 'qpsk uncoded', (-2, 14)),
        ('QPSK with rs8 (the production payload code)', fam('rs8'), 'qpsk rs8', (-2, 14)),
        ('QPSK with v27 r1/2, soft decision', fam('v27 soft'), 'qpsk v27 soft', (-2, 14)),
        ('QPSK with v27 r1/2, hard decision', fam('v27 hard'), 'qpsk v27 hard', (-2, 14)),
        ('QPSK with v29p78 (no theory curve)', fam('v29p78 soft'), 'qpsk v29p78 soft', (-2, 14)),
        ('16-QAM with rs8', {'native+v29hdr': 'native+v29hdr qam16 rs8', 'native': 'native qam16 rs8', 'liquid': 'liquid qam16 rs8'}, 'qam16 rs8', (6, 22)),
        ('Uncoded 16-QAM', {'native': 'native qam16 uncoded chase0', 'liquid': 'liquid qam16 uncoded'}, 'qam16 uncoded', (6, 22)),
    ]

    def fmt(v, d=1, suffix=''):
        return '—' if v is None else f'{v:.{d}f}{suffix}'

    table = ['<table><thead><tr><th>Configuration</th><th>SNR for PER 1%</th><th>Theory</th><th>Gap</th>'
             '<th>Demod µs/packet</th><th>Frame samples</th></tr></thead><tbody>']
    for c in configs:
        s = summary[c]
        table.append(f'<tr><td>{html.escape(c)}</td><td>{fmt(s["snr_at_1pct"], 1, " dB")}</td><td>{fmt(s["theory_snr_at_1pct"], 1, " dB")}</td>'
                     f'<td>{fmt(s["gap_db"], 1, " dB")}</td><td>{s["demod_us_high_snr"]:.0f}</td><td>{s["frame_samples"]:.0f}</td></tr>')
    table.append('</tbody></table>')

    # Failure breakdown at 6 and 8 dB (QPSK)
    brk = ['<table><thead><tr><th>Configuration</th><th>SNR</th><th>Delivered</th><th>Header / acquisition failures</th><th>Payload failures</th></tr></thead><tbody>']
    for c in configs:
        if 'qam16' in c:
            continue
        for s in (6.0, 8.0):
            r = next((r for r in rows if r['config'] == c and r['snr_db'] == s), None)
            if r:
                brk.append(f'<tr><td>{html.escape(c)}</td><td>{s:g} dB</td><td>{r["delivered"]}/{r["sent"]}</td>'
                           f'<td>{r["header_fail"]}</td><td>{r["payload_fail"]}</td></tr>')
    brk.append('</tbody></table>')

    # Chase on/off ablation (uncoded QPSK, native, uncoded header): PER and
    # demodulation time at every SNR.
    chase = ['<table><thead><tr><th>SNR</th><th>PER, Chase off</th><th>PER, Chase 4 bits</th>'
             '<th>Demod µs, off</th><th>Demod µs, on</th></tr></thead><tbody>']
    off = {r['snr_db']: r for r in rows if r['config'] == 'native qpsk uncoded chase0'}
    on = {r['snr_db']: r for r in rows if r['config'] == 'native qpsk uncoded chase4'}
    for snr in sorted(set(off) & set(on)):
        a, b = off[snr], on[snr]
        chase.append(f'<tr><td>{snr:g} dB</td><td>{a["per"]:.3f}</td><td>{b["per"]:.3f}</td>'
                     f'<td>{a["demod_us"]:.0f}</td><td>{b["demod_us"]:.0f}</td></tr>')
    chase.append('</tbody></table>')
    s_off = summary.get('native qpsk uncoded chase0', {}).get('snr_at_1pct')
    s_on = summary.get('native qpsk uncoded chase4', {}).get('snr_at_1pct')
    chase_gain = (s_off - s_on) if s_off is not None and s_on is not None else None

    corrupt = sum(r['corrupt'] for r in rows)
    page = TEMPLATE.format(
        info=html.escape(info.strip()),
        nbytes=nbytes,
        npackets=rows[0]['sent'],
        points=len(rows),
        corrupt=corrupt,
        charts='\n'.join(chart(t, f, rows, k, nbytes, x) for t, f, k, x in charts),
        table='\n'.join(table),
        breakdown='\n'.join(brk),
        chase='\n'.join(chase),
        chase_gain='—' if chase_gain is None else f'{chase_gain:.1f} dB')
    Path(args.output).write_text(page)
    print(f'wrote {args.output} ({len(rows)} points, {len(configs)} configurations)')


TEMPLATE = '''<title>Coded PER Benchmark</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;500&family=IBM+Plex+Sans:wght@400;500;600&family=IBM+Plex+Serif:wght@600&display=swap">
<style>
/* Layout: one reading column; each chart is a framed instrument panel, tables scroll inside their own box. */
:root {{ --bg: #f3f5f7; --surface: #fcfcfb; --ink: #101820; --ink-2: #48535e; --muted: #6f7a85;
  --grid: #e1e5ea; --s1: #1baf7a; --s2: #2a78d6; --s3: #eb6834;
  --f-display: "IBM Plex Serif", Georgia, serif; --f-body: "IBM Plex Sans", system-ui, -apple-system, "Segoe UI", sans-serif;
  --f-mono: "IBM Plex Mono", ui-monospace, Menlo, Consolas, monospace; }}
@media (prefers-color-scheme: dark) {{ :root:not([data-theme="light"]) {{
  --bg: #0e1116; --surface: #161a20; --ink: #eef1f4; --ink-2: #b9c1c9; --muted: #8a939c;
  --grid: #272d35; --s1: #2bbf89; --s2: #4d95ea; --s3: #e8733f; color-scheme: dark; }} }}
:root[data-theme="dark"] {{ --bg: #0e1116; --surface: #161a20; --ink: #eef1f4; --ink-2: #b9c1c9; --muted: #8a939c;
  --grid: #272d35; --s1: #2bbf89; --s2: #4d95ea; --s3: #e8733f; color-scheme: dark; }}
* {{ box-sizing: border-box; }}
body {{ background: var(--bg); color: var(--ink); font: 15px/1.6 var(--f-body); margin: 0; }}
.page {{ max-width: 1000px; margin: 0 auto; padding-left: max(16px, env(safe-area-inset-left, 0px));
  padding-right: max(16px, env(safe-area-inset-right, 0px)); padding-block: 32px 64px; display: grid; gap: 32px; }}
.page > * {{ min-width: 0; }}
.eyebrow {{ font: 500 12px/1.4 var(--f-mono); letter-spacing: .06em; text-transform: uppercase; color: var(--muted); }}
h1 {{ font: 600 clamp(26px, 4vw, 36px)/1.15 var(--f-display); margin: 0; text-wrap: balance; }}
h2 {{ font: 600 21px/1.3 var(--f-display); margin: 0; text-wrap: balance; }}
header, section {{ display: grid; gap: 12px; }}
p {{ margin: 0; max-width: 68ch; color: var(--ink-2); }}
.chart {{ background: var(--surface); border-radius: 8px; padding: 14px; margin: 0; }}
.chart figcaption {{ font-weight: 600; margin-bottom: 6px; }}
svg {{ width: 100%; height: auto; display: block; }}
.grid {{ stroke: var(--grid); stroke-width: 1; fill: none; }}
.tick, .axis-label {{ fill: var(--muted); font: 12px var(--f-mono); }}
.legend {{ display: flex; flex-wrap: wrap; gap: 6px 18px; font-size: 13px; color: var(--ink-2); margin-top: 6px; }}
.key i {{ display: inline-block; width: 18px; border-top-width: 3px; margin-right: 6px; vertical-align: middle; }}
.charts {{ display: grid; gap: 16px; }}
.wrap {{ overflow-x: auto; background: var(--surface); border-radius: 8px; }}
table {{ border-collapse: collapse; width: 100%; font-size: 14px; font-variant-numeric: tabular-nums; }}
th, td {{ text-align: left; padding: 7px 10px; border-bottom: 1px solid var(--grid); white-space: nowrap; }}
th {{ color: var(--muted); font-weight: 600; }}
pre {{ font: 12px/1.5 var(--f-mono); color: var(--muted); white-space: pre-wrap; margin: 0; }}
</style>
<main class="page">
<header><div class="eyebrow">DragonRadio P3 · AWGN · M=48 CP=6</div>
<h1>Coded packet error rate: native OFDM and liquid OFDM</h1>
<p>Liquid's default tone mask, {nbytes}-byte frame payload (1000 data bytes plus the extended header), {npackets}
packets per point, {points} points. Both PHYs run through the production PHY classes with real acquisition. Native
coded payloads are bit-identical to liquid's packetizer (tests/fullstack/test_native_fec.cc). Packets that passed
CRC with wrong bytes: {corrupt}.</p></header>
<section><h2>Packet error rate against SNR</h2>
<p>Theory curves are ideal references: AWGN, perfect synchronization and channel knowledge, independent bit
errors, payload section only. Measured curves include header failures and acquisition. Hover a point for its
counts.</p>
<div class="charts">{charts}</div></section>
<section><h2>SNR needed for 1% packet error rate</h2>
<p>Interpolated from the measured points; with 1000 packets per point, about 10 errors set each crossing, so
differences under about 0.3 dB are within noise. Gap is the distance to the ideal payload-only theory.
Demodulation time is per packet at the highest SNR measured, one thread per job with many jobs in parallel.</p>
<div class="wrap">{table}</div></section>
<section><h2>Where packets are lost at 6 and 8 dB</h2>
<p>Neither PHY produces a packet for a frame whose header fails, so header and acquisition failures are counted
together.</p>
<div class="wrap">{breakdown}</div></section>
<section><h2>CRC-aided Chase search, on and off</h2>
<p>Native uncoded QPSK only (the search is not used with FEC). At 1% packet error rate the 4-bit search gains
{chase_gain}. It is CRC-aided error correction and not part of the uncoded theory curve. Demodulation time is per
packet at each SNR, measured under the same parallel load as every other point.</p>
<div class="wrap">{chase}</div></section>
<section><h2>Run</h2><pre>{info}</pre></section>
</main>
'''

if __name__ == '__main__':
    main()
