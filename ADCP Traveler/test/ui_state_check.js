const fs = require('fs');
const path = require('path');
const vm = require('vm');
const assert = require('assert');
const script = fs.readFileSync(path.join(__dirname, '..', 'data', 'script.js'), 'utf8');
const commands = [];
let timer;
const element = () => ({ classList: { toggle() {} }, setAttribute() {} });
const context = {
  motorEnabled: false, currentSpeed: 0, isUserDragging: false,
  pendingSpeedValue: null, speedSendTimer: null,
  messageElement: {}, distanceMessage: {}, distanceInput: { value: '10' },
  selectedDistanceDirection: 'cw', stateElements: [element(), element()],
  motorSpeedValue: {}, motorSpeedPercent: {}, motorControl: { dataset: {} },
  motorSpeedSlider: {}, motorArcHitArea: element(),
  clamp(value, min, max) { return Math.min(max, Math.max(min, value)); },
  positionArcKnob() {}, updateSystemToggleDisplay(enabled) { context.motorEnabled = enabled; },
  sendCommand(command) { commands.push(command); },
  setTimeout(callback) { timer = callback; return 1; },
};
vm.createContext(context);
for (const name of ['updateMotorSpeedDisplay', 'updateState', 'sendSpeedValue', 'startDistanceMove', 'sendOffCommand']) {
  const start = script.indexOf(`function ${name}(`);
  vm.runInContext(script.slice(start, script.indexOf('\nfunction ', start + 1)), context);
}
for (const [state, speed, label, heading] of [
  ['off', 0, 'STOPPED', 'Stopped'], ['on', 0, 'STOPPED', 'Stopped'],
  ['on', 100, 'Right Bank', 'Right Bank'], ['on', -100, 'Left Bank', 'Left Bank'],
]) {
  context.updateState(state, speed);
  context.stateElements.forEach((item) => assert.equal(item.textContent, label));
  assert.equal(context.motorSpeedValue.textContent, heading);
}
context.motorEnabled = false;
context.sendSpeedValue(100);
assert.equal(commands.length, 0);
context.startDistanceMove();
assert.equal(commands[0].cmd, 'distance_start');
commands.length = 0;
assert.equal(context.messageElement.textContent, 'Enable Traveller First');
assert.equal(context.distanceMessage.textContent, 'Started Move');
context.motorEnabled = true;
context.sendSpeedValue(100);
context.sendOffCommand();
timer();
assert.deepEqual(commands, [{ cmd: 'off' }]);
context.motorEnabled = true;
context.speedSendTimer = null;
context.sendSpeedValue(100);
timer();
context.startDistanceMove();
assert.equal(commands[1].cmd, 'speed');
assert.equal(commands[2].cmd, 'distance_start');
console.log('State labels, disabled speed, direct distance start, pending stop, and enabled movement checks passed.');
