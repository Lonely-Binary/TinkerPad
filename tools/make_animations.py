#!/usr/bin/env python3
"""Draws the animated SVGs that show what each example does.

    python3 tools/make_animations.py        ->  docs/images/anim/<name>.svg

Each one is a small illustration of the idea behind the example (moles, a ping-pong
table, a ukulele...), animated with plain CSS and SMIL inside the SVG, so it plays on
GitHub and in any browser without scripts. Text colours switch for dark mode.
"""
import os

OUT = os.path.join(os.path.dirname(__file__), "..", "docs", "images", "anim")
W, H = 560, 280
FONT = "-apple-system,'Segoe UI',Helvetica,Arial,sans-serif"
TEAL, TEAL_INK = "#14898F", "#117A80"


def page(title, period, css, body):
    """Wraps a drawing: background card, shared styles, the scene's own keyframes."""
    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" role="img" aria-label="{title}">
<title>{title}</title>
<style>
.bg{{fill:#f3f7f7}} .ink{{fill:#1d2b2c}} .inks{{stroke:#1d2b2c}} .soft{{fill:#5d6b6c}}
.t1{{font:800 24px {FONT};fill:#1d2b2c}} .t2{{font:500 16px {FONT};fill:#4a5859}}
.t3{{font:700 16px {FONT};fill:{TEAL_INK}}} .big{{font:900 30px {FONT}}}
.fb{{transform-box:fill-box}}
@media (prefers-color-scheme: dark){{
.bg{{fill:#1b2327}} .ink{{fill:#e6edf3}} .inks{{stroke:#e6edf3}} .soft{{fill:#a9b4bd}}
.t1{{fill:#e6edf3}} .t2{{fill:#a9b4bd}} .t3{{fill:#4fc3c9}}}}
{css}
</style>
<rect width="{W}" height="{H}" rx="22" class="bg"/>
{body}
</svg>
"""


def kf(name, period, stops, prop="opacity", origin=None, timing="linear"):
    """A keyframe animation as a class. stops: [(seconds, css value)] for `prop`."""
    pts = sorted({**{0: stops[0][1]}, **{round(t / period * 100, 2): v for t, v in stops},
                  **{100: stops[-1][1]}}.items())
    body = " ".join(f"{p}%{{{prop}:{v}}}" for p, v in pts)
    o = f"transform-origin:{origin};" if origin else ""
    return (f"@keyframes {name}{{{body}}}"
            f".{name}{{{o}animation:{name} {period}s {timing} infinite}}")


def blink(name, period, windows, on=1, off=0):
    """Opacity `on` inside each (start, end) window, `off` outside, with quick fades."""
    st = [(0, off)]
    for a, b in windows:
        st += [(max(a - 0.06, 0), off), (a, on), (b, on), (min(b + 0.06, period), off)]
    st.append((period, off))
    return kf(name, period, st)


def T(x, y, s, cls="t2", anchor="start", extra=""):
    return f'<text x="{x}" y="{y}" class="{cls}" text-anchor="{anchor}" {extra}>{s}</text>'


# ------------------------------------------------------------------ MemoryLights

def memory_lights():
    P = 6.0
    cx, cy, r = 150, 140, 108
    cols = ["#2ecc71", "#ff3b30", "#ffcc00", "#2f80ff"]
    # quadrant k spans angles from -180+90k to -90+90k (top-left, top-right, bottom-right, bottom-left)
    import math
    def wedge(k):
        a0, a1 = math.radians(-180 + 90 * k), math.radians(-90 + 90 * k)
        x0, y0 = cx + r * math.cos(a0), cy + r * math.sin(a0)
        x1, y1 = cx + r * math.cos(a1), cy + r * math.sin(a1)
        return f"M{cx} {cy} L{x0:.1f} {y0:.1f} A{r} {r} 0 0 1 {x1:.1f} {y1:.1f} Z"
    order = [0, 2, 1, 3]
    watch = [0.3 + i * 0.55 for i in range(4)]
    repeat = [3.3 + i * 0.55 for i in range(4)]
    css, body = [], []
    for k in range(4):
        body.append(f'<path d="{wedge(k)}" fill="{cols[k]}" opacity="0.28"/>')
    for i, k in enumerate(order):
        css.append(blink(f"q{k}", P, [(watch[i], watch[i] + 0.4), (repeat[i], repeat[i] + 0.4)]))
        body.append(f'<path class="q{k}" d="{wedge(k)}" fill="{cols[k]}"/>')
    body.append(f'<line x1="{cx - r}" y1="{cy}" x2="{cx + r}" y2="{cy}" stroke="#f3f7f7" stroke-width="10" class="gap"/>'
                f'<line x1="{cx}" y1="{cy - r}" x2="{cx}" y2="{cy + r}" stroke="#f3f7f7" stroke-width="10" class="gap"/>')
    body.append(f'<circle cx="{cx}" cy="{cy}" r="40" fill="#1d2b2c"/>'
                f'<text x="{cx}" y="{cy + 6}" text-anchor="middle" style="font:800 15px {FONT};fill:#fff">SIMON</text>')
    # a tapping finger during the repeat
    for i, k in enumerate(order):
        import math as m
        ang = m.radians(-135 + 90 * k)
        fx, fy = cx + 74 * m.cos(ang), cy + 74 * m.sin(ang)
        css.append(blink(f"f{k}", P, [(repeat[i], repeat[i] + 0.4)]))
        body.append(f'<circle class="f{k}" cx="{fx:.0f}" cy="{fy:.0f}" r="14" fill="none" stroke="#ffffff" stroke-width="5"/>')
        # a note rising while it plays
        css.append(kf(f"n{k}", P, [(watch[i] - 0.01, "translate(0px,0px)"), (watch[i], "translate(0px,0px)"),
                                   (watch[i] + 0.6, "translate(0px,-34px)"), (watch[i] + 0.61, "translate(0px,0px)")],
                      prop="transform"))
        css.append(blink(f"no{k}", P, [(watch[i], watch[i] + 0.55)]))
        body.append(f'<g class="no{k}"><text class="n{k} big" x="{fx + 30:.0f}" y="{fy:.0f}" style="font:900 28px {FONT};fill:{cols[k]}">♪</text></g>')
    css.append(blink("w1", P, [(0.0, 3.0)]))
    css.append(blink("w2", P, [(3.1, 5.9)]))
    body.append(f'<g class="w1">{T(300, 118, "1. Watch", "t1")}{T(300, 148, "Colours light up, each")}{T(300, 170, "with its own note.")}</g>')
    body.append(f'<g class="w2">{T(300, 118, "2. Repeat", "t1")}{T(300, 148, "Press them back in")}{T(300, 170, "the same order.")}</g>')
    body.append(T(300, 222, "Every round adds one more.", "t3"))
    css.append(".gap{stroke:#f3f7f7}@media (prefers-color-scheme: dark){.gap{stroke:#1b2327}}")
    return page("MemoryLights: watch the colours, then repeat them", P, "".join(css), "".join(body))


# -------------------------------------------------------------------- WhackAMole

def mole_shape(color="#8b5a3c"):
    return (f'<ellipse cx="0" cy="0" rx="32" ry="38" fill="{color}"/>'
            f'<ellipse cx="0" cy="12" rx="18" ry="20" fill="#d9a77e" opacity="0.6"/>'
            f'<circle cx="-11" cy="-10" r="5" fill="#1d1d1d"/><circle cx="11" cy="-10" r="5" fill="#1d1d1d"/>'
            f'<circle cx="-9" cy="-12" r="1.8" fill="#fff"/><circle cx="13" cy="-12" r="1.8" fill="#fff"/>'
            f'<ellipse cx="0" cy="2" rx="7" ry="5" fill="#ff7aa2"/>'
            f'<rect x="-6" y="8" width="5" height="7" rx="1" fill="#fff"/><rect x="1" y="8" width="5" height="7" rx="1" fill="#fff"/>')


def whack_a_mole():
    P = 3.6
    holes = [80, 200, 320]
    gy = 210
    css, body = [], []
    body.append(f'<rect x="20" y="{gy - 4}" width="360" height="60" rx="14" fill="#7cc36a"/>')
    ups = [(0, 0.2, 1.0, "#8b5a3c"), (1, 0.8, 1.6, "#8b5a3c"), (2, 1.9, 2.9, "#e0a800")]
    for i, (h, a, b, col) in enumerate(ups):
        x = holes[h]
        body.append(f'<ellipse cx="{x}" cy="{gy}" rx="48" ry="14" fill="#3b2a1d"/>')
        body.append(f'<clipPath id="c{i}"><rect x="{x - 60}" y="40" width="120" height="{gy - 40}"/></clipPath>')
        down = "translate(0px,90px)"
        stops = [(0, down), (a, down), (a + 0.18, "translate(0px,0px)"), (b - 0.12, "translate(0px,0px)"),
                 (b, down), (P, down)]
        if h == 1:   # this one gets whacked: squashes and drops at once
            stops = [(0, down), (a, down), (a + 0.18, "translate(0px,0px)"), (1.32, "translate(0px,0px)"),
                     (1.38, "translate(0px,14px) scale(1.15,0.7)"), (1.5, down), (P, down)]
        css.append(kf(f"m{i}", P, stops, prop="transform"))
        body.append(f'<g clip-path="url(#c{i})"><g transform="translate({x} {gy - 34})"><g class="m{i}">{mole_shape(col)}</g></g></g>')
        body.append(f'<path d="M{x - 48} {gy} a48 14 0 0 0 96 0" fill="#7cc36a"/>')
    # golden sparkle
    css.append(blink("gs", P, [(2.15, 2.8)]))
    body.append(f'<g class="gs" style="font:900 22px {FONT};fill:#e0a800">'
                f'<text x="{holes[2] + 34}" y="130">✦</text><text x="{holes[2] - 50}" y="140">✦</text></g>')
    # hammer swinging onto the middle hole
    css.append(kf("hm", P, [(0, "rotate(-55deg)"), (1.15, "rotate(-55deg)"), (1.32, "rotate(8deg)"),
                            (1.5, "rotate(8deg)"), (1.8, "rotate(-55deg)"), (P, "rotate(-55deg)")],
                  prop="transform", origin="300px 40px"))
    body.append(f'<g class="hm"><rect x="236" y="34" width="66" height="12" rx="5" fill="#b07a44"/>'
                f'<rect x="206" y="14" width="40" height="54" rx="9" fill="#e74c3c"/>'
                f'<rect x="206" y="14" width="40" height="12" rx="6" fill="#ff7b6b"/></g>')
    css.append(blink("pow", P, [(1.33, 1.75)]))
    body.append(f'<g class="pow"><text x="{holes[1]}" y="118" text-anchor="middle" style="font:900 28px {FONT};fill:#ff3b30">POW!</text></g>')
    css.append(kf("plus", P, [(0, "translate(0px,0px)"), (1.33, "translate(0px,0px)"), (2.0, "translate(0px,-40px)"),
                              (2.01, "translate(0px,0px)"), (P, "translate(0px,0px)")], prop="transform"))
    css.append(blink("plo", P, [(1.33, 1.95)]))
    body.append(f'<g class="plo"><g class="plus"><text x="{holes[1] + 54}" y="160" style="font:900 26px {FONT};fill:#2ecc71">+1</text></g></g>')
    body.append(T(410, 110, "Whack!", "t1"))
    body.append(T(410, 140, "Hit the moles"))
    body.append(T(410, 162, "before they hide."))
    body.append(T(410, 204, "Gold +3", "t3") + T(410, 226, "Bomb −5", "t3"))
    return page("WhackAMole: hit the moles before they hide", P, "".join(css), "".join(body))


# ---------------------------------------------------------------------- PingPong

def ping_pong():
    P = 2.4
    css, body = [], []
    # table in perspective
    body.append('<polygon points="110,90 450,90 520,230 40,230" fill="#1f6fb2"/>'
                '<polygon points="110,90 450,90 520,230 40,230" fill="none" stroke="#fff" stroke-width="4"/>'
                '<line x1="280" y1="90" x2="280" y2="230" stroke="#fff" stroke-width="2" opacity="0.7"/>'
                '<rect x="268" y="70" width="24" height="168" fill="#dfe6ea"/>'
                '<line x1="280" y1="70" x2="280" y2="238" stroke="#9aa5ab" stroke-width="2"/>'
                '<rect x="40" y="230" width="480" height="12" fill="#17507f"/>')
    # paddles
    css.append(kf("pl", P, [(0, "rotate(-25deg)"), (0.12, "rotate(10deg)"), (0.35, "rotate(-25deg)"), (P, "rotate(-25deg)")],
                  prop="transform", origin="52px 180px"))
    css.append(kf("pr", P, [(0, "rotate(25deg)"), (1.2, "rotate(25deg)"), (1.32, "rotate(-10deg)"), (1.55, "rotate(25deg)"), (P, "rotate(25deg)")],
                  prop="transform", origin="508px 180px"))
    body.append('<g class="pl"><rect x="46" y="176" width="12" height="44" rx="5" fill="#8b5a3c"/>'
                '<circle cx="52" cy="160" r="30" fill="#e74c3c"/></g>')
    body.append('<g class="pr"><rect x="502" y="176" width="12" height="44" rx="5" fill="#8b5a3c"/>'
                f'<circle cx="508" cy="160" r="30" fill="{TEAL}"/></g>')
    # ball: an arc each way with a bounce on the far half
    path = "M80 150 Q190 40 320 170 Q400 70 480 150 Q370 40 240 170 Q160 70 80 150"
    body.append(f'<ellipse rx="10" ry="4" fill="#000" opacity="0.18"><animateMotion dur="{P}s" repeatCount="indefinite" '
                f'path="M80 210 L320 205 L480 210 L240 205 L80 210"/></ellipse>')
    body.append(f'<circle r="10" fill="#ffffff" stroke="#f39c12" stroke-width="2"><animateMotion dur="{P}s" repeatCount="indefinite" path="{path}"/></circle>')
    body.append(T(280, 44, "Two players, one board", "t1", "middle"))
    body.append(T(280, 270, "Hit it back when it reaches your edge · first to 5 wins", "t3", "middle"))
    return page("PingPong: table tennis for two players", P, "".join(css), "".join(body))


# -------------------------------------------------------------------- SoundMatch

def cat(x, y):
    return (f'<g transform="translate({x} {y})"><polygon points="-22,-10 -16,-34 -4,-18" fill="#ff9f1c"/>'
            f'<polygon points="22,-10 16,-34 4,-18" fill="#ff9f1c"/><circle r="24" fill="#ff9f1c"/>'
            f'<circle cx="-8" cy="-4" r="3.5" fill="#222"/><circle cx="8" cy="-4" r="3.5" fill="#222"/>'
            f'<polygon points="-4,4 4,4 0,9" fill="#ff5c8a"/>'
            f'<path d="M-6 13 q6 5 12 0 M-28 4 h14 M-28 10 h14 M14 4 h14 M14 10 h14" stroke="#222" stroke-width="1.6" fill="none"/></g>')


def dog(x, y):
    return (f'<g transform="translate({x} {y})"><ellipse cx="-22" cy="-2" rx="9" ry="18" fill="#7a4a2a"/>'
            f'<ellipse cx="22" cy="-2" rx="9" ry="18" fill="#7a4a2a"/><circle r="23" fill="#b5835a"/>'
            f'<circle cx="-8" cy="-5" r="3.5" fill="#222"/><circle cx="8" cy="-5" r="3.5" fill="#222"/>'
            f'<ellipse cx="0" cy="7" rx="6" ry="4.5" fill="#222"/><path d="M0 11 v5 M-6 16 q6 4 12 0" stroke="#222" stroke-width="1.6" fill="none"/></g>')


def bird(x, y):
    return (f'<g transform="translate({x} {y})"><circle r="22" fill="#ffd166"/>'
            f'<polygon points="18,0 32,5 18,10" fill="#f77f00"/><circle cx="7" cy="-5" r="3.5" fill="#222"/>'
            f'<path d="M-14 4 q-10 -14 6 -10" fill="#f4a300"/></g>')


def sound_match():
    P = 6.5
    css, body = [], []
    xs = [48, 128, 208, 288]
    ys = [52, 156]
    cards = {(0, 0): None, (1, 0): None, (2, 0): None, (3, 0): None, (0, 1): None, (1, 1): None, (2, 1): None, (3, 1): None}
    # flips: (col,row, face, open, close)
    flips = [((0, 0), cat, 0.4, P - 0.2), ((2, 1), cat, 1.3, P - 0.2),
             ((3, 0), dog, 2.6, 4.3), ((1, 1), bird, 3.3, 4.3)]
    flipped = {f[0] for f in flips}
    for (c, r) in cards:
        x, y = xs[c], ys[r]
        back = (f'<rect x="{x - 34}" y="{y - 44}" width="68" height="88" rx="10" fill="{TEAL}"/>'
                f'<text x="{x}" y="{y + 12}" text-anchor="middle" style="font:900 34px {FONT};fill:#ffffff">?</text>')
        if (c, r) not in flipped:
            body.append(back)
            continue
        f = next(f for f in flips if f[0] == (c, r))
        _, face, a, b = f
        n = f"c{c}{r}"
        css.append(kf(n, P, [(0, "scaleX(1)"), (a - 0.15, "scaleX(1)"), (a, "scaleX(0)"), (a + 0.15, "scaleX(1)"),
                             (b - 0.15, "scaleX(1)"), (b, "scaleX(0)"), (b + 0.15, "scaleX(1)"), (P, "scaleX(1)")],
                      prop="transform", origin=f"{x}px {y}px"))
        css.append(blink(n + "f", P, [(a, b)]))
        css.append(blink(n + "b", P, [(0, a), (b, P)]))
        body.append(f'<g class="{n}"><g class="{n}b">{back}</g><g class="{n}f">'
                    f'<rect x="{x - 34}" y="{y - 44}" width="68" height="88" rx="10" fill="#ffffff" stroke="#d6dfe0" stroke-width="2"/>'
                    f'{face(x, y)}</g></g>')
    # what you hear
    def bubble(word, windows, n):
        css.append(blink(n, P, windows))
        return (f'<g class="{n}"><rect x="360" y="70" width="150" height="48" rx="24" fill="#ffffff" stroke="#d6dfe0" stroke-width="2"/>'
                f'<polygon points="372,112 360,130 392,114" fill="#ffffff"/>'
                f'<text x="435" y="102" text-anchor="middle" style="font:800 22px {FONT};fill:#1d2b2c">{word}</text></g>')
    body.append(bubble("Meow!", [(0.5, 1.1), (1.4, 2.0)], "b1"))
    body.append(bubble("Woof!", [(2.7, 3.2)], "b2"))
    body.append(bubble("Tweet!", [(3.4, 3.9)], "b3"))
    css.append(blink("ok", P, [(2.0, 2.5)]))
    css.append(blink("no", P, [(3.95, 4.4)]))
    body.append(f'<g class="ok">{T(360, 168, "A pair! ✓", "t1")}</g>')
    body.append(f'<g class="no">{T(360, 168, "Not a pair", "t1")}</g>')
    body.append(T(360, 210, "Find the pairs"))
    body.append(T(360, 232, "by their sound."))
    return page("SoundMatch: find the pairs by their sound", P, "".join(css), "".join(body))


# ---------------------------------------------------------------------- BeatGrid

def beat_grid():
    P = 2.4
    css, body = [], []
    # phone
    body.append('<rect x="40" y="22" width="150" height="236" rx="22" fill="#1d2b2c"/>'
                '<rect x="50" y="40" width="130" height="200" rx="10" fill="#26323a"/>')
    beats = [(0, 82, 100), (0.8, 148, 160), (1.6, 100, 196)]
    for i, (t, x, y) in enumerate(beats):
        body.append(f'<circle cx="{x}" cy="{y}" r="16" fill="#ff4fa3" opacity="0.35"/>')
        body.append(f'<circle cx="{x}" cy="{y}" r="16" fill="none" stroke="#ff4fa3" stroke-width="3">'
                    f'<animate attributeName="r" dur="{P}s" repeatCount="indefinite" '
                    f'values="40;40;16;16;40" keyTimes="0;{t / P:.3f};{(t + 0.6) / P:.3f};{(t + 0.62) / P:.3f};1"/>'
                    f'<animate attributeName="opacity" dur="{P}s" repeatCount="indefinite" '
                    f'values="0;0;1;0;0" keyTimes="0;{t / P:.3f};{(t + 0.6) / P:.3f};{(t + 0.66) / P:.3f};1"/></circle>')
        css.append(blink(f"h{i}", P, [(t + 0.58, t + 0.8)]))
        body.append(f'<circle class="h{i}" cx="{x}" cy="{y}" r="20" fill="#ff4fa3"/>')
    body.append(f'<text x="115" y="230" text-anchor="middle" style="font:800 13px {FONT};fill:#9fb0b8">SCORE 2480</text>')
    # PERFECT and an equalizer
    css.append(blink("pf", P, [(0.6, 0.95), (1.4, 1.75), (2.2, 2.4)]))
    body.append(f'<g class="pf"><text x="230" y="96" style="font:900 30px {FONT};fill:#ff4fa3">PERFECT!</text></g>')
    for i in range(9):
        h0, h1 = 20 + (i * 37) % 50, 70 - (i * 23) % 45
        x = 232 + i * 30
        body.append(f'<rect x="{x}" y="160" width="20" height="40" rx="5" fill="{TEAL}">'
                    f'<animate attributeName="height" dur="0.8s" repeatCount="indefinite" values="{h0};{h1};{h0}"/>'
                    f'<animate attributeName="y" dur="0.8s" repeatCount="indefinite" values="{230 - h0};{230 - h1};{230 - h0}"/></rect>')
    body.append(T(232, 130, "Press when the ring lands", "t2"))
    body.append(T(232, 262, "The board plays the music.", "t3"))
    return page("BeatGrid: a rhythm game on your phone and the board", P, "".join(css), "".join(body))


# ----------------------------------------------------------------------- MidiPad

def midi_pad():
    P = 2.0
    css, body = [], []
    # drum kit
    body.append('<line x1="70" y1="70" x2="70" y2="230" stroke="#9aa5ab" stroke-width="4"/>'
                '<ellipse cx="70" cy="76" rx="46" ry="8" fill="#e8c547"/>'
                '<line x1="250" y1="60" x2="250" y2="230" stroke="#9aa5ab" stroke-width="4"/>')
    css.append(kf("cy", P, [(0, "rotate(0deg)"), (1.7, "rotate(0deg)"), (1.75, "rotate(-8deg)"), (1.95, "rotate(0deg)"), (P, "rotate(0deg)")],
                  prop="transform", origin="250px 62px"))
    body.append('<g class="cy"><ellipse cx="250" cy="62" rx="52" ry="9" fill="#e8c547"/></g>')
    css.append(kf("kick", P, [(0, "scale(1.06)"), (0.12, "scale(1)"), (1.0, "scale(1.06)"), (1.12, "scale(1)"),
                              (1.25, "scale(1.06)"), (1.37, "scale(1)"), (P, "scale(1)")], prop="transform", origin="160px 190px"))
    body.append('<g class="kick"><circle cx="160" cy="190" r="62" fill="#c0392b"/><circle cx="160" cy="190" r="50" fill="#f2f2f2"/>'
                f'<text x="160" y="197" text-anchor="middle" style="font:900 18px {FONT};fill:{TEAL}">TINKER</text></g>')
    css.append(kf("sn", P, [(0, "scale(1)"), (0.5, "scale(1.08)"), (0.6, "scale(1)"), (1.5, "scale(1.08)"), (1.6, "scale(1)"), (P, "scale(1)")],
                  prop="transform", origin="80px 150px"))
    body.append('<g class="sn"><ellipse cx="80" cy="150" rx="40" ry="12" fill="#dfe6ea"/><rect x="40" y="150" width="80" height="26" fill="#9b59b6"/>'
                '<ellipse cx="80" cy="176" rx="40" ry="12" fill="#7d3c98"/></g>')
    # sticks
    css.append(kf("st1", P, [(0, "rotate(-30deg)"), (0.45, "rotate(-30deg)"), (0.5, "rotate(10deg)"), (0.62, "rotate(-30deg)"),
                             (1.45, "rotate(-30deg)"), (1.5, "rotate(10deg)"), (1.62, "rotate(-30deg)"), (P, "rotate(-30deg)")],
                  prop="transform", origin="150px 110px"))
    body.append('<g class="st1"><line x1="150" y1="110" x2="92" y2="140" stroke="#c49a6c" stroke-width="7" stroke-linecap="round"/></g>')
    css.append(kf("st2", P, [(0, "rotate(20deg)"), (1.7, "rotate(20deg)"), (1.75, "rotate(-12deg)"), (1.9, "rotate(20deg)"), (P, "rotate(20deg)")],
                  prop="transform", origin="200px 120px"))
    body.append('<g class="st2"><line x1="200" y1="120" x2="240" y2="70" stroke="#c49a6c" stroke-width="7" stroke-linecap="round"/></g>')
    # USB to a laptop with MIDI notes
    body.append('<path d="M222 200 C 270 220, 300 220, 340 200" stroke="#9aa5ab" stroke-width="5" fill="none" stroke-dasharray="2 9" stroke-linecap="round"/>')
    body.append('<rect x="340" y="80" width="190" height="122" rx="10" fill="#26323a"/>'
                '<rect x="350" y="90" width="170" height="100" rx="4" fill="#1b2327"/>'
                '<path d="M324 214 h222 l-14 14 h-194 z" fill="#3a464d"/>')
    for i, (lane, t, col) in enumerate([(2, 0.0, "#ef476f"), (1, 0.5, "#06d6a0"), (2, 1.0, "#ef476f"),
                                        (2, 1.25, "#ef476f"), (1, 1.5, "#06d6a0"), (0, 1.75, "#ffd166")]):
        css.append(blink(f"m{i}", P, [(t, P)]))
        body.append(f'<rect class="m{i}" x="{358 + t * 76:.0f}" y="{100 + lane * 28}" width="22" height="16" rx="3" fill="{col}"/>')
    body.append(T(435, 52, "USB MIDI drums", "t1", "middle"))
    body.append(T(435, 258, "GarageBand · Logic · Ableton", "t3", "middle"))
    return page("MidiPad: USB MIDI drum pads for your music app", P, "".join(css), "".join(body))


# ---------------------------------------------------------------- UkuleleTeacher

def ukulele_teacher():
    P = 4.0
    css, body = [], []
    # ukulele lying across the picture
    body.append('<circle cx="112" cy="150" r="62" fill="#d99a4e"/><circle cx="178" cy="150" r="50" fill="#d99a4e"/>'
                '<circle cx="150" cy="150" r="20" fill="#5b3a1e"/>'
                '<rect x="74" y="140" width="22" height="20" rx="4" fill="#5b3a1e"/>'
                '<rect x="200" y="132" width="250" height="36" rx="4" fill="#6b4423"/>'
                '<path d="M450 128 h56 a8 8 0 0 1 8 8 v28 a8 8 0 0 1 -8 8 h-56 z" fill="#5b3a1e"/>')
    for i in range(4):
        x = 250 + i * 50
        body.append(f'<line x1="{x}" y1="132" x2="{x}" y2="168" stroke="#d9d9d9" stroke-width="3"/>')
    for px in (470, 492):
        body.append(f'<circle cx="{px}" cy="124" r="6" fill="#ddd"/><circle cx="{px}" cy="176" r="6" fill="#ddd"/>')
    strings = [138, 146, 154, 162]
    names = "GCEA"
    for s, y in enumerate(strings):
        body.append(f'<line x1="80" y1="{y}" x2="460" y2="{y}" stroke="#f2f2f2" stroke-width="{1.2 + s * 0.4:.1f}">'
                    f'<animate attributeName="y1" dur="0.12s" repeatCount="indefinite" values="{y};{y - 1.2};{y + 1.2};{y}"/>'
                    f'<animate attributeName="y2" dur="0.12s" repeatCount="indefinite" values="{y};{y + 1.2};{y - 1.2};{y}"/></line>')
    # the finger: where to press next, moving along the frets
    spots = [(275, 154), (325, 146), (275, 162), (375, 138)]
    vals = ";".join(f"{x} {y}" for x, y in spots)
    for i, (x, y) in enumerate(spots):
        t = i * 1.0
        css.append(blink(f"d{i}", P, [(t, t + 0.85)]))
        body.append(f'<g class="d{i}"><circle cx="{x}" cy="{y}" r="15" fill="{TEAL}" opacity="0.35"/>'
                    f'<circle cx="{x}" cy="{y}" r="9" fill="#ffffff" stroke="{TEAL}" stroke-width="4"/></g>')
        nxt = spots[(i + 1) % 4]
        css.append(blink(f"e{i}", P, [(t, t + 0.85)], on=0.45))
        body.append(f'<circle class="e{i}" cx="{nxt[0]}" cy="{nxt[1]}" r="8" fill="none" stroke="#ffffff" stroke-width="2" stroke-dasharray="3 3"/>')
        # a note rising from the sound hole
        css.append(kf(f"n{i}", P, [(t, "translate(0px,0px)"), (t + 0.9, "translate(18px,-70px)"), (t + 0.91, "translate(0px,0px)")],
                      prop="transform"))
        css.append(blink(f"no{i}", P, [(t + 0.05, t + 0.85)]))
        body.append(f'<g class="no{i}"><text class="n{i}" x="146" y="110" style="font:900 26px {FONT};fill:#d99a4e">{"♪♫♪♬"[i]}</text></g>')
    body.append(T(40, 46, "Follow the lights, learn the song", "t1"))
    body.append(T(40, 262, "The keys are the fretboard: one row per string.", "t3"))
    return page("UkuleleTeacher: learn a song, one phrase at a time", P, "".join(css), "".join(body))


# -------------------------------------------------------------------- VoiceBoard

def person(x, y, col):
    return (f'<circle cx="{x}" cy="{y - 30}" r="18" fill="#f2c9a0"/>'
            f'<path d="M{x - 26} {y + 22} a26 30 0 0 1 52 0 z" fill="{col}"/>')


def house(x, y):
    return (f'<polygon points="{x - 70},{y} {x},{y - 52} {x + 70},{y}" fill="{TEAL}"/>'
            f'<rect x="{x - 60}" y="{y}" width="120" height="110" rx="6" fill="#ffffff" stroke="#d6dfe0" stroke-width="2"/>')


def voice_board():
    P = 6.0
    css, body = [], []
    body.append(house(110, 104) + house(450, 104))
    body.append(person(92, 186, "#ff6b4a"))
    body.append(person(468, 186, "#2f80ff"))
    # a small board in each house
    for x in (138, 422):
        body.append(f'<rect x="{x - 16}" y="150" width="32" height="32" rx="5" fill="#1d2b2c"/>')
        for i in range(4):
            body.append(f'<rect x="{x - 12 + (i % 2) * 13}" y="{154 + (i // 2) * 13}" width="10" height="10" rx="2" fill="#3a464d"/>')
    css.append(blink("rec", P, [(0.3, 2.0)]))
    body.append(f'<g class="rec"><rect x="126" y="154" width="10" height="10" rx="2" fill="#ff2a2a"/>'
                f'<rect x="20" y="20" width="150" height="40" rx="20" fill="#ffffff" stroke="#d6dfe0" stroke-width="2"/>'
                f'<polygon points="80,58 92,74 98,58" fill="#ffffff"/>'
                f'<text x="95" y="46" text-anchor="middle" style="font:800 16px {FONT};fill:#1d2b2c">Dinner at 7!</text></g>')
    # the message travelling to the other house
    body.append('<path d="M170 140 Q280 40 390 140" fill="none" stroke="#9aa5ab" stroke-width="3" stroke-dasharray="4 8"/>')
    css.append(blink("fly", P, [(2.0, 3.0)]))
    body.append(f'<g class="fly"><g><animateMotion dur="{P}s" repeatCount="indefinite" keyPoints="0;0;1;1" keyTimes="0;{2.0 / P:.3f};{3.0 / P:.3f};1" '
                f'calcMode="linear" path="M170 140 Q280 40 390 140"/>'
                f'<rect x="-18" y="-12" width="36" height="24" rx="4" fill="#ffffff" stroke="{TEAL}" stroke-width="2.5"/>'
                f'<path d="M-18 -12 L0 2 L18 -12" fill="none" stroke="{TEAL}" stroke-width="2.5"/></g></g>')
    # it waits on their key, glowing, until they listen
    css.append(kf("wait", P, [(0, "0"), (3.0, "0"), (3.1, "1"), (3.5, "0.5"), (3.9, "1"), (4.3, "0.5"), (4.6, "1"), (4.7, "0"), (P, "0")]))
    body.append(f'<rect class="wait" x="413" y="154" width="10" height="10" rx="2" fill="#2f80ff"/>')
    css.append(blink("hear", P, [(4.7, 5.9)]))
    body.append(f'<g class="hear"><path d="M498 144 q12 12 0 24 M508 136 q20 20 0 40" stroke="{TEAL}" stroke-width="3" fill="none" stroke-linecap="round"/>'
                f'<text x="380" y="46" style="font:800 16px {FONT};fill:{TEAL_INK}">♪ Dinner at 7!</text></g>')
    body.append(T(280, 254, "Leave a voice message · it waits on their key · boards stay in sync", "t3", "middle"))
    return page("VoiceBoard: a family voice message board", P, "".join(css), "".join(body))


# ---------------------------------------------------------------------- MacroPad

def macro_pad():
    P = 4.5
    css, body = [], []
    keys = [("⌘C", "Copy"), ("⌘V", "Paste"), ("⌘⇧4", "Shot")]
    for i, (k, w) in enumerate(keys):
        x, y = 40 + i * 78, 110
        t = 0.3 + i * 1.4
        css.append(kf(f"k{i}", P, [(0, "translate(0px,0px)"), (t, "translate(0px,0px)"), (t + 0.08, "translate(0px,6px)"),
                                   (t + 0.35, "translate(0px,6px)"), (t + 0.45, "translate(0px,0px)"), (P, "translate(0px,0px)")],
                      prop="transform"))
        body.append(f'<rect x="{x}" y="{y + 8}" width="66" height="66" rx="12" fill="#0d5e62"/>'
                    f'<g class="k{i}"><rect x="{x}" y="{y}" width="66" height="66" rx="12" fill="{TEAL}"/>'
                    f'<text x="{x + 33}" y="{y + 34}" text-anchor="middle" style="font:800 18px {FONT};fill:#fff">{k}</text>'
                    f'<text x="{x + 33}" y="{y + 54}" text-anchor="middle" style="font:600 12px {FONT};fill:#cdeff0">{w}</text></g>')
    # bluetooth
    body.append(f'<g transform="translate(296 128)"><circle r="22" fill="#2f80ff"/>'
                f'<path d="M-6 -9 L8 4 L1 11 V-11 L8 -4 L-6 9" stroke="#fff" stroke-width="3" fill="none" stroke-linejoin="round"/></g>')
    for i in range(3):
        t = 0.3 + i * 1.4
        css.append(blink(f"w{i}", P, [(t, t + 0.5)]))
        body.append(f'<path class="w{i}" d="M326 112 q12 16 0 32 M336 104 q20 24 0 48" stroke="#2f80ff" stroke-width="3" fill="none" stroke-linecap="round"/>')
    # laptop doing the thing
    body.append('<rect x="350" y="62" width="180" height="120" rx="10" fill="#26323a"/>'
                '<rect x="360" y="72" width="160" height="100" rx="4" fill="#ffffff"/>'
                '<path d="M334 194 h212 l-14 14 h-184 z" fill="#3a464d"/>')
    for j in range(4):
        body.append(f'<rect x="372" y="{84 + j * 18}" width="{110 - (j % 2) * 30}" height="8" rx="4" fill="#d6dfe0"/>')
    css.append(blink("cp", P, [(0.45, 1.6)]))
    body.append(f'<g class="cp"><rect x="370" y="80" width="114" height="16" rx="3" fill="#2f80ff" opacity="0.35"/>'
                f'<text x="440" y="164" text-anchor="middle" style="font:800 14px {FONT};fill:#2f80ff">Copied</text></g>')
    css.append(blink("ps", P, [(1.85, 3.0)]))
    body.append(f'<g class="ps"><rect x="372" y="138" width="110" height="8" rx="4" fill="#2ecc71"/>'
                f'<text x="440" y="164" text-anchor="middle" style="font:800 14px {FONT};fill:#2ecc71">Pasted</text></g>')
    css.append(blink("sh", P, [(3.25, 3.45)], on=0.9))
    body.append('<rect class="sh" x="360" y="72" width="160" height="100" rx="4" fill="#ffffff"/>')
    css.append(blink("sh2", P, [(3.45, 4.3)]))
    body.append(f'<g class="sh2"><rect x="376" y="80" width="128" height="76" rx="4" fill="none" stroke="#ff9f1c" stroke-width="3" stroke-dasharray="6 4"/>'
                f'<text x="440" y="164" text-anchor="middle" style="font:800 14px {FONT};fill:#ff9f1c">Screenshot</text></g>')
    body.append(T(40, 62, "Any shortcut on any key", "t1"))
    body.append(T(40, 236, "Set them up from a web page,", "t2"))
    body.append(T(40, 258, "then pair over Bluetooth.", "t2"))
    return page("MacroPad: a Bluetooth shortcut keyboard", P, "".join(css), "".join(body))


# ---------------------------------------------------------------------- ComboPad

def fighter(x, col, cls_arm, cls_leg, flip=1):
    s = flip
    return (f'<circle cx="{x}" cy="128" r="16" fill="{col}"/>'
            f'<line x1="{x}" y1="144" x2="{x}" y2="196" stroke="{col}" stroke-width="9" stroke-linecap="round"/>'
            f'<g class="{cls_arm}"><line x1="{x}" y1="156" x2="{x + 34 * s}" y2="166" stroke="{col}" stroke-width="8" stroke-linecap="round"/></g>'
            f'<line x1="{x}" y1="156" x2="{x - 26 * s}" y2="178" stroke="{col}" stroke-width="8" stroke-linecap="round"/>'
            f'<g class="{cls_leg}"><line x1="{x}" y1="196" x2="{x + 22 * s}" y2="236" stroke="{col}" stroke-width="9" stroke-linecap="round"/></g>'
            f'<line x1="{x}" y1="196" x2="{x - 18 * s}" y2="236" stroke="{col}" stroke-width="9" stroke-linecap="round"/>')


def combo_pad():
    P = 3.6
    css, body = [], []
    body.append('<rect x="20" y="236" width="520" height="10" rx="5" fill="#9aa5ab"/>')
    # health bars
    body.append('<rect x="30" y="24" width="210" height="16" rx="8" fill="#d6dfe0"/><rect x="30" y="24" width="190" height="16" rx="8" fill="#2ecc71"/>')
    body.append('<rect x="320" y="24" width="210" height="16" rx="8" fill="#d6dfe0"/>')
    css.append(kf("hp", P, [(0, "scaleX(1)"), (0.7, "scaleX(1)"), (0.8, "scaleX(0.78)"), (1.3, "scaleX(0.78)"),
                            (1.4, "scaleX(0.5)"), (1.9, "scaleX(0.5)"), (2.0, "scaleX(0.2)"), (3.3, "scaleX(0.2)"), (3.35, "scaleX(1)"), (P, "scaleX(1)")],
                  prop="transform", origin="530px 32px"))
    body.append('<rect class="hp" x="320" y="24" width="210" height="16" rx="8" fill="#ff4f3a"/>')
    # attacker: punch, punch, kick
    css.append(kf("arm", P, [(0, "rotate(40deg)"), (0.6, "rotate(40deg)"), (0.7, "rotate(0deg)"), (0.95, "rotate(40deg)"),
                             (1.2, "rotate(40deg)"), (1.3, "rotate(0deg)"), (1.55, "rotate(40deg)"), (P, "rotate(40deg)")],
                  prop="transform", origin="190px 156px"))
    css.append(kf("leg", P, [(0, "rotate(0deg)"), (1.8, "rotate(0deg)"), (1.9, "rotate(-70deg)"), (2.2, "rotate(0deg)"), (P, "rotate(0deg)")],
                  prop="transform", origin="190px 196px"))
    body.append(fighter(190, "#ff4f3a", "arm", "leg", 1))
    css.append(kf("def", P, [(0, "translate(0px,0px)"), (0.7, "translate(0px,0px)"), (0.78, "translate(14px,0px)"),
                             (1.3, "translate(14px,0px)"), (1.38, "translate(30px,0px)"), (1.9, "translate(30px,0px)"),
                             (2.0, "translate(70px,-10px) rotate(12deg)"), (3.2, "translate(70px,-10px) rotate(12deg)"),
                             (3.4, "translate(0px,0px)"), (P, "translate(0px,0px)")], prop="transform"))
    body.append(f'<g class="def">{fighter(262, "#3a7bff", "x1", "x2", -1)}</g>')
    for i, (t, x, y) in enumerate([(0.7, 236, 160), (1.3, 252, 160), (1.9, 262, 200)]):
        css.append(blink(f"hit{i}", P, [(t, t + 0.22)]))
        body.append(f'<g class="hit{i}"><polygon points="{x},{y - 22} {x + 6},{y - 6} {x + 22},{y} {x + 6},{y + 6} {x},{y + 22} {x - 6},{y + 6} {x - 22},{y} {x - 6},{y - 6}" fill="#ffd166"/></g>')
    # the inputs and the combo counter
    moves = ["↓", "↘", "→", "P", "P", "K"]
    for i, m in enumerate(moves):
        t = 0.1 + i * 0.3
        css.append(blink(f"in{i}", P, [(t, 3.2)]))
        body.append(f'<g class="in{i}"><rect x="{368 + i * 28}" y="90" width="24" height="30" rx="6" fill="#ffffff" stroke="#d6dfe0" stroke-width="2"/>'
                    f'<text x="{380 + i * 28}" y="112" text-anchor="middle" style="font:800 16px {FONT};fill:#1d2b2c">{m}</text></g>')
    css.append(blink("combo", P, [(2.0, 3.2)]))
    body.append(f'<g class="combo"><text x="368" y="168" style="font:900 30px {FONT};fill:#ff4f3a">3 HIT</text>'
                f'<text x="368" y="198" style="font:900 30px {FONT};fill:#ff4f3a">COMBO!</text></g>')
    body.append(T(368, 76, "One key, whole combo", "t3"))
    return page("ComboPad: one key plays a whole fighting-game combo", P, "".join(css), "".join(body))


# ------------------------------------------------------------------ Hardware test

def hardware_test():
    P = 5.0
    css, body = [], []
    # a circuit board
    body.append('<rect x="30" y="40" width="250" height="200" rx="14" fill="#1f7a4a"/>'
                '<rect x="110" y="96" width="80" height="80" rx="6" fill="#1d1d1d"/>'
                '<rect x="124" y="110" width="52" height="52" rx="4" fill="#bfc7cc"/>')
    for i in range(7):
        body.append(f'<rect x="{104 - 10}" y="{102 + i * 11}" width="10" height="4" fill="#d4b483"/>'
                    f'<rect x="{190}" y="{102 + i * 11}" width="10" height="4" fill="#d4b483"/>')
    body.append('<path d="M40 70 H100 V100 M260 70 H200 V100 M40 210 H100 V172 M260 210 H200 V172" stroke="#d4b483" stroke-width="3" fill="none"/>'
                '<circle cx="60" cy="140" r="14" fill="#2b2b2b"/><rect x="214" y="126" width="40" height="28" rx="4" fill="#e8e8e8"/>')
    # magnifier sweeping the board
    body.append(f'<g><animateMotion dur="{P}s" repeatCount="indefinite" path="M80 90 L220 90 L220 190 L80 190 Z"/>'
                f'<circle r="30" fill="#ffffff" opacity="0.25" stroke="#ffffff" stroke-width="6"/>'
                f'<line x1="21" y1="21" x2="44" y2="44" stroke="#ffffff" stroke-width="9" stroke-linecap="round"/></g>')
    checks = ["16 keys", "16 LEDs", "Speaker", "Microphone", "Memory (PSRAM)"]
    body.append(T(320, 50, "Test every part", "t1"))
    for i, c in enumerate(checks):
        t = 0.5 + i * 0.8
        y = 84 + i * 36
        body.append(f'<rect x="320" y="{y - 18}" width="24" height="24" rx="6" fill="none" stroke="#9aa5ab" stroke-width="2.5"/>')
        css.append(blink(f"ck{i}", P, [(t, P - 0.1)]))
        body.append(f'<path class="ck{i}" d="M325 {y - 6} l6 7 l12 -14" stroke="#2ecc71" stroke-width="4" fill="none" stroke-linecap="round" stroke-linejoin="round"/>')
        body.append(T(356, y, c))
    return page("Hardware tests: check every part of the board", P, "".join(css), "".join(body))


SCENES = {"memorylights": memory_lights, "whackamole": whack_a_mole, "pingpong": ping_pong,
          "soundmatch": sound_match, "beatgrid": beat_grid, "midipad": midi_pad,
          "ukuleleteacher": ukulele_teacher, "voiceboard": voice_board, "macropad": macro_pad,
          "combopad": combo_pad, "hardwaretest": hardware_test}

if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    for name, make in SCENES.items():
        path = os.path.join(OUT, name + ".svg")
        with open(path, "w", encoding="utf-8") as f:
            f.write(make())
        print(f"{name:16s} {os.path.getsize(path) // 1024} KB")
