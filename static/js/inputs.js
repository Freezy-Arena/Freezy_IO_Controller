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
