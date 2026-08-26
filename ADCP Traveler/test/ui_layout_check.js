const fs = require("fs");
const path = require("path");

const root = path.resolve(__dirname, "..");
const html = fs.readFileSync(path.join(root, "data", "index.html"), "utf8");
const css = fs.readFileSync(path.join(root, "data", "style.css"), "utf8");

function fail(message) {
  console.error(message);
  process.exitCode = 1;
}

function ruleBody(selector) {
  const escaped = selector.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  const match = css.match(new RegExp(`${escaped}\\s*\\{([^}]*)\\}`, "m"));
  return match ? match[1] : "";
}

function hasDeclaration(selector, property, expected) {
  const body = ruleBody(selector);
  const escapedProperty = property.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  const escapedExpected = expected.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  return new RegExp(`${escapedProperty}\\s*:\\s*${escapedExpected}\\s*;`).test(body);
}

function mediaBody(query) {
  const start = css.indexOf(`@media ${query}`);
  if (start === -1) return "";
  const blockStart = css.indexOf("{", start);
  if (blockStart === -1) return "";

  let depth = 0;
  for (let index = blockStart; index < css.length; index++) {
    if (css[index] === "{") depth++;
    if (css[index] === "}") depth--;
    if (depth === 0) {
      return css.slice(blockStart + 1, index);
    }
  }
  return "";
}

function mediaHasDeclaration(query, selector, property, expected) {
  const body = mediaBody(query);
  const escapedSelector = selector.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  const match = body.match(new RegExp(`${escapedSelector}\\s*\\{([^}]*)\\}`, "m"));
  if (!match) return false;
  const escapedProperty = property.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  const escapedExpected = expected.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  return new RegExp(`${escapedProperty}\\s*:\\s*${escapedExpected}\\s*;`).test(match[1]);
}

if (!html.includes('<div class="page-window" id="page-window">')) {
  fail("Expected the carousel page window to wrap both control pages.");
}

if (!html.includes('<div class="page-viewport">')) {
  fail("Expected an inner clipped page viewport so adjacent slides cannot bleed through card padding.");
}

if (!hasDeclaration(".page-window", "background", "var(--card)")) {
  fail("Expected .page-window to be the shared white panel background.");
}

if (!hasDeclaration(".page-window", "width", "min(100%, 460px)")) {
  fail("Expected .page-window to use the same fixed responsive width for both pages.");
}

if (!hasDeclaration(".page-window", "position", "relative")) {
  fail("Expected .page-window to anchor the page dots inside the white panel.");
}

if (!hasDeclaration(".page-viewport", "overflow", "hidden")) {
  fail("Expected .page-viewport to clip the sliding pages.");
}

if (hasDeclaration(".page-window", "overflow", "hidden")) {
  fail("Expected .page-window not to be the clipping viewport because its padding reveals adjacent pages.");
}

if (!hasDeclaration(".control-card", "height", "100%")) {
  fail("Expected each page's content box to stretch to the shared panel height.");
}

if (!hasDeclaration(".app-page", "flex", "0 0 50%")) {
  fail("Expected each carousel page to have a fixed non-shrinking width.");
}

if (!hasDeclaration(".app-page", "overflow", "hidden")) {
  fail("Expected each carousel page to clip adjacent page controls at the boundary.");
}

if (!hasDeclaration(".page-dots", "position", "absolute")) {
  fail("Expected page dots to be absolutely positioned inside the shared panel.");
}

if (!hasDeclaration(".page-dots", "bottom", "18px")) {
  fail("Expected page dots to sit at the bottom inside the shared panel.");
}

const wifiPanelCount = (html.match(/class="status-panel wifi-panel"/g) || []).length;
if (wifiPanelCount !== 2) {
  fail("Expected both Speed Control and Distance Control pages to show a Wi-Fi signal panel.");
}

const motorStateCount = (html.match(/motor-state-value/g) || []).length;
if (motorStateCount !== 2) {
  fail("Expected both pages to show the same current-state UI.");
}

const connectionMessageCount = (html.match(/class="message connection-message"/g) || []).length;
if (connectionMessageCount !== 2) {
  fail("Expected both pages to show the same connection status message.");
}

if (!html.includes('id="distance-connection-message"')) {
  fail("Expected the distance page to include its own connection status message.");
}

if (!html.includes('<div class="button-row motor-button-row">')) {
  fail("Expected speed control buttons to use the full-width stacked button row.");
}

if (!html.includes('<div class="distance-control">')) {
  fail("Expected distance input to use the same header-and-control block as motor speed.");
}

if (!hasDeclaration(".motor-button-row", "grid-template-columns", "1fr")) {
  fail("Expected speed control buttons to span the page width.");
}

if (!hasDeclaration(".action-button", "box-shadow", "none")) {
  fail("Expected action buttons to have no colored glow or blur.");
}

if (!hasDeclaration(".motor-control", "box-shadow", "none")) {
  fail("Expected motor control box to have no blur or shadow.");
}

if (!mediaHasDeclaration("(max-width: 520px)", ".app-shell", "padding", "10px")) {
  fail("Expected mobile shell padding to be compact enough for an iPhone viewport.");
}

if (!mediaHasDeclaration("(max-width: 520px)", ".page-window", "min-height", "calc(100vh - 20px)")) {
  fail("Expected mobile page window to use nearly the full iPhone viewport height.");
}

if (!mediaHasDeclaration("(max-width: 520px)", ".status-panel", "margin-bottom", "4px")) {
  fail("Expected mobile status panels to sit closer together.");
}

if (!mediaHasDeclaration("(max-width: 520px)", ".distance-control", "margin", "7px 0 6px")) {
  fail("Expected mobile distance control spacing to be compact.");
}

if (!hasDeclaration(".message:empty", "display", "none")) {
  fail("Expected empty message rows to collapse so connection spacing matches both pages.");
}

if (process.exitCode) {
  process.exit(process.exitCode);
}

console.log("UI layout contract passed.");
