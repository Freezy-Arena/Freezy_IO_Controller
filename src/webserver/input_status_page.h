#pragma once
#include "page_branding.h"

static const char INPUT_STATUS_PAGE[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Input Status</title>
  <style>
    * { box-sizing: border-box; }
    body { margin: 0; padding: 32px 16px; background: #0f1117; color: #e2e8f0;
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif; }
    main { max-width: 720px; margin: auto; }
    a { color: #a5b4fc; text-underline-offset: 4px; }
    a:focus-visible { outline: 2px solid #a5b4fc; outline-offset: 5px; }
    h1 { font-size: 1.8rem; margin: 28px 0 8px; }
    #role { font-size: 1.05rem; color: #cbd5e1; }
    .note { color: #94a3b8; line-height: 1.6; }
    #connection { border: 1px solid #475569; border-radius: 8px; padding: 12px 16px; }
    #connection.live { border-color: #15803d; color: #86efac; }
    #fault { border: 1px solid #f87171; background: #3a1d27; padding: 14px; border-radius: 8px; }
    #inputs { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 16px; margin: 24px 0; }
    .input { background: #1a1d27; border: 1px solid #2d3148; border-radius: 12px; padding: 20px; }
    .input h2 { font-size: 1rem; margin: 0 0 18px; }
    .reading { display: flex; align-items: center; gap: 14px; }
    .light { width: 30px; height: 30px; border-radius: 50%; background: #64748b; flex-shrink: 0; }
    .input.active-stop .light { background: #ef4444; box-shadow: 0 0 16px #ef444440; }
    .input.active-start .light { background: #22c55e; box-shadow: 0 0 16px #22c55e40; }
    .input.unknown .light { background: transparent; border: 2px dashed #94a3b8; }
    .pin { color: #94a3b8; font-size: .85rem; margin: 16px 0 0; }
    @media (max-width: 420px) { #inputs { grid-template-columns: 1fr; } }
  </style>
</head>
<body>
<main>
)HTML" DEVICE_TITLE_BAR R"HTML(
  <a href="/">&larr; Device configuration</a>
  <h1>Input Status</h1>
  <p id="role">Reading active device role&hellip;</p>
  <p class="note">Live local button readings. These indicators do not confirm that the arena received a stop or accepted a start.</p>
  <p id="connection" role="status">Connecting to controller&hellip;</p>
  <p id="fault" role="alert" hidden>Controller fault is latched. The readings below show the physical inputs; stop delivery is forcing stops while this fault is active.</p>
  <section id="inputs" aria-label="Input indicators"></section>
  <p class="note">Red: stop pressed &middot; Green: start pressed &middot; Gray: released<br>
    Dashed: reading unavailable. Refreshes every half second.</p>
  <noscript><p>Enable JavaScript to view live input status.</p></noscript>
</main>
<script>
(() => {
  const roleNames = { FMS_TABLE: 'FMS Table', RED_ALLIANCE: 'Red Alliance', BLUE_ALLIANCE: 'Blue Alliance' };
  const connection = document.getElementById('connection');
  const fault = document.getElementById('fault');
  const inputs = document.getElementById('inputs');
  let rows = [];
  let activeRole = '';
  let staleTimer;
  function setText(element, text) {
    if (element.textContent !== text) element.textContent = text;
  }
  function unavailable(message) {
    clearTimeout(staleTimer);
    connection.className = '';
    setText(connection, message);
    fault.hidden = true;
    rows.forEach(row => {
      row.card.className = 'input unknown';
      setText(row.state, 'Unavailable');
    });
  }
  function buildRows(data) {
    inputs.replaceChildren();
    rows = data.inputs.map(input => {
      const card = document.createElement('article');
      card.className = 'input unknown';
      const heading = document.createElement('h2');
      heading.textContent = input.label;
      const reading = document.createElement('div');
      reading.className = 'reading';
      const light = document.createElement('span');
      light.className = 'light';
      light.setAttribute('aria-hidden', 'true');
      const state = document.createElement('strong');
      state.textContent = 'Unavailable';
      reading.append(light, state);
      const pin = document.createElement('p');
      pin.className = 'pin';
      pin.textContent = 'GPIO ' + input.pin;
      card.append(heading, reading, pin);
      inputs.append(card);
      return { card, state };
    });
    activeRole = data.role;
    setText(document.getElementById('role'), roleNames[data.role]);
  }
  async function poll() {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 2000);
    try {
      const response = await fetch('/api/inputs', { cache: 'no-store', signal: controller.signal });
      if (!response.ok) throw new Error('Input status unavailable');
      const data = await response.json();
      const expectedCount = data.role === 'FMS_TABLE' ? 2 : 6;
      if (!Object.prototype.hasOwnProperty.call(roleNames, data.role) ||
          typeof data.sampled !== 'boolean' || typeof data.fault !== 'boolean' ||
          !Number.isFinite(data.sampleAgeMs) || data.sampleAgeMs < 0 ||
          !Array.isArray(data.inputs) || data.inputs.length !== expectedCount ||
          !data.inputs.every(input => typeof input.label === 'string' &&
            Number.isInteger(input.pin) && typeof input.pressed === 'boolean' &&
            (input.kind === 'stop' || input.kind === 'start'))) throw new Error('Invalid input status');
      if (activeRole !== data.role) buildRows(data);
      if (!data.sampled || data.sampleAgeMs >= 1000) {
        unavailable('Input sampling is stale or unavailable.');
      } else {
        rows.forEach((row, index) => {
          const input = data.inputs[index];
          row.card.className = 'input' + (input.pressed ? ' active-' + input.kind : '');
          setText(row.state, input.pressed ? 'Pressed' : 'Released');
        });
        connection.className = 'live';
        setText(connection, 'Live local inputs');
        fault.hidden = !data.fault;
        clearTimeout(staleTimer);
        staleTimer = setTimeout(() => unavailable('Connection lost. Input readings are unavailable.'), 2000);
      }
    } catch (error) {
      unavailable('Cannot read controller inputs. Retrying…');
    } finally {
      clearTimeout(timeout);
      setTimeout(poll, 500); // Wait for the previous request; never overlap polls.
    }
  }
  // Hidden/suspended tabs must not resume displaying old readings as live.
  document.addEventListener('visibilitychange', () => unavailable('Refreshing input readings…'));
  poll();
})();
</script>
</body>
</html>)HTML";
