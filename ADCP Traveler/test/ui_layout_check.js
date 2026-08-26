const fs = require("fs");
const path = require("path");

const root = path.resolve(__dirname, "..");
const html = fs.readFileSync(path.join(root, "data", "index.html"), "utf8");
const css = fs.readFileSync(path.join(root, "data", "style.css"), "utf8");
const script = fs.readFileSync(path.join(root, "data", "script.js"), "utf8");

function fail(message) {
  console.error(message);
  process.exitCode = 1;
}

function expectText(source, text, message) {
  if (!source.includes(text)) fail(message);
}

function expectCss(selector, property, expected) {
  const escapedSelector = selector.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  const match = css.match(new RegExp(`${escapedSelector}\\s*\\{([^}]*)\\}`, "m"));
  if (!match) {
    fail(`Missing CSS rule ${selector}`);
    return;
  }
  const escapedProperty = property.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  const escapedExpected = expected.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  if (!new RegExp(`${escapedProperty}\\s*:\\s*${escapedExpected}\\s*;`).test(match[1])) {
    fail(`Expected ${selector} to contain ${property}: ${expected};`);
  }
}

// Concept-image structure.
expectText(html, 'class="app-header"', "Expected the dark Gauge Glide header.");
expectText(html, 'class="brand-lockup"', "Expected the University of Auckland and Gauge Glide lockup.");
expectText(html, 'class="system-status-card"', "Expected the System OFF/ON status card.");
expectText(html, 'class="speed-gauge"', "Expected the semicircular speed gauge.");
expectText(html, 'id="motor-speed-percent"', "Expected a dedicated speed percentage display.");
expectText(html, 'class="bottom-nav"', "Expected the bottom Speed Control / Distance Control navigation.");
expectText(html, 'class="page-nav-button active"', "Expected an active bottom navigation item.");
expectText(html, 'data-page="0"', "Expected Speed Control navigation target.");
expectText(html, 'data-page="1"', "Expected Distance Control navigation target.");
expectText(html, 'class="distance-summary-card"', "Expected combined target and travelled distance summary.");
expectText(html, 'class="zero-helper"', "Expected explanatory text under ZERO.");

// Core existing controls must remain available to the JavaScript/controller interface.
[
  "motor-speed-slider",
  "motor-on",
  "motor-off",
  "message",
  "distance-input",
  "distance-cw",
  "distance-ccw",
  "distance-start",
  "distance-stop",
  "distance-zero",
  "distance-position",
  "distance-status",
  "distance-message",
  "distance-connection-message"
].forEach((id) => expectText(html, `id="${id}"`, `Missing required UI control #${id}.`));

// Visual contract matching the supplied concepts.
expectCss(".app-header", "background", "linear-gradient(135deg, #112338 0%, #0b1a2b 100%)");
expectCss(".device-shell", "border-radius", "24px");
expectCss(".action-on", "background", "linear-gradient(135deg, #22aa84 0%, #239a75 100%)");
expectCss(".action-off", "background", "linear-gradient(135deg, #f2262f 0%, #e72b34 100%)");
expectCss(".bottom-nav", "border-top", "1px solid var(--line)");

// Motor/control command semantics must remain unchanged by this UI-only redesign.
[
  'sendCommand({ cmd: "on" })',
  'sendCommand({ cmd: "off" })',
  'sendCommand({ cmd: "speed", value: valueToSend })',
  'sendCommand({ cmd: "distance_start", distanceCm, direction: selectedDistanceDirection })',
  'sendCommand({ cmd: "distance_stop" })',
  'sendCommand({ cmd: "distance_zero" })',
  'endpoint = `/motor/speed?value=${encodeURIComponent(cmd.value)}`',
  'endpoint = "/motor/on"',
  'endpoint = "/motor/off"',
  'endpoint = `/distance/start?distance=${encodeURIComponent(cmd.distanceCm)}&direction=${encodeURIComponent(cmd.direction)}`',
  'endpoint = "/distance/stop"',
  'endpoint = "/distance/zero"'
].forEach((text) => expectText(script, text, `Control logic contract changed: ${text}`));

if (process.exitCode) process.exit(process.exitCode);
console.log("Concept UI contract passed.");
