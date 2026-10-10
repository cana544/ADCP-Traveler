const headerBattery = document.querySelector('.header-battery');
const batteryFill = document.querySelector('.battery-fill');
const batteryReadout = document.querySelector('.battery-readout');
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
const headerWifiIcon = document.querySelector('.header-wifi-icon');
const pageWindow = document.getElementById('page-window');
const pageTrack = document.getElementById('page-track');
const pageButtons = Array.from(document.querySelectorAll('.nav-button'));
const actionButtons = Array.from(document.querySelectorAll('.action-button'));
const systemToggleButtons = Array.from(document.querySelectorAll('.system-toggle'));
const systemToggleLabels = Array.from(document.querySelectorAll('.system-toggle-label'));

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
let stateBootId = null;
let stateSequence = null;
const retiredBootIds = new Set();
let stateBootGeneration = 0;
const sectionUI = new SectionControlUI(document, sendSectionCommand);

const arcConfig = {
  cx: 200,
  cy: 150,
  radiusX: 165,
  radiusY: 130,
  minAngle: 160,
  maxAngle: 20,
  trackTolerance: 18,
  dragTolerance: 72,
  knobTolerance: 26,
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
  const midpoint = (arcConfig.minAngle + arcConfig.maxAngle) / 2;
  const halfSpan = (arcConfig.minAngle - arcConfig.maxAngle) / 2;
  return midpoint - normalised * halfSpan;
}

function angleToPoint(angleDegrees) {
  const radians = (angleDegrees * Math.PI) / 180;
  return {
    x: arcConfig.cx + arcConfig.radiusX * Math.cos(radians),
    y: arcConfig.cy - arcConfig.radiusY * Math.sin(radians),
  };
}

function pointToSpeed(x, y) {
  const dx = (x - arcConfig.cx) / arcConfig.radiusX;
  const dy = (arcConfig.cy - y) / arcConfig.radiusY;
  const angle = clamp(
    (Math.atan2(dy, dx) * 180) / Math.PI,
    arcConfig.maxAngle,
    arcConfig.minAngle
  );
  const midpoint = (arcConfig.minAngle + arcConfig.maxAngle) / 2;
  const halfSpan = (arcConfig.minAngle - arcConfig.maxAngle) / 2;
  const normalised = (midpoint - angle) / halfSpan;
  return Math.round(clamp(normalised, -1, 1) * 255);
}

function eventToArcPoint(event) {
  const rect = motorArcSvg.getBoundingClientRect();
  return {
    x: ((event.clientX - rect.left) / rect.width) * 400,
    y: ((event.clientY - rect.top) / rect.height) * 190,
  };
}

function isPointOnArcControl(event, trackTolerance = arcConfig.trackTolerance) {
  const point = eventToArcPoint(event);
  const knobX = Number.parseFloat(motorArcKnob.getAttribute('cx'));
  const knobY = Number.parseFloat(motorArcKnob.getAttribute('cy'));
  const knobDistance = Math.hypot(point.x - knobX, point.y - knobY);

  if (knobDistance <= arcConfig.knobTolerance) return true;

  const dx = (point.x - arcConfig.cx) / arcConfig.radiusX;
  const dy = (arcConfig.cy - point.y) / arcConfig.radiusY;
  const angle = (Math.atan2(dy, dx) * 180) / Math.PI;
  if (angle < arcConfig.maxAngle || angle > arcConfig.minAngle) return false;

  const nearestPoint = angleToPoint(angle);
  return Math.hypot(point.x - nearestPoint.x, point.y - nearestPoint.y) <= trackTolerance;
}

function renderArcTicks() {
  const tickValues = [-204, -153, -102, -51, 0, 51, 102, 153, 204];
  motorArcTicks.innerHTML = '';

  tickValues.forEach((value) => {
    const angle = valueToAngle(value);
    const point = angleToPoint(angle);
    const radians = (angle * Math.PI) / 180;
    // Centre each notch across the track, perpendicular to the ellipse.
    const normalX = Math.cos(radians) / arcConfig.radiusX;
    const normalY = -Math.sin(radians) / arcConfig.radiusY;
    const normalLength = Math.hypot(normalX, normalY);
    const halfLength = (value === 0 ? 24 : 15) / 2;
    const offsetX = (normalX / normalLength) * halfLength;
    const offsetY = (normalY / normalLength) * halfLength;
    const innerPoint = {
      x: point.x - offsetX,
      y: point.y - offsetY,
    };
    const outerPoint = {
      x: point.x + offsetX,
      y: point.y + offsetY,
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
  let direction = 'Stopped';

  if (currentSpeed > 0) direction = 'Right Bank';
  else if (currentSpeed < 0) direction = 'Left Bank';

  motorSpeedValue.textContent = currentSpeed === 0 ? 'Stopped' : direction;
  motorSpeedPercent.textContent = `${percentage}%`;
  motorControl.dataset.direction = currentSpeed > 0 ? 'rb' : currentSpeed < 0 ? 'lb' : 'stop';
  positionArcKnob(currentSpeed);

  if (!isUserDragging) {
    motorSpeedSlider.value = String(currentSpeed);
  }

  motorArcHitArea.setAttribute('aria-valuenow', String(currentSpeed));
  motorArcHitArea.setAttribute('aria-valuetext', `${direction} ${percentage}%`);
}

function updateState(state, speed) {
  const enabled = state === 'on';
  let stateText = 'STOPPED';

  if (enabled) {
    if (speed > 0) stateText = 'Right Bank';
    else if (speed < 0) stateText = 'Left Bank';
    else stateText = 'STOPPED';
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
  if (!distanceStatus) return;
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
  const active = Boolean(data.distanceActive);
  if (Number.isFinite(data.positionCm)) {
    distancePosition.textContent = `${data.positionCm.toFixed(active ? 1 : 0)} cm`;
  }

  if (typeof data.distanceStatus === 'string') {
    if (distanceStatus) {
      distanceStatus.textContent = data.distanceStatus.toUpperCase();
      setDistanceStatusTone(data.distanceStatus);
    }
  }

  distanceInput.disabled = active;
  distanceCwButton.disabled = active;
  distanceCcwButton.disabled = active;
  distanceStartButton.disabled = active;
  distanceZeroButton.disabled = active;
  distanceStopButton.disabled = false;
}

function applyStateMessage(data, requestGeneration) {
  if (Number.isFinite(data.stateSequence)) {
    if (requestGeneration !== undefined && requestGeneration !== stateBootGeneration &&
        data.bootId !== stateBootId) return;
    if (retiredBootIds.has(data.bootId)) return;
    if (data.bootId === stateBootId && stateSequence !== null &&
        ((data.stateSequence - stateSequence) | 0) <= 0) return;
    if (data.bootId !== stateBootId) {
      if (stateBootId !== null) retiredBootIds.add(stateBootId);
      ++stateBootGeneration;
    }
    stateBootId = data.bootId;
    stateSequence = data.stateSequence;
  }
  if (Object.prototype.hasOwnProperty.call(data, 'batteryValid')) updateBattery(data);
  sectionUI.update(data);
  if (typeof data.error === 'string') {
    messageElement.textContent = data.error;
    distanceMessage.textContent = data.error;
    return;
  }
  if (data.state !== undefined && data.speed !== undefined) {
    updateState(data.state, data.speed);
  }

  updateDistanceState(data);
}

function updateBattery(data) {
  if (!headerBattery || !batteryFill || !batteryReadout) return;
  const valid = data.batteryValid === true &&
    Number.isFinite(data.batteryVoltage) && data.batteryVoltage > 0 &&
    Number.isFinite(data.batteryPercent);
  const percent = valid ? Math.min(100, Math.max(0, data.batteryPercent)) : 0;
  const height = 15 * percent / 100;
  batteryFill.setAttribute('height', String(height));
  batteryFill.setAttribute('y', String(21 - height));
  headerBattery.dataset.level = valid ? (percent <= 20 ? 'low' : 'normal') : 'unavailable';
  const text = valid ? `${data.batteryVoltage.toFixed(1)} V | ~${Math.round(percent)}%` : 'Unavailable';
  batteryReadout.textContent = text;
  headerBattery.setAttribute('aria-label', valid ? `Battery ${text}, estimated charge` : 'Battery unavailable');
  headerBattery.setAttribute('title', valid ? `${text} (estimated charge)` : 'Battery unavailable');
}

function updateWifiSignal(data) {
  const connected = Boolean(data.connected);
  let quality = 0;
  let text = 'Unavailable';

  if (!connected) {
    text = 'Disconnected';
  } else if (typeof data.label === 'string' && data.label.trim()) {
    text = titleCase(data.label);
  } else {
    text = 'Connected';
  }

  if (connected && Number.isFinite(data.quality)) {
    quality = clamp(Math.round(data.quality), 1, 4);
  }

  if (headerWifiIcon) {
    headerWifiIcon.dataset.quality = String(quality);
    headerWifiIcon.dataset.connected = String(connected);
    headerWifiIcon.setAttribute('aria-label', connected ? `Wi-Fi ${text}` : 'Wi-Fi Disconnected');
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

function setConnectionState(isConnected) {
  if (!isConnected) {
    updateWifiSignal({ connected: false });
    updateBattery({ batteryValid: false });
  }
  else refreshWifiSignal();
}

function setConnectionMessage(text) {
  connectionMessageElements.forEach((element) => {
    element.textContent = text;
  });
}

function showPage(pageIndex) {
  currentPage = clamp(pageIndex, 0, pageButtons.length - 1);
  Array.from(pageTrack.querySelectorAll('.app-page')).forEach((page, index) => {
    // Animate each page independently so nonadjacent tabs never travel through
    // the intervening page. Offscreen pages stay on their navigation side.
    page.style.transform = `translateX(${index === currentPage ? 0 : index < currentPage ? -100 : 100}%)`;
    page.setAttribute('aria-hidden', String(index !== currentPage));
    page.inert = index !== currentPage;
  });

  pageButtons.forEach((button, index) => {
    const active = index === currentPage;
    button.classList.toggle('active', active);
    button.setAttribute('aria-current', active ? 'true' : 'false');
  });
}

function handleSwipeStart(event) {
  if (event.target.closest('button, input, .arc-hit-area, .section-details, .section-overview')) {
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
    setConnectionMessage('Connected To ESP32');
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
    sectionUI.setOnline(false);
    setConnectionState(false, 'CONNECTION ERROR');
    setConnectionMessage('WebSocket Connection Error');
  };

  ws.onclose = () => {
    sectionUI.setOnline(false);
    setConnectionState(false, 'DISCONNECTED');
    setConnectionMessage('Disconnected From ESP32. Reconnecting');
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
  const requestGeneration = stateBootGeneration;
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
    applyStateMessage(data, requestGeneration);
  } catch (error) {
    if (cmd.cmd.startsWith('distance_')) {
      distanceMessage.textContent = error.message;
    } else {
      messageElement.textContent = error.message === 'Enable Traveller First'
        ? error.message : 'Traveller Command Failed';
    }
    console.error('Command failed:', error);
  }
}

function sendCommand(cmd) {
  if (!sendWebSocketCommand(cmd)) sendHttpCommand(cmd);
}

async function sendSectionCommand(cmd) {
  const requestGeneration = stateBootGeneration;
  const action = cmd.cmd.slice('section_'.length);
  if (action === 'stop' && sendWebSocketCommand(cmd)) {
    // Use one mutation transport. A delayed duplicate STOP must not cancel a
    // subsequent GO after the traveller has already acknowledged the first STOP.
    await refreshSectionStatus();
    return null;
  }
  const params = new URLSearchParams();
  if (cmd.direction !== undefined) params.set('direction', cmd.direction);
  if (cmd.count !== undefined) params.set('count', String(cmd.count));
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 5000);
  try {
    const response = await fetch(`/section/${action}?${params}`, {
      cache: 'no-store', signal: controller.signal,
    });
    const data = await response.json();
    if (!response.ok) {
      await refreshSectionStatus();
      throw new Error(data.sectionError || `HTTP ${response.status}`);
    }
    applyStateMessage(data, requestGeneration);
    return null; // State ordering is handled centrally, including delayed responses.
  } finally {
    clearTimeout(timeout);
  }
}

async function refreshSectionStatus() {
  const requestGeneration = stateBootGeneration;
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 5000);
  try {
    const response = await fetch('/section/status', { cache: 'no-store', signal: controller.signal });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    applyStateMessage(await response.json(), requestGeneration);
  } catch (error) {
    sectionUI.setOnline(false);
  } finally {
    clearTimeout(timeout);
  }
}

function sendSpeedValue(speedValue) {
  if (!motorEnabled) {
    pendingSpeedValue = null;
    updateMotorSpeedDisplay(0);
    messageElement.textContent = 'Enable Traveller First';
    return;
  }
  pendingSpeedValue = speedValue;
  if (speedSendTimer) return;

  speedSendTimer = setTimeout(() => {
    speedSendTimer = null;
    if (pendingSpeedValue === null || !motorEnabled) return;
    const valueToSend = pendingSpeedValue;
    pendingSpeedValue = null;
    sendCommand({ cmd: 'speed', value: valueToSend });
  }, 30);
}

function sendOnCommand() {
  messageElement.textContent = 'Started Traveller';
  sendCommand({ cmd: 'on' });
}

function sendOffCommand() {
  pendingSpeedValue = null;
  updateSystemToggleDisplay(false);
  messageElement.textContent = 'Stopped Traveller';
  updateMotorSpeedDisplay(0);
  sendCommand({ cmd: 'off' });
}

function toggleSystemPower() {
  if (motorEnabled) sendOffCommand();
  else sendOnCommand();
}

function selectDistanceDirection(direction) {
  selectedDistanceDirection = selectedDistanceDirection === direction ? null : direction;
  distanceCwButton.classList.toggle('selected', selectedDistanceDirection === 'cw');
  distanceCcwButton.classList.toggle('selected', selectedDistanceDirection === 'ccw');
}

function startDistanceMove() {
  const distanceCm = Number.parseFloat(distanceInput.value);
  if (!Number.isFinite(distanceCm) || distanceCm <= 0) {
    distanceMessage.textContent = 'Enter A Distance Greater Than 0 cm';
    return;
  }
  if (!selectedDistanceDirection) {
    distanceMessage.textContent = 'Select RB Or LB';
    return;
  }
  distanceMessage.textContent = 'Started Move';
  sendCommand({ cmd: 'distance_start', distanceCm, direction: selectedDistanceDirection });
}

function stopDistanceMove() {
  distanceMessage.textContent = 'Stopped Move';
  sendCommand({ cmd: 'distance_stop' });
}

function zeroDistancePosition() {
  distanceMessage.textContent = 'Setting Zero';
  sendCommand({ cmd: 'distance_zero' });
}

function updateArcFromPointerEvent(event) {
  const { x, y } = eventToArcPoint(event);
  const speed = pointToSpeed(x, y);

  updateMotorSpeedDisplay(speed);
  messageElement.textContent = 'Adjusting Traveller Speed';
  sendSpeedValue(speed);
}

function beginArcDrag(event) {
  if (!isPointOnArcControl(event)) return;
  isUserDragging = true;
  event.preventDefault();
  motorArcHitArea.setPointerCapture(event.pointerId);
  updateArcFromPointerEvent(event);
}

function continueArcDrag(event) {
  if (!isUserDragging) return;
  if (!isPointOnArcControl(event, arcConfig.dragTolerance)) {
    endArcDrag(event);
    return;
  }
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
  messageElement.textContent = 'Adjusting Traveller Speed';
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
refreshSectionStatus();
setInterval(() => {
  if (!ws || ws.readyState !== WebSocket.OPEN) refreshSectionStatus();
}, 1000);
