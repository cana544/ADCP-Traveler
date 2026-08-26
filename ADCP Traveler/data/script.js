const stateElement = document.getElementById("motor-state");
const stateElements = Array.from(document.querySelectorAll(".motor-state-value"));
const wifiSignalElements = Array.from(document.querySelectorAll(".wifi-signal-value"));
const wifiBarsElements = Array.from(document.querySelectorAll(".wifi-bars"));
const messageElement = document.getElementById("message");
const connectionMessageElements = Array.from(document.querySelectorAll(".connection-message"));
const motorSpeedSlider = document.getElementById("motor-speed-slider");
const motorSpeedValue = document.getElementById("motor-speed-value");
const motorControl = document.querySelector(".motor-control");
const buttons = Array.from(document.querySelectorAll(".action-button"));
const pageWindow = document.getElementById("page-window");
const pageTrack = document.getElementById("page-track");
const pageDots = Array.from(document.querySelectorAll(".page-dot"));

const distanceInput = document.getElementById("distance-input");
const distanceCwButton = document.getElementById("distance-cw");
const distanceCcwButton = document.getElementById("distance-ccw");
const distanceStartButton = document.getElementById("distance-start");
const distanceStopButton = document.getElementById("distance-stop");
const distanceZeroButton = document.getElementById("distance-zero");
const distancePosition = document.getElementById("distance-position");
const distanceStatus = document.getElementById("distance-status");
const distanceMessage = document.getElementById("distance-message");

let currentSpeed = 0;
let isUserDragging = false;
let ws = null;
let speedSendTimer = null;
let pendingSpeedValue = null;
let currentPage = 0;
let swipeStartX = 0;
let swipeStartY = 0;
let isSwiping = false;
let selectedDistanceDirection = null;

function showPage(pageIndex) {
  currentPage = Math.max(0, Math.min(pageDots.length - 1, pageIndex));
  pageTrack.style.transform = `translateX(-${currentPage * 50}%)`;

  pageDots.forEach((dot, index) => {
    const active = index === currentPage;
    dot.classList.toggle("active", active);
    dot.setAttribute("aria-current", active ? "true" : "false");
  });
}

function handleSwipeStart(event) {
  if (event.target.closest("input, button")) {
    isSwiping = false;
    return;
  }
  const point = event.touches ? event.touches[0] : event;
  swipeStartX = point.clientX;
  swipeStartY = point.clientY;
  isSwiping = true;
}

function handleSwipeEnd(event) {
  if (!isSwiping) return;
  const point = event.changedTouches ? event.changedTouches[0] : event;
  const deltaX = point.clientX - swipeStartX;
  const deltaY = point.clientY - swipeStartY;
  isSwiping = false;
  if (Math.abs(deltaX) < 50 || Math.abs(deltaX) < Math.abs(deltaY)) return;
  showPage(deltaX < 0 ? currentPage + 1 : currentPage - 1);
}

function setButtonsDisabled(disabled) {
  buttons.forEach((button) => {
    if (button !== distanceStopButton) button.disabled = disabled;
  });
}

function updateMotorSpeedDisplay(speed) {
  currentSpeed = Math.max(-255, Math.min(255, speed));
  const percentage = Math.round((Math.abs(currentSpeed) / 255) * 100);
  let direction = "STOP";
  if (currentSpeed > 0) direction = "CW";
  else if (currentSpeed < 0) direction = "CCW";

  motorSpeedValue.textContent = currentSpeed === 0 ? "STOP" : `${direction} ${percentage}%`;
  if (!isUserDragging) motorSpeedSlider.value = currentSpeed;
  motorControl.dataset.direction = direction.toLowerCase();
}

function updateState(state, speed) {
  const enabled = state === "on";
  let stateText = "STOPPED";
  if (!enabled) stateText = "OFF";
  else if (speed > 0) stateText = "CW";
  else if (speed < 0) stateText = "CCW";

  stateElements.forEach((element) => {
    element.textContent = stateText;
    element.classList.toggle("status-on", enabled);
    element.classList.toggle("status-off", !enabled);
  });
  updateMotorSpeedDisplay(Number.isFinite(speed) ? speed : 0);
}

function updateDistanceState(data) {
  if (Number.isFinite(data.positionCm)) {
    distancePosition.textContent = `${data.positionCm.toFixed(1)} cm`;
  }

  if (typeof data.distanceStatus === "string") {
    distanceStatus.textContent = data.distanceStatus;
  }

  const active = Boolean(data.distanceActive);
  distanceInput.disabled = active;
  distanceCwButton.disabled = active;
  distanceCcwButton.disabled = active;
  distanceStartButton.disabled = active;
  distanceZeroButton.disabled = active;
  distanceStopButton.disabled = false;
}

function applyStateMessage(data) {
  if (data.state !== undefined && data.speed !== undefined) {
    updateState(data.state, data.speed);
  }
  updateDistanceState(data);
}

function updateWifiSignal(data) {
  const quality = Number.isFinite(data.quality) ? data.quality : 0;
  const text = !data.connected
    ? "No device connected"
    : Number.isFinite(data.rssi)
    ? `${data.label.toUpperCase()} (${data.rssi} dBm)`
    : data.label.toUpperCase();

  wifiBarsElements.forEach((element) => {
    element.dataset.quality = String(quality);
  });
  wifiSignalElements.forEach((element) => {
    element.textContent = text;
    element.classList.toggle("status-on", Boolean(data.connected));
    element.classList.toggle("status-off", !data.connected);
  });
}

async function refreshWifiSignal() {
  try {
    const response = await fetch("/wifi/signal", { keepalive: true });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    updateWifiSignal(await response.json());
  } catch (error) {
    wifiBarsElements.forEach((element) => {
      element.dataset.quality = "0";
    });
    wifiSignalElements.forEach((element) => {
      element.textContent = "Unavailable";
      element.classList.remove("status-on");
      element.classList.add("status-off");
    });
  }
}

function setConnectionMessage(text) {
  connectionMessageElements.forEach((element) => {
    element.textContent = text;
  });
}

function connectWebSocket() {
  const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
  ws = new WebSocket(`${protocol}//${window.location.host}/ws`);

  ws.onopen = () => {
    setConnectionMessage("Connected to ESP32 via WebSocket");
    setButtonsDisabled(false);
    ws.send(JSON.stringify({ cmd: "status" }));
  };

  ws.onmessage = (event) => {
    try {
      applyStateMessage(JSON.parse(event.data));
    } catch (error) {
      console.error("Failed to parse message:", error);
    }
  };

  ws.onerror = () => {
    setConnectionMessage("WebSocket connection error");
  };

  ws.onclose = () => {
    setConnectionMessage("Disconnected from ESP32. Reconnecting...");
    setButtonsDisabled(true);
    setTimeout(connectWebSocket, 3000);
  };
}

function sendWebSocketCommand(cmd) {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(cmd));
    return true;
  }
  return false;
}

async function sendHttpCommand(cmd) {
  let endpoint = null;
  if (cmd.cmd === "speed") endpoint = `/motor/speed?value=${encodeURIComponent(cmd.value)}`;
  else if (cmd.cmd === "on") endpoint = "/motor/on";
  else if (cmd.cmd === "off") endpoint = "/motor/off";
  else if (cmd.cmd === "status") endpoint = "/motor/status";
  else if (cmd.cmd === "distance_start") {
    endpoint = `/distance/start?distance=${encodeURIComponent(cmd.distanceCm)}&direction=${encodeURIComponent(cmd.direction)}`;
  } else if (cmd.cmd === "distance_stop") endpoint = "/distance/stop";
  else if (cmd.cmd === "distance_zero") endpoint = "/distance/zero";
  else if (cmd.cmd === "distance_status") endpoint = "/distance/status";

  if (!endpoint) return;

  try {
    const response = await fetch(endpoint, { cache: "no-store" });
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || `HTTP ${response.status}`);
    applyStateMessage(data);
  } catch (error) {
    if (cmd.cmd.startsWith("distance_")) distanceMessage.textContent = error.message;
    else messageElement.textContent = "Motor command failed";
    console.error("Command failed:", error);
  }
}

function sendCommand(cmd) {
  if (!sendWebSocketCommand(cmd)) sendHttpCommand(cmd);
}

function sendSpeedValue(speedValue) {
  pendingSpeedValue = speedValue;
  if (speedSendTimer) return;
  speedSendTimer = setTimeout(() => {
    speedSendTimer = null;
    if (pendingSpeedValue === null) return;
    const valueToSend = pendingSpeedValue;
    pendingSpeedValue = null;
    sendCommand({ cmd: "speed", value: valueToSend });
  }, 30);
}

function sendOnCommand() {
  messageElement.textContent = "Enabling motor...";
  sendCommand({ cmd: "on" });
}

function sendOffCommand() {
  messageElement.textContent = "Stopping motor...";
  updateMotorSpeedDisplay(0);
  sendCommand({ cmd: "off" });
}

function selectDistanceDirection(direction) {
  selectedDistanceDirection = direction;
  distanceCwButton.classList.toggle("selected", direction === "cw");
  distanceCcwButton.classList.toggle("selected", direction === "ccw");
}

function startDistanceMove() {
  const distanceCm = Number.parseFloat(distanceInput.value);
  if (!Number.isFinite(distanceCm) || distanceCm <= 0) {
    distanceMessage.textContent = "Enter a distance greater than 0 cm";
    return;
  }
  if (!selectedDistanceDirection) {
    distanceMessage.textContent = "Select CW or CCW";
    return;
  }
  distanceMessage.textContent = "Starting move...";
  sendCommand({ cmd: "distance_start", distanceCm, direction: selectedDistanceDirection });
}

function stopDistanceMove() {
  distanceMessage.textContent = "Stopping...";
  sendCommand({ cmd: "distance_stop" });
}

function zeroDistancePosition() {
  distanceMessage.textContent = "Setting zero...";
  sendCommand({ cmd: "distance_zero" });
}

motorSpeedSlider.addEventListener("mousedown", () => { isUserDragging = true; });
motorSpeedSlider.addEventListener("touchstart", () => { isUserDragging = true; });
motorSpeedSlider.addEventListener("mouseup", () => { isUserDragging = false; });
motorSpeedSlider.addEventListener("touchend", () => { isUserDragging = false; });
motorSpeedSlider.addEventListener("input", (event) => {
  const sliderValue = parseInt(event.target.value, 10);
  updateMotorSpeedDisplay(sliderValue);
  messageElement.textContent = "Adjusting motor speed...";
  sendSpeedValue(sliderValue);
});
motorSpeedSlider.addEventListener("change", (event) => {
  sendSpeedValue(parseInt(event.target.value, 10));
});

document.getElementById("motor-on").addEventListener("click", sendOnCommand);
document.getElementById("motor-off").addEventListener("click", sendOffCommand);
distanceCwButton.addEventListener("click", () => selectDistanceDirection("cw"));
distanceCcwButton.addEventListener("click", () => selectDistanceDirection("ccw"));
distanceStartButton.addEventListener("click", startDistanceMove);
distanceStopButton.addEventListener("click", stopDistanceMove);
distanceZeroButton.addEventListener("click", zeroDistancePosition);

pageWindow.addEventListener("touchstart", handleSwipeStart, { passive: true });
pageWindow.addEventListener("touchend", handleSwipeEnd);
pageWindow.addEventListener("mousedown", handleSwipeStart);
pageWindow.addEventListener("mouseup", handleSwipeEnd);
pageDots.forEach((dot) => {
  dot.addEventListener("click", () => showPage(parseInt(dot.dataset.page, 10)));
});

showPage(0);
connectWebSocket();
refreshWifiSignal();
setInterval(refreshWifiSignal, 5000);
