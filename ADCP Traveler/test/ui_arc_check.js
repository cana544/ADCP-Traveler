const fs = require('fs');
const path = require('path');
const vm = require('vm');
const assert = require('assert');

const script = fs.readFileSync(path.join(__dirname, '..', 'data', 'script.js'), 'utf8');
const lines = [];
const context = {
  motorArcTicks: { innerHTML: '', appendChild(line) { lines.push(line); } },
  document: {
    createElementNS() {
      return { setAttribute(name, value) { this[name] = Number(value); } };
    },
  },
};
vm.createContext(context);
vm.runInContext(script.slice(script.indexOf('const arcConfig ='), script.indexOf('function eventToArcPoint')), context);
vm.runInContext(script.slice(script.indexOf('function renderArcTicks()'), script.indexOf('function positionArcKnob')), context);
context.renderArcTicks();

const values = [-204, -153, -102, -51, 0, 51, 102, 153, 204];
assert.equal(lines.length, values.length);
lines.forEach((line, index) => {
  const point = context.angleToPoint(context.valueToAngle(values[index]));
  assert(Math.abs((line.x1 + line.x2) / 2 - point.x) < 0.01, 'Tick must be centred on arc horizontally');
  assert(Math.abs((line.y1 + line.y2) / 2 - point.y) < 0.01, 'Tick must be centred on arc vertically');
  const nx = (point.x - 200) / (165 * 165);
  const ny = (point.y - 150) / (130 * 130);
  assert(Math.abs((line.x2 - line.x1) * ny - (line.y2 - line.y1) * nx) < 0.0002, 'Tick must follow the ellipse normal');
});
console.log('Arc notch centring checks passed.');
