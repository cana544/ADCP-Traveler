const stateElements = Array.from(document.querySelectorAll('.motor-state-value'));
const wifiSignalElements = Array.from(document.querySelectorAll('.wifi-signal-value'));
const messageElement = document.getElementById('message');
const connectionMessageElements = Array.from(document.querySelectorAll('.connection-message'));
const motorSpeedSlider = document.getElementById('motor-speed-slider');
const motorSpeedValue = document.getElementById('motor-speed-value');
const motorSpeedPercent = document.getElementById('motor-speed-percent');
const motorControl = document.getElementById('motor-arc-control');
const motorArcSvg = document.getElementById('motor-arc-svg');
const motorArcKnob = document.getElementById('motor-arc-knob');
const motorArcTicks = document.getElementById('motor-arc-ticks');
const motorArcHitArea = document.getElementById('motor-arc-hit-area');
const pageWindow = document.getElementById('page-window');
const pageTrack = document.getElementById('page-track');
const pageButtons = Array.from(document.querySelectorAll('.nav-button'));
const actionButtons = Array.from(document.querySelectorAll('.action-button'));
const systemToggleButtons = Array.from(document.querySelectorAll('.system-toggle'));
const systemToggleLabels = Array.from(document.querySelectorAll('.system-toggle-label'));
const connectionPill = document.getElementById('connection-pill');
const connectionStatusLabel = document.getElementById('connection-status-label');

const distanceInput = document.getElementById('distance-input');
const distanceCwButton = document.getElementById('distance-cw');
const distanceCcwButton = document.getElementById('distance-ccw');
const distanceStartButton = document.getElementById('distance-start');
const distanceStopButton = document.getElementById('distance-stop');
const distanceZeroButton = document.getElementById('distance-zero');
const distancePosition = document.getElementById('distance-position');
const distanceStatus = document.getElementById('distance-status');
const distanceMessage = document.getElementById('distance-message');

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
let motorEnabled = false;

const arcConfig = {
  cx: 200,
  cy: 190,
  radius: 165,
  minAngle: 180,
  maxAngle: 0,
};

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function titleCase(text) {
  if (!text) return '';
  return text
    .toString()
    .toLowerCase()
    .split(/\s+/)
    .map((word) => word.charAt(0).toUpperCase() + word.slice(1))
    .join(' ');
}

function valueToAngle(speed) {
  const normalised = clamp(speed, -255, 255) / 255;
  return 90 - normalised * 90;
}

function angleToPoint(angleDegrees) {
  const radians = (angleDegrees * Math.PI) / 180;
  return {
    x: arcConfig.cx + arcConfig.radius * Math.cos(radians),
    y: arcConfig.cy - arcConfig.radius * Math.sin(radians),
  };
}

function pointToSpeed(x, y) {
  const dx = x - arcConfig.cx;
  const dy = arcConfig.cy - y;
  const angle = clamp((Math.atan2(dy, dx) * 180) / Math.PI, 0, 180);
  const normalised = (90 - angle) / 90;
  return Math.round(clamp(normalised, -1, 1) * 255);
}

function renderArcTicks() {
  const tickValues = [-204, -153, -102, -51, 0, 51, 102, 153, 204];
  motorArcTicks.innerHTML = '';

  tickValues.forEach((value) => {
    const angle = valueToAngle(value);
    const outerPoint = angleToPoint(angle);
    const innerRadius = value === 0 ? arcConfig.radius - 24 : arcConfig.radius - 15;
    const innerPoint = {
      x: arcConfig.cx + innerRadius * Math.cos((angle * Math.PI) / 180),
      y: arcConfig.cy - innerRadius * Math.sin((angle * Math.PI) / 180),
    };

    const tick = document.createElementNS('http://www.w3.org/2000/svg', 'line');
    tick.setAttribute('x1', innerPoint.x.toFixed(2));
    tick.setAttribute('y1', innerPoint.y.toFixed(2));
    tick.setAttribute('x2', outerPoint.x.toFixed(2));
    tick.setAttribute('y2', outerPoint.y.toFixed(2));
    motorArcTicks.appendChild(tick);
  });
}

function positionArcKnob(speed) {
  const angle = valueToAngle(speed);
  const point = angleToPoint(angle);
  motorArcKnob.setAttribute('cx', point.x.toFixed(2));
  motorArcKnob.setAttribute('cy', point.y.toFixed(2));
}

function updateSystemToggleDisplay(enabled) {
  motorEnabled = enabled;
  systemToggleButtons.forEach((button) => {
    button.classList.toggle('is-on', enabled);
    button.classList.toggle('is-off', !enabled);
    button.setAttribute('aria-pressed', enabled ? 'true' : 'false');
  });

  systemToggleLabels.forEach((label) => {
    label.textContent = enabled ? 'System ON' : 'System OFF';
  });
}

function updateMotorSpeedDisplay(speed) {
  currentSpeed = clamp(Number.isFinite(speed) ? speed : 0, -255, 255);
  const percentage = Math.round((Math.abs(currentSpeed) / 255) * 100);
  let direction = 'STOP';

  if (currentSpeed > 0) direction = 'CW';
  else if (currentSpeed < 0) direction = 'CCW';

  motorSpeedValue.textContent = currentSpeed === 0 ? 'STOP' : direction;
  motorSpeedPercent.textContent = `${percentage}%`;
  motorControl.dataset.direction = direction.toLowerCase();
  positionArcKnob(currentSpeed);

  if (!isUserDragging) {
    motorSpeedSlider.value = String(currentSpeed);
  }

  motorArcHitArea.setAttribute('aria-valuenow', String(currentSpeed));
  motorArcHitArea.setAttribute('aria-valuetext', `${direction} ${percentage}%`);
}

function updateState(state, speed) {
  const enabled = state === 'on';
  let stateText = 'OFF';

  if (enabled) {
    if (speed > 0) stateText = 'CW';
    else if (speed < 0) stateText = 'CCW';
    else stateText = 'STOP';
  }

  stateElements.forEach((element) => {
    element.textContent = stateText;
    element.classList.toggle('status-on', enabled);
    element.classList.toggle('status-off', !enabled);
  });

  updateSystemToggleDisplay(enabled);
  updateMotorSpeedDisplay(Number.isFinite(speed) ? speed : 0);
}

function setDistanceStatusTone(statusText) {
  const normalised = (statusText || '').toUpperCase();
  distanceStatus.classList.remove('status-on', 'status-off', 'status-accent');

  if (['COMPLETE', 'DONE', 'IDLE'].includes(normalised)) {
    distanceStatus.classList.add('status-accent');
  } else if (['MOVING', 'RUNNING', 'ACTIVE'].includes(normalised)) {
    distanceStatus.classList.add('status-on');
  } else if (['ERROR', 'STOPPED', 'CANCELLED'].includes(normalised)) {
    distanceStatus.classList.add('status-off');
  }
}

function updateDistanceState(data) {
  if (Number.isFinite(data.positionCm)) {
    distancePosition.textContent = `${data.positionCm.toFixed(1)} cm`;
  }

  if (typeof data.distanceStatus === 'string') {
    distanceStatus.textContent = data.distanceStatus.toUpperCase();
    setDistanceStatusTone(data.distanceStatus);
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
  const connected = Boolean(data.connected);
  let text = 'Unavailable';

  if (!connected) {
    text = 'Disconnected';
  } else if (typeof data.label === 'string' && data.label.trim()) {
    text = titleCase(data.label);
  } else {
    text = 'Connected';
  }

  wifiSignalElements.forEach((element) => {
    element.textContent = text;
    element.classList.toggle('status-accent', connected);
    element.classList.toggle('status-off', !connected);
  });
}

async function refreshWifiSignal() {
  try {
    const response = await fetch('/wifi/signal', { keepalive: true });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    updateWifiSignal(await response.json());
  } catch (error) {
    updateWifiSignal({ connected: false, label: 'disconnected' });
  }
}

function setConnectionState(isConnected, labelText) {
  connectionPill.classList.toggle('connected', isConnected);
  connectionPill.classList.toggle('disconnected', !isConnected);
  connectionStatusLabel.textContent = labelText;
}

function setConnectionMessage(text) {
  connectionMessageElements.forEach((element) => {
    element.textContent = text;
  });
}

function showPage(pageIndex) {
  currentPage = clamp(pageIndex, 0, pageButtons.length - 1);
  pageTrack.style.transform = `translateX(-${currentPage * 50}%)`;

  pageButtons.forEach((button, index) => {
    const active = index === currentPage;
    button.classList.toggle('active', active);
    button.setAttribute('aria-current', active ? 'true' : 'false');
  });
}

function handleSwipeStart(event) {
  if (event.target.closest('button, input, .arc-hit-area')) {
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
  actionButtons.forEach((button) => {
    if (button !== distanceStopButton) button.disabled = disabled;
  });
}

function connectWebSocket() {
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  ws = new WebSocket(`${protocol}//${window.location.host}/ws`);

  ws.onopen = () => {
    setConnectionState(true, 'CONNECTED');
    setConnectionMessage('Connected to ESP32');
    setButtonsDisabled(false);
    ws.send(JSON.stringify({ cmd: 'status' }));
  };

  ws.onmessage = (event) => {
    try {
      applyStateMessage(JSON.parse(event.data));
    } catch (error) {
      console.error('Failed to parse message:', error);
    }
  };

  ws.onerror = () => {
    setConnectionState(false, 'CONNECTION ERROR');
    setConnectionMessage('WebSocket connection error');
  };

  ws.onclose = () => {
    setConnectionState(false, 'DISCONNECTED');
    setConnectionMessage('Disconnected from ESP32. Reconnecting...');
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
  if (cmd.cmd === 'speed') endpoint = `/motor/speed?value=${encodeURIComponent(cmd.value)}`;
  else if (cmd.cmd === 'on') endpoint = '/motor/on';
  else if (cmd.cmd === 'off') endpoint = '/motor/off';
  else if (cmd.cmd === 'status') endpoint = '/motor/status';
  else if (cmd.cmd === 'distance_start') {
    endpoint = `/distance/start?distance=${encodeURIComponent(cmd.distanceCm)}&direction=${encodeURIComponent(cmd.direction)}`;
  } else if (cmd.cmd === 'distance_stop') endpoint = '/distance/stop';
  else if (cmd.cmd === 'distance_zero') endpoint = '/distance/zero';
  else if (cmd.cmd === 'distance_status') endpoint = '/distance/status';

  if (!endpoint) return;

  try {
    const response = await fetch(endpoint, { cache: 'no-store' });
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || `HTTP ${response.status}`);
    applyStateMessage(data);
  } catch (error) {
    if (cmd.cmd.startsWith('distance_')) {
      distanceMessage.textContent = error.message;
    } else {
      messageElement.textContent = 'Motor command failed';
    }
    console.error('Command failed:', error);
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
    sendCommand({ cmd: 'speed', value: valueToSend });
  }, 30);
}

function sendOnCommand() {
  messageElement.textContent = 'Enabling motor...';
  sendCommand({ cmd: 'on' });
}

function sendOffCommand() {
  messageElement.textContent = 'Stopping motor...';
  updateMotorSpeedDisplay(0);
  sendCommand({ cmd: 'off' });
}

function toggleSystemPower() {
  if (motorEnabled) sendOffCommand();
  else sendOnCommand();
}

function selectDistanceDirection(direction) {
  selectedDistanceDirection = direction;
  distanceCwButton.classList.toggle('selected', direction === 'cw');
  distanceCcwButton.classList.toggle('selected', direction === 'ccw');
}

function startDistanceMove() {
  const distanceCm = Number.parseFloat(distanceInput.value);
  if (!Number.isFinite(distanceCm) || distanceCm <= 0) {
    distanceMessage.textContent = 'Enter a distance greater than 0 cm';
    return;
  }
  if (!selectedDistanceDirection) {
    distanceMessage.textContent = 'Select CW or CCW';
    return;
  }
  distanceMessage.textContent = 'Starting move...';
  sendCommand({ cmd: 'distance_start', distanceCm, direction: selectedDistanceDirection });
}

function stopDistanceMove() {
  distanceMessage.textContent = 'Stopping...';
  sendCommand({ cmd: 'distance_stop' });
}

function zeroDistancePosition() {
  distanceMessage.textContent = 'Setting zero...';
  sendCommand({ cmd: 'distance_zero' });
}

function updateArcFromPointerEvent(event) {
  const rect = motorArcSvg.getBoundingClientRect();
  const clientX = event.clientX;
  const clientY = event.clientY;
  const x = ((clientX - rect.left) / rect.width) * 400;
  const y = ((clientY - rect.top) / rect.height) * 240;
  const speed = pointToSpeed(x, y);

  updateMotorSpeedDisplay(speed);
  messageElement.textContent = 'Adjusting motor speed...';
  sendSpeedValue(speed);
}

function beginArcDrag(event) {
  isUserDragging = true;
  event.preventDefault();
  motorArcHitArea.setPointerCapture(event.pointerId);
  updateArcFromPointerEvent(event);
}

function continueArcDrag(event) {
  if (!isUserDragging) return;
  updateArcFromPointerEvent(event);
}

function endArcDrag(event) {
  if (!isUserDragging) return;
  if (typeof event.pointerId === 'number') {
    try {
      motorArcHitArea.releasePointerCapture(event.pointerId);
    } catch (error) {
      // Ignore capture release errors.
    }
  }
  isUserDragging = false;
}

function handleArcKeydown(event) {
  let nextSpeed = currentSpeed;
  const coarseStep = 25;
  const fineStep = 5;

  switch (event.key) {
    case 'ArrowLeft':
    case 'ArrowDown':
      nextSpeed -= fineStep;
      break;
    case 'ArrowRight':
    case 'ArrowUp':
      nextSpeed += fineStep;
      break;
    case 'PageDown':
      nextSpeed -= coarseStep;
      break;
    case 'PageUp':
      nextSpeed += coarseStep;
      break;
    case 'Home':
      nextSpeed = -255;
      break;
    case 'End':
      nextSpeed = 255;
      break;
    case '0':
      nextSpeed = 0;
      break;
    default:
      return;
  }

  event.preventDefault();
  nextSpeed = clamp(nextSpeed, -255, 255);
  updateMotorSpeedDisplay(nextSpeed);
  messageElement.textContent = 'Adjusting motor speed...';
  sendSpeedValue(nextSpeed);
}

systemToggleButtons.forEach((button) => {
  button.addEventListener('click', toggleSystemPower);
});

document.getElementById('motor-on').addEventListener('click', sendOnCommand);
document.getElementById('motor-off').addEventListener('click', sendOffCommand);
distanceCwButton.addEventListener('click', () => selectDistanceDirection('cw'));
distanceCcwButton.addEventListener('click', () => selectDistanceDirection('ccw'));
distanceStartButton.addEventListener('click', startDistanceMove);
distanceStopButton.addEventListener('click', stopDistanceMove);
distanceZeroButton.addEventListener('click', zeroDistancePosition);

motorArcHitArea.addEventListener('pointerdown', beginArcDrag);
motorArcHitArea.addEventListener('pointermove', continueArcDrag);
motorArcHitArea.addEventListener('pointerup', endArcDrag);
motorArcHitArea.addEventListener('pointercancel', endArcDrag);
motorArcHitArea.addEventListener('keydown', handleArcKeydown);

motorSpeedSlider.addEventListener('input', (event) => {
  const value = Number.parseInt(event.target.value, 10);
  updateMotorSpeedDisplay(value);
  sendSpeedValue(value);
});

pageWindow.addEventListener('touchstart', handleSwipeStart, { passive: true });
pageWindow.addEventListener('touchend', handleSwipeEnd);
pageWindow.addEventListener('mousedown', handleSwipeStart);
pageWindow.addEventListener('mouseup', handleSwipeEnd);

pageButtons.forEach((button) => {
  button.addEventListener('click', () => {
    showPage(Number.parseInt(button.dataset.page, 10));
  });
});

renderArcTicks();
showPage(0);
updateMotorSpeedDisplay(0);
updateSystemToggleDisplay(false);
setDistanceStatusTone('IDLE');
setConnectionState(false, 'CONNECTING');
connectWebSocket();
refreshWifiSignal();
setInterval(refreshWifiSignal, 5000);
