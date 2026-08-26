const fs = require("fs");
const path = require("path");

const root = path.resolve(__dirname, "..");
const html = fs.readFileSync(path.join(root, "data", "index.html"), "utf8");
const css = fs.readFileSync(path.join(root, "data", "style.css"), "utf8");
const script = fs.readFileSync(path.join(root, "data", "script.js"), "utf8");
const logoPath = path.join(root, "data", "uoa-logo-white.png");

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
    if (depth === 0) return css.slice(blockStart + 1, index);
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

if (!fs.existsSync(logoPath)) {
  fail("Expected a local University of Auckland logo asset for ESP32 preview use.");
}

if (!html.includes('src="/uoa-logo-white.png"')) {
  fail("Expected the header to render the inverted University of Auckland PNG logo asset.");
}

if (!hasDeclaration(".brand-logo", "height", "52px")) {
  fail("Expected the logo to be sized cleanly in the desktop header.");
}

if (!hasDeclaration(".distance-field-block", "display", "grid")) {
  fail("Expected target distance label and input to use a grid for vertical alignment.");
}

if (!hasDeclaration(".travelled-block", "display", "grid")) {
  fail("Expected travelled text to align with the target distance label row.");
}

if (!hasDeclaration(".distance-field-label", "align-self", "center")) {
  fail("Expected target distance and travelled labels to be centred above the input row.");
}

if (!mediaHasDeclaration("(max-width: 640px)", ".app-header", "padding", "12px 14px")) {
  fail("Expected iPhone header padding to be compact.");
}

if (!mediaHasDeclaration("(max-width: 640px)", ".page-window", "height", "100dvh")) {
  fail("Expected mobile page window to fit exactly within an iPhone viewport.");
}

if (!mediaHasDeclaration("(max-width: 640px)", ".page-viewport", "min-height", "0")) {
  fail("Expected mobile page viewport to shrink within the app shell.");
}

if (!mediaHasDeclaration("(max-width: 640px)", ".brand-logo", "height", "28px")) {
  fail("Expected the logo to fit in an iPhone header.");
}

if (!mediaHasDeclaration("(max-width: 640px)", ".page-content", "padding", "10px 12px 8px")) {
  fail("Expected page content padding to fit iPhone screens.");
}

if (!mediaHasDeclaration("(max-width: 640px)", ".arc-widget", "min-height", "180px")) {
  fail("Expected the speed control arc to fit iPhone screens.");
}

if (!html.includes('<div class="arc-axis-label arc-axis-left">CCW</div>')) {
  fail("Expected the speed control page to keep the CCW side indicator label.");
}

if (!html.includes('<div class="arc-axis-label arc-axis-right">CW</div>')) {
  fail("Expected the speed control page to keep the CW side indicator label.");
}

if (html.includes("arc-axis-centre") || html.includes("motor-direction-display")) {
  fail("Expected the speed control page to omit only the middle STOP direction indicator.");
}

if (!mediaHasDeclaration("(max-width: 640px)", ".nav-button", "min-height", "58px")) {
  fail("Expected bottom navigation to fit iPhone screens.");
}

if (!mediaHasDeclaration("(max-width: 640px)", ".distance-columns", "grid-template-columns", "minmax(0, 1fr) auto")) {
  fail("Expected mobile distance panel to keep target and travelled fields in one compact row.");
}

if (html.includes('id="distance-input"') && html.includes('placeholder="500"')) {
  fail("Expected the target distance input to start empty without a 500 placeholder.");
}

if (!mediaHasDeclaration("(max-width: 640px)", ".action-button", "min-height", "48px")) {
  fail("Expected mobile action buttons to be short enough for one-screen use.");
}

if (!hasDeclaration(".message:empty", "display", "none")) {
  fail("Expected empty message rows to collapse for one-screen use.");
}

if (script.includes("motorArcHitArea.setAttribute('aria-disabled'")) {
  fail("Expected disconnected preview state not to disable the speed arc drag area.");
}

if (script.includes("getAttribute('aria-disabled') === 'true'")) {
  fail("Expected speed arc drag and keyboard handlers not to ignore input while preview is disconnected.");
}

if (process.exitCode) process.exit(process.exitCode);

console.log("UI layout contract passed.");
