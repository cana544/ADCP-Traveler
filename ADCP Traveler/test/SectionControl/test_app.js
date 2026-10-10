const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const { SectionControlUI } = require('../../data/section-control.js');

function app(fetchOverride) {
  const nodes = new Map();
  function element() {
    const classes = new Set();
    return { dataset: {}, style: {}, attributes: {}, textContent: '', value: '', children: [],
      classList: { toggle(name, on) { on ? classes.add(name) : classes.delete(name); },
        remove(...names) { names.forEach(name => classes.delete(name)); },
        add(...names) { names.forEach(name => classes.add(name)); } },
      setAttribute(name, value) { this.attributes[name] = value; },
      addEventListener() {}, appendChild(child) { this.children.push(child); },
      replaceChildren() { this.children = []; }, querySelectorAll() { return pages; },
    };
  }
  const pages = [element(), element(), element()];
  const nav = [0, 1, 2].map(page => ({ ...element(), dataset: { page: String(page) } }));
  const states = [element(), element()];
  const doc = { activeElement: null,
    getElementById(id) { if (!nodes.has(id)) nodes.set(id, element()); return nodes.get(id); },
    querySelector(selector) { return doc.getElementById(selector); },
    querySelectorAll(selector) {
      if (selector === '.nav-button') return nav;
      if (selector === '.motor-state-value') return states;
      return [];
    }, createElement: element, createElementNS: element,
  };
  class Socket { static OPEN = 1; readyState = 0; send() {} }
  const context = vm.createContext({ document: doc, SectionControlUI,
    window: { location: { protocol: 'http:', host: 'test.local' } },
    WebSocket: Socket, console, fetch: fetchOverride || (() => new Promise(() => {})),
    setTimeout, clearTimeout, setInterval() {}, URLSearchParams, AbortController,
  });
  vm.runInContext(fs.readFileSync(path.join(__dirname, '../../data/script.js'), 'utf8') +
    '\nglobalThis.api = { applyStateMessage, setConnectionState, showPage, handleSwipeStart, handleSwipeEnd, sendSectionCommand, openSocket() { ws.readyState = WebSocket.OPEN; } };', context);
  return { api: context.api, nodes, pages };
}
function state(sequence, stage = 'READY_FOR_SECTION', bootId = 10) {
  return { stateSequence: sequence, bootId, state: 'on', speed: 0, distanceActive: false,
    section: { stage, state: 'STOPPED', stationary: true, count: 6, maxCount: 60,
      spanCm: 760, nextSection: 6, remainingCm: 180, completed: 0, widthsCm: [180,100,100,100,100,180] } };
}
test('older HTTP snapshot cannot undo newer STOP or completion state', () => {
  const { api, nodes } = app();
  api.applyStateMessage(state(20, 'FINISHED'));
  api.applyStateMessage(state(19, 'MOVING_SECTION'));
  assert.equal(nodes.get('section-action-label').textContent, 'COMPLETE');
  api.applyStateMessage(state(1, 'PRE_SCAN', 11)); // ESP reboot
  assert.equal(nodes.get('section-action-label').textContent, 'RUN');
  api.applyStateMessage(state(30, 'FINISHED', 10)); // delayed pre-reboot HTTP response
  assert.equal(nodes.get('section-action-label').textContent, 'RUN');
});
test('third tab uses full page width and makes other controls inert', () => {
  const { api, nodes, pages } = app();
  api.showPage(2);
  assert.equal(pages[2].style.transform, 'translateX(0%)');
  assert.equal(pages[2].inert, false);
  assert.equal(pages[0].inert, true);
});
test('swiping section details does not change tabs', () => {
  const { api, pages } = app();
  api.showPage(2);
  api.handleSwipeStart({ target: { closest() { return true; } }, clientX: 300, clientY: 100 });
  api.handleSwipeEnd({ clientX: 400, clientY: 100 });
  assert.equal(pages[2].style.transform, 'translateX(0%)');
});

test('Speed and Section slide directly one page width in both directions', () => {
  const { api, pages } = app();
  api.showPage(2);
  assert.equal(pages[0].style.transform, 'translateX(-100%)');
  assert.equal(pages[2].style.transform, 'translateX(0%)');
  assert.equal(pages[1].attributes['aria-hidden'], 'true');
  api.showPage(0);
  assert.equal(pages[2].style.transform, 'translateX(100%)');
  assert.equal(pages[0].style.transform, 'translateX(0%)');
  assert.equal(pages[1].attributes['aria-hidden'], 'true');
});
test('live-socket STOP does not enqueue a second delayed HTTP STOP', async () => {
  const urls = [];
  const { api } = app(async url => {
    urls.push(url); return { ok: true, json: async () => ({}) };
  });
  api.openSocket();
  await api.sendSectionCommand({ cmd: 'section_stop' });
  assert.equal(urls.filter(url => url.startsWith('/section/stop')).length, 0);
  assert.ok(urls.includes('/section/status'));
});
test('HTTP snapshot started before a first-seen reboot cannot replace that reboot', () => {
  const { api, nodes } = app();
  api.applyStateMessage(state(1, 'PRE_SCAN', 11));
  // Generation zero request started before any firmware state had arrived.
  api.applyStateMessage(state(30, 'FINISHED', 10), 0);
  assert.equal(nodes.get('section-action-label').textContent, 'RUN');
});

test('status JSON updates battery and rejects stale readings; disconnect clears it', () => {
  const { api, nodes } = app();
  const reading = {...state(20), batteryValid:true, batteryVoltage:12.2, batteryPercent:73};
  api.applyStateMessage(JSON.parse(JSON.stringify(reading)));
  assert.equal(nodes.get('.battery-readout').textContent, '12.2 V | ~73%');
  assert.equal(nodes.get('.header-battery').dataset.level, 'normal');
  api.applyStateMessage({...reading, stateSequence:19, batteryPercent:10});
  assert.equal(nodes.get('.header-battery').dataset.level, 'normal');
  api.applyStateMessage({...reading, stateSequence:21, batteryValid:false, batteryVoltage:null, batteryPercent:null});
  assert.equal(nodes.get('.battery-readout').textContent, 'Unavailable');
  api.applyStateMessage({...reading, stateSequence:22, batteryPercent:20});
  assert.equal(nodes.get('.header-battery').dataset.level, 'low');
  api.setConnectionState(false);
  assert.equal(nodes.get('.battery-readout').textContent, 'Unavailable');
});
