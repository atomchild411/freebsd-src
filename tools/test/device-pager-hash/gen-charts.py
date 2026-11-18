#!/usr/bin/env python3
"""gen-charts.py DEVOBJS_TXT OUTDIR: the four README charts as static SVG."""
import math, sys
from xml.sax.saxutils import escape

BEFORE, AFTER, NEUTRAL = "#2a78d6", "#eb6834", "#b9b6af"
INK, MUTED, FAINT, GRID, BG, RULE = "#1d1d1b", "#5f5e5a", "#8a8883", "#ecebe7", "#ffffff", "#dddad3"
FONT = "font-family='-apple-system,Segoe UI,Helvetica,Arial,sans-serif'"
MONO = "font-family='SFMono-Regular,Menlo,Consolas,monospace'"

class SVG:
    def __init__(s, w, h, title, sub=None):
        s.w, s.h, s.o = w, h, []
        s.o.append(f"<rect x='0.5' y='0.5' width='{w-1}' height='{h-1}' rx='10' fill='{BG}' stroke='{RULE}'/>")
        s.t(24, 34, title, size=17, weight=600)
        if sub: s.t(24, 56, sub, size=12.5, fill=MUTED)
    def t(s, x, y, text, size=12, fill=INK, anchor="start", weight=400, mono=False):
        s.o.append(f"<text x='{x:.1f}' y='{y:.1f}' font-size='{size}' fill='{fill}' text-anchor='{anchor}' font-weight='{weight}' {MONO if mono else FONT}>{escape(text)}</text>")
    def line(s, x1, y1, x2, y2, stroke=GRID, w=1, cap="butt"):
        s.o.append(f"<line x1='{x1:.1f}' y1='{y1:.1f}' x2='{x2:.1f}' y2='{y2:.1f}' stroke='{stroke}' stroke-width='{w}' stroke-linecap='{cap}'/>")
    def rect(s, x, y, w, h, fill, rx=3):
        s.o.append(f"<rect x='{x:.1f}' y='{y:.1f}' width='{max(w,1.5):.1f}' height='{h:.1f}' rx='{rx}' fill='{fill}'/>")
    def dot(s, x, y, fill, r=6):
        s.o.append(f"<circle cx='{x:.1f}' cy='{y:.1f}' r='{r}' fill='{fill}'/>")
    def legend(s, x, y, items):
        for col, lab in items:
            s.dot(x + 5, y - 4, col, 5); s.t(x + 15, y, lab, size=12.5, fill=MUTED)
            x += 30 + 7.2 * len(lab)
    def save(s, path):
        open(path, "w").write(f"<svg xmlns='http://www.w3.org/2000/svg' width='{s.w}' height='{s.h}' viewBox='0 0 {s.w} {s.h}'>" + "".join(s.o) + "</svg>\n")

def fus(v): return f"{v:.0f} µs" if v >= 10 else f"{v:.1f} µs" if v >= 1 else f"{v:.2f} µs"

def cost(out):
    rows = [("vm_pager_object_lookup", "walks dev_pager_object_list", 151.13, 0.99),
            ("linux_cdev_pager_populate", "one GPU-buffer page fault", 80.74, 1.14),
            ("cdev_pager_allocate", "one device mapping", 307.51, 34.96),
            ("mmap(2)", "system call, GL client", 181.36, 34.57),
            ("munmap(2)", "system call, GL client", 36.65, 4.41)]
    W, L, R, top, rh = 820, 230, 40, 118, 46
    s = SVG(W, top + len(rows) * rh + 16, "Per-call cost during a Doom 3 timedemo",
            "Average time per call, DTrace fbt entry/return (pager.d). Log scale.")
    s.legend(24, 84, [(BEFORE, "Stock kernel"), (AFTER, "With the hash patch")])
    lo, hi = math.log10(0.5), math.log10(500)
    x = lambda v: L + (math.log10(v) - lo) / (hi - lo) * (W - L - R)
    for v in (1, 10, 100):
        s.line(x(v), top - 8, x(v), top + len(rows) * rh); s.t(x(v), top - 12, f"{v} µs", size=11, fill=FAINT, anchor="middle")
    for i, (n, d, b, a) in enumerate(rows):
        y = top + i * rh + rh / 2
        s.t(24, y - 2, n, size=12, mono=True); s.t(24, y + 14, d, size=11, fill=FAINT)
        s.line(x(a), y, x(b), y, stroke=RULE, w=3, cap="round")
        s.dot(x(b), y, BEFORE); s.dot(x(a), y, AFTER)
        s.t(x(b) + 10, y + 4, fus(b), fill=MUTED); s.t(x(a) - 10, y + 4, fus(a), fill=MUTED, anchor="end")
    s.save(f"{out}/per-call-cost.svg")

def devobjs(path, out):
    pts = []
    for line in open(path):
        p = line.split()
        if len(p) == 2 and p[0].isdigit(): pts.append((int(p[0]), int(p[1])))
    W, H, L, R, T, B = 820, 300, 70, 30, 76, 36
    s = SVG(W, H, "Device-pager objects during the timedemo",
            "Created and not yet terminated, from the start of tracing (devobjs.d, once a second).")
    xm = pts[-1][0]; peak = max(pts, key=lambda p: p[1]); ym = math.ceil(peak[1] / 4000) * 4000
    x = lambda v: L + v / xm * (W - L - R); y = lambda v: T + (1 - v / ym) * (H - T - B)
    for v in range(0, ym + 1, 4000):
        s.line(L, y(v), W - R, y(v)); s.t(L - 8, y(v) + 4, f"{v:,}", size=11, fill=FAINT, anchor="end")
    for v in range(0, xm + 1, 30):
        s.t(x(v), H - 14, f"{v} s", size=11, fill=FAINT, anchor="middle")
    d = "M" + "L".join(f"{x(a):.1f},{y(b):.1f}" for a, b in pts)
    s.o.append(f"<path d='{d}L{x(xm):.1f},{y(0):.1f}L{x(0):.1f},{y(0):.1f}Z' fill='{BEFORE}' opacity='0.12'/>")
    s.o.append(f"<path d='{d}' fill='none' stroke='{BEFORE}' stroke-width='2.5' stroke-linejoin='round'/>")
    s.dot(x(peak[0]), y(peak[1]), BEFORE, 4.5)
    s.t(x(peak[0]) - 8, y(peak[1]) - 10, f"{peak[1]:,} objects", weight=600, anchor="end")
    s.save(f"{out}/device-objects.svg")
    return peak

def profile(out):
    rows = [("spinlock_exit", 21.37, 0), ("do_el0_sync", 14.9, 0), ("copycommon", 13.27, 0),
            ("vm_pager_object_lookup", 5.54, 1), ("linux_cdev_pager_populate", 3.26, 1),
            ("linux_file_mmap", 2.9, 1), ("kern_select", 2.59, 0), ("taskqueue_enqueue_locked", 1.8, 0),
            ("lock_delay", 1.37, 0), ("smp_rendezvous_cpus", 1.24, 0)]
    W, L, R, top, rh = 820, 230, 70, 100, 26
    s = SVG(W, top + len(rows) * rh + 40, "Where busy kernel time goes, without the patch",
            "Leaf functions, share of non-idle kernel profile samples during the timedemo (td.d, 997 Hz).")
    s.legend(24, 82, [(AFTER, "List walk the patch removes"), (NEUTRAL, "Other kernel work")])
    for i, (n, v, h) in enumerate(rows):
        y = top + i * rh
        s.t(L - 10, y + rh / 2 + 4, n, mono=True, anchor="end", weight=600 if h else 400)
        s.rect(L, y + 4, v / 22 * (W - L - R), rh - 8, AFTER if h else NEUTRAL)
        s.t(L + v / 22 * (W - L - R) + 6, y + rh / 2 + 4, f"{v:.1f}%", fill=INK if h else MUTED, weight=600 if h else 400)
    s.t(24, top + len(rows) * rh + 24, "The three walks: 11.7% together. With the patch none is in the top 25 (the 25th is 0.45%).", size=12, fill=MUTED)
    s.save(f"{out}/kernel-profile.svg")

def throughput(out):
    rows = [("DRM dumb buffers", "6,000 held", 334644, 3483699), ("/dev/dsp mappings", "2,000 held", 1945042, 12561466)]
    W, L, R, top, bh, gap = 820, 190, 130, 100, 18, 5
    rh = 2 * bh + gap + 26
    s = SVG(W, top + len(rows) * rh + 10, "Device mappings per minute (mapload)",
            "Stock kernel against the same kernel with only the hash patch; median of 3 one-minute runs.")
    s.legend(24, 82, [(BEFORE, "Stock kernel"), (AFTER, "With the hash patch")])
    sh = lambda v: f"{v/1e6:.2f} M" if v >= 1e6 else f"{v/1e3:.0f} k"
    for i, (n, sub, b, a) in enumerate(rows):
        y = top + i * rh; sc = lambda v: v / a * (W - L - R)
        s.t(24, y + bh, n, weight=600); s.t(24, y + bh + 16, sub, size=11, fill=FAINT)
        s.rect(L, y, sc(b), bh, BEFORE); s.t(L + sc(b) + 6, y + bh - 5, sh(b), fill=MUTED)
        s.rect(L, y + bh + gap, sc(a), bh, AFTER)
        s.t(L + sc(a) + 6, y + 2 * bh + gap - 5, f"{sh(a)} ({a/b:.1f}×)", weight=600)
    s.save(f"{out}/mappings-per-minute.svg")

if __name__ == "__main__":
    cost(sys.argv[2]); profile(sys.argv[2]); throughput(sys.argv[2])
    print("peak", devobjs(sys.argv[1], sys.argv[2]))
