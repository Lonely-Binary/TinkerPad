#!/usr/bin/env python3
"""Draws the animated SVGs that show what each example does.

    python3 tools/make_animations.py        ->  docs/images/anim/<name>.svg

Each picture is a TinkerPad seen from above: a 4 x 4 grid of keys whose lights switch on
and off the way the real program does, plus a small device or caption beside it when
that helps. Animations are plain CSS keyframes inside the SVG, so they play on GitHub
and in any browser without scripts.

Keys are numbered 0-15, row by row, 0 top-left - the same numbering as the firmware.
"""
import os

OUT = os.path.join(os.path.dirname(__file__), "..", "docs", "images", "anim")

W, H = 560, 280
KEY, GAP, MARGIN = 52, 10, 21          # board is 260 x 260 at (10, 10)
FADE = 0.05                            # seconds to fade a light in or out

TEAL, TEAL_INK = "#14898F", "#117A80"
PERSON = ["#ff3c00", "#ffaa00", "#aae628", "#32c83c", "#00c8a0", "#00aae6",
          "#2850ff", "#9650ff", "#e632c8", "#ff508c", "#e6dcbe", "#c86e1e"]


def key_xy(pos):
    r, c = divmod(pos, 4)
    return 10 + MARGIN + c * (KEY + GAP), 10 + MARGIN + r * (KEY + GAP)


class Scene:
    def __init__(self, name, period, title):
        self.name, self.T, self.title = name, period, title
        self.css, self.leds, self.extra = [], [], []
        self.n = 0

    # A CSS animation class whose opacity is `level` during each (start, end, level)
    # window and `base` everywhere else.
    def anim(self, events, base=0.0):
        self.n += 1
        cls = f"a{self.n}"
        stops = {0.0: base, 100.0: base}

        def pct(t):
            return round(max(0.0, min(self.T, t)) / self.T * 100, 3)

        for a, b, lvl in sorted(events):
            stops[pct(a - FADE)] = base
            stops[pct(a)] = lvl
            stops[pct(b)] = lvl
            stops[pct(b + FADE)] = base
        frames = " ".join(f"{p}%{{opacity:{v}}}" for p, v in sorted(stops.items()))
        self.css.append(f"@keyframes {cls}{{{frames}}}"
                        f".{cls}{{opacity:{base};animation:{cls} {self.T}s linear infinite}}")
        return cls

    def light(self, pos, color, events, base=0.0):
        """A key's LED: a coloured keycap glow, on during the events."""
        x, y = key_xy(pos)
        cls = self.anim(events, base)
        self.leds.append(
            f'<g class="{cls}"><rect x="{x - 4}" y="{y - 4}" width="{KEY + 8}" height="{KEY + 8}" '
            f'rx="12" fill="{color}" filter="url(#glow)"/>'
            f'<rect x="{x}" y="{y}" width="{KEY}" height="{KEY}" rx="9" fill="{color}"/></g>')

    def press(self, pos, events):
        """A finger press: a white ring on the key."""
        x, y = key_xy(pos)
        cls = self.anim(events)
        self.leds.append(
            f'<rect class="{cls}" x="{x + 3}" y="{y + 3}" width="{KEY - 6}" height="{KEY - 6}" '
            f'rx="7" fill="none" stroke="#ffffff" stroke-width="4"/>')

    def blink(self, pos, color, start, end, on=0.18, off=0.14, level=1.0):
        ev, t = [], start
        while t < end:
            ev.append((t, min(t + on, end), level))
            t += on + off
        self.light(pos, color, ev)

    def show(self, svg, events, base=0.0):
        """Any extra drawing, visible during the events."""
        cls = self.anim(events, base)
        self.extra.append(f'<g class="{cls}">{svg}</g>')

    def always(self, svg):
        self.extra.append(svg)

    def svg(self):
        keys = "".join(
            f'<rect x="{x}" y="{y}" width="{KEY}" height="{KEY}" rx="9" class="cap"/>'
            for x, y in (key_xy(p) for p in range(16)))
        return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" role="img" aria-label="{self.title}">
<title>{self.title}</title>
<defs><filter id="glow" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="7"/></filter></defs>
<style>
.cap{{fill:#2a3136;stroke:#3a4349;stroke-width:1.5}}
.t1{{font:700 21px -apple-system,'Segoe UI',Helvetica,Arial,sans-serif;fill:#1d2b2c}}
.t2{{font:400 16px -apple-system,'Segoe UI',Helvetica,Arial,sans-serif;fill:#4a5859}}
.t3{{font:700 15px -apple-system,'Segoe UI',Helvetica,Arial,sans-serif;fill:{TEAL_INK}}}
.ink{{stroke:#1d2b2c;fill:none}} .inkf{{fill:#1d2b2c}} .pale{{fill:#eef3f3}}
@media (prefers-color-scheme: dark){{
.t1{{fill:#e6edf3}} .t2{{fill:#a9b4bd}} .t3{{fill:#4fc3c9}}
.ink{{stroke:#c9d3d9}} .inkf{{fill:#c9d3d9}} .pale{{fill:#1e2a30}}}}
{"".join(self.css)}
</style>
<rect x="10" y="10" width="260" height="260" rx="20" fill="#14181b"/>
{keys}
{"".join(self.leds)}
{"".join(self.extra)}
</svg>
"""


def text(x, y, s, cls="t2", anchor="start"):
    return f'<text x="{x}" y="{y}" class="{cls}" text-anchor="{anchor}">{s}</text>'


def steps(scene, items):
    """Captions on the right that take turns: (start, end, title, subtitle)."""
    for a, b, t1, t2 in items:
        scene.show(text(300, 120, t1, "t1") + text(300, 148, t2), [(a, b, 1)])


# ----------------------------------------------------------------------- scenes

def memory_lights():
    s = Scene("memorylights", 8.0, "MemoryLights: watch the lights, then play them back")
    seq = [(5, "#ff7a00"), (10, "#00b4ff"), (6, "#9b5cff"), (9, "#2ecc71")]
    for i, (k, col) in enumerate(seq):
        s.light(k, col, [(0.4 + i * 0.7, 0.95 + i * 0.7, 1), (4.4 + i * 0.7, 4.95 + i * 0.7, 1)])
        s.press(k, [(4.4 + i * 0.7, 4.95 + i * 0.7, 1)])
    for k in (12, 13, 14):   # the level row, dim
        s.light(k, ["#2ecc71", "#ffd200", "#ff3b30"][k - 12], [], base=0.25)
    steps(s, [(0.0, 3.7, "1. Watch", "The pads light up one by one."),
              (4.0, 7.6, "2. Repeat", "Press them in the same order.")])
    s.always(text(300, 196, "Every round adds one more step.", "t3"))
    return s


def whack_a_mole():
    s = Scene("whackamole", 4.8, "WhackAMole: hit the moles before they hide")
    moles = [(2, 0.2, 1.0), (9, 0.9, 1.7), (4, 1.7, 2.4), (15, 3.0, 3.8), (7, 3.6, 4.4)]
    for k, a, b in moles:
        s.light(k, "#ff8a00", [(a, b, 1)])
        s.press(k, [(b - 0.25, b, 1)])
    s.blink(13, "#ffd400", 2.2, 3.2, on=0.12, off=0.06)            # golden mole
    s.press(13, [(2.95, 3.2, 1)])
    s.blink(6, "#ff2a2a", 2.5, 3.9, on=0.2, off=0.15)              # bomb: leave it
    s.always(text(300, 92, "Hit the moles!", "t1"))
    s.always('<rect x="300" y="112" width="18" height="18" rx="4" fill="#ff8a00"/>' + text(328, 126, "mole  +1"))
    s.always('<rect x="300" y="140" width="18" height="18" rx="4" fill="#ffd400"/>' + text(328, 154, "golden mole  +3"))
    s.always('<rect x="300" y="168" width="18" height="18" rx="4" fill="#ff2a2a"/>' + text(328, 182, "bomb, leave it!  −5"))
    s.always(text(300, 222, "30 seconds. They get faster.", "t3"))
    return s


def ping_pong():
    s = Scene("pingpong", 3.6, "PingPong: table tennis for two players")
    for r in range(4):
        s.light(r * 4, TEAL, [], base=0.35)
        s.light(r * 4 + 3, "#ff8a00", [], base=0.35)
    # ball: row 1 left to right, then row 2 right to left (a smash changes row)
    path = [(4, 0.0), (5, 0.3), (6, 0.6), (7, 0.9), (11, 1.8), (10, 2.1), (9, 2.4), (8, 2.7)]
    for k, t in path:
        s.light(k, "#ffffff", [(t, t + 0.3, 1)])
    s.press(7, [(0.9, 1.3, 1)])
    s.light(7, "#ff8a00", [(1.0, 1.3, 1)])
    s.press(8, [(2.7, 3.1, 1)])
    s.light(8, TEAL, [(2.8, 3.1, 1)])
    s.always(text(300, 100, "Two players, one board", "t1"))
    s.always(f'<rect x="300" y="122" width="18" height="18" rx="4" fill="{TEAL}"/>' + text(328, 136, "player 1: the left column"))
    s.always('<rect x="300" y="150" width="18" height="18" rx="4" fill="#ff8a00"/>' + text(328, 164, "player 2: the right column"))
    s.always(text(300, 206, "Hit the ball back when it lands", "t3"))
    s.always(text(300, 226, "on your edge. First to 5 wins.", "t3"))
    return s


def sound_match():
    s = Scene("soundmatch", 7.0, "SoundMatch: a memory game for your ears")
    # a matching pair (cat) stays lit, a wrong pair goes dark, then another match
    s.light(1, "#ff9f1c", [(0.3, 7.0, 1)]);  s.press(1, [(0.3, 0.7, 1)])
    s.light(11, "#ff9f1c", [(1.1, 7.0, 1)]); s.press(11, [(1.1, 1.5, 1)])
    s.light(4, "#3a86ff", [(2.3, 3.6, 1)]);  s.press(4, [(2.3, 2.7, 1)])
    s.light(14, "#8ac926", [(3.0, 3.6, 1)]); s.press(14, [(3.0, 3.4, 1)])
    s.light(6, "#3a86ff", [(4.3, 7.0, 1)]);  s.press(6, [(4.3, 4.7, 1)])
    s.light(4, "#3a86ff", [(5.1, 7.0, 1)]);  s.press(4, [(5.1, 5.5, 1)])
    bubble = lambda word: (f'<rect x="300" y="96" width="140" height="44" rx="22" class="pale"/>'
                           + text(370, 124, word, "t1", "middle"))
    s.show(bubble("meow!"), [(0.3, 0.9, 1), (1.1, 1.7, 1)])
    s.show(bubble("woof!"), [(2.3, 2.9, 1), (4.3, 4.9, 1), (5.1, 5.7, 1)])
    s.show(bubble("baa!"), [(3.0, 3.6, 1)])
    s.show(text(300, 170, "A pair! Both stay lit.", "t3"), [(1.5, 2.2, 1), (5.5, 6.9, 1)])
    s.show(text(300, 170, "Not a pair. Remember them!", "t3"), [(3.4, 4.2, 1)])
    s.always(text(300, 218, "16 keys hide 8 pairs of sounds."))
    return s


def beat_grid():
    s = Scene("beatgrid", 3.0, "BeatGrid: a rhythm game shown on your phone")
    notes = [(5, 0.0), (10, 0.75), (7, 1.5), (12, 2.25)]
    px, py = 300, 40
    s.always(f'<rect x="{px}" y="{py}" width="120" height="210" rx="18" class="pale"/>'
             f'<rect x="{px}" y="{py}" width="120" height="210" rx="18" class="ink" stroke-width="3"/>')
    for k, t in notes:
        r, c = divmod(k, 4)
        cx, cy = px + 18 + c * 28, py + 60 + r * 28
        s.light(k, "#ff4fa3", [(t + 0.45, t + 0.75, 1)])
        s.light(k, "#ff4fa3", [(t, t + 0.45, 0.35)])
        s.press(k, [(t + 0.45, t + 0.75, 1)])
        ring = (f'<circle cx="{cx + 10}" cy="{cy + 10}" r="22" fill="none" stroke="#ff4fa3" stroke-width="3">'
                f'<animate attributeName="r" values="26;26;9;9" keyTimes="0;{t / 3.0:.3f};{(t + 0.6) / 3.0:.3f};1" '
                f'dur="3s" repeatCount="indefinite"/></circle>')
        s.show(ring, [(t, t + 0.6, 1)])
    for k in range(16):
        r, c = divmod(k, 4)
        s.always(f'<rect x="{px + 18 + c * 28}" y="{py + 60 + r * 28}" width="20" height="20" rx="4" fill="#ff4fa3" opacity="0.18"/>')
    s.always(text(px + 60, py + 38, "SCORE", "t3", "middle"))
    s.always(text(438, 118, "Press the pad", "t2") + text(438, 140, "when the ring", "t2") + text(438, 162, "lands on it.", "t2"))
    return s


def midi_pad():
    s = Scene("midipad", 2.0, "MidiPad: USB MIDI drum pads")
    for i in range(8):
        t = i * 0.25
        s.light(14, "#ffd166", [(t, t + 0.12, 1)])              # closed hat
    for t in (0.0, 1.0, 1.25):
        s.light(12, "#ef476f", [(t, t + 0.18, 1)]); s.press(12, [(t, t + 0.18, 1)])   # kick
    for t in (0.5, 1.5):
        s.light(13, "#06d6a0", [(t, t + 0.18, 1)]); s.press(13, [(t, t + 0.18, 1)])   # snare
    s.light(0, "#118ab2", [(1.75, 1.95, 1)])                     # crash
    lx, ly = 320, 60
    s.always(f'<rect x="{lx}" y="{ly}" width="200" height="120" rx="8" class="pale"/>'
             f'<rect x="{lx}" y="{ly}" width="200" height="120" rx="8" class="ink" stroke-width="3"/>'
             f'<path d="M{lx - 18} {ly + 132} h236 l-14 14 h-208 z" class="inkf"/>')
    for i, (lane, t) in enumerate([(2, 0.0), (1, 0.0), (0, 0.5), (2, 1.0), (2, 1.25), (0, 1.5)]):
        x = lx + 20 + t * 80
        s.show(f'<rect x="{x}" y="{ly + 24 + lane * 28}" width="22" height="16" rx="3" fill="{["#06d6a0", "#ffd166", "#ef476f"][lane]}"/>',
               [(t, 2.0, 1)])
    s.always(text(lx + 100, ly + 168, "plays drums in GarageBand", "t3", "middle"))
    return s


def ukulele_teacher():
    s = Scene("ukuleleteacher", 4.8, "UkuleleTeacher: learn a song on the ukulele")
    strings = ["#9650ff", "#2850ff", "#32c83c", "#ff8a00"]      # G C E A
    for r in range(4):
        for c in range(4):
            s.light(r * 4 + c, strings[r], [], base=0.22)
    tune = [8, 10, 4, 6, 8, 0]                                    # follow the lights
    for i, k in enumerate(tune):
        t = i * 0.8
        s.blink(k, "#ffffff", t, t + 0.6, on=0.15, off=0.1)
        if i + 1 < len(tune):
            s.light(tune[i + 1], "#ffffff", [(t, t + 0.6, 0.35)])
        s.press(k, [(t + 0.45, t + 0.7, 1)])
    for i, name in enumerate("GCEA"):
        s.always(text(286, 52 + i * 62, name, "t3", "middle"))
    s.always(text(310, 110, "The keys are a fretboard", "t1"))
    s.always(text(310, 138, "White blinks: press here."))
    s.always(text(310, 160, "Soft glow: the next note."))
    s.always(text(310, 200, "Learn one phrase at a time.", "t3"))
    return s


def voice_board():
    s = Scene("voiceboard", 9.0, "VoiceBoard: a family voice message board")
    s.light(12, "#ff3c3c", [(0, 9, 0.25)])                        # MIC, dim when idle
    s.light(12, "#ff3c3c", [(0.8, 2.6, 1)]); s.press(12, [(0.8, 1.2, 1)])
    for k in range(12):
        s.light(k, PERSON[k], [(1.0, 2.6, 0.55)])                 # "who is it for?"
    s.light(15, "#ffc828", [(1.0, 2.6, 0.55)])
    s.press(5, [(2.6, 3.0, 1)])
    s.blink(5, "#ff2020", 2.7, 5.2, on=0.25, off=0.12)            # recording
    s.light(5, "#ffffff", [(5.3, 5.5, 1), (5.7, 5.9, 1)])         # sent
    s.blink(5, PERSON[5], 6.1, 9.0, on=0.5, off=0.12, level=0.9)  # waiting to be heard
    steps(s, [(0.0, 2.5, "1. Tap MIC", "Then tap who it is for."),
              (2.6, 5.2, "2. Talk", "Only their key glows red."),
              (5.3, 6.0, "3. Sent", "Two white blinks."),
              (6.1, 9.0, "4. Waiting", "Their key glows until heard.")])
    s.show(f'<rect x="300" y="174" width="200" height="40" rx="20" class="pale"/>'
           + text(400, 200, "“Dinner at seven!”", "t3", "middle"), [(2.8, 5.1, 1)])
    s.always(text(300, 246, "Several boards stay in sync.", "t2"))
    return s


def macro_pad():
    s = Scene("macropad", 4.5, "MacroPad: a Bluetooth shortcut keyboard")
    for k in range(16):
        s.light(k, TEAL, [], base=0.18)
    lx, ly = 340, 70
    s.always(f'<rect x="{lx}" y="{ly}" width="180" height="110" rx="8" class="pale"/>'
             f'<rect x="{lx}" y="{ly}" width="180" height="110" rx="8" class="ink" stroke-width="3"/>'
             f'<path d="M{lx - 16} {ly + 122} h212 l-12 12 h-188 z" class="inkf"/>')
    for i, (k, keys, what) in enumerate([(0, "⌘ C", "Copy"), (5, "⌘ V", "Paste"), (10, "⌘ ⇧ 4", "Screenshot")]):
        t = 0.3 + i * 1.4
        s.light(k, TEAL, [(t, t + 0.4, 1)]); s.press(k, [(t, t + 0.4, 1)])
        s.show(text(lx + 90, ly + 52, keys, "t1", "middle") + text(lx + 90, ly + 80, what, "t2", "middle"),
               [(t + 0.25, t + 1.3, 1)])
        s.show('<path d="M282 140 q14 -14 0 -28 M292 150 q24 -24 0 -48 M302 160 q34 -34 0 -68" '
               'class="ink" stroke-width="3" stroke-linecap="round"/>', [(t, t + 0.5, 1)])
    s.always(text(lx + 90, ly + 160, "each key sends a shortcut", "t3", "middle"))
    return s


def combo_pad():
    s = Scene("combopad", 4.0, "ComboPad: a fighting-game combo keyboard")
    for r in range(4):
        for c in range(4):
            s.light(r * 4 + c, "#ff4f3a" if c < 2 else "#3a7bff", [], base=0.2)
    s.light(5, "#ff4f3a", [(0.3, 3.4, 1)]); s.press(5, [(0.3, 0.7, 1)])
    moves = ["↓", "↘", "→", "P", "↓", "↙", "←", "K"]
    for i, m in enumerate(moves):
        t = 0.5 + i * 0.3
        s.show(f'<rect x="{300 + i * 30}" y="104" width="26" height="34" rx="6" class="pale"/>'
               + text(313 + i * 30, 128, m, "t1", "middle"), [(t, 3.6, 1)])
    s.always(text(300, 80, "One key = one combo", "t1"))
    s.always(text(300, 176, "Sent as a USB keyboard,", "t2"))
    s.always(text(300, 198, "frame by frame (60 fps).", "t2"))
    s.always(text(300, 238, "Left half faces right, right half left.", "t3"))
    return s


def hardware_test():
    s = Scene("hardwaretest", 6.0, "Hardware tests: check every part of the board")
    for k in range(16):
        s.light(k, "#ff2a2a", [(0.0, 1.0, 1)])
        s.light(k, "#22dd55", [(1.1, 2.1, 1)])
        s.light(k, "#2a6bff", [(2.2, 3.2, 1)])
        t = 3.4 + k * 0.12
        s.light(k, "#ffffff", [(t, t + 0.18, 1)])
    s.always(text(300, 92, "Test each part", "t1"))
    for i, line in enumerate(["16 keys and 16 LEDs", "Speaker and microphone",
                              "PSRAM and the I²C bus"]):
        s.always(f'<circle cx="309" cy="{124 + i * 30}" r="6" fill="{TEAL}"/>' + text(324, 129 + i * 30, line))
    s.always(text(300, 236, "Run these first.", "t3"))
    return s


SCENES = [memory_lights, whack_a_mole, ping_pong, sound_match, beat_grid, midi_pad,
          ukulele_teacher, voice_board, macro_pad, combo_pad, hardware_test]

if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    for make in SCENES:
        sc = make()
        path = os.path.join(OUT, sc.name + ".svg")
        with open(path, "w", encoding="utf-8") as f:
            f.write(sc.svg())
        print(f"{path}  {os.path.getsize(path) // 1024} KB")
