#!/usr/bin/env python3
"""
memtrace_dashboard.py - Interactive HTML dashboard from kdec_memtrace.bin

Usage:
  python3 memtrace_dashboard.py <trace.bin> [--elf sysmodule.elf]
          [--gdb PATH] [--output dashboard.html] [--max-events N]

Options:
  --elf PATH       ELF file for symbolization (optional)
  --gdb PATH       aarch64-none-elf-gdb path (default: devkitpro location)
  --output PATH    Output HTML file (default: memtrace_dashboard.html)
  --max-events N   Max alloc events shown in the address canvas (default: 8000)
"""

import argparse, json, math, os, re, struct, subprocess, sys, tempfile
from collections import defaultdict

# ── constants ─────────────────────────────────────────────────────────────
MAGIC    = b'KDMT'
_HDR     = struct.Struct('<4sBxxxQ')   # 16 bytes
_REC1    = struct.Struct('<BBIQ')      # 14 bytes  (v1, no timestamp)
_REC2    = struct.Struct('<BBIQQ')     # 22 bytes  (v2, with timestamp)
TICK_HZ  = 19_200_000
BK_LIM   = [16, 64, 256, 1024, 4096, 16384, 65536, 262144]
BK_LAB   = ['≤16B','≤64B','≤256B','≤1KB','≤4KB','≤16KB','≤64KB','≤256KB','>256KB']

def _ms(ticks): return ticks * 1000.0 / TICK_HZ

def _bucket(sz):
    for i, l in enumerate(BK_LIM):
        if sz <= l: return i
    return len(BK_LIM)

# ── parse ─────────────────────────────────────────────────────────────────
def parse(path):
    recs, anchor, ver = [], 0, 1
    with open(path, 'rb') as f:
        hdr = f.read(_HDR.size)
        if len(hdr) < 8 or hdr[:4] != MAGIC:
            raise ValueError(f"Not a KDMT file (got {hdr[:4]!r})")
        ver = hdr[4]
        if ver not in (1, 2):
            raise ValueError(f"Unsupported version {ver}")
        if len(hdr) == _HDR.size:
            _, _, anchor = _HDR.unpack(hdr)
        rf = _REC2 if ver == 2 else _REC1
        while True:
            chunk = f.read(rf.size)
            if not chunk or len(chunk) < rf.size: break
            if ver == 2: tb, nf, sz, ptr, ts = rf.unpack(chunk)
            else:        tb, nf, sz, ptr     = rf.unpack(chunk); ts = 0
            k = chr(tb)
            if k not in ('A', 'F'): break
            fr = ()
            if nf:
                raw = f.read(nf * 8)
                if len(raw) < nf * 8: break
                fr = struct.unpack(f'<{nf}Q', raw)
            recs.append((k, sz, ptr, ts, fr))
    print(f"Parsed {len(recs):,} records (v{ver})", file=sys.stderr)
    return recs, anchor, ver

# ── symbolize ─────────────────────────────────────────────────────────────
_MGL = '_ZN10MemTracker4initEPKc'

def detect_base(anchor, elf, gdb):
    if not anchor or not elf: return 0
    nm = re.sub(r'gdb$', 'nm', gdb)
    vma = None
    try:
        out = subprocess.run([nm, '--defined-only', elf],
                             capture_output=True, text=True, timeout=30).stdout
        for line in out.splitlines():
            if _MGL in line or ('MemTracker' in line and 'init' in line):
                try: vma = int(line.split()[0], 16); break
                except: pass
    except: pass
    if vma is None:
        try:
            out = subprocess.run(
                [gdb, '--batch', '-nx', '-ex', f'file {elf}',
                 '-ex', f'info address {_MGL}'],
                capture_output=True, text=True, timeout=30).stdout
            m = re.search(r'0x([0-9a-fA-F]+)', out)
            if m: vma = int(m.group(1), 16)
        except: pass
    if vma is None:
        print("Warning: MemTracker::init not found in ELF", file=sys.stderr); return 0
    slide = anchor - vma
    print(f"ASLR slide: {slide:#x}", file=sys.stderr)
    return slide

def symbolize(addrs, elf, gdb, base):
    if not addrs or not elf: return {}
    unique = sorted(addrs)
    print(f"Symbolizing {len(unique)} addresses...", file=sys.stderr)
    with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.gdb') as tf:
        scr = tf.name
        for a in unique:
            off = a - base
            tf.write(f'printf "|||{a:016x}|||\\n"\n')
            tf.write(f'info symbol {off:#x}\n')
            tf.write(f'info line *{off:#x}\n')
    try:
        out = subprocess.run([gdb, '--batch', '-nx', '-ex', f'file {elf}', '-x', scr],
                             capture_output=True, text=True, timeout=300).stdout
    finally:
        os.remove(scr)
    sm = {}
    blocks = re.split(r'\|\|\|([0-9a-fA-F]+)\|\|\|', out)
    for i in range(1, len(blocks) - 1, 2):
        a   = int(blocks[i], 16)
        txt = blocks[i + 1].strip()
        fn  = '??'
        m   = re.search(r'^(.*?)(?:\s+in section|\s*$)', txt, re.MULTILINE)
        if m and not m.group(1).startswith('No symbol'): fn = m.group(1).strip()
        loc = ''
        m = re.search(r'Line (\d+) of "(.*?)"', txt)
        if m: loc = f" @ {os.path.basename(m.group(2))}:{m.group(1)}"
        sm[a] = fn + loc
    return sm

# ── build data ────────────────────────────────────────────────────────────
def build(recs, sym_map, max_evs=8000, n_tl=2000, n_rate=150):
    if not recs: return {}
    has_ts = any(ts for _, _, _, ts, _ in recs)
    srecs  = sorted(recs, key=lambda r: r[3]) if has_ts else list(recs)
    t0     = srecs[0][3]  if has_ts else 0
    t1     = srecs[-1][3] if has_ts else len(srecs) - 1
    span_t = t1 - t0 or 1
    dur_ms = _ms(span_t) if has_ts else len(srecs)

    def tms(ts): return round(_ms(ts - t0), 2) if has_ts else 0

    # ── timeline (live bytes, count, fragmentation) ──
    live, lb, mn, mx = {}, 0, None, None
    timeline, step = [], max(1, len(srecs) // n_tl)
    for idx, (k, sz, ptr, ts, fr) in enumerate(srecs):
        if k == 'A':
            live[ptr] = (sz, ts); lb += sz
            mn = ptr        if mn is None else min(mn, ptr)
            mx = ptr + sz   if mx is None else max(mx, ptr + sz)
        elif ptr in live:
            lb -= live.pop(ptr)[0]
        if idx % step == 0 or idx == len(srecs) - 1:
            if live:
                lo = min(live)
                hi = max(p + s for p, (s, _) in live.items())
                sp = hi - lo
                fp = max(0.0, (sp - lb) / sp * 100) if sp > 0 else 0.0
            else:
                sp = fp = 0.0
            timeline.append({'t': tms(ts), 'lb': lb, 'lc': len(live),
                              'fp': round(fp, 1), 'sp': sp})

    # ── alloc/free rate ──
    rate = []
    if has_ts:
        rate = [{'t': round(dur_ms * i / n_rate, 1), 'a': 0, 'f': 0}
                for i in range(n_rate)]
        for k, sz, ptr, ts, fr in srecs:
            bi = min(n_rate - 1, int((ts - t0) * n_rate / span_t))
            if k == 'A': rate[bi]['a'] += 1
            else:        rate[bi]['f'] += 1

    # ── size histogram ──
    sh = [{'l': l, 'c': 0, 'b': 0} for l in BK_LAB]
    for k, sz, ptr, ts, fr in recs:
        b = _bucket(sz); sh[b]['c'] += 1; sh[b]['b'] += sz

    # ── hold time histogram (log-spaced bins) ──
    lv2 = {}; held = []
    for k, sz, ptr, ts, fr in srecs:
        if k == 'A': lv2[ptr] = ts
        elif ptr in lv2:
            if has_ts and ts > lv2[ptr]: held.append(_ms(ts - lv2[ptr]))
            del lv2[ptr]
    hold_hist = []
    if held:
        held.sort()
        lmin = max(-2, math.floor(math.log10(min(held) + 0.001)))
        lmax = math.ceil(math.log10(max(held) + 0.001))
        nb   = max(10, min(50, (lmax - lmin) * 5))
        edges = [10 ** (lmin + i * (lmax - lmin) / nb) for i in range(nb + 1)]
        cnts  = [0] * nb; bi = 0
        for v in held:
            while bi < nb - 1 and v >= edges[bi + 1]: bi += 1
            cnts[bi] += 1
        for i in range(nb):
            e   = edges[i]
            lbl = f"{e:.3g}ms" if e < 1000 else f"{e/1000:.2g}s"
            hold_hist.append({'l': lbl, 'c': cnts[i]})

    # ── frame filtering: strip tracker-internal frames from every call stack ──
    _SKIP = ('mem_tracker', 'track_alloc', 'track_free',
             '__wrap_malloc', '__wrap_free', '__wrap_realloc',
             '__wrap_calloc', '__wrap_memalign', '__wrap_aligned_alloc',
             '__wrap_valloc', '__wrap_posix_memalign')
    def _syms(fr, limit=None):
        raw = [sym_map.get(a, f'{a:#x}') for a in fr]
        cleaned = [s for s in raw if not any(k in s for k in _SKIP)]
        return cleaned[:limit] if limit else cleaned

    # ── hotspots ──
    _TLS_MARKERS = ('mbedtls')

    def _is_tls(syms):
        return any('mbedtls' in s for s in syms)

    hmap = defaultdict(lambda: {'c': 0, 't': 0, 'fr': ()})
    for k, sz, ptr, ts, fr in recs:
        if k == 'A': h = hmap[fr]; h['c'] += 1; h['t'] += sz; h['fr'] = fr
    all_hot = sorted(hmap.values(), key=lambda h: h['t'], reverse=True)

    def _fmt_hot(h):
        return {'c': h['c'], 't': h['t'], 'a': h['t'] // max(h['c'], 1),
                'f': _syms(h['fr'])}

    hotspots     = [_fmt_hot(h) for h in all_hot if not _is_tls(_syms(h['fr']))][:100]
    hotspots_tls = [_fmt_hot(h) for h in all_hot if     _is_tls(_syms(h['fr']))][:100]

    # ── live at exit ──
    lf = {}
    for k, sz, ptr, ts, fr in srecs:
        if k == 'A': lf[ptr] = (sz, ts, fr)
        elif ptr in lf: del lf[ptr]
    live_out = []
    for ptr, (sz, ts, fr) in sorted(lf.items(), key=lambda x: -x[1][0])[:200]:
        age = round(_ms(t1 - ts), 1) if has_ts and ts else 0
        at  = round(_ms(ts - t0), 1) if has_ts and ts else 0
        live_out.append({'p': f'{ptr:#x}', 's': sz, 'ag': age, 'at': at,
                         'f': _syms(fr)})

    # ── address space events for canvas ──
    lv3 = {}; evs = []
    for k, sz, ptr, ts, fr in srecs:
        if k == 'A': lv3[ptr] = (sz, ts, fr)
        elif ptr in lv3:
            s2, ts2, fr2 = lv3.pop(ptr)
            evs.append({'p': ptr, 's': s2, 'a': tms(ts2), 'f': tms(ts),
                        'fr': _syms(fr2, 5)})
    for ptr, (sz, ts, fr) in lv3.items():
        evs.append({'p': ptr, 's': sz, 'a': tms(ts), 'f': round(dur_ms, 1), 'v': 1,
                    'fr': _syms(fr, 5)})
    evs.sort(key=lambda e: -e['s'])
    evs = evs[:max_evs]

    # ── summary stats ──
    na   = sum(1 for k, *_ in recs if k == 'A')
    nf   = sum(1 for k, *_ in recs if k == 'F')
    peak = max((p['lb'] for p in timeline), default=0)
    orp  = 0; lp = set()
    for k, sz, ptr, ts, fr in srecs:
        if k == 'A': lp.add(ptr)
        elif ptr not in lp: orp += 1
        else: lp.discard(ptr)
    avg_frag = round(sum(p['fp'] for p in timeline) / len(timeline), 1) if timeline else 0
    peak_frag = max((p['fp'] for p in timeline), default=0)
    live_bytes_final = sum(sz for _, (sz, _, _) in lf.items())

    return {
        'meta': {
            'has_ts': has_ts, 'nr': len(recs), 'na': na, 'nf': nf, 'orp': orp,
            'le': len(lf), 'peak': peak, 'dur': round(dur_ms, 1),
            'amin': mn or 0, 'amax': mx or 1,
            'avg_frag': avg_frag, 'peak_frag': round(peak_frag, 1),
            'live_bytes': live_bytes_final,
        },
        'tl': timeline, 'rate': rate, 'sh': sh, 'hh': hold_hist,
        'hot': hotspots, 'hot_tls': hotspots_tls, 'live': live_out, 'evs': evs,
    }

# ── HTML template ─────────────────────────────────────────────────────────
def render(data, trace_path, out_path):
    json_data = json.dumps(data, separators=(',', ':'))

    html = f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Memory Trace - {os.path.basename(trace_path)}</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js"></script>
<style>
*{{box-sizing:border-box;margin:0;padding:0}}
:root{{
  --bg:#0d1117;--surface:#161b22;--surface2:#1c2128;--border:#30363d;
  --text:#e6edf3;--muted:#8b949e;--blue:#58a6ff;--green:#3fb950;
  --yellow:#d29922;--red:#f85149;--orange:#f0883e;--purple:#bc8cff;
}}
body{{background:var(--bg);color:var(--text);font:13px/1.5 'Segoe UI',system-ui,sans-serif;padding:20px 24px;max-width:1400px;margin:0 auto}}
h1{{font-size:18px;font-weight:600;margin-bottom:2px}}
.subtitle{{color:var(--muted);font-size:11px;margin-bottom:20px}}
.cards{{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px;margin-bottom:16px}}
.card{{background:var(--surface);border:1px solid var(--border);border-radius:8px;padding:14px}}
.card-label{{color:var(--muted);font-size:10px;text-transform:uppercase;letter-spacing:.6px;margin-bottom:6px}}
.card-value{{font-size:20px;font-weight:700}}
.card-sub{{color:var(--muted);font-size:10px;margin-top:3px}}
.card.red .card-value{{color:var(--red)}}
.card.green .card-value{{color:var(--green)}}
.card.yellow .card-value{{color:var(--yellow)}}
.card.blue .card-value{{color:var(--blue)}}
.section{{background:var(--surface);border:1px solid var(--border);border-radius:8px;padding:16px;margin-bottom:12px}}
.section-head{{display:flex;align-items:center;justify-content:space-between;margin-bottom:12px}}
.section-title{{font-size:11px;font-weight:600;color:var(--muted);text-transform:uppercase;letter-spacing:.6px}}
.section-note{{font-size:10px;color:var(--muted);font-style:italic}}
.chart-wrap{{position:relative;height:200px}}
.chart-wrap.tall{{height:280px}}
.chart-wrap.short{{height:160px}}
.two-col{{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:12px}}
.three-col{{display:grid;grid-template-columns:2fr 1fr;gap:12px;margin-bottom:12px}}
#addrCanvas{{display:block;width:100%;height:300px;border-radius:4px;cursor:crosshair;background:#0d1117}}
.addr-legend{{display:flex;gap:16px;flex-wrap:wrap;font-size:10px;color:var(--muted);margin-top:8px;align-items:center}}
.swatch{{width:10px;height:10px;border-radius:2px;flex-shrink:0;display:inline-block;vertical-align:middle;margin-right:4px}}
.addr-controls{{display:flex;gap:12px;align-items:center;margin-bottom:8px;font-size:11px;color:var(--muted)}}
.addr-controls label{{display:flex;align-items:center;gap:5px;cursor:pointer}}
#tooltip{{position:fixed;background:var(--surface2);border:1px solid var(--border);border-radius:6px;padding:10px 14px;font-size:11px;pointer-events:none;display:none;z-index:9999;min-width:220px;line-height:1.9;box-shadow:0 8px 24px rgba(0,0,0,.5)}}
.tt-row{{display:flex;gap:6px}}
.tt-key{{color:var(--muted);min-width:72px;flex-shrink:0}}
.tt-val{{color:var(--text);font-family:monospace;word-break:break-all}}
table{{width:100%;border-collapse:collapse;font-size:11px}}
th{{text-align:left;padding:7px 10px;color:var(--muted);font-weight:500;border-bottom:1px solid var(--border);cursor:pointer;user-select:none;white-space:nowrap;font-size:10px;text-transform:uppercase;letter-spacing:.4px}}
th:hover{{color:var(--text)}}
th .sort-icon{{margin-left:3px;opacity:.35}}
th.asc .sort-icon{{opacity:1}}th.desc .sort-icon{{opacity:1}}
td{{padding:6px 10px;border-bottom:1px solid rgba(48,54,61,.5);vertical-align:top}}
td.mono{{font-family:'Cascadia Code','Consolas',monospace;font-size:10px}}
tr:last-child td{{border-bottom:none}}
tr:hover td{{background:rgba(255,255,255,.02)}}
.badge{{display:inline-block;padding:0 6px;border-radius:10px;font-size:10px;font-weight:500}}
.badge.blue{{background:rgba(88,166,255,.15);color:var(--blue)}}
.badge.red{{background:rgba(248,81,73,.15);color:var(--red)}}
.badge.green{{background:rgba(63,185,80,.15);color:var(--green)}}
.badge.yellow{{background:rgba(210,153,34,.15);color:var(--yellow)}}
.badge.purple{{background:rgba(188,140,255,.15);color:var(--purple)}}
.frames{{margin-top:4px;font-size:10px;color:var(--muted);line-height:1.7;font-family:monospace}}
details summary{{cursor:pointer;color:var(--blue);font-size:10px;list-style:none;outline:none}}
details summary::-webkit-details-marker{{display:none}}
details[open] summary{{margin-bottom:2px}}
.bar-track{{background:var(--border);border-radius:2px;height:3px;margin-top:3px;min-width:40px}}
.bar-fill{{background:var(--blue);border-radius:2px;height:3px}}
.no-data{{color:var(--muted);font-style:italic;padding:40px;text-align:center;font-size:12px}}
.tbl-scroll{{max-height:400px;overflow-y:auto}}
.frag-annotation{{font-size:10px;color:var(--red);margin-left:8px}}
@media(max-width:800px){{.two-col,.three-col{{grid-template-columns:1fr}}}}
</style>
</head>
<body>
<h1>Memory Trace Dashboard</h1>
<div class="subtitle" id="subtitle">Loading...</div>

<div class="cards" id="cards">
  <div class="card blue"><div class="card-label">Total Allocs</div><div class="card-value" id="c-na">-</div><div class="card-sub" id="c-na-sub"></div></div>
  <div class="card"><div class="card-label">Live at Exit</div><div class="card-value" id="c-le">-</div><div class="card-sub" id="c-le-sub"></div></div>
  <div class="card"><div class="card-label">Peak Usage</div><div class="card-value" id="c-peak">-</div><div class="card-sub" id="c-peak-sub"></div></div>
  <div class="card"><div class="card-label">Duration</div><div class="card-value" id="c-dur">-</div><div class="card-sub"></div></div>
  <div class="card yellow"><div class="card-label">Avg Frag</div><div class="card-value" id="c-frag">-</div><div class="card-sub" id="c-frag-sub"></div></div>
  <div class="card red"><div class="card-label">Orphan Frees</div><div class="card-value" id="c-orp">-</div><div class="card-sub">no matching alloc</div></div>
</div>

<!-- Memory Usage Timeline -->
<div class="section">
  <div class="section-head">
    <span class="section-title">Memory Usage Over Time</span>
    <span class="section-note">blue = live bytes (left axis) · green dashes = live alloc count (right)</span>
  </div>
  <div class="chart-wrap tall"><canvas id="chartUsage"></canvas></div>
</div>

<!-- Fragmentation -->
<div class="section">
  <div class="section-head">
    <span class="section-title">Fragmentation Over Time</span>
    <span class="section-note">estimated: (address span of live allocs − live bytes) / span</span>
  </div>
  <div class="three-col" style="margin-bottom:0">
    <div class="chart-wrap tall"><canvas id="chartFrag"></canvas></div>
    <div>
      <div class="section-title" style="margin-bottom:10px">Span vs Live Bytes</div>
      <div class="chart-wrap" style="height:200px"><canvas id="chartSpan"></canvas></div>
      <div style="font-size:10px;color:var(--muted);margin-top:8px;line-height:1.8">
        <b style="color:var(--text)">External fragmentation</b> occurs when free memory is split into chunks
        too small to satisfy new requests. The gap between address span and live bytes is
        a proxy - large gaps indicate holes between live allocations.
      </div>
    </div>
  </div>
</div>

<!-- Address Space Timeline -->
<div class="section">
  <div class="section-head">
    <span class="section-title">Address Space Timeline</span>
    <span class="section-note">each bar = one allocation · X = time · Y = heap address · color = size</span>
  </div>
  <div class="addr-controls">
    <span>Color by:</span>
    <label><input type="radio" name="colorMode" value="size" checked> Size</label>
    <label><input type="radio" name="colorMode" value="age"> Hold time</label>
    <label><input type="radio" name="colorMode" value="frag"> Fragmentation</label>
    <span style="margin-left:auto;font-size:10px;color:var(--muted)" id="evCount"></span>
  </div>
  <canvas id="addrCanvas"></canvas>
  <div style="display:flex;align-items:center;justify-content:space-between;margin-top:6px">
    <div id="addr-legend" class="addr-legend"></div>
    <span style="font-size:10px;color:var(--muted)">scroll to zoom · drag to pan · dbl-click to reset</span>
  </div>
</div>

<!-- Rate + Size -->
<div class="two-col">
  <div class="section">
    <div class="section-head"><span class="section-title">Alloc / Free Rate</span></div>
    <div class="chart-wrap"><canvas id="chartRate"></canvas></div>
  </div>
  <div class="section">
    <div class="section-head"><span class="section-title">Size Distribution</span></div>
    <div class="chart-wrap"><canvas id="chartSize"></canvas></div>
  </div>
</div>

<!-- Hold Time -->
<div class="section">
  <div class="section-head">
    <span class="section-title">Hold Time Distribution</span>
    <span class="section-note">how long each freed allocation was alive</span>
  </div>
  <div class="chart-wrap"><canvas id="chartHold"></canvas></div>
</div>

<!-- Hotspots -->
<div class="section">
  <div class="section-head"><span class="section-title">Top Allocation Sites</span><span class="section-note">by total bytes allocated · click row for stack · mbedtls separated below</span></div>
  <div class="tbl-scroll"><table id="tblHot">
    <thead><tr>
      <th data-col="t">Total<span class="sort-icon">⇕</span></th>
      <th data-col="c">Count<span class="sort-icon">⇕</span></th>
      <th data-col="a">Avg Size<span class="sort-icon">⇕</span></th>
      <th>Top Frame</th>
    </tr></thead>
    <tbody id="tbodyHot"></tbody>
  </table></div>
</div>

<!-- mbedTLS hotspots -->
<details id="tlsSection" style="margin-bottom:12px">
  <summary style="background:var(--surface);border:1px solid var(--border);border-radius:8px;padding:12px 16px;cursor:pointer;list-style:none;display:flex;align-items:center;gap:10px;font-size:11px;font-weight:600;color:var(--muted);text-transform:uppercase;letter-spacing:.6px;outline:none">
    <span style="color:var(--orange)">▶</span>
    <span>mbedTLS / TLS Allocation Sites</span>
    <span id="tlsBadge" class="badge yellow" style="text-transform:none;font-weight:400;letter-spacing:0"></span>
    <span style="margin-left:auto;font-style:italic;font-weight:400;letter-spacing:0">click to expand</span>
  </summary>
  <div class="section" style="margin-top:4px;border-top-left-radius:0;border-top-right-radius:0">
    <div class="tbl-scroll"><table id="tblHotTls">
      <thead><tr>
        <th data-col="t">Total<span class="sort-icon">⇕</span></th>
        <th data-col="c">Count<span class="sort-icon">⇕</span></th>
        <th data-col="a">Avg Size<span class="sort-icon">⇕</span></th>
        <th>Top Frame</th>
      </tr></thead>
      <tbody id="tbodyHotTls"></tbody>
    </table></div>
  </div>
</details>

<!-- Live at exit -->
<div class="section">
  <div class="section-head"><span class="section-title">Live Allocations at Exit</span><span class="section-note">top 200 by size · click row for stack</span></div>
  <div class="tbl-scroll"><table id="tblLive">
    <thead><tr>
      <th>Address</th>
      <th data-col="s">Size<span class="sort-icon">⇕</span></th>
      <th data-col="ag">Age<span class="sort-icon">⇕</span></th>
      <th data-col="at">Alloc At<span class="sort-icon">⇕</span></th>
      <th>Top Frame</th>
    </tr></thead>
    <tbody id="tbodyLive"></tbody>
  </table></div>
</div>

<div id="tooltip">
  <div class="tt-row"><span class="tt-key">Address</span><span class="tt-val" id="tt-ptr"></span></div>
  <div class="tt-row"><span class="tt-key">Size</span><span class="tt-val" id="tt-size"></span></div>
  <div class="tt-row"><span class="tt-key">Alloc at</span><span class="tt-val" id="tt-alloc"></span></div>
  <div class="tt-row"><span class="tt-key">Free at</span><span class="tt-val" id="tt-free"></span></div>
  <div class="tt-row"><span class="tt-key">Duration</span><span class="tt-val" id="tt-dur"></span></div>
  <div id="tt-frames" style="display:none;margin-top:7px;padding-top:7px;border-top:1px solid var(--border);font-size:10px;color:var(--muted);line-height:1.8;font-family:'Cascadia Code','Consolas',monospace;max-height:130px;overflow-y:auto;white-space:nowrap"></div>
</div>

<script>
const D = {json_data};

// ── helpers ────────────────────────────────────────────────────────────────
const fmtB = n => n>=1<<20?(n/(1<<20)).toFixed(2)+'MB':n>=1<<10?(n/(1<<10)).toFixed(1)+'KB':n+'B';
const fmtMs = ms => ms>=60000?(ms/60000).toFixed(1)+'min':ms>=1000?(ms/1000).toFixed(2)+'s':ms.toFixed(1)+'ms';
const fmtN = n => n.toLocaleString();
const clamp = (v,lo,hi) => Math.max(lo,Math.min(hi,v));

// ── summary cards ──────────────────────────────────────────────────────────
const m = D.meta;
(function() {{
  document.getElementById('subtitle').textContent =
    `{os.path.basename(trace_path)}  ·  ${{m.nr.toLocaleString()}} records  ·  ${{m.has_ts ? 'timestamps: yes' : 'no timestamps (v1)'}}`;
  document.getElementById('c-na').textContent = fmtN(m.na);
  document.getElementById('c-na-sub').textContent = fmtN(m.nf)+' frees';
  document.getElementById('c-le').textContent = fmtN(m.le);
  document.getElementById('c-le-sub').textContent = fmtB(m.live_bytes);
  document.getElementById('c-peak').textContent = fmtB(m.peak);
  document.getElementById('c-peak-sub').textContent = 'heap high-water mark';
  document.getElementById('c-dur').textContent = m.has_ts ? fmtMs(m.dur) : m.nr+' rec';
  document.getElementById('c-frag').textContent = m.avg_frag.toFixed(1)+'%';
  document.getElementById('c-frag-sub').textContent = 'peak '+m.peak_frag.toFixed(1)+'%';
  document.getElementById('c-orp').textContent = fmtN(m.orp);
}})();

// ── Chart.js defaults ──────────────────────────────────────────────────────
Chart.defaults.color='#8b949e';
Chart.defaults.borderColor='#30363d';
Chart.defaults.font={{family:"'Segoe UI',system-ui,sans-serif",size:10}};
const mkChart=(id,cfg)=>new Chart(document.getElementById(id),cfg);
const tlX = D.tl.map(p=>p.t);

// ── Usage ──────────────────────────────────────────────────────────────────
mkChart('chartUsage',{{
  type:'line',
  data:{{labels:tlX,datasets:[
    {{label:'Live Bytes',data:D.tl.map(p=>p.lb),borderColor:'#58a6ff',
      backgroundColor:'rgba(88,166,255,.08)',fill:true,tension:.3,pointRadius:0,yAxisID:'yB'}},
    {{label:'Live Count',data:D.tl.map(p=>p.lc),borderColor:'#3fb950',
      backgroundColor:'transparent',tension:.3,pointRadius:0,yAxisID:'yC',borderDash:[4,2]}},
  ]}},
  options:{{responsive:true,maintainAspectRatio:false,
    interaction:{{mode:'index',intersect:false}},
    plugins:{{legend:{{display:true,labels:{{boxWidth:10}}}},tooltip:{{callbacks:{{
      label:ctx=>ctx.datasetIndex===0?fmtB(ctx.parsed.y):fmtN(ctx.parsed.y)+' allocs',
      title:ctx=>m.has_ts?fmtMs(+ctx[0].label):'Record '+ctx[0].label,
    }}}}}},
    scales:{{
      x:{{ticks:{{maxTicksLimit:8,callback:v=>m.has_ts?fmtMs(+v):v}}}},
      yB:{{position:'left',ticks:{{callback:v=>fmtB(v)}},title:{{display:true,text:'Bytes'}}}},
      yC:{{position:'right',grid:{{drawOnChartArea:false}},title:{{display:true,text:'Count'}}}},
    }},
  }},
}});

// ── Fragmentation % ────────────────────────────────────────────────────────
mkChart('chartFrag',{{
  type:'line',
  data:{{labels:tlX,datasets:[
    {{label:'Frag %',data:D.tl.map(p=>p.fp),borderColor:'#f85149',
      backgroundColor:'rgba(248,81,73,.08)',fill:true,tension:.3,pointRadius:0}},
  ]}},
  options:{{responsive:true,maintainAspectRatio:false,
    interaction:{{mode:'index',intersect:false}},
    plugins:{{legend:{{display:false}},tooltip:{{callbacks:{{
      label:ctx=>ctx.parsed.y.toFixed(1)+'%',
      title:ctx=>m.has_ts?fmtMs(+ctx[0].label):'Record '+ctx[0].label,
    }}}}}},
    scales:{{
      x:{{ticks:{{maxTicksLimit:8,callback:v=>m.has_ts?fmtMs(+v):v}}}},
      y:{{min:0,max:100,ticks:{{callback:v=>v+'%'}},title:{{display:true,text:'Frag %'}}}},
    }},
  }},
}});

// ── Span vs Live bytes ────────────────────────────────────────────────────
mkChart('chartSpan',{{
  type:'line',
  data:{{labels:tlX,datasets:[
    {{label:'Address Span',data:D.tl.map(p=>p.sp),borderColor:'#f85149',
      backgroundColor:'rgba(248,81,73,.12)',fill:true,tension:.3,pointRadius:0}},
    {{label:'Live Bytes',data:D.tl.map(p=>p.lb),borderColor:'#58a6ff',
      backgroundColor:'rgba(88,166,255,.15)',fill:true,tension:.3,pointRadius:0}},
  ]}},
  options:{{responsive:true,maintainAspectRatio:false,
    interaction:{{mode:'index',intersect:false}},
    plugins:{{legend:{{display:true,labels:{{boxWidth:8,font:{{size:9}}}}}},tooltip:{{callbacks:{{
      label:ctx=>ctx.dataset.label+': '+fmtB(ctx.parsed.y),
      title:ctx=>m.has_ts?fmtMs(+ctx[0].label):'Record '+ctx[0].label,
    }}}}}},
    scales:{{
      x:{{ticks:{{maxTicksLimit:5,callback:v=>m.has_ts?fmtMs(+v):v}}}},
      y:{{ticks:{{callback:v=>fmtB(v)}}}},
    }},
  }},
}});

// ── Rate ──────────────────────────────────────────────────────────────────
if(D.rate.length>0) mkChart('chartRate',{{
  type:'bar',
  data:{{labels:D.rate.map(r=>r.t),datasets:[
    {{label:'Allocs',data:D.rate.map(r=>r.a),backgroundColor:'rgba(88,166,255,.65)',borderWidth:0}},
    {{label:'Frees', data:D.rate.map(r=>-r.f),backgroundColor:'rgba(248,81,73,.55)',borderWidth:0}},
  ]}},
  options:{{responsive:true,maintainAspectRatio:false,
    plugins:{{legend:{{display:true,labels:{{boxWidth:8}}}},tooltip:{{callbacks:{{
      label:ctx=>ctx.dataset.label+': '+fmtN(Math.abs(ctx.parsed.y)),
      title:ctx=>m.has_ts?fmtMs(+ctx[0].label):ctx[0].label,
    }}}}}},
    scales:{{
      x:{{stacked:true,ticks:{{maxTicksLimit:8,callback:v=>m.has_ts?fmtMs(+v):v}}}},
      y:{{stacked:true,ticks:{{callback:v=>fmtN(Math.abs(v))}},title:{{display:true,text:'ops / bucket'}}}},
    }},
  }},
}});

// ── Size distribution ─────────────────────────────────────────────────────
const SZ_CLR=['#1f6feb','#388bfd','#58a6ff','#4ac26b','#d29922','#f0883e','#f85149','#bc8cff','#ff7b72'];
mkChart('chartSize',{{
  type:'bar',
  data:{{labels:D.sh.map(b=>b.l),datasets:[
    {{label:'Bytes',data:D.sh.map(b=>b.b),backgroundColor:SZ_CLR,borderWidth:0}},
  ]}},
  options:{{indexAxis:'y',responsive:true,maintainAspectRatio:false,
    plugins:{{legend:{{display:false}},tooltip:{{callbacks:{{
      label:ctx=>fmtB(ctx.parsed.x)+' ('+fmtN(D.sh[ctx.dataIndex].c)+' allocs)',
    }}}}}},
    scales:{{
      x:{{ticks:{{callback:v=>fmtB(v)}}}},
      y:{{ticks:{{font:{{size:9}}}}}},
    }},
  }},
}});

// ── Hold time histogram ───────────────────────────────────────────────────
if(D.hh.length>0) mkChart('chartHold',{{
  type:'bar',
  data:{{labels:D.hh.map(b=>b.l),datasets:[
    {{label:'Count',data:D.hh.map(b=>b.c),backgroundColor:'rgba(88,166,255,.55)',borderWidth:0}},
  ]}},
  options:{{responsive:true,maintainAspectRatio:false,
    plugins:{{legend:{{display:false}}}},
    scales:{{
      x:{{ticks:{{maxTicksLimit:20,maxRotation:45,font:{{size:9}}}}}},
      y:{{title:{{display:true,text:'freed allocs'}}}},
    }},
  }},
}});

// ── Address Space Canvas ──────────────────────────────────────────────────
(function() {{
  const canvas = document.getElementById('addrCanvas');
  const ctx2   = canvas.getContext('2d');
  const evs    = D.evs;
  const meta   = D.meta;

  document.getElementById('evCount').textContent =
    evs.length.toLocaleString()+' events (top by size)';

  if(!evs.length) {{
    ctx2.fillStyle='#8b949e'; ctx2.font='14px sans-serif';
    ctx2.fillText('No events', 20, 40); return;
  }}

  const durMs    = meta.dur || 1;
  const addrMin  = Math.min(...evs.map(e=>e.p));
  const addrMax  = Math.max(...evs.map(e=>e.p+e.s));
  const addrSpan = addrMax - addrMin || 1;
  const maxHold  = Math.max(...evs.map(e=>e.f-e.a)) || 1;

  // ── zoom / pan state (Y = address axis) ──────────────────────────────
  let vAddrLo = addrMin, vAddrHi = addrMax;
  let isDragging = false, dragStartY = 0, dragVAddrLo0 = 0, dragVAddrHi0 = 0, hasMoved = false;

  const addrToY = (addr, H) => (1 - (addr - vAddrLo) / (vAddrHi - vAddrLo)) * H;
  const yToAddr = (y, H)    => vAddrLo + (1 - y / H) * (vAddrHi - vAddrLo);

  function clampView() {{
    const span = vAddrHi - vAddrLo;
    if(vAddrLo < addrMin) {{ vAddrLo = addrMin; vAddrHi = addrMin + span; }}
    if(vAddrHi > addrMax) {{ vAddrHi = addrMax; vAddrLo = addrMax - span; }}
  }}

  // ── color functions ───────────────────────────────────────────────────
  let colorMode = 'size';

  function sizeColor(sz) {{
    if(sz<=64)     return '#1f6feb';
    if(sz<=1024)   return '#388bfd';
    if(sz<=16384)  return '#3fb950';
    if(sz<=262144) return '#d29922';
    return '#f85149';
  }}
  function ageColor(hold) {{
    const t = clamp(hold / maxHold, 0, 1);
    if(t<.5) {{ const u=t*2; return `rgb(${{Math.round(63+u*192)}},${{Math.round(185-u*90)}},80)`; }}
    const u=(t-.5)*2; return `rgb(255,${{Math.round(95-u*95)}},${{Math.round(80-u*80)}})`;
  }}
  function fragColor(e) {{
    const frac = (e.f - e.a) / durMs;
    if(frac>.9) return '#f85149';
    if(frac>.5) return '#d29922';
    if(frac>.1) return '#3fb950';
    return '#388bfd';
  }}

  // ── legend ────────────────────────────────────────────────────────────
  const legendEl = document.getElementById('addr-legend');
  const liveSw = `<span><span class="swatch" style="background:#fff;opacity:.7;outline:1px solid rgba(255,255,255,.5)"></span>still live</span>`;
  const LEGENDS = {{
    size: `<span><span class="swatch" style="background:#1f6feb"></span>≤64B</span>
           <span><span class="swatch" style="background:#388bfd"></span>≤1KB</span>
           <span><span class="swatch" style="background:#3fb950"></span>≤16KB</span>
           <span><span class="swatch" style="background:#d29922"></span>≤256KB</span>
           <span><span class="swatch" style="background:#f85149"></span>&gt;256KB</span>${{liveSw}}`,
    age:  `<span><span class="swatch" style="background:#3fb950"></span>short-lived</span>
           <span><span class="swatch" style="background:rgb(159,140,80)"></span>medium</span>
           <span><span class="swatch" style="background:#f85149"></span>long-held (max ${{fmtMs(maxHold)}})</span>${{liveSw}}`,
    frag: `<span><span class="swatch" style="background:#388bfd"></span>&lt;10% of trace</span>
           <span><span class="swatch" style="background:#3fb950"></span>10–50%</span>
           <span><span class="swatch" style="background:#d29922"></span>50–90%</span>
           <span><span class="swatch" style="background:#f85149"></span>&gt;90% - fragmentation risk</span>${{liveSw}}`,
  }};
  function updateLegend() {{ legendEl.innerHTML = LEGENDS[colorMode]; }}
  updateLegend();

  // ── draw ──────────────────────────────────────────────────────────────
  const sorted = [...evs].sort((a,b)=>b.s-a.s); // large first so small are on top

  function draw() {{
    const W = canvas.clientWidth;
    const H = canvas.clientHeight;
    if(canvas.width!==W || canvas.height!==H) {{ canvas.width=W; canvas.height=H; }}
    ctx2.clearRect(0,0,W,H);
    ctx2.fillStyle='#0d1117'; ctx2.fillRect(0,0,W,H);

    const AXIS_H = 20; // bottom axis height in px
    const PLOT_H = H - AXIS_H;

    for(const e of sorted) {{
      const x0 = e.a / durMs * W;
      const x1 = Math.max(x0 + 1, e.f / durMs * W);
      const cx0 = Math.max(0, x0), cx1 = Math.min(W, x1);
      if(cx1 <= 0 || cx0 >= W) continue;
      const ey_top = Math.max(0, addrToY(e.p + e.s, PLOT_H));
      const ey_bot = Math.min(PLOT_H, addrToY(e.p, PLOT_H));
      if(ey_top >= PLOT_H || ey_bot <= 0) continue;
      const pw = Math.max(1, cx1 - cx0);
      const ph = Math.max(1, ey_bot - ey_top);

      const col = colorMode==='size' ? sizeColor(e.s)
                : colorMode==='age'  ? ageColor(e.f-e.a)
                : fragColor(e);

      ctx2.globalAlpha = e.v ? 1.0 : 0.72;
      ctx2.fillStyle   = col;
      ctx2.fillRect(cx0, ey_top, pw, ph);
      if(e.v) {{
        ctx2.globalAlpha=0.85; ctx2.strokeStyle='rgba(255,255,255,.55)';
        ctx2.lineWidth=1; ctx2.strokeRect(cx0, ey_top, pw, ph);
      }}
    }}
    ctx2.globalAlpha=1;

    // X axis baseline
    ctx2.fillStyle='#30363d'; ctx2.fillRect(0, PLOT_H, W, 1);

    // Y-axis address labels (reflect current visible address range)
    ctx2.fillStyle='#8b949e'; ctx2.font='9px monospace';
    for(let i=0; i<=4; i++) {{
      const addr = vAddrLo + (vAddrHi - vAddrLo) * i / 4;
      const y    = addrToY(addr, PLOT_H) + 2;
      ctx2.fillText('0x'+addr.toString(16).toUpperCase(), 2, y);
    }}

    // X-axis time labels (full trace duration, no X zoom)
    const nTicks = Math.min(8, Math.max(3, Math.floor(W/90)));
    for(let i=0; i<=nTicks; i++) {{
      const t   = durMs * i / nTicks;
      const x   = i / nTicks * W;
      const lbl = meta.has_ts ? fmtMs(t) : Math.round(t)+'r';
      ctx2.fillStyle='#8b949e';
      ctx2.fillText(lbl, Math.min(x+2, W-50), H-4);
    }}

    // Y-zoom indicator (shown only when zoomed in)
    const vSpan = vAddrHi - vAddrLo;
    if(vSpan < addrSpan * 0.99) {{
      const lbl = `0x${{vAddrLo.toString(16).toUpperCase()}}–0x${{vAddrHi.toString(16).toUpperCase()}}  ·  ×${{(addrSpan/vSpan).toFixed(1)}} zoom  (dbl-click resets)`;
      ctx2.font='9px monospace'; ctx2.fillStyle='rgba(88,166,255,.9)';
      ctx2.fillText(lbl, W - ctx2.measureText(lbl).width - 6, 12);
    }}
  }}

  draw();
  new ResizeObserver(draw).observe(canvas);

  // ── zoom: mouse wheel (Y = address axis) ─────────────────────────────
  canvas.addEventListener('wheel', ev => {{
    ev.preventDefault();
    const rect   = canvas.getBoundingClientRect();
    const my     = ev.clientY - rect.top;
    const H      = canvas.clientHeight;
    const AXIS_H = 20, PLOT_H = H - AXIS_H;
    const aCur   = yToAddr(Math.min(my, PLOT_H), PLOT_H);
    const factor = ev.deltaY > 0 ? 1.28 : 1/1.28;
    const vSpan  = vAddrHi - vAddrLo;
    const newSpan = clamp(vSpan * factor, 1, addrSpan);
    const frac   = Math.min(my, PLOT_H) / PLOT_H;
    vAddrLo = aCur - (1 - frac) * newSpan;
    vAddrHi = aCur + frac * newSpan;
    clampView();
    draw();
  }}, {{passive:false}});

  // ── pan: click+drag (content follows cursor) ──────────────────────────
  canvas.addEventListener('mousedown', ev => {{
    if(ev.button!==0) return;
    isDragging=true; hasMoved=false;
    dragStartY=ev.clientY; dragVAddrLo0=vAddrLo; dragVAddrHi0=vAddrHi;
    canvas.style.cursor='grabbing';
  }});
  window.addEventListener('mousemove', ev => {{
    if(!isDragging) return;
    const dy = ev.clientY - dragStartY;
    if(Math.abs(dy)>3) hasMoved=true;
    const H = canvas.clientHeight, AXIS_H = 20, PLOT_H = H - AXIS_H;
    const addrPerPx = (dragVAddrHi0 - dragVAddrLo0) / PLOT_H;
    // drag down (dy > 0): content follows cursor → view shifts to higher addresses
    vAddrLo = dragVAddrLo0 + dy * addrPerPx;
    vAddrHi = dragVAddrHi0 + dy * addrPerPx;
    clampView();
    draw();
  }});
  window.addEventListener('mouseup', () => {{
    isDragging=false; canvas.style.cursor='crosshair';
  }});

  // ── double-click: reset zoom ──────────────────────────────────────────
  canvas.addEventListener('dblclick', () => {{ vAddrLo=addrMin; vAddrHi=addrMax; draw(); }});

  // ── click: copy tooltip contents to clipboard ─────────────────────────
  let copyFlashUntil = 0;
  canvas.addEventListener('click', ev => {{
    if(hasMoved) return; // was a drag
    const rect = canvas.getBoundingClientRect();
    const mx   = ev.clientX - rect.left;
    const my   = ev.clientY - rect.top;
    const W    = canvas.width;
    const H    = canvas.height;
    const AXIS_H = 20, PLOT_H = H - AXIS_H;
    let hit=null, minSz=Infinity;
    for(const e of evs) {{
      const x0 = e.a / durMs * W, x1 = Math.max(e.a / durMs * W + 1, e.f / durMs * W);
      if(x1<0||x0>W) continue;
      const ey_top = addrToY(e.p + e.s, PLOT_H);
      const ey_bot = addrToY(e.p, PLOT_H);
      if(mx>=Math.max(0,x0)&&mx<=Math.min(W,x1)&&my>=ey_top&&my<=Math.max(ey_top+1,ey_bot))
        if(e.s<minSz) {{ minSz=e.s; hit=e; }}
    }}
    if(!hit) return;
    const lines = [
      'ptr:   0x'+hit.p.toString(16).toUpperCase(),
      'size:  '+fmtB(hit.s),
      'alloc: '+(meta.has_ts?fmtMs(hit.a):'record '+hit.a),
      'free:  '+(hit.v?'still live':meta.has_ts?fmtMs(hit.f):'record '+hit.f),
      'held:  '+fmtMs(hit.f-hit.a),
    ];
    if(hit.fr&&hit.fr.length) hit.fr.forEach((f,i)=>lines.push('#'+i+' '+f));
    navigator.clipboard.writeText(lines.join('\\n')).catch(()=>{{}});
    copyFlashUntil = performance.now() + 1200;
    // flash "Copied!" overlay on canvas
    (function flashCopy() {{
      const now = performance.now();
      draw();
      if(now < copyFlashUntil) {{
        const msg = 'Copied!';
        ctx2.font='bold 13px monospace';
        const tw = ctx2.measureText(msg).width;
        const fx = Math.min(mx+10, W-tw-6), fy = Math.max(my-10, 16);
        ctx2.fillStyle='rgba(30,40,50,.85)'; ctx2.fillRect(fx-4,fy-13,tw+8,18);
        ctx2.fillStyle='#3fb950'; ctx2.fillText(msg, fx, fy);
        requestAnimationFrame(flashCopy);
      }}
    }})();
  }});

  // ── color mode → redraw + legend ──────────────────────────────────────
  document.querySelectorAll('input[name=colorMode]').forEach(r =>
    r.addEventListener('change', () => {{ colorMode=r.value; updateLegend(); draw(); }}));

  // ── tooltip ───────────────────────────────────────────────────────────
  const tip = document.getElementById('tooltip');

  canvas.addEventListener('mousemove', ev => {{
    if(isDragging && hasMoved) {{ tip.style.display='none'; return; }}
    const rect = canvas.getBoundingClientRect();
    const mx   = ev.clientX - rect.left;
    const my   = ev.clientY - rect.top;
    const W    = canvas.width;
    const H    = canvas.height;
    const AXIS_H = 20, PLOT_H = H - AXIS_H;

    let hit=null, minSz=Infinity;
    for(const e of evs) {{
      const x0 = e.a / durMs * W, x1 = Math.max(e.a / durMs * W + 1, e.f / durMs * W);
      if(x1<0||x0>W) continue;
      const ey_top = addrToY(e.p + e.s, PLOT_H);
      const ey_bot = addrToY(e.p, PLOT_H);
      if(mx>=Math.max(0,x0)&&mx<=Math.min(W,x1)&&my>=ey_top&&my<=Math.max(ey_top+1,ey_bot))
        if(e.s<minSz) {{ minSz=e.s; hit=e; }}
    }}
    if(hit) {{
      document.getElementById('tt-ptr').textContent   = '0x'+hit.p.toString(16).toUpperCase();
      document.getElementById('tt-size').textContent  = fmtB(hit.s);
      document.getElementById('tt-alloc').textContent = meta.has_ts?fmtMs(hit.a):'record '+hit.a;
      document.getElementById('tt-free').textContent  = hit.v?'still live':meta.has_ts?fmtMs(hit.f):'record '+hit.f;
      document.getElementById('tt-dur').textContent   = fmtMs(hit.f-hit.a);
      const frDiv = document.getElementById('tt-frames');
      if(hit.fr&&hit.fr.length) {{
        frDiv.style.display='block';
        frDiv.innerHTML=hit.fr.map((f,i)=>`<span style="color:var(--blue)">#${{i}}</span> ${{f.replace(/</g,'&lt;')}}`).join('<br>');
      }} else frDiv.style.display='none';
      tip.style.display='block';
      tip.style.left=(ev.clientX+14)+'px';
      tip.style.top =(ev.clientY-10)+'px';
    }} else tip.style.display='none';
  }});
  canvas.addEventListener('mouseleave', () => {{ tip.style.display='none'; }});
}})();

// ── Tables ────────────────────────────────────────────────────────────────
function makeSortable(tbodyId, data, renderRow, defaultCol, defaultDir, initialRows) {{
  const tbody   = document.getElementById(tbodyId);
  const table   = tbody.closest('table');
  const section = table.closest('.section');
  let col=defaultCol, dir=defaultDir||'desc';
  let showAll = false;
  const limit = initialRows || data.length;

  // "Show more" button injected below the table
  let moreBtn = null;
  if(initialRows && data.length > initialRows) {{
    moreBtn = document.createElement('button');
    moreBtn.style.cssText =
      'margin:10px 0 2px;background:none;border:1px solid var(--border);color:var(--blue);' +
      'border-radius:4px;padding:4px 14px;font-size:11px;cursor:pointer;width:100%';
    moreBtn.addEventListener('click', () => {{
      showAll = !showAll;
      refresh();
    }});
    section.appendChild(moreBtn);
  }}

  function refresh() {{
    const sorted=[...data].sort((a,b)=>{{
      const av=a[col]??0, bv=b[col]??0;
      if(typeof av==='string') return dir==='asc'?av.localeCompare(bv):bv.localeCompare(av);
      return dir==='asc'?av-bv:bv-av;
    }});
    const visible = showAll ? sorted : sorted.slice(0, limit);
    tbody.innerHTML='';
    visible.forEach((row,i)=>tbody.appendChild(renderRow(row,i)));
    table.querySelectorAll('th').forEach(th=>{{
      th.classList.remove('asc','desc');
      if(th.dataset.col===col) th.classList.add(dir);
    }});
    if(moreBtn) {{
      const hidden = sorted.length - visible.length;
      moreBtn.textContent = showAll
        ? `Show fewer (collapse to ${{limit}})`
        : `Show ${{hidden}} more (${{sorted.length}} total)`;
    }}
  }}

  table.querySelectorAll('th[data-col]').forEach(th=>{{
    th.addEventListener('click',()=>{{
      if(col===th.dataset.col) dir=dir==='asc'?'desc':'asc';
      else {{ col=th.dataset.col; dir='desc'; }}
      refresh();
    }});
  }});
  refresh();
}}

// Hotspots
makeSortable('tbodyHot', D.hot, (h,i) => {{
  const tr = document.createElement('tr');
  const pct = D.meta.peak>0?Math.round(h.t/D.meta.peak*100):0;
  tr.innerHTML = `
    <td><b>${{fmtB(h.t)}}</b>
      <div class="bar-track"><div class="bar-fill" style="width:${{pct}}%"></div></div>
    </td>
    <td class="mono">${{fmtN(h.c)}}</td>
    <td class="mono">${{fmtB(h.a)}}</td>
    <td>
      ${{h.f.length?`<details><summary>${{h.f[0]}}</summary><div class="frames">${{h.f.map((f,i)=>'#'+i+' '+f).join('<br>')}}</div></details>`:'<span style="color:var(--muted)">(no frames)</span>'}}
    </td>`;
  return tr;
}}, 't', 'desc', 30);

// mbedTLS hotspots
(function() {{
  const tls = D.hot_tls;
  const totalBytes = tls.reduce((s,h)=>s+h.t, 0);
  const badge = document.getElementById('tlsBadge');
  if(tls.length) {{
    badge.textContent = `${{tls.length}} sites · ${{fmtB(totalBytes)}} total`;
    // rotate arrow on open
    const det = document.getElementById('tlsSection');
    det.addEventListener('toggle', () => {{
      det.querySelector('span').textContent = det.open ? '▼' : '▶';
      det.querySelector('summary > span:last-child').textContent = det.open ? '' : 'click to expand';
    }});
    makeSortable('tbodyHotTls', tls, (h,i) => {{
      const tr  = document.createElement('tr');
      const pct = D.meta.peak>0?Math.round(h.t/D.meta.peak*100):0;
      tr.innerHTML = `
        <td><b>${{fmtB(h.t)}}</b>
          <div class="bar-track"><div class="bar-fill" style="width:${{pct}}%;background:var(--orange)"></div></div>
        </td>
        <td class="mono">${{fmtN(h.c)}}</td>
        <td class="mono">${{fmtB(h.a)}}</td>
        <td>
          ${{h.f.length?`<details><summary>${{h.f[0]}}</summary><div class="frames">${{h.f.map((f,i)=>'#'+i+' '+f).join('<br>')}}</div></details>`:'<span style="color:var(--muted)">(no frames)</span>'}}
        </td>`;
      return tr;
    }}, 't', 'desc', 30);
  }} else {{
    badge.textContent = 'none detected';
    document.getElementById('tlsSection').style.display = 'none';
  }}
}})();

// Live at exit
makeSortable('tbodyLive', D.live, (r,i) => {{
  const tr = document.createElement('tr');
  const ageClass = r.ag>30000?'red':r.ag>5000?'yellow':'green';
  tr.innerHTML = `
    <td class="mono" style="font-size:10px">${{r.p}}</td>
    <td class="mono"><b>${{fmtB(r.s)}}</b></td>
    <td><span class="badge ${{ageClass}}">${{D.meta.has_ts?fmtMs(r.ag):'?'}}</span></td>
    <td class="mono">${{D.meta.has_ts?fmtMs(r.at):'?'}}</td>
    <td>
      ${{r.f.length?`<details><summary>${{r.f[0]}}</summary><div class="frames">${{r.f.map((f,j)=>'#'+j+' '+f).join('<br>')}}</div></details>`:'<span style="color:var(--muted)">(no frames)</span>'}}
    </td>`;
  return tr;
}}, 's', 'desc', 30);
</script>
</body>
</html>"""

    # Fix the subtitle JS (it's a bit awkward with the f-string nesting)
    with open(out_path, 'w', encoding='utf-8') as f:
        f.write(html)
    print(f"Dashboard written to {out_path}", file=sys.stderr)

# ── main ──────────────────────────────────────────────────────────────────
def main():
    ap = argparse.ArgumentParser(description='Generate HTML memory trace dashboard')
    ap.add_argument('trace',              help='Path to kdec_memtrace.bin')
    ap.add_argument('--elf',   default=None, help='ELF for symbolization')
    ap.add_argument('--gdb',   default='/opt/devkitpro/devkitA64/bin/aarch64-none-elf-gdb')
    ap.add_argument('--output',default='memtrace_dashboard.html', help='Output HTML path')
    ap.add_argument('--max-events', type=int, default=8000,
                    help='Max alloc events in address canvas (default: 8000)')
    args = ap.parse_args()

    recs, anchor, ver = parse(args.trace)
    if not recs:
        print("No records parsed.", file=sys.stderr); sys.exit(1)

    sym_map = {}
    if args.elf:
        base    = detect_base(anchor, args.elf, args.gdb)
        all_frames = set()
        for _, _, _, _, fr in recs:
            all_frames.update(fr)
        sym_map = symbolize(all_frames, args.elf, args.gdb, base)

    data = build(recs, sym_map, max_evs=args.max_events)
    render(data, args.trace, args.output)
    print(f"Open in browser:  file://{os.path.abspath(args.output)}", file=sys.stderr)

if __name__ == '__main__':
    main()
