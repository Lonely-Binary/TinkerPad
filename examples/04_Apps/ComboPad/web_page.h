// web_page.h - the ComboPad setup page (HTML, CSS and JavaScript in one string)
//
// The page does all the thinking: it turns combo notation like "2MK > 236HP"
// into frame-by-frame steps, mirrors them for the right half of the keypad,
// and saves everything to the board as plain text.

#pragma once

const char WEB_PAGE[] = R"rawpage(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>ComboPad</title>
<style>
:root{
  --bg:#f5f6f7;--card:#fff;--text:#16191c;--muted:#6b7378;--line:#e3e6e8;--chip:#eef0f2;
  --accent:#117A80;--accent-soft:#e3f1f2;--right:#d9771a;--right-soft:#fbeedf;--danger:#c2372c;
}
@media (prefers-color-scheme:dark){:root{
  --bg:#111416;--card:#1b1f22;--text:#eef1f2;--muted:#8d969b;--line:#2b3135;--chip:#262c30;
  --accent:#3fb3b9;--accent-soft:#173235;--right:#f0954a;--right-soft:#35281c;--danger:#ef6b5f;
}}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{margin:0;background:var(--bg);color:var(--text);
  font:15px/1.45 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif}
main{max-width:560px;margin:0 auto;padding:20px 16px 56px}
header{display:flex;align-items:baseline;justify-content:space-between;margin-bottom:14px}
h1{font-size:22px;margin:0}
.saved{font-size:13px;color:var(--muted)}
.seg{display:inline-flex;background:var(--chip);border-radius:10px;padding:3px}
.seg button{border:0;background:none;color:var(--muted);font:inherit;font-size:14px;padding:6px 12px;border-radius:8px;cursor:pointer}
.seg button.on{background:var(--card);color:var(--text);box-shadow:0 1px 2px rgba(0,0,0,.12)}
.row{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:14px}
.label{font-size:13px;color:var(--muted)}
/* keypad */
.sides{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:6px;font-size:12px;font-weight:600}
.sides span:first-child{color:var(--accent)} .sides span:last-child{color:var(--right);text-align:right}
.grid{display:grid;grid-template-columns:repeat(4,1fr);gap:8px}
.key{position:relative;aspect-ratio:1;border:1px solid var(--line);border-radius:12px;background:var(--card);
  cursor:pointer;padding:20px 6px 6px;font:inherit;color:var(--text);text-align:center;overflow:hidden;
  display:flex;flex-direction:column;align-items:center;justify-content:center;gap:3px;transition:transform .1s}
.key:active{transform:scale(.97)}
.key .n{position:absolute;top:6px;left:8px;font-size:11px;color:var(--muted)}
.key .bar{position:absolute;top:0;left:0;right:0;height:3px;background:var(--accent)}
.key.r .bar{background:var(--right)}
.key .name{font-size:12px;font-weight:600;line-height:1.2;display:-webkit-box;-webkit-line-clamp:2;-webkit-box-orient:vertical;overflow:hidden}
.key .src{font-size:11px;color:var(--muted);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:100%}
.key .empty{color:var(--line);font-size:22px}
.key.r{background:var(--right-soft);border-color:transparent}
.key.mid{margin-right:6px}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:16px;margin-top:18px}
.card h2{font-size:15px;margin:0 0 12px}
.switch{position:relative;width:44px;height:26px;flex:none}
.switch input{opacity:0;width:0;height:0}
.switch span{position:absolute;inset:0;background:var(--line);border-radius:13px;transition:.2s;cursor:pointer}
.switch span:before{content:"";position:absolute;width:20px;height:20px;left:3px;top:3px;background:#fff;border-radius:50%;transition:.2s}
.switch input:checked+span{background:var(--accent)}
.switch input:checked+span:before{transform:translateX(18px)}
.maps{display:grid;grid-template-columns:repeat(2,1fr);gap:8px}
.map{display:flex;justify-content:space-between;align-items:center;border:1px solid var(--line);border-radius:10px;padding:6px 6px 6px 10px;font-size:14px}
.map select,.map button{width:auto;min-width:84px;padding:5px 8px;font-size:13px}
.map button.listen{border-color:var(--accent);background:var(--accent-soft)}
.times{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}
input,select,textarea{width:100%;font:inherit;color:var(--text);background:var(--bg);border:1px solid var(--line);border-radius:10px;padding:9px 11px;outline:none}
input:focus,select:focus,textarea:focus{border-color:var(--accent)}
textarea{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:16px;resize:vertical;min-height:64px}
.btn{border:0;border-radius:10px;padding:10px 14px;font:inherit;font-weight:600;cursor:pointer;background:var(--chip);color:var(--text)}
.btn.primary{background:var(--accent);color:#fff}
.btn.ghost{background:none;color:var(--muted);padding:6px}
.btn.rec{background:var(--danger);color:#fff}
.small{font-size:12px;color:var(--muted);margin:6px 0 0}
.err{color:var(--danger)}
/* editor sheet */
.shade{position:fixed;inset:0;background:rgba(0,0,0,.35);display:none;align-items:flex-end;justify-content:center;z-index:10}
.shade.open{display:flex}
@media (min-width:640px){.shade{align-items:center}}
.sheet{background:var(--card);width:100%;max-width:480px;border-radius:18px 18px 0 0;padding:18px 16px calc(14px + env(safe-area-inset-bottom));
  max-height:94vh;overflow:auto}
@media (min-width:640px){.sheet{border-radius:18px}}
.sheet-head{display:flex;justify-content:space-between;align-items:center;margin-bottom:12px}
.sheet-head h2{margin:0;font-size:18px}
.tabs{display:flex;gap:4px;margin:12px 0}
.tabs button{flex:1}
.pane[hidden]{display:none}
.moves{display:flex;flex-direction:column;gap:6px;max-height:240px;overflow:auto}
.move{display:flex;justify-content:space-between;gap:8px;text-align:left;border:1px solid var(--line);background:none;border-radius:10px;
  padding:9px 11px;font:inherit;color:var(--text);cursor:pointer}
.move:hover{border-color:var(--accent)}
.move code{color:var(--muted);font-size:13px}
.pad{display:grid;grid-template-columns:repeat(3,44px) 1fr;gap:6px;margin-top:10px}
.pad .dirs{display:contents}
.pad button{height:40px;border:1px solid var(--line);background:var(--bg);border-radius:9px;font:inherit;font-size:17px;color:var(--text);cursor:pointer}
.pad .btns{grid-column:4;grid-row:1/4;display:grid;grid-template-columns:repeat(3,1fr);gap:6px}
.pad .btns button{font-size:13px;font-weight:600}
.timeline{border:1px solid var(--line);border-radius:12px;margin-top:14px;max-height:230px;overflow:auto}
.step{display:grid;grid-template-columns:28px 1fr 70px 30px;align-items:center;gap:6px;padding:4px 8px;border-bottom:1px solid var(--line)}
.step:last-child{border-bottom:0}
.step .i{font-size:11px;color:var(--muted)}
.step .in{font-size:15px}
.step .in b{font-size:12px;background:var(--chip);border-radius:5px;padding:1px 5px;margin-left:3px}
.step input{padding:4px 6px;font-size:14px;text-align:right}
.step .x{border:0;background:none;color:var(--muted);cursor:pointer;font-size:16px}
.tl-head{display:flex;justify-content:space-between;font-size:12px;color:var(--muted);margin-top:14px}
.actions{display:flex;gap:8px;margin-top:14px}
.actions .grow{flex:1}
.recbox{border:2px dashed var(--line);border-radius:14px;padding:18px;text-align:center;outline:none}
.recbox.on{border-color:var(--danger)}
.toast{position:fixed;left:50%;bottom:24px;transform:translateX(-50%);background:var(--text);color:var(--bg);padding:9px 16px;
  border-radius:20px;font-size:14px;opacity:0;transition:opacity .2s;pointer-events:none;z-index:20}
.toast.show{opacity:1}
details summary{cursor:pointer;font-weight:600;font-size:15px}
.wifibar{display:flex;align-items:center;justify-content:space-between;gap:10px;background:var(--accent-soft);
  border-radius:12px;padding:8px 8px 8px 14px;margin-bottom:16px;font-size:14px}
.wifibar.warn{background:var(--right-soft)}
.wifibar b{font-variant-numeric:tabular-nums}
.wifibar .btn{padding:7px 12px;font-size:13px;background:var(--card)}
.btn.danger{background:none;border:1px solid var(--danger);color:var(--danger)}
</style>
</head>
<body>
<main>
  <header><h1>ComboPad</h1><span class="saved" id="saved"></span></header>
  <div class="wifibar" id="wifibar" hidden>
    <span id="wifiText"></span>
    <button class="btn" id="resetTimer">Reset timer</button>
  </div>

  <div class="row">
    <span class="label">Game</span>
    <div class="seg" id="game"><button data-g="kof">King of Fighters '98</button><button data-g="sf6">Street Fighter 6</button></div>
  </div>

  <div class="row">
    <span class="label">Character</span>
    <select id="mainChar" style="width:auto;min-width:200px"></select>
  </div>

  <div class="row">
    <span class="label">Mirror layout — right half plays the left half facing left</span>
    <label class="switch"><input type="checkbox" id="mirror"><span></span></label>
  </div>

  <div class="sides" id="sides"><span>→ Facing right</span><span>Facing left ←</span></div>
  <div class="grid" id="grid"></div>
  <p class="small">Tap a key to set its combo. Presets are a starting point: try them in training mode and adjust the frames.</p>

  <section class="card">
    <h2>Game keys</h2>
    <p class="small" style="margin:-6px 0 12px">Set these to the keys your game uses for player 1. ComboPad presses these keys.</p>
    <div class="maps" id="maps"></div>
  </section>

  <section class="card">
    <details>
      <summary>Default timing (frames, 60 = 1 second)</summary>
      <p class="small">Used when a combo is built from notation.</p>
      <div class="times" id="times" style="margin-top:10px"></div>
    </details>
  </section>

  <section class="card">
    <details>
      <summary>How to write combos</summary>
      <p class="small">Directions use the numpad, with your character facing right:<br>
      <code>7 8 9 = ↖ ↑ ↗ &nbsp; 4 5 6 = ← · → &nbsp; 1 2 3 = ↙ ↓ ↘</code></p>
      <p class="small"><code>236HP</code> quarter-circle forward + HP &nbsp; <code>[4]6P</code> charge back, then forward + P<br>
      <code>&gt;</code> cancel into the next move &nbsp; <code>,</code> or space: link (wait for the move to finish)<br>
      <code>j.HK</code> jump in with HK &nbsp; <code>w20</code> wait 20 frames &nbsp; <code>236A x3</code> do it 3 times<br>
      <code>LP+LK</code> or <code>AB</code> press together &nbsp; <code>P</code>/<code>K</code> = strongest punch/kick &nbsp; <code>PP</code> two punches</p>
    </details>
  </section>

  <section class="card">
    <div class="row" style="margin:0">
      <div><b>Reset to defaults</b><p class="small" style="margin:2px 0 0">Forget every combo and setting.</p></div>
      <button class="btn danger" id="factory">Reset</button>
    </div>
  </section>
</main>

<div class="shade" id="shade">
  <div class="sheet" role="dialog" aria-modal="true">
    <div class="sheet-head"><h2 id="title">Key 1</h2><button class="btn ghost" id="close">Cancel</button></div>
    <input id="name" placeholder="Name, e.g. Hadoken" maxlength="40">

    <div class="tabs seg" id="tabs" style="display:flex">
      <button data-t="preset">Characters</button><button data-t="notation">Notation</button><button data-t="record" id="recTab">Record</button>
    </div>

    <div class="pane" id="pane-preset">
      <select id="char"></select>
      <div class="moves" id="moves" style="margin-top:8px"></div>
    </div>

    <div class="pane" id="pane-notation">
      <textarea id="src" placeholder="e.g. 2MK > 236HP" spellcheck="false" autocapitalize="characters"></textarea>
      <div class="small err" id="srcErr"></div>
      <div class="pad" id="pad"></div>
    </div>

    <div class="pane" id="pane-record">
      <div class="recbox" id="recbox" tabindex="0">
        <div id="recText">Uses your <b>Game keys</b>. Press Start, play the combo on your keyboard, then press Stop.</div>
        <div class="actions" style="justify-content:center"><button class="btn rec" id="recBtn">Start recording</button></div>
      </div>
    </div>

    <div class="tl-head"><span>Steps</span><span id="total"></span></div>
    <div class="timeline" id="timeline"></div>

    <div class="actions">
      <button class="btn" id="clear">Clear</button>
      <button class="btn" id="test">Test</button>
      <button class="btn primary grow" id="done">Done</button>
    </div>
  </div>
</div>
<div class="toast" id="toast"></div>

<script>
// ================= Game data =================
const HID = {Up:0x52,Down:0x51,Left:0x50,Right:0x4F,W:0x1A,A:0x04,S:0x16,D:0x07,U:0x18,I:0x0C,O:0x12,J:0x0D,K:0x0E,L:0x0F,Z:0x1D,X:0x1B,C:0x06,V:0x19};
const GAMES = {
  kof: {
    buttons: ["A","B","C","D"],
    names: {A:1,B:2,C:4,D:8,P:4,K:8},
    groups: {PP:1|4, KK:2|8},
    map: [HID.Up,HID.Down,HID.Left,HID.Right,HID.Z,HID.X,HID.C,HID.V,0,0],
    chars: {
      "Kyo Kusanagi": [
        ["108 Shiki: Yami Barai","236C"], ["100 Shiki: Oniyaki","623C"], ["75 Shiki: Kai","236D, 236D"],
        ["R.E.D. Kick","214D"], ["Ura 108 Shiki: Orochinagi","2141236C"], ["Combo: low B, low C, Oniyaki","2B 2C > 623C"]],
      "Iori Yagami": [
        ["108 Shiki: Yami Barai","236C"], ["100 Shiki: Oniyaki","623C"], ["127 Shiki: Aoi Hana (x3)","236A x3"],
        ["Kin 1211 Shiki: Ya Otome","2363214C"], ["Combo: low B, low A, Aoi Hana","2B 2A > 236A x3"]],
      "Terry Bogard": [
        ["Power Wave","236A"], ["Burn Knuckle","214A"], ["Rising Tackle","[2]8A"], ["Crack Shoot","214B"],
        ["Power Dunk","623D"], ["Combo: low B, low A, Burn Knuckle","2B 2A > 214A"]],
      "Ryo Sakazaki": [
        ["Ko-Ou Ken","236A"], ["Kohou","623C"], ["Haoh Shokoh Ken","641236C"], ["Combo: low B, low A, Kohou","2B 2A > 623C"]],
      "Everyone": [["Roll forward","AB"], ["Blowback attack","CD"], ["Dash","66"], ["Back step","44"]],
    },
  },
  sf6: {
    buttons: ["LP","MP","HP","LK","MK","HK"],
    names: {LP:1,MP:2,HP:4,LK:8,MK:16,HK:32,P:4,K:32},
    groups: {PP:2|4, KK:16|32, PPP:7, KKK:56},
    map: [HID.W,HID.S,HID.A,HID.D,HID.U,HID.I,HID.O,HID.J,HID.K,HID.L],
    chars: {
      "Ryu": [
        ["Hadoken","236HP"], ["Shoryuken","623HP"], ["Tatsumaki Senpukyaku","214HK"], ["OD Hadoken","236PP"],
        ["Shinku Hadoken (SA1)","236236P"], ["Shin Hashogeki (SA2)","214214P"], ["Shin Shoryuken (SA3)","236236K"],
        ["Combo: low MK, Hadoken","2MK > 236HP"]],
      "Ken": [
        ["Hadoken","236HP"], ["Shoryuken","623HP"], ["Tatsumaki Senpukyaku","214HK"], ["Jinrai Kick","236HK"],
        ["Dragonlash Flame (SA1)","236236K"], ["Shippu Jinraikyaku (SA2)","214214K"], ["Shinryuken (SA3)","236236P"],
        ["Combo: low MK, Hadoken","2MK > 236HP"]],
      "Chun-Li": [
        ["Kikoken","[4]6HP"], ["Spinning Bird Kick","[2]8HK"], ["Hyakuretsukyaku","236HK"], ["Kikosho","236236P"],
        ["Combo: low MK, Hyakuretsukyaku","2MK > 236HK"]],
      "Guile": [["Sonic Boom","[4]6HP"], ["Somersault Kick","[2]8HK"]],
      "Cammy": [
        ["Spiral Arrow","236HK"], ["Cannon Spike","623HK"], ["Hooligan Combination","236HP"], ["Spin Drive Smasher (SA1)","236236K"],
        ["Combo: low MK, Spiral Arrow","2MK > 236HK"]],
      "Luke": [
        ["Sand Blast","236HP"], ["Rising Uppercut","623HP"], ["Flash Knuckle","214HP"], ["Vulcan Blast (SA1)","236236P"],
        ["Pale Rider (SA3)","236236K"], ["Combo: low MK, Sand Blast","2MK > 236HP"]],
      "Everyone": [["Drive Impulse","HP+HK"], ["Drive Parry","MP+MK"], ["Throw","LP+LK"], ["Dash","66"], ["Back dash","44"]],
    },
  },
};
const MAP_NAMES = ["Up","Down","Left","Right"];
const TIMING = [["dir","Direction",2],["button","Button",3],["cancel","Cancel gap",2],["link","Link gap",14],["charge","Charge",45],["jump","Jump",24]];
const ARROW = {1:"↙",2:"↓",3:"↘",4:"←",5:"·",6:"→",7:"↖",8:"↑",9:"↗"};
const MIRROR = {1:3,2:2,3:1,4:6,5:5,6:4,7:9,8:8,9:7};

// Keyboard keys a game can use: [browser code, HID code, label]
const KEYS = [];
for (let i = 0; i < 26; i++) { const L = String.fromCharCode(65 + i); KEYS.push(["Key" + L, 4 + i, L]); }
for (let i = 1; i <= 9; i++) KEYS.push(["Digit" + i, 0x1D + i, "" + i]);
KEYS.push(["Digit0",0x27,"0"]);
[["ArrowUp",0x52,"↑"],["ArrowDown",0x51,"↓"],["ArrowLeft",0x50,"←"],["ArrowRight",0x4F,"→"],["Space",0x2C,"Space"],
 ["Enter",0x28,"Enter"],["Tab",0x2B,"Tab"],["Backspace",0x2A,"Backspace"],["Comma",0x36,","],["Period",0x37,"."],
 ["Slash",0x38,"/"],["Semicolon",0x33,";"],["Quote",0x34,"'"],["BracketLeft",0x2F,"["],["BracketRight",0x30,"]"],
 ["Minus",0x2D,"-"],["Equal",0x2E,"="],["Numpad0",0x62,"Num 0"],["Numpad1",0x59,"Num 1"],["Numpad2",0x5A,"Num 2"],
 ["Numpad3",0x5B,"Num 3"],["Numpad4",0x5C,"Num 4"],["Numpad5",0x5D,"Num 5"],["Numpad6",0x5E,"Num 6"],
 ["Numpad7",0x5F,"Num 7"],["Numpad8",0x60,"Num 8"],["Numpad9",0x61,"Num 9"]].forEach(k => KEYS.push(k));
const keyLabel = hid => hid ? ((KEYS.find(k => k[1] === hid) || [0,0,"#" + hid])[2]) : "—";

// ================= State =================
// slots[i] = {name, src, steps:[{d,b,f}]} for each of the 16 keys
let S = {game:"kof", char:"Kyo Kusanagi", mirror:true, map:GAMES.kof.map.slice(), timing:{}, slots:[]};
TIMING.forEach(t => S.timing[t[0]] = t[2]);
for (let i = 0; i < 16; i++) S.slots.push(emptySlot());
function emptySlot(){ return {name:"", src:"", steps:[]}; }

const $ = id => document.getElementById(id);
const isDesktop = matchMedia("(hover:hover) and (pointer:fine)").matches;
const G = () => GAMES[S.game];
const esc = t => String(t).replace(/[&<>"]/g, c => ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;"}[c]));

// In mirror layout, keys in the right two columns copy the key opposite them
function isMirrorKey(i){ return S.mirror && i % 4 >= 2; }
function sourceOf(i){ return isMirrorKey(i) ? i - i % 4 + (3 - i % 4) : i; }

// ================= Notation -> steps =================
function parseButtons(text){
  const g = G();
  if (!text) return 0;
  let mask = 0;
  const names = Object.keys(g.names).sort((a, b) => b.length - a.length);
  for (const part of text.split("+")) {
    if (!part) continue;
    if (g.groups[part] !== undefined) { mask |= g.groups[part]; continue; }
    let s = part;
    while (s.length) {
      const n = names.find(x => s.startsWith(x));
      if (!n) return null;
      mask |= g.names[n]; s = s.slice(n.length);
    }
  }
  return mask;
}

function compile(src){
  const t = S.timing, steps = [], errors = [];
  const push = (d, b, f) => {
    if (f <= 0) return;
    const last = steps[steps.length - 1];
    if (last && last.d === d && last.b === b && last.f + f <= 255) last.f += f;
    else steps.push({d, b, f: Math.min(f, 255)});
  };
  const pushDir = d => {
    const last = steps[steps.length - 1];
    if (last && last.d === d && !last.b) push(5, 0, 1);      // "66": let go between taps
    push(d, 0, t.dir);
  };
  const tokens = src.replace(/\s+x(\d)/gi, "x$1").replace(/xx/gi, " > ").replace(/~/g, " > ").replace(/([>,])/g, " $1 ").trim().split(/\s+/).filter(Boolean);
  let sep = null, any = false;
  for (const tok of tokens) {
    if (tok === ">") { sep = "cancel"; continue; }
    if (tok === ",") { sep = "link"; continue; }
    if (/^w\d+$/i.test(tok)) { push(5, 0, +tok.slice(1)); sep = null; any = false; continue; }   // the wait is the gap
    const m = tok.match(/^(j\.)?(?:\[([1-9])\])?([1-9]*)([A-Za-z+]*?)(?:x(\d+))?$/i);
    const mask = m ? parseButtons(m[4].toUpperCase()) : null;
    if (!m || mask === null || (!m[1] && !m[2] && !m[3] && !m[4])) { errors.push(tok); continue; }
    const reps = Math.min(+(m[5] || 1), 9);
    for (let r = 0; r < reps; r++) {
      if (any) push(5, 0, sep === "cancel" && r === 0 ? t.cancel : t.link);   // gap between moves
      if (m[1]) { push(9, 0, 3); push(5, 0, t.jump); }                         // jump forward
      if (m[2]) push(+m[2], 0, t.charge);                                       // charge
      const dirs = m[3].split("").map(Number);
      if (mask) {
        dirs.slice(0, -1).forEach(pushDir);
        push(dirs.length ? dirs[dirs.length - 1] : 5, mask, t.button);
      } else dirs.forEach(pushDir);
      any = true;
    }
    sep = null;
  }
  if (steps.length > 96) errors.push("too long (max 96 steps)");
  return {steps: steps.slice(0, 96), errors};
}

// ================= Showing steps =================
function inputHtml(s){
  const g = G();
  let h = ARROW[s.d];
  g.buttons.forEach((n, i) => { if (s.b & (1 << i)) h += "<b>" + n + "</b>"; });
  return h;
}
function frames(steps){ return steps.reduce((a, s) => a + s.f, 0); }
function mirrored(steps){ return steps.map(s => ({d: MIRROR[s.d], b: s.b, f: s.f})); }

// ================= Main screen =================
function renderGrid(){
  $("sides").style.visibility = S.mirror ? "visible" : "hidden";
  let html = "";
  for (let i = 0; i < 16; i++) {
    const src = S.slots[sourceOf(i)], r = isMirrorKey(i);
    html += '<button class="key' + (r ? " r" : "") + (S.mirror && i % 4 === 1 ? " mid" : "") + '" data-i="' + i + '">' +
      '<span class="bar"></span><span class="n">' + (i + 1) + '</span>' +
      (src.steps.length
        ? '<span class="name">' + esc(src.name || "Combo") + '</span><span class="src">' + (r ? "↔ mirror of " + (sourceOf(i) + 1) : esc(src.src || "recorded")) + '</span>'
        : '<span class="empty">' + (r ? "↔" : "+") + '</span>') +
      '</button>';
  }
  $("grid").innerHTML = html;
}
function renderGame(){
  document.querySelectorAll("#game button").forEach(b => b.classList.toggle("on", b.dataset.g === S.game));
  if (!fighters().includes(S.char)) S.char = fighters()[0];
  $("mainChar").innerHTML = fighters().map(c => '<option' + (c === S.char ? " selected" : "") + '>' + esc(c) + '</option>').join("");
  $("mirror").checked = S.mirror;
}
let listening = -1;
function renderMaps(){
  const names = MAP_NAMES.concat(G().buttons);
  $("maps").innerHTML = names.map((n, i) => {
    const v = S.map[i];
    const ctl = isDesktop
      ? '<button class="btn' + (listening === i ? " listen" : "") + '" data-m="' + i + '">' + (listening === i ? "Press…" : esc(keyLabel(v))) + '</button>'
      : '<select data-m="' + i + '"><option value="0">—</option>' + KEYS.map(k => '<option value="' + k[1] + '"' + (k[1] === v ? " selected" : "") + '>' + esc(k[2]) + '</option>').join("") + '</select>';
    return '<div class="map"><span>' + n + '</span>' + ctl + '</div>';
  }).join("");
}
function renderTimes(){
  $("times").innerHTML = TIMING.map(t => '<label class="small" style="margin:0">' + t[1] +
    '<input type="number" min="1" max="255" data-t="' + t[0] + '" value="' + S.timing[t[0]] + '"></label>').join("");
}
function renderAll(){ renderGame(); renderGrid(); renderMaps(); renderTimes(); }

// ================= Editor =================
let editing = -1, draft = null, tab = "preset";
function openEditor(i){
  editing = sourceOf(i);
  draft = JSON.parse(JSON.stringify(S.slots[editing]));
  $("title").textContent = "Key " + (editing + 1) + (S.mirror ? " (and mirror " + (editing - editing % 4 + 3 - editing % 4 + 1) + ")" : "");
  $("name").value = draft.name;
  $("src").value = draft.src;
  $("srcErr").textContent = "";
  $("recTab").hidden = !isDesktop;
  buildChars(); buildPad();
  setTab(draft.src || !draft.steps.length ? (draft.steps.length ? "notation" : "preset") : (isDesktop ? "record" : "notation"));
  renderTimeline();
  $("shade").classList.add("open");
}
function closeEditor(){ stopRecording(); $("shade").classList.remove("open"); editing = -1; }
function setTab(t){
  tab = t;
  document.querySelectorAll("#tabs button").forEach(b => b.classList.toggle("on", b.dataset.t === t));
  ["preset","notation","record"].forEach(p => $("pane-" + p).hidden = p !== t);
}
function buildChars(){
  const chars = Object.keys(G().chars);
  $("char").innerHTML = chars.map(c => '<option>' + esc(c) + '</option>').join("");
  $("char").value = chars.includes(S.char) ? S.char : chars[0];
  buildMoves();
}
function buildMoves(){
  $("moves").innerHTML = G().chars[$("char").value].map((m, n) =>
    '<button class="move" data-n="' + n + '"><span>' + esc(m[0]) + '</span><code>' + esc(m[1]) + '</code></button>').join("");
}
function buildPad(){
  const dirs = [7,8,9,4,5,6,1,2,3].map(d => '<button data-ins="' + d + '">' + ARROW[d] + '</button>').join("");
  const btns = G().buttons.concat([">", ",", "⌫"]).map(b => '<button data-ins="' + b + '">' + b + '</button>').join("");
  $("pad").innerHTML = '<div class="dirs">' + dirs + '</div><div class="btns">' + btns + '</div>';
}
function setSource(src){
  draft.src = src;
  const r = compile(src);
  draft.steps = r.steps;
  $("srcErr").textContent = r.errors.length ? "Not understood: " + r.errors.join("  ") : "";
  renderTimeline();
}
function renderTimeline(){
  const st = draft.steps;
  $("total").textContent = st.length ? st.length + " steps · " + frames(st) + " frames · " + (frames(st) / 60).toFixed(2) + " s" : "";
  $("timeline").innerHTML = st.length ? st.map((s, i) =>
    '<div class="step"><span class="i">' + (i + 1) + '</span><span class="in">' + inputHtml(s) + '</span>' +
    '<input type="number" min="1" max="255" value="' + s.f + '" data-s="' + i + '"><button class="x" data-del="' + i + '" title="Remove">×</button></div>').join("")
    : '<div class="small" style="padding:12px">No steps yet.</div>';
}

// ---- Recording on the computer keyboard ----
let rec = null;
function stateFromHeld(held){
  const has = i => S.map[i] && held.has(S.map[i]);
  const v = has(0) ? 1 : has(1) ? -1 : 0, h = has(3) ? 1 : has(2) ? -1 : 0;
  const d = 5 + h + v * 3;
  let b = 0;
  for (let i = 0; i < 6; i++) if (has(4 + i)) b |= 1 << i;
  return {d, b};
}
function startRecording(){
  rec = {held:new Set(), last:null, t:0, steps:[]};
  $("recBtn").textContent = "Stop"; $("recbox").classList.add("on"); $("recbox").focus();
  $("recText").innerHTML = "Recording… play the combo now.";
}
function stopRecording(){
  if (!rec) return;
  const r = rec; rec = null;
  $("recBtn").textContent = "Start recording"; $("recbox").classList.remove("on");
  $("recText").innerHTML = "Uses your <b>Game keys</b>. Press Start, play the combo on your keyboard, then press Stop.";
  if (r.last) r.steps.push({d:r.last.d, b:r.last.b, f:Math.max(1, Math.round((performance.now() - r.t) / 16.667))});
  while (r.steps.length && r.steps[r.steps.length - 1].d === 5 && !r.steps[r.steps.length - 1].b) r.steps.pop();
  if (r.steps.length) {
    draft.steps = r.steps.slice(0, 96).map(s => ({d:s.d, b:s.b, f:Math.min(s.f, 255)}));
    draft.src = "";
    renderTimeline();
  }
}
function recordEvent(e, down){
  const k = KEYS.find(x => x[0] === e.code);
  if (!k || !S.map.includes(k[1])) return;
  e.preventDefault();
  if (e.repeat) return;
  down ? rec.held.add(k[1]) : rec.held.delete(k[1]);
  const now = performance.now(), st = stateFromHeld(rec.held);
  if (rec.last) {
    const f = Math.max(1, Math.round((now - rec.t) / 16.667));
    if (rec.steps.length || rec.last.d !== 5 || rec.last.b) rec.steps.push({d:rec.last.d, b:rec.last.b, f});
  }
  rec.last = st; rec.t = now;
}

// ================= Saving =================
function clean(t){ return String(t).replace(/[|\n\r]/g, " ").trim(); }
function serialize(){
  const lines = ["game " + S.game, "char " + S.char, "mirror " + (S.mirror ? 1 : 0),
    "timing " + TIMING.map(t => S.timing[t[0]]).join(" "), "map " + S.map.join(" ")];
  for (let i = 0; i < 16; i++) {
    const src = S.slots[sourceOf(i)];
    const steps = isMirrorKey(i) ? mirrored(src.steps) : src.steps;
    lines.push("k" + i + " " + clean(src.name) + "|" + clean(src.src) + "|" + steps.map(s => s.d + "." + s.b + "." + s.f).join(" "));
  }
  return lines.join("\n");
}
function deserialize(text){
  for (const line of text.split("\n")) {
    const sp = line.indexOf(" "), key = line.slice(0, sp), rest = line.slice(sp + 1);
    if (key === "game" && GAMES[rest]) S.game = rest;
    else if (key === "char") S.char = rest;
    else if (key === "mirror") S.mirror = rest === "1";
    else if (key === "timing") rest.split(" ").forEach((v, i) => TIMING[i] && (S.timing[TIMING[i][0]] = +v));
    else if (key === "map") S.map = rest.split(" ").map(Number);
    else if (/^k\d+$/.test(key)) {
      const [name, src, steps] = rest.split("|");
      S.slots[+key.slice(1)] = {name, src, steps: (steps || "").trim() ? steps.trim().split(" ").map(x => { const [d, b, f] = x.split(".").map(Number); return {d, b, f}; }) : []};
    }
  }
}
let saveTimer;
function save(){
  $("saved").textContent = "Saving…";
  clearTimeout(saveTimer);
  saveTimer = setTimeout(async () => {
    try {
      const r = await fetch("/api/save", {method:"POST", body:serialize()});
      if (!r.ok) throw 0;
      $("saved").textContent = "Saved";
    } catch (e) { $("saved").textContent = "Not saved"; toast("Couldn't reach ComboPad"); }
  }, 400);
}
function toast(t){
  $("toast").textContent = t; $("toast").classList.add("show");
  clearTimeout(toast.timer); toast.timer = setTimeout(() => $("toast").classList.remove("show"), 1800);
}

// First start: fill the left half with presets for the first character
// Fighters of the current game ("Everyone" is shared moves, not a fighter)
function fighters(){ return Object.keys(G().chars).filter(c => c !== "Everyone"); }

// Put the chosen character's moves (then the shared moves) onto the keypad
function defaults(){
  if (!G().chars[S.char]) S.char = fighters()[0];
  const moves = G().chars[S.char].concat(G().chars.Everyone || []);
  for (let i = 0; i < 16; i++) S.slots[i] = emptySlot();
  let n = 0;
  for (let i = 0; i < 16 && n < moves.length; i++) {
    if (S.mirror && i % 4 >= 2) continue;
    const r = compile(moves[n][1]);
    S.slots[i] = {name: moves[n][0], src: moves[n][1], steps: r.steps};
    n++;
  }
}

// ================= Events =================
$("grid").onclick = e => { const b = e.target.closest(".key"); if (b) openEditor(+b.dataset.i); };
// True when every key holds a preset move as it came (nothing the user made)
function onlyPresets(){
  const all = [];
  Object.values(G().chars).forEach(list => list.forEach(m => all.push(m[0] + "|" + m[1])));
  return S.slots.every((sl, i) => isMirrorKey(i) || !sl.steps.length ||
    (all.includes(sl.name + "|" + sl.src) && JSON.stringify(sl.steps) === JSON.stringify(compile(sl.src).steps)));
}
// Load a character's moves, but ask first if that would replace the user's own combos
function loadCharacter(){
  if (onlyPresets() || confirm("Replace the keys with " + S.char + "'s moves? Your own combos will be lost.")) {
    defaults(); toast(S.char + "'s moves are on the keypad");
  }
  renderAll(); save();
}
$("game").onclick = e => {
  const g = e.target.dataset.g; if (!g || g === S.game) return;
  const wasPreset = onlyPresets();
  const usedDefault = S.map.join() === G().map.join();
  S.game = g;
  if (usedDefault) S.map = G().map.slice();
  S.char = fighters()[0];
  if (wasPreset || confirm("Replace the keys with " + S.char + "'s moves? Your own combos will be lost.")) {
    defaults(); toast(S.char + "'s moves are on the keypad");
  }
  renderAll(); save();
};
$("mainChar").onchange = e => { S.char = e.target.value; loadCharacter(); };
$("mirror").onchange = e => {
  S.mirror = e.target.checked;
  // Turning the mirror off: the right half keeps what it was playing
  if (!S.mirror) for (let i = 0; i < 16; i++) if (i % 4 >= 2) {
    const src = S.slots[i - i % 4 + 3 - i % 4];
    S.slots[i] = {name: src.name, src: "", steps: mirrored(src.steps)};
  }
  renderGrid(); save();
};
$("maps").onclick = e => {
  const b = e.target.closest("button[data-m]"); if (!b) return;
  listening = +b.dataset.m; renderMaps();
};
$("maps").onchange = e => { const s = e.target.closest("select"); if (s) { S.map[+s.dataset.m] = +s.value; save(); } };
$("times").onchange = e => { const v = Math.max(1, Math.min(255, +e.target.value || 1)); S.timing[e.target.dataset.t] = v; save(); };

window.addEventListener("keydown", e => {
  if (rec) { recordEvent(e, true); return; }
  if (listening >= 0) {
    e.preventDefault();
    const k = KEYS.find(x => x[0] === e.code);
    if (e.code !== "Escape" && k) { S.map[listening] = k[1]; save(); }
    listening = -1; renderMaps();
  }
});
window.addEventListener("keyup", e => { if (rec) recordEvent(e, false); });

$("close").onclick = closeEditor;
$("shade").onclick = e => { if (e.target === $("shade")) closeEditor(); };
$("tabs").onclick = e => { const t = e.target.dataset.t; if (t) setTab(t); };
$("char").onchange = buildMoves;
$("moves").onclick = e => {
  const b = e.target.closest(".move"); if (!b) return;
  const m = G().chars[$("char").value][+b.dataset.n];
  draft.name = m[0]; $("name").value = m[0];
  $("src").value = m[1]; setSource(m[1]);
  setTab("notation");
};
$("src").oninput = e => setSource(e.target.value);
$("pad").onclick = e => {
  const v = e.target.dataset.ins; if (!v) return;
  let s = $("src").value;
  if (v === "⌫") s = s.replace(/(\s*[>,]\s*|[A-Z]{1,2}|.)$/, "");
  else if (v === ">" || v === ",") s = s.trimEnd() + " " + v + " ";
  else if (/^[1-9]$/.test(v)) s += (/[A-Z]$/.test(s) ? " " : "") + v;
  else s += v;
  $("src").value = s; setSource(s);
};
$("timeline").onchange = e => {
  const i = e.target.dataset.s; if (i === undefined) return;
  draft.steps[+i].f = Math.max(1, Math.min(255, +e.target.value || 1));
  renderTimeline();
};
$("timeline").onclick = e => {
  const i = e.target.dataset.del; if (i === undefined) return;
  draft.steps.splice(+i, 1); renderTimeline();
};
$("recBtn").onclick = () => rec ? stopRecording() : startRecording();
$("clear").onclick = () => { draft = emptySlot(); $("name").value = ""; $("src").value = ""; $("srcErr").textContent = ""; renderTimeline(); };
$("test").onclick = async () => {
  if (!draft.steps.length) { toast("No steps yet"); return; }
  try {
    await fetch("/api/test", {method:"POST", body: draft.steps.map(s => s.d + "." + s.b + "." + s.f).join(" ")});
    toast("Playing on ComboPad (facing right)");
  } catch (e) { toast("Couldn't reach ComboPad"); }
};
$("done").onclick = () => {
  stopRecording();
  draft.name = $("name").value.trim();
  S.slots[editing] = draft;
  closeEditor(); renderGrid(); save();
};

// ================= Wi-Fi countdown =================
// The board turns Wi-Fi off after 10 minutes. The page counts down and can
// ask for another 10 minutes.
let secondsLeft = null;
function renderTimer(){
  if (secondsLeft === null) return;
  $("wifibar").hidden = false;
  $("wifibar").classList.toggle("warn", secondsLeft < 60);
  if (secondsLeft > 0) {
    const m = Math.floor(secondsLeft / 60), s = String(secondsLeft % 60).padStart(2, "0");
    $("wifiText").innerHTML = "Wi-Fi turns off in <b>" + m + ":" + s + "</b>";
    $("resetTimer").hidden = false;
  } else {
    $("wifiText").textContent = "Wi-Fi is off. Hold BOOT on the board for 3 seconds to turn it on again.";
    $("resetTimer").hidden = true;
  }
}
async function syncTimer(reset){
  try {
    const r = await fetch("/api/timer", {method: reset ? "POST" : "GET"});
    secondsLeft = +(await r.text()) || 0;
    renderTimer();
  } catch (e) {}
}
setInterval(() => { if (secondsLeft > 0) { secondsLeft--; renderTimer(); } }, 1000);
setInterval(() => syncTimer(false), 30000);
$("resetTimer").onclick = async () => { await syncTimer(true); toast("Wi-Fi stays on for another 10 minutes"); };

$("factory").onclick = async () => {
  if (!confirm("Forget every combo and setting on ComboPad?")) return;
  try { await fetch("/api/reset", {method:"POST"}); location.reload(); }
  catch (e) { toast("Couldn't reach ComboPad"); }
};

// ================= Start =================
(async () => {
  let text = "";
  try { text = await (await fetch("/api/config")).text(); } catch (e) { toast("Couldn't reach ComboPad"); }
  if (text.trim()) deserialize(text);
  else { defaults(); save(); }
  renderAll();
  $("saved").textContent = text.trim() ? "Saved" : "";
  syncTimer(false);
})();
</script>
</body>
</html>
)rawpage";
