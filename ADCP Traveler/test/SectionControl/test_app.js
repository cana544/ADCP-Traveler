const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const { SectionControlUI } = require('../../data/section-control.js');

function app() {
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
    querySelector() { return element(); },
    querySelectorAll(selector) {
      if (selector === '.nav-button') return nav;
      if (selector === '.motor-state-value') return states;
      return [];
    }, createElement: element, createElementNS: element,
  };
  class Socket { static OPEN = 1; readyState = 0; send() {} }
  const context = vm.createContext({ document: doc, SectionControlUI,
    window: { location: { protocol: 'http:', host: 'test.local' } },
    WebSocket: Socket, console, fetch: () => new Promise(() => {}),
    setTimeout, clearTimeout, setInterval() {}, URLSearchParams, AbortController,
  });
  vm.runInContext(fs.readFileSync(path.join(__dirname, '../../data/script.js'), 'utf8') +
    '\nglobalThis.api = { applyStateMessage, showPage, handleSwipeStart, handleSwipeEnd };', context);
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
});
test('third tab uses full page width and makes other controls inert', () => {
  const { api, nodes, pages } = app();
  api.showPage(2);
  assert.equal(nodes.get('page-track').style.transform, 'translateX(-66.66666666666667%)');
  assert.equal(pages[2].inert, false);
  assert.equal(pages[0].inert, true);
});
test('swiping section details does not change tabs', () => {
  const { api, nodes } = app();
  api.showPage(2);
  api.handleSwipeStart({ target: { closest() { return true; } }, clientX: 300, clientY: 100 });
  api.handleSwipeEnd({ clientX: 400, clientY: 100 });
  assert.equal(nodes.get('page-track').style.transform, 'translateX(-66.66666666666667%)');
});
