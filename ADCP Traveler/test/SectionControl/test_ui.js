const assert = require('node:assert/strict');
const test = require('node:test');
const { sectionView, sectionColor, SectionControlUI } = require('../../data/section-control.js');

const ready = { stage: 'READY_FOR_SECTION', state: 'STOPPED', stationary: true,
  spanCm: 2480, count: 20, maxCount: 60, completed: 0, nextSection: 20,
  lastCompletedSection: 0, remainingCm: 180, locked: false, scanDirection: 'cw', widthsCm: [180, 100] };

test('pre-scan requires enabled system and fresh connection, then offers RUN', () => {
  const state = { ...ready, stage: 'PRE_SCAN', count: 0 };
  assert.equal(sectionView(state, true, true).action, 'RUN');
  assert.equal(sectionView(state, true, true).disabled, false);
  assert.equal(sectionView(state, false, true).disabled, true);
  assert.equal(sectionView(state, true, false).disabled, true);
  assert.equal(sectionView(state, true, true).showConfiguration, false);
});
test('moving and scanning keep STOP available while power is off', () => {
  for (const stage of ['SCANNING', 'MOVING_SECTION']) {
    const view = sectionView({ ...ready, stage, stationary: false }, false, true);
    assert.equal(view.action, 'STOP');
    assert.equal(view.disabled, false);
    assert.equal(view.showNewScan, false);
  }
});
test('invalid or coasting plans cannot GO; completed run cannot GO', () => {
  assert.equal(sectionView({ ...ready, stage: 'CONFIGURE', count: 0 }, true, true).disabled, true);
  assert.equal(sectionView({ ...ready, stationary: false }, true, true).disabled, true);
  assert.equal(sectionView({ ...ready, stage: 'FINISHED' }, true, true).disabled, true);
  assert.equal(sectionView(ready, false, true).disabled, false); // distance controller enables drive
});
test('width colors correspond to size, with equal widths all red', () => {
  assert.equal(sectionColor(100, 100, 180), 'hsl(0, 82%, 48%)');
  assert.equal(sectionColor(180, 100, 180), 'hsl(270, 82%, 48%)');
  assert.equal(sectionColor(100, 100, 100), 'hsl(0, 82%, 48%)');
});

// A small DOM boundary stub; production rendering and command handling run here.
function element() {
  const values = new Set();
  return { textContent: '', value: '', disabled: false, hidden: false, children: [],
    style: {}, dataset: {}, attributes: {}, handlers: {}, scrollLeft: 0,
    classList: { toggle(name, on) { if (on) values.add(name); else values.delete(name); },
      contains(name) { return values.has(name); } },
    setAttribute(name, value) { this.attributes[name] = value; },
    addEventListener(name, fn) { this.handlers[name] = fn; },
    appendChild(child) { this.children.push(child); },
    replaceChildren() { this.children = []; },
    scrollIntoView() {},
  };
}
function uiRig(send = async () => ({})) {
  const nodes = new Map();
  const doc = { activeElement: null, getElementById(id) {
    if (!nodes.has(id)) nodes.set(id, element()); return nodes.get(id);
  }, createElement: element };
  const ui = new SectionControlUI(doc, send);
  return { ui, nodes, doc };
}
test('renderer reveals configuration, progress and proportional strips from firmware', () => {
  const { ui, nodes } = uiRig();
  ui.update({ state: 'on', section: ready });
  assert.equal(nodes.get('section-action-label').textContent, 'GO');
  assert.equal(nodes.get('section-config').hidden, false);
  assert.equal(nodes.get('section-span').textContent, '24.80 m');
  assert.equal(nodes.get('section-overview').children.length, 2);
  assert.equal(nodes.get('section-details').children.length, 2);
  assert.equal(nodes.get('section-overview').children[0].style.flexGrow, '180');
  assert.equal(nodes.get('section-details').children[0].textContent, 'S1\n1.80 m');
  ui.setOnline(false);
  assert.equal(nodes.get('section-action').disabled, true);
});
test('one action selects STOP or GO and blocks duplicate requests', async () => {
  const sent = [];
  let release;
  const { ui } = uiRig(command => {
    sent.push(command); return new Promise(resolve => { release = resolve; });
  });
  ui.update({ state: 'on', section: ready });
  const pending = ui.primaryAction();
  await ui.primaryAction();
  assert.deepEqual(sent, [{ cmd: 'section_go' }]);
  release({}); await pending;
  ui.update({ state: 'off', section: { ...ready, stage: 'MOVING_SECTION', stationary: false } });
  const stop = ui.primaryAction();
  assert.equal(sent[1].cmd, 'section_stop');
  release({}); await stop;
});
test('locked plan cannot be changed and finished renders completion', () => {
  const { ui, nodes } = uiRig();
  ui.update({ state: 'on', section: { ...ready, locked: true } });
  assert.equal(nodes.get('section-count').disabled, true);
  ui.update({ state: 'on', section: { ...ready, stage: 'FINISHED', state: 'FINISHED', completed: 20 } });
  assert.equal(nodes.get('section-action').disabled, true);
  assert.match(nodes.get('section-progress').textContent, /All sections completed/);
});

test('STOP remains available if an earlier GO response is delayed', async () => {
  const sent = [];
  let release;
  const { ui, nodes } = uiRig(command => {
    sent.push(command);
    if (command.cmd === 'section_go') return new Promise(resolve => { release = resolve; });
    return Promise.resolve({});
  });
  ui.update({ state: 'on', section: ready });
  const go = ui.primaryAction();
  ui.update({ state: 'on', section: { ...ready, stage: 'MOVING_SECTION', state: 'MOVING', stationary: false } });
  assert.equal(nodes.get('section-action').disabled, false);
  await ui.primaryAction();
  assert.equal(sent[1].cmd, 'section_stop');
  release({}); await go;
});

test('count edit made while offline is submitted after reconnect', async () => {
  const sent = [];
  const { ui, nodes } = uiRig(async command => { sent.push(command); return {}; });
  ui.update({ state: 'on', section: ready });
  ui.setOnline(false);
  nodes.get('section-count').value = '12';
  nodes.get('section-count').handlers.input();
  await new Promise(resolve => setTimeout(resolve, 350));
  assert.equal(sent.length, 0);
  ui.update({ state: 'on', section: ready });
  await new Promise(resolve => setTimeout(resolve, 350));
  assert.deepEqual(sent, [{ cmd: 'section_configure', count: 12 }]);
  assert.equal(nodes.get('section-action').disabled, false);
});
