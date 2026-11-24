// web_page.h - the settings page the board serves at http://192.168.4.1
// (HTML, CSS and JavaScript in one string)

#pragma once

const char WEB_PAGE[] = R"rawpage(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>TinkerPad</title>
<style>
:root{
  --bg:#f5f6f7;--card:#fff;--text:#16191c;--muted:#6b7378;--line:#e3e6e8;
  --accent:#117A80;--accent-soft:#e3f1f2;--chip:#eef0f2;--danger:#c2372c;
}
@media (prefers-color-scheme:dark){:root{
  --bg:#111416;--card:#1b1f22;--text:#eef1f2;--muted:#8d969b;--line:#2b3135;
  --accent:#3fb3b9;--accent-soft:#173235;--chip:#262c30;--danger:#ef6b5f;
}}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{margin:0;background:var(--bg);color:var(--text);
  font:15px/1.45 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif}
main{max-width:520px;margin:0 auto;padding:20px 16px 48px}
header{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:6px}
h1{font-size:22px;margin:0;letter-spacing:-.01em}
.status{display:flex;align-items:center;gap:6px;font-size:13px;color:var(--muted)}
.dot{width:8px;height:8px;border-radius:50%;background:var(--muted)}
.dot.on{background:#2fa36b}
.hint{color:var(--muted);font-size:13px;margin:0 0 16px}
.seg{display:inline-flex;background:var(--chip);border-radius:10px;padding:3px}
.seg button{border:0;background:none;color:var(--muted);font:inherit;font-size:14px;
  padding:6px 14px;border-radius:8px;cursor:pointer}
.seg button.on{background:var(--card);color:var(--text);box-shadow:0 1px 2px rgba(0,0,0,.12)}
.row{display:flex;align-items:center;justify-content:space-between;margin:0 0 14px}
.label{font-size:13px;color:var(--muted)}
.grid{display:grid;grid-template-columns:repeat(4,1fr);gap:8px}
.key{position:relative;aspect-ratio:1;border:1px solid var(--line);border-radius:12px;
  background:var(--card);cursor:pointer;display:flex;align-items:center;justify-content:center;
  padding:18px 4px 6px;font:inherit;color:var(--text);transition:border-color .15s,transform .1s}
.key:hover{border-color:var(--accent)}
.key:active{transform:scale(.97)}
.key .n{position:absolute;top:6px;left:8px;font-size:11px;color:var(--muted)}
.key .empty{color:var(--line);font-size:22px}
.caps{display:flex;flex-wrap:wrap;justify-content:center;gap:3px}
.cap{background:var(--chip);border-radius:5px;padding:1px 5px;font-size:13px;font-weight:500;white-space:nowrap}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:16px;margin-top:20px}
.card h2{font-size:15px;margin:0 0 12px}
.field{margin-bottom:12px}
.field label{display:block;font-size:13px;color:var(--muted);margin-bottom:4px}
input,select{width:100%;font:inherit;color:var(--text);background:var(--bg);border:1px solid var(--line);
  border-radius:10px;padding:10px 12px;outline:none}
input:focus,select:focus{border-color:var(--accent)}
.btn{border:0;border-radius:10px;padding:11px 16px;font:inherit;font-weight:600;cursor:pointer;
  background:var(--chip);color:var(--text)}
.btn.primary{background:var(--accent);color:#fff}
.btn.ghost{background:none;color:var(--muted);padding-left:4px;padding-right:4px}
.btn:disabled{opacity:.5;cursor:default}
.banner{background:var(--accent-soft);border-radius:10px;padding:10px 12px;font-size:13px;margin-bottom:12px}
.banner.warn{background:rgba(194,55,44,.12);color:var(--danger)}
/* editor sheet */
.shade{position:fixed;inset:0;background:rgba(0,0,0,.35);display:none;align-items:flex-end;justify-content:center;z-index:10}
.shade.open{display:flex}
@media (min-width:640px){.shade{align-items:center}}
.sheet{background:var(--card);width:100%;max-width:440px;border-radius:18px 18px 0 0;padding:20px 16px calc(16px + env(safe-area-inset-bottom));
  max-height:92vh;overflow:auto;animation:up .18s ease-out}
@media (min-width:640px){.sheet{border-radius:18px}}
@keyframes up{from{transform:translateY(24px);opacity:0}}
.sheet-head{display:flex;justify-content:space-between;align-items:center;margin-bottom:14px}
.sheet-head h2{margin:0;font-size:18px}
.capture{border:2px dashed var(--line);border-radius:14px;min-height:96px;display:flex;flex-direction:column;
  align-items:center;justify-content:center;gap:8px;margin-bottom:6px;outline:none;cursor:text}
.capture.listening{border-color:var(--accent);background:var(--accent-soft)}
.capture .caps .cap{font-size:18px;padding:4px 10px;background:var(--card);box-shadow:0 1px 0 var(--line)}
.capture .ph{color:var(--muted);font-size:14px}
.or{display:flex;align-items:center;gap:10px;color:var(--muted);font-size:12px;margin:14px 0}
.or:before,.or:after{content:"";flex:1;height:1px;background:var(--line)}
.mods{display:grid;grid-template-columns:repeat(4,1fr);gap:6px;margin-bottom:12px}
.mod{border:1px solid var(--line);background:none;border-radius:10px;padding:9px 2px;font:inherit;font-size:13px;
  color:var(--text);cursor:pointer}
.mod.on{background:var(--accent);border-color:var(--accent);color:#fff}
.mod .sym{display:block;font-size:17px;line-height:1.2}
.actions{display:flex;gap:8px;margin-top:16px}
.actions .grow{flex:1}
.toast{position:fixed;left:50%;bottom:24px;transform:translateX(-50%);background:var(--text);color:var(--bg);
  padding:9px 16px;border-radius:20px;font-size:14px;opacity:0;transition:opacity .2s;pointer-events:none;z-index:20}
.toast.show{opacity:1}
.small{font-size:12px;color:var(--muted);margin:6px 0 0}
</style>
</head>
<body>
<main>
  <header>
    <h1>TinkerPad</h1>
    <div class="status"><span class="dot" id="bleDot"></span><span id="bleText">Bluetooth</span></div>
  </header>
  <p class="hint">Tap a key to give it a shortcut.</p>

  <div class="row">
    <span class="label">Computer</span>
    <div class="seg" id="platform">
      <button data-p="mac">macOS</button><button data-p="win">Windows</button>
    </div>
  </div>

  <div class="grid" id="grid"></div>

  <section class="card">
    <h2>Wi-Fi</h2>
    <div id="wifiBanner"></div>
    <div class="row">
      <span class="label">Mode</span>
      <div class="seg" id="mode">
        <button data-m="ap">Own network</button><button data-m="sta">My router</button>
      </div>
    </div>
    <div id="apFields">
      <div class="field"><label>Network name</label><input id="apName" disabled></div>
      <div class="field"><label>Password (8+ characters)</label><input id="apPass" autocomplete="off"></div>
      <p class="small">Join this network and open <b>192.168.4.1</b>.</p>
    </div>
    <div id="staFields" hidden>
      <div class="field"><label>Router name</label><input id="ssid" autocomplete="off"></div>
      <div class="field"><label>Router password</label><input id="pass" type="password" autocomplete="off"></div>
      <p class="small">Afterwards open <b>tinkerpad.local</b>. If the router can't be reached, TinkerPad goes back to its own network. Hold key 1 while powering on to force it.</p>
    </div>
    <div class="actions"><button class="btn primary grow" id="saveWifi">Save and restart</button></div>
  </section>
</main>

<div class="shade" id="shade">
  <div class="sheet" role="dialog" aria-modal="true">
    <div class="sheet-head">
      <h2 id="sheetTitle">Key 1</h2>
      <button class="btn ghost" id="close">Cancel</button>
    </div>

    <div id="captureBox">
      <div class="capture" id="capture" tabindex="0">
        <div class="caps" id="captureCaps"></div>
        <div class="ph" id="capturePh">Press a shortcut on your keyboard</div>
      </div>
      <p class="small">Some system shortcuts (like Command-Tab) can't be captured by a browser. Pick those below.</p>
      <div class="or">or choose</div>
    </div>

    <div class="field">
      <label>Common shortcuts</label>
      <select id="preset"></select>
    </div>
    <div class="field">
      <label>Modifiers</label>
      <div class="mods" id="mods"></div>
    </div>
    <div class="field">
      <label>Key</label>
      <select id="keySel"></select>
    </div>

    <div class="actions">
      <button class="btn" id="clear">Clear</button>
      <button class="btn" id="test">Test</button>
      <button class="btn primary grow" id="save">Save</button>
    </div>
  </div>
</div>

<div class="toast" id="toast"></div>

<script>
// ---------- Key tables ----------
// Modifier bits (same as the firmware): Ctrl 1, Shift 2, Alt/Option 4, Win/Command 8
const MODS = [
  {bit:1, mac:"⌃", macName:"Control", win:"Ctrl"},
  {bit:4, mac:"⌥", macName:"Option",  win:"Alt"},
  {bit:2, mac:"⇧", macName:"Shift",   win:"Shift"},
  {bit:8, mac:"⌘", macName:"Command", win:"Win"},
];

// [browser key code, HID code, label, mac label (optional)]
const KEYS = [];
const GROUPS = {Letters:[], Numbers:[], "Function keys":[], "Editing & navigation":[], Symbols:[]};
function addKey(group, code, hid, label, macLabel){
  const k = {code, hid, label, mac: macLabel || label};
  KEYS.push(k); GROUPS[group].push(k);
}
for (let i = 0; i < 26; i++){ const L = String.fromCharCode(65 + i); addKey("Letters", "Key" + L, 4 + i, L); }
for (let i = 1; i <= 9; i++) addKey("Numbers", "Digit" + i, 0x1D + i, String(i));
addKey("Numbers", "Digit0", 0x27, "0");
for (let i = 1; i <= 12; i++) addKey("Function keys", "F" + i, 0x39 + i, "F" + i);
for (let i = 13; i <= 24; i++) addKey("Function keys", "F" + i, 0x5B + i, "F" + i);
[["Enter",0x28,"Enter","↩"],["Escape",0x29,"Esc","⎋"],["Backspace",0x2A,"Backspace","⌫"],["Tab",0x2B,"Tab","⇥"],
 ["Space",0x2C,"Space","Space"],["Delete",0x4C,"Delete","⌦"],["Insert",0x49,"Insert"],["Home",0x4A,"Home","↖"],
 ["End",0x4D,"End","↘"],["PageUp",0x4B,"Page Up","⇞"],["PageDown",0x4E,"Page Down","⇟"],
 ["ArrowUp",0x52,"↑"],["ArrowDown",0x51,"↓"],["ArrowLeft",0x50,"←"],["ArrowRight",0x4F,"→"],
 ["PrintScreen",0x46,"Print Screen"],["CapsLock",0x39,"Caps Lock","⇪"]
].forEach(k => addKey("Editing & navigation", ...k));
[["Minus",0x2D,"-"],["Equal",0x2E,"="],["BracketLeft",0x2F,"["],["BracketRight",0x30,"]"],["Backslash",0x31,"\\"],
 ["Semicolon",0x33,";"],["Quote",0x34,"'"],["Backquote",0x35,"`"],["Comma",0x36,","],["Period",0x37,"."],["Slash",0x38,"/"]
].forEach(k => addKey("Symbols", ...k));

const MEDIA = [
  {c:0xCD, name:"Play / Pause", short:"Play/Pause"},
  {c:0xB5, name:"Next track",   short:"Next"},
  {c:0xB6, name:"Previous track", short:"Prev"},
  {c:0xE9, name:"Volume up",    short:"Vol +"},
  {c:0xEA, name:"Volume down",  short:"Vol −"},
  {c:0xE2, name:"Mute",         short:"Mute"},
  {c:0x6F, name:"Brightness up",   short:"Bright +"},
  {c:0x70, name:"Brightness down", short:"Bright −"},
];

// Common shortcuts: [name, modifiers, HID key]
const PRESETS = {
  mac: [["Copy",8,0x06],["Paste",8,0x19],["Cut",8,0x1B],["Undo",8,0x1D],["Redo",10,0x1D],["Save",8,0x16],
        ["Select all",8,0x04],["Find",8,0x09],["New tab",8,0x17],["Close tab / window",8,0x1A],["Reopen closed tab",10,0x17],
        ["Quit app",8,0x14],["Spotlight",8,0x2C],["Screenshot (area)",10,0x21],["Screenshot tool",10,0x22],
        ["Mission Control",1,0x52],["Lock screen",9,0x14],["Force quit",12,0x29]],
  win: [["Copy",1,0x06],["Paste",1,0x19],["Cut",1,0x1B],["Undo",1,0x1D],["Redo",1,0x1C],["Save",1,0x16],
        ["Select all",1,0x04],["Find",1,0x09],["New tab",1,0x17],["Close tab",1,0x1A],["Reopen closed tab",3,0x17],
        ["Close app",4,0x3D],["Search",8,0x16],["Screenshot (Snipping Tool)",10,0x16],["Show desktop",8,0x07],
        ["Task view",8,0x2B],["File Explorer",8,0x08],["Lock screen",8,0x0F],["Task Manager",3,0x29]],
};

// ---------- State ----------
let config = {platform:"mac", keys:[]};
let editing = -1;                 // key being edited
let draft = {m:0, k:0, c:0};      // shortcut in the editor
const $ = id => document.getElementById(id);
const isDesktop = matchMedia("(hover:hover) and (pointer:fine)").matches;

// ---------- Labels ----------
function parts(s){
  const mac = config.platform === "mac";
  if (s.c){ const m = MEDIA.find(x => x.c === s.c); return [m ? m.short : "Media"]; }
  const out = MODS.filter(x => s.m & x.bit).map(x => mac ? x.mac : x.win);
  const k = KEYS.find(x => x.hid === s.k);
  if (k) out.push(mac ? k.mac : k.label);
  return out;
}
function capsHtml(s){
  return '<div class="caps">' + parts(s).map(p => '<span class="cap">' + esc(p) + '</span>').join("") + '</div>';
}
function esc(t){ return t.replace(/[&<>"]/g, c => ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;"}[c])); }
function isEmpty(s){ return !s.m && !s.k && !s.c; }

// ---------- Main screen ----------
function renderGrid(){
  $("grid").innerHTML = config.keys.map((s, i) =>
    '<button class="key" data-i="' + i + '"><span class="n">' + (i + 1) + '</span>' +
    (isEmpty(s) ? '<span class="empty">+</span>' : capsHtml(s)) + '</button>').join("");
}
function renderPlatform(){
  document.querySelectorAll("#platform button").forEach(b => b.classList.toggle("on", b.dataset.p === config.platform));
}
function renderWifi(){
  const sta = config.mode === "sta";
  document.querySelectorAll("#mode button").forEach(b => b.classList.toggle("on", b.dataset.m === config.mode));
  $("apFields").hidden = sta; $("staFields").hidden = !sta;
  $("apName").value = config.apName || "";
  $("wifiBanner").innerHTML = config.staFailed
    ? '<div class="banner warn">Couldn\'t join "' + esc(config.ssid) + '". Running on its own network for now.</div>' : "";
}
function renderBle(on){
  $("bleDot").classList.toggle("on", on);
  $("bleText").textContent = on ? "Connected" : "Not paired";
}

// ---------- Editor ----------
function openEditor(i){
  editing = i;
  draft = Object.assign({}, config.keys[i]);
  $("sheetTitle").textContent = "Key " + (i + 1);
  $("captureBox").hidden = !isDesktop;
  buildPresetList(); buildMods(); buildKeyList();
  renderDraft();
  $("shade").classList.add("open");
  if (isDesktop) $("capture").focus();
}
function closeEditor(){ $("shade").classList.remove("open"); editing = -1; }

function buildPresetList(){
  const list = PRESETS[config.platform];
  let html = '<option value="">Choose…</option><optgroup label="Shortcuts">';
  list.forEach((p, n) => html += '<option value="p' + n + '">' + p[0] + ' — ' + parts({m:p[1], k:p[2], c:0}).join(config.platform === "mac" ? "" : "+") + '</option>');
  html += '</optgroup><optgroup label="Media">';
  MEDIA.forEach(m => html += '<option value="c' + m.c + '">' + m.name + '</option>');
  $("preset").innerHTML = html + '</optgroup>';
}
function buildMods(){
  const mac = config.platform === "mac";
  $("mods").innerHTML = MODS.map(x => '<button class="mod" data-bit="' + x.bit + '">' +
    (mac ? '<span class="sym">' + x.mac + '</span>' + x.macName : x.win) + '</button>').join("");
}
function buildKeyList(){
  const mac = config.platform === "mac";
  let html = '<option value="0">None</option>';
  for (const g in GROUPS){
    html += '<optgroup label="' + g + '">';
    GROUPS[g].forEach(k => html += '<option value="k' + k.hid + '">' + esc(mac && k.mac !== k.label ? k.mac + "  " + k.label : k.label) + '</option>');
    html += '</optgroup>';
  }
  html += '<optgroup label="Media">';
  MEDIA.forEach(m => html += '<option value="c' + m.c + '">' + m.name + '</option>');
  $("keySel").innerHTML = html + '</optgroup>';
}
function renderDraft(){
  document.querySelectorAll(".mod").forEach(b => {
    b.classList.toggle("on", !draft.c && (draft.m & +b.dataset.bit) > 0);
    b.disabled = draft.c > 0;
  });
  $("keySel").value = draft.c ? "c" + draft.c : (draft.k ? "k" + draft.k : "0");
  $("preset").value = "";
  $("captureCaps").innerHTML = isEmpty(draft) ? "" : capsHtml(draft).replace(/^<div class="caps">|<\/div>$/g, "");
  $("capturePh").hidden = !isEmpty(draft);
}

// Desktop: press the shortcut itself
const MOD_CODES = ["ControlLeft","ControlRight","ShiftLeft","ShiftRight","AltLeft","AltRight","MetaLeft","MetaRight","OSLeft","OSRight"];
function modsOf(e){ return (e.ctrlKey?1:0) | (e.shiftKey?2:0) | (e.altKey?4:0) | (e.metaKey?8:0); }
window.addEventListener("keydown", e => {
  if (editing < 0 || !isDesktop) return;
  if (e.target.tagName === "SELECT" && !e.ctrlKey && !e.metaKey && !e.altKey) return;
  if (e.key === "Escape" && !modsOf(e) && document.activeElement !== $("capture")) { closeEditor(); return; }
  e.preventDefault();
  if (e.repeat) return;
  if (MOD_CODES.includes(e.code)) {           // only modifiers so far: show them live
    draft = {m: modsOf(e), k:0, c:0};
  } else {
    const k = KEYS.find(x => x.code === e.code);
    if (!k) return;
    draft = {m: modsOf(e), k: k.hid, c:0};
  }
  $("capture").classList.add("listening");
  renderDraft();
});
window.addEventListener("keyup", () => $("capture").classList.remove("listening"));

// ---------- Talking to the board ----------
async function post(url, data){
  const r = await fetch(url, {method:"POST", body:new URLSearchParams(data)});
  const t = await r.text();
  if (!r.ok) throw new Error(t);
  return t;
}
function toast(t){
  $("toast").textContent = t; $("toast").classList.add("show");
  clearTimeout(toast.timer); toast.timer = setTimeout(() => $("toast").classList.remove("show"), 1800);
}

// ---------- Events ----------
$("grid").onclick = e => { const b = e.target.closest(".key"); if (b) openEditor(+b.dataset.i); };
$("close").onclick = closeEditor;
$("shade").onclick = e => { if (e.target === $("shade")) closeEditor(); };
$("capture").onclick = () => $("capture").focus();

$("platform").onclick = async e => {
  const p = e.target.dataset.p; if (!p || p === config.platform) return;
  config.platform = p; renderPlatform(); renderGrid();
  try { await post("/api/platform", {p}); } catch (err) { toast("Couldn't save"); }
};
$("mods").onclick = e => {
  const b = e.target.closest(".mod"); if (!b || b.disabled) return;
  draft.m ^= +b.dataset.bit; renderDraft();
};
$("keySel").onchange = e => {
  const v = e.target.value;
  if (v[0] === "c") draft = {m:0, k:0, c:+v.slice(1)};
  else draft = {m: draft.c ? 0 : draft.m, k: v === "0" ? 0 : +v.slice(1), c:0};
  renderDraft();
};
$("preset").onchange = e => {
  const v = e.target.value; if (!v) return;
  if (v[0] === "c") draft = {m:0, k:0, c:+v.slice(1)};
  else { const p = PRESETS[config.platform][+v.slice(1)]; draft = {m:p[1], k:p[2], c:0}; }
  renderDraft();
};
$("clear").onclick = () => { draft = {m:0, k:0, c:0}; renderDraft(); };
$("test").onclick = async () => {
  try { const t = await post("/api/test", draft); toast(t === "sent" ? "Sent to your computer" : "Pair TinkerPad over Bluetooth first"); }
  catch (err) { toast("Couldn't reach TinkerPad"); }
};
$("save").onclick = async () => {
  if (!isEmpty(draft) && !draft.k && !draft.c) { toast("Choose a key too"); return; }
  try {
    await post("/api/key", Object.assign({i: editing}, draft));
    config.keys[editing] = draft; renderGrid(); closeEditor(); toast("Saved");
  } catch (err) { toast("Couldn't save"); }
};

$("mode").onclick = e => { const m = e.target.dataset.m; if (m) { config.mode = m; renderWifi(); } };
$("saveWifi").onclick = async () => {
  const data = {mode: config.mode, appass: $("apPass").value, ssid: $("ssid").value, pass: $("pass").value};
  try {
    await post("/api/wifi", data);
    toast("Restarting…");
    $("saveWifi").disabled = true;
  } catch (err) { toast(err.message || "Couldn't save"); }
};

// ---------- Start ----------
async function load(){
  try {
    config = await (await fetch("/api/config")).json();
    $("apPass").value = config.apPass || "";
    $("ssid").value = config.ssid || "";
    renderPlatform(); renderGrid(); renderWifi(); renderBle(config.ble);
  } catch (err) { toast("Couldn't reach TinkerPad"); }
}
setInterval(async () => {
  try { renderBle((await (await fetch("/api/status")).json()).ble); } catch (err) {}
}, 3000);
load();
</script>
</body>
</html>
)rawpage";
