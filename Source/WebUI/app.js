/* ============================================================================
   PowerExpressionMapper — WebView UI logic.

   Talks to the plugin over the JUCE native integration bridge:
     UI -> plugin:  window.__JUCE__.backend.emitEvent("uiCommand", command)
     plugin -> UI:  "stateChanged" / "toast" events.

   The note-name helpers mirror Source/NoteNameUtils.cpp exactly (Bitwig/DAW
   convention: MIDI 60 -> "C3").
   ============================================================================ */

"use strict";

/* ---------------------------------------------------------------- helpers -- */

const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

function midiToName(n) {
  if (!(n >= 0 && n < 128)) return "";
  return NOTE_NAMES[n % 12] + (Math.floor(n / 12) - 2);
}

function midiToNameWithNumber(n) {
  const name = midiToName(n);
  return name === "" ? "---" : name + " (" + n + ")";
}

function letterToPitchClass(c) {
  const map = { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 };
  return map[c.toUpperCase()] ?? -1;
}

/* Mirror of NoteNameUtils::nameToMidi ("C1", "Db0", "D#2", "c-1", ...). */
function nameToMidi(input) {
  const t = input.trim();
  if (t === "") return -1;

  const m = /^([A-Ga-g])([#b]*)\s*([-+]?\d+)$/.exec(t);
  if (!m) return -1;

  let pc = letterToPitchClass(m[1]);
  if (pc < 0) return -1;

  for (const ch of m[2]) pc += ch === "#" ? 1 : -1;
  pc = ((pc % 12) + 12) % 12;

  const octave = parseInt(m[3], 10);
  const note = (octave + 2) * 12 + pc;

  return note >= 0 && note < 128 ? note : -1;
}

/* Mirror of NoteNameUtils::parseNoteText: "C1 (36)", "C1", "db0", "36". */
function parseNoteText(input) {
  const t = input.trim();
  if (t === "") return -1;

  if (t.includes("(") || t.includes(")")) {
    if (!(t.includes("(") && t.endsWith(")"))) return -1;
    const inner = t.slice(t.indexOf("(") + 1, t.lastIndexOf(")")).trim();
    if (!/^\d+$/.test(inner)) return -1;
    const v = parseInt(inner, 10);
    return v >= 0 && v < 128 ? v : -1;
  }

  const byName = nameToMidi(t);
  if (byName >= 0) return byName;

  if (/^\d+$/.test(t)) {
    const v = parseInt(t, 10);
    return v >= 0 && v < 128 ? v : -1;
  }

  return -1;
}

function clampNote(n) { return Math.min(127, Math.max(0, n)); }

function el(tag, className, text) {
  const e = document.createElement(tag);
  if (className) e.className = className;
  if (text !== undefined) e.textContent = text;
  return e;
}

/* ----------------------------------------------------------------- bridge -- */

const backend = window.__JUCE__ ? window.__JUCE__.backend : null;

function send(command) {
  if (backend) backend.emitEvent("uiCommand", command);
}

if (backend) {
  backend.addEventListener("stateChanged", (s) => applyState(s));
  backend.addEventListener("toast", (t) => showToast(t.kind || "info", t.text || ""));
}

/* Mock backend so the UI can be designed in a plain browser: open with ?mock */
if (!backend && window.location.search.includes("mock")) {
  const state = {
    entries: [
      { name: "Kick", note: 36 },
      { name: "Snare", note: 38 },
      { name: "ESnare", note: 39 },
      { name: "HiHat", note: 42 },
    ],
    delaySamples: 350,
    sampleRate: 48000,
  };
  const listeners = {};
  window.__JUCE__ = {
    backend: {
      emitEvent(id, payload) {
        if (id !== "uiCommand") return;
        switch (payload.type) {
          case "requestState": break;
          case "setDelay": state.delaySamples = payload.samples; break;
          case "setEntryName": state.entries[payload.index].name = payload.name; break;
          case "setEntryNote": state.entries[payload.index].note = payload.note; break;
          case "addEntry": state.entries.splice(payload.index ?? state.entries.length, 0, { name: "New", note: 60 }); break;
          case "removeEntry": state.entries.splice(payload.index, 1); break;
          case "clearEntries": state.entries = []; break;
          default: console.log("mock uiCommand", payload);
        }
        (listeners.stateChanged || []).forEach((fn) => fn(state));
      },
      addEventListener(id, fn) { (listeners[id] = listeners[id] || []).push(fn); },
    },
  };
  window.__JUCE__.backend.emitEvent("uiCommand", { type: "requestState" });
  setTimeout(() => (listeners.stateChanged || []).forEach((fn) => fn(state)), 0);
}

/* -------------------------------------------------------------- app state -- */

let state = { entries: [], delaySamples: 16, sampleRate: 0 };

const tableBody = document.getElementById("tableBody");
const emptyState = document.getElementById("emptyState");
const delaySlider = document.getElementById("delaySlider");
const delaySamplesEl = document.getElementById("delaySamples");
const delayMsEl = document.getElementById("delayMs");
const entryCountEl = document.getElementById("entryCount");

/* ------------------------------------------------------------- delay UI --- */

function updateDelayUI() {
  const samples = state.delaySamples;
  delaySlider.value = samples;
  delaySamplesEl.textContent = samples;
  delaySlider.style.setProperty("--fill", ((samples / 8192) * 100).toFixed(2) + "%");

  const sr = state.sampleRate;
  if (sr >= 8000) {
    const ms = (samples / sr) * 1000;
    delayMsEl.textContent = "\u2248 " + (ms >= 10 ? ms.toFixed(1) : ms.toFixed(2)) + " ms @ " +
      (sr / 1000).toFixed(1) + " kHz";
  } else {
    delayMsEl.textContent = "";
  }
}

let delaySendTimer = null;
delaySlider.addEventListener("input", () => {
  state.delaySamples = parseInt(delaySlider.value, 10);
  updateDelayUI();
  clearTimeout(delaySendTimer);
  delaySendTimer = setTimeout(() => send({ type: "setDelay", samples: state.delaySamples }), 60);
});
delaySlider.addEventListener("change", () => {
  clearTimeout(delaySendTimer);
  send({ type: "setDelay", samples: state.delaySamples });
});

/* ------------------------------------------------------------ table view -- */

/* Keep DOM rows when only values change, so in-progress edits (and focus)
   survive host-driven state updates. */
function renderTable() {
  const count = state.entries.length;
  const previousDomCount = tableBody.children.length;
  emptyState.hidden = count > 0;

  const dupNotes = new Set();
  {
    const seen = new Set();
    for (const e of state.entries) {
      if (seen.has(e.note)) dupNotes.add(e.note);
      seen.add(e.note);
    }
  }

  while (tableBody.children.length > count) tableBody.lastChild.remove();
  while (tableBody.children.length < count) tableBody.appendChild(buildRow(tableBody.children.length));

  for (let i = 0; i < count; ++i) {
    const row = tableBody.children[i];
    row.dataset.index = i;
    const entry = state.entries[i];

    const nameInput = row.querySelector(".name-input");
    if (nameInput.value !== entry.name && document.activeElement !== nameInput)
      nameInput.value = entry.name;

    const noteInput = row.querySelector(".note-input");
    const label = midiToNameWithNumber(entry.note);
    if (noteInput.value !== label && document.activeElement !== noteInput)
      noteInput.value = label;

    row.classList.toggle("dup", dupNotes.has(entry.note));
    row.title = dupNotes.has(entry.note)
      ? "Another entry already uses " + midiToName(entry.note) + " \u2014 only the first one is direct-triggered."
      : "";
  }

  entryCountEl.textContent = count === 0 ? "" : count + (count === 1 ? " entry" : " entries");

  if (focusNewRowPending && count > previousDomCount) {
    focusNewRowPending = false;
    const last = tableBody.lastChild;
    const nameInput = last.querySelector(".name-input");
    nameInput.focus();
    nameInput.select();
    last.scrollIntoView({ block: "nearest" });
  } else if (count <= previousDomCount) {
    focusNewRowPending = false;
  }
}

function buildRow(index) {
  const row = el("div", "row");

  const nameInput = el("input", "cell name-input");
  nameInput.type = "text";
  nameInput.spellcheck = false;
  nameInput.placeholder = "Name";
  nameInput.value = state.entries[index].name;
  wireNameInput(nameInput);
  row.appendChild(nameInput);

  const noteWrap = el("div", "note-wrap");
  const noteInput = el("input", "cell note-input");
  noteInput.type = "text";
  noteInput.spellcheck = false;
  noteInput.placeholder = "C3 (60)";
  noteInput.value = midiToNameWithNumber(state.entries[index].note);
  wireNoteInput(noteInput);
  noteWrap.appendChild(noteInput);
  noteWrap.appendChild(buildSuggestBox(noteInput));
  row.appendChild(noteWrap);

  const del = el("button", "row-del");
  del.type = "button";
  del.title = "Remove entry";
  del.innerHTML =
    '<svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" ' +
    'stroke-width="2.2" stroke-linecap="round"><path d="M18 6 6 18M6 6l12 12"/></svg>';
  del.addEventListener("click", () => {
    send({ type: "removeEntry", index: Number(row.dataset.index) });
  });
  row.appendChild(del);

  return row;
}

/* name editing: commit on Enter / blur; revert on Escape. */
function wireNameInput(input) {
  const row = () => input.closest(".row");

  const commit = () => {
    const value = input.value;
    if (value !== state.entries[Number(row().dataset.index)].name)
      send({ type: "setEntryName", index: Number(row().dataset.index), name: value });
  };

  input.addEventListener("keydown", (e) => {
    if (e.key === "Enter") {
      e.preventDefault();
      commit();
      const noteInput = row().querySelector(".note-input");
      noteInput.focus();
      noteInput.select();
    } else if (e.key === "Escape") {
      input.value = state.entries[Number(row().dataset.index)].name;
      input.blur();
    }
  });

  input.addEventListener("blur", commit);
}

/* note editing: autocomplete dropdown + parse-or-revert commit. */
function wireNoteInput(input) {
  const row = () => input.closest(".row");

  const commit = (note) => {
    note = clampNote(note);
    const index = Number(row().dataset.index);
    if (state.entries[index].note !== note)
      send({ type: "setEntryNote", index, note });
    input.value = midiToNameWithNumber(note);
  };

  const commitFromText = () => {
    const parsed = parseNoteText(input.value);
    commit(parsed >= 0 ? parsed : state.entries[Number(row().dataset.index)].note);
  };

  input.addEventListener("keydown", (e) => {
    const box = row().querySelector(".suggest");
    const suggestOpen = !!box && !box.hidden;

    if (e.key === "ArrowDown" || e.key === "ArrowUp") {
      if (suggestOpen) {
        e.preventDefault();
        moveSuggestActive(box, e.key === "ArrowDown" ? 1 : -1, input);
      }
      return;
    }

    if (e.key === "Enter") {
      e.preventDefault();
      const active = suggestOpen ? getSuggestActive(box) : null;
      if (active !== null) {
        pickSuggestion(box, active, input);
      } else {
        commitFromText();
        hideSuggestions(row());
        input.blur();
      }
      return;
    }

    if (e.key === "Escape") {
      input.value = midiToNameWithNumber(state.entries[Number(row().dataset.index)].note);
      hideSuggestions(row());
      input.blur();
    }
  });

  input.addEventListener("input", () => showSuggestions(row()));

  input.addEventListener("blur", () => {
    commitFromText();
    hideSuggestions(row());
  });

  input.addEventListener("focus", () => showSuggestions(row()));
}

/* suggestions */

function buildSuggestBox(input) {
  const box = el("div", "suggest");
  box.hidden = true;
  box.addEventListener("mousedown", (e) => e.preventDefault()); // keep input focus
  return box;
}

function matchedNotes(query) {
  const text = query.trim().toLowerCase();
  const all = [];
  for (let n = 0; n < 128; ++n) all.push(n);
  if (text === "") return all;
  const starts = [];
  const contains = [];
  for (const n of all) {
    const label = midiToNameWithNumber(n).toLowerCase();
    if (label.startsWith(text)) starts.push(n);
    else if (label.includes(text)) contains.push(n);
  }
  return starts.concat(contains);
}

function showSuggestions(row) {
  const input = row.querySelector(".note-input");
  const box = row.querySelector(".suggest");
  const matches = matchedNotes(input.value);

  box.textContent = "";
  box.dataset.active = "0";

  if (matches.length === 0) {
    box.appendChild(el("div", "suggest-item no-match", "(no match)"));
  } else {
    matches.forEach((n, i) => {
      const item = el("div", "suggest-item" + (i === 0 ? " active" : ""));
      item.dataset.note = n;
      item.appendChild(el("b", null, midiToName(n)));
      item.appendChild(el("i", null, String(n)));
      item.addEventListener("mousedown", (e) => {
        e.preventDefault();
        pickSuggestion(box, i, input);
      });
      box.appendChild(item);
    });
  }

  box.hidden = false;
  positionSuggestBox(row);
}

function positionSuggestBox(row) {
  const box = row.querySelector(".suggest");
  const list = box.parentElement.closest(".table-body");
  const inputRect = row.querySelector(".note-input").getBoundingClientRect();
  const listRect = list.getBoundingClientRect();
  const boxH = box.offsetHeight;

  box.style.top = "";
  if (inputRect.bottom + 4 + boxH > listRect.bottom && inputRect.top - 4 - boxH >= listRect.top)
    box.style.top = "calc(100% + 4px)";
  else if (inputRect.bottom + 4 + boxH > listRect.bottom)
    box.style.bottom = "0";
}

function hideSuggestions(row) {
  const box = row.querySelector(".suggest");
  if (box) box.hidden = true;
}

function getSuggestActive(box) {
  if (!box || box.hidden || box.querySelector(".no-match")) return null;
  return Math.min(Number(box.dataset.active || 0), box.children.length - 1);
}

function moveSuggestActive(box, delta, input) {
  if (!box || box.hidden) return;
  let i = Number(box.dataset.active || 0) + delta;
  i = Math.max(0, Math.min(box.children.length - 1, i));
  box.dataset.active = i;
  [...box.children].forEach((c, ci) => c.classList.toggle("active", ci === i));
  box.children[i].scrollIntoView({ block: "nearest" });
  if (box.children[i].dataset.note !== undefined)
    input.value = midiToNameWithNumber(Number(box.children[i].dataset.note));
}

function pickSuggestion(box, index, input) {
  const item = box.children[index];
  if (!item || item.dataset.note === undefined) return;
  const note = Number(item.dataset.note);
  const rowEl = input.closest(".row");
  const rowState = state.entries[Number(rowEl.dataset.index)];
  if (rowState.note !== note)
    send({ type: "setEntryNote", index: Number(rowEl.dataset.index), note });
  input.value = midiToNameWithNumber(note);
  hideSuggestions(rowEl);
  input.blur();
}

/* ---------------------------------------------------------------- toolbar -- */

/* Set when the user asks for a new row; consumed by renderTable once the
   stateChanged echo has actually appended it (focusing earlier would race the
   async rebuild and land the caret in the wrong row). */
let focusNewRowPending = false;

function addEntry() {
  focusNewRowPending = true;
  send({ type: "addEntry", index: state.entries.length });
}

document.getElementById("addBtn").addEventListener("click", addEntry);
document.getElementById("emptyAddBtn").addEventListener("click", addEntry);
document.getElementById("clearBtn").addEventListener("click", () => send({ type: "clearEntries" }));
document.getElementById("emptyImportBtn").addEventListener("click", () => send({ type: "importFile" }));
document.getElementById("importBtn").addEventListener("click", () => send({ type: "importFile" }));
document.getElementById("exportBtn").addEventListener("click", () => send({ type: "exportFile" }));

/* ----------------------------------------------------------------- toasts -- */

function showToast(kind, text) {
  const host = document.getElementById("toastHost");
  const toast = el("div", "toast" + (kind === "error" ? " error" : ""), text);
  host.appendChild(toast);
  setTimeout(() => {
    toast.classList.add("leaving");
    setTimeout(() => toast.remove(), 300);
  }, kind === "error" ? 6000 : 2600);
}

/* -------------------------------------------------------------- bootstrap -- */

function applyState(s) {
  state = s;
  renderTable();
  updateDelayUI();
}

if (!backend && !window.location.search.includes("mock")) {
  showToast("error", "Native bridge unavailable \u2014 the UI cannot reach the plugin backend.");
}

renderTable();
updateDelayUI();
send({ type: "requestState" });
