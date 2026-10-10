const fs = require('fs');
const vm = require('vm');
const assert = require('assert');
const source = fs.readFileSync('data/script.js', 'utf8');
const element = () => ({dataset: {}, setAttribute(k,v) { this[k] = v; }});
const context = {headerBattery: element(), batteryFill: element(), batteryReadout: element()};
vm.createContext(context);
const start = source.indexOf('function updateBattery(');
assert(start >= 0, 'Battery status renderer is missing');
vm.runInContext(source.slice(start, source.indexOf('\nfunction ', start + 1)), context);
for (const percent of [0, 10, 20, 20.01, 25, 50, 75, 100]) {
  context.updateBattery(JSON.parse(JSON.stringify({batteryValid:true, batteryVoltage:12.2, batteryPercent:percent})));
  assert.equal(context.batteryFill.height, String(15 * percent / 100));
  assert.equal(context.batteryFill.y, String(21 - 15 * percent / 100));
  assert.equal(context.headerBattery.dataset.level, percent <= 20 ? 'low' : 'normal');
  assert(context.batteryReadout.textContent.includes('12.2 V'));
}
for (const data of [{}, {batteryValid:false,batteryVoltage:12,batteryPercent:100},
  {batteryValid:true,batteryVoltage:null,batteryPercent:null},
  {batteryValid:true,batteryVoltage:12,batteryPercent:NaN}]) {
  context.updateBattery(data);
  assert.equal(context.batteryFill.height, '0');
  assert.equal(context.headerBattery.dataset.level, 'unavailable');
  assert.equal(context.batteryReadout.textContent, 'Unavailable');
}
console.log('Battery continuous fill, threshold, numeric validation and unavailable checks passed.');

const css = fs.readFileSync('data/style.css', 'utf8');
assert(css.includes('.battery-fill { fill: currentColor; }'));
assert(css.includes('[data-level="normal"] { color: #22d062; }'));
assert(css.includes('[data-level="low"] { color: #ff4d4f; }'));
assert(css.includes('[data-level="unavailable"] .battery-outline { stroke: #8591a4; }'));
