const fs = require('fs');
const path = require('path');
const vm = require('vm');
const assert = require('assert');
const root = path.join(__dirname, '..');
const script = fs.readFileSync(path.join(root, 'data/script.js'), 'utf8');
const element = () => ({ dataset: {}, setAttribute(name, value) { this[name] = value; } });
const context = {
  headerWifiIcon: element(),
  wifiSignalElements: [], titleCase(text) { return text; },
  clamp(value, min, max) { return Math.min(max, Math.max(min, value)); },
};
vm.createContext(context);
for (const name of ['updateWifiSignal']) {
  const start = script.indexOf(`function ${name}(`);
  vm.runInContext(script.slice(start, script.indexOf('\nfunction ', start + 1)), context);
}
context.updateWifiSignal({ connected: true, quality: 3 });
assert.equal(context.headerWifiIcon.dataset.connected, 'true');
assert.equal(context.headerWifiIcon.dataset.quality, '3');
context.updateWifiSignal({ connected: false, quality: 4 });
assert.equal(context.headerWifiIcon.dataset.connected, 'false');
assert.equal(context.headerWifiIcon.dataset.quality, '0');
const html = fs.readFileSync(path.join(root, 'data/index.html'), 'utf8');
assert(!html.includes('connection-pill') && !html.includes('connection-dot'));
assert(html.includes('wifi-disconnected-cross'));
assert(html.includes('Battery Full (Fixed Display)'));
assert(html.includes('class="battery-fill" x="7" y="6" width="10" height="15"'));
console.log('Header Wi-Fi states and fixed full battery checks passed.');
