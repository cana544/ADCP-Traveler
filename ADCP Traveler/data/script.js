const stateElement = document.getElementById("led-state");
const wifiSignalElement = document.getElementById("wifi-signal");
const wifiBarsElement = document.getElementById("wifi-bars");
const messageElement = document.getElementById("message");
const brightnessSlider = document.getElementById("brightness-slider");
const brightnessValue = document.getElementById("brightness-value");
const brightnessControl = document.querySelector(".brightness-control");
const buttons = Array.from(document.querySelectorAll(".action-button"));

let currentPwm = 0;
let isUserDragging = false;
let isLedOn = false;
let ws = null;
let pwmSendTimer = null;
let pendingPwmValue = null;

function setButtonsDisabled(disabled) {
  buttons.forEach((button) => {
    button.disabled = disabled;
  });
}

function updateBrightnessDisplay(displayValue) {
  // displayValue is the slider position (0-255), not necessarily the actual PWM
  currentPwm = displayValue;
  const percentage = Math.round((displayValue / 255) * 100);
  brightnessValue.textContent = `${percentage}%`;
  if (!isUserDragging) {
    brightnessSlider.value = displayValue;
  }

  let glowIntensity = 0;
  if (displayValue <= 0) glowIntensity = 0;
  else if (displayValue <= 64) glowIntensity = 1;
  else if (displayValue <= 127) glowIntensity = 2;
  else if (displayValue <= 191) glowIntensity = 3;
  else glowIntensity = 4;

  brightnessControl.dataset.brightness = glowIntensity;
}

function updateStateFromPwm(pwm) {
  // Linear mapping: PWM 0-255 maps directly to slider 0-255
  updateBrightnessDisplay(pwm);
}

function updateState(state, pwm) {
  isLedOn = state === "on";
  stateElement.textContent = isLedOn ? "ON" : "OFF";
  stateElement.classList.toggle("status-on", isLedOn);
  stateElement.classList.toggle("status-off", !isLedOn);

  // When LED is OFF, always display 0% brightness
  if (!isLedOn) {
    updateBrightnessDisplay(0);
  } else if (Number.isFinite(pwm)) {
    updateStateFromPwm(pwm);
  }
}

function updateWifiSignal(data) {
  const quality = Number.isFinite(data.quality) ? data.quality : 0;
  wifiBarsElement.dataset.quality = String(quality);

  if (!data.connected) {
    wifiSignalElement.textContent = "No device connected";
    wifiSignalElement.classList.remove("status-on");
    wifiSignalElement.classList.add("status-off");
    return;
  }

  if (Number.isFinite(data.rssi)) {
    wifiSignalElement.textContent = `${data.label.toUpperCase()} (${data.rssi} dBm)`;
  } else {
    wifiSignalElement.textContent = `${data.label.toUpperCase()}`;
  }
  wifiSignalElement.classList.remove("status-off");
  wifiSignalElement.classList.add("status-on");
}

async function refreshWifiSignal() {
  try {
    const response = await fetch('/wifi/signal', { keepalive: true });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }

    const data = await response.json();
    updateWifiSignal(data);
  } catch (error) {
    wifiBarsElement.dataset.quality = '0';
    wifiSignalElement.textContent = 'Unavailable';
    wifiSignalElement.classList.remove('status-on');
    wifiSignalElement.classList.add('status-off');
  }
}

// WebSocket connection
function connectWebSocket() {
  ws = new WebSocket('ws://192.168.4.1/ws');

  ws.onopen = () => {
    console.log('WebSocket connected');
    messageElement.textContent = 'Connected to ESP32 via WebSocket';
    setButtonsDisabled(false);
    
    // Request initial status
    ws.send(JSON.stringify({ cmd: 'status' }));
  };

  ws.onmessage = (event) => {
    try {
      const data = JSON.parse(event.data);
      console.log('Received:', data);

      if (data.state !== undefined && data.pwm !== undefined) {
        updateState(data.state, data.pwm);
      }
    } catch (error) {
      console.error('Failed to parse message:', error);
    }
  };

  ws.onerror = (error) => {
    console.error('WebSocket error:', error);
    messageElement.textContent = 'WebSocket connection error';
  };

  ws.onclose = () => {
    console.log('WebSocket disconnected');
    messageElement.textContent = 'Disconnected from ESP32. Reconnecting...';
    setButtonsDisabled(true);
    
    // Try to reconnect after 3 seconds
    setTimeout(connectWebSocket, 3000);
  };
}

function sendWebSocketCommand(cmd) {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(cmd));
  } else {
    messageElement.textContent = 'WebSocket not connected';
  }
}

function sendPwmValue(pwmValue) {
  pendingPwmValue = pwmValue;

  if (pwmSendTimer) {
    return;
  }

  pwmSendTimer = setTimeout(() => {
    pwmSendTimer = null;

    if (pendingPwmValue === null) {
      return;
    }

    const valueToSend = pendingPwmValue;
    pendingPwmValue = null;
    sendWebSocketCommand({ cmd: 'pwm', value: valueToSend });
  }, 30);
}

function sendOnCommand() {
  setButtonsDisabled(true);
  messageElement.textContent = 'Turning LED ON...';
  sendWebSocketCommand({ cmd: 'on' });
  setTimeout(() => { setButtonsDisabled(false); }, 500);
}

function sendOffCommand() {
  setButtonsDisabled(true);
  messageElement.textContent = 'Turning LED OFF...';
  sendWebSocketCommand({ cmd: 'off' });
  setTimeout(() => { setButtonsDisabled(false); }, 500);
}
brightnessSlider.addEventListener("mousedown", () => {
  isUserDragging = true;
});

brightnessSlider.addEventListener("touchstart", () => {
  isUserDragging = true;
});

brightnessSlider.addEventListener("mouseup", () => {
  isUserDragging = false;
});

brightnessSlider.addEventListener("touchend", () => {
  isUserDragging = false;
});

brightnessSlider.addEventListener("input", (event) => {
  // Only send commands when LED is on
  if (!isLedOn) {
    return;
  }

  const sliderValue = parseInt(event.target.value, 10);
  updateBrightnessDisplay(sliderValue);
  messageElement.textContent = "Adjusting brightness...";
  
  // Send immediate PWM update via WebSocket
  sendPwmValue(sliderValue);
});

brightnessSlider.addEventListener("change", (event) => {
  // Only send commands when LED is on
  if (!isLedOn) {
    return;
  }

  const sliderValue = parseInt(event.target.value, 10);
  sendPwmValue(sliderValue);
});

// Button event listeners
document
  .getElementById("led-on")
  .addEventListener("click", sendOnCommand);

document
  .getElementById("led-off")
  .addEventListener("click", sendOffCommand);

// Initialize WebSocket on page load
connectWebSocket();
refreshWifiSignal();
setInterval(refreshWifiSignal, 5000);
