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
let pendingPwmTimeout = null;
const PWM_THROTTLE_MS = 100;  // Send PWM updates every 100ms while dragging

function setButtonsDisabled(disabled) {
  buttons.forEach((button) => {
    button.disabled = disabled;
  });
  brightnessSlider.disabled = disabled;
}

function updateBrightnessDisplay(pwm) {
  currentPwm = pwm;
  const percentage = Math.round((pwm / 255) * 100);
  brightnessValue.textContent = `${percentage}%`;
  if (!isUserDragging) {
    brightnessSlider.value = pwm;
  }

  let glowIntensity = 0;
  if (pwm <= 0) glowIntensity = 0;
  else if (pwm <= 64) glowIntensity = 1;
  else if (pwm <= 127) glowIntensity = 2;
  else if (pwm <= 191) glowIntensity = 3;
  else glowIntensity = 4;

  brightnessControl.dataset.brightness = glowIntensity;
}

function updateState(state, pwm) {
  const isOn = state === "on";
  stateElement.textContent = isOn ? "ON" : "OFF";
  stateElement.classList.toggle("status-on", isOn);
  stateElement.classList.toggle("status-off", !isOn);

  if (Number.isFinite(pwm)) {
    updateBrightnessDisplay(pwm);
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

  wifiSignalElement.textContent = `${data.label.toUpperCase()} (${data.rssi} dBm)`;
  wifiSignalElement.classList.remove("status-off");
  wifiSignalElement.classList.add("status-on");
}

async function sendPwmCommand(pwmValue) {
  try {
    const response = await fetch(`/led/pwm/${pwmValue}`);
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }

    const data = await response.json();
    updateState(data.state, data.pwm);
    messageElement.textContent = "Brightness updated.";
  } catch (error) {
    messageElement.textContent =
      "Could not reach the ESP32. Check that you are connected to its Wi-Fi.";
  }
}

async function sendLedCommand(path, pendingText) {
  setButtonsDisabled(true);
  messageElement.textContent = pendingText;

  try {
    const response = await fetch(path);
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }

    const data = await response.json();
    updateState(data.state, data.pwm);
    messageElement.textContent =
      path === "/led/on"
        ? "LED turned ON (restored brightness)."
        : "LED turned OFF.";
  } catch (error) {
    messageElement.textContent =
      "Could not reach the ESP32. Check that you are connected to its Wi-Fi.";
  } finally {
    setButtonsDisabled(false);
  }
}

async function refreshStatus() {
  try {
    const [ledResponse, wifiResponse] = await Promise.all([
      fetch("/led/status"),
      fetch("/wifi/signal"),
    ]);

    if (!ledResponse.ok || !wifiResponse.ok) {
      throw new Error(`HTTP ${ledResponse.status}/${wifiResponse.status}`);
    }

    const ledData = await ledResponse.json();
    const wifiData = await wifiResponse.json();
    updateState(ledData.state, ledData.pwm || 0);
    updateWifiSignal(wifiData);
    messageElement.textContent = "Connected to ESP32 control page.";
  } catch (error) {
    wifiBarsElement.dataset.quality = "0";
    wifiSignalElement.textContent = "Unavailable";
    wifiSignalElement.classList.remove("status-on");
    wifiSignalElement.classList.add("status-off");
    messageElement.textContent =
      "Waiting for the ESP32 to respond. Reload if needed.";
  }
}

brightnessSlider.addEventListener("mousedown", () => {
  isUserDragging = true;
});

brightnessSlider.addEventListener("touchstart", () => {
  isUserDragging = true;
});

brightnessSlider.addEventListener("mouseup", () => {
  isUserDragging = false;
  // Send final PWM value when done dragging
  if (pendingPwmTimeout) {
    clearTimeout(pendingPwmTimeout);
    pendingPwmTimeout = null;
  }
  const pwmValue = parseInt(brightnessSlider.value, 10);
  sendPwmCommand(pwmValue);
});

brightnessSlider.addEventListener("touchend", () => {
  isUserDragging = false;
  // Send final PWM value when done dragging
  if (pendingPwmTimeout) {
    clearTimeout(pendingPwmTimeout);
    pendingPwmTimeout = null;
  }
  const pwmValue = parseInt(brightnessSlider.value, 10);
  sendPwmCommand(pwmValue);
});

brightnessSlider.addEventListener("input", (event) => {
  const pwmValue = parseInt(event.target.value, 10);
  updateBrightnessDisplay(pwmValue);
  messageElement.textContent = "Adjusting brightness...";

  // Throttle PWM commands while dragging
  if (pendingPwmTimeout) {
    clearTimeout(pendingPwmTimeout);
  }

  pendingPwmTimeout = setTimeout(() => {
    sendPwmCommand(pwmValue);
    pendingPwmTimeout = null;
  }, PWM_THROTTLE_MS);
});

brightnessSlider.addEventListener("change", (event) => {
  // Change event fires on drag end on some browsers
  // Ensure final value is sent
  if (!isUserDragging) {
    const pwmValue = parseInt(event.target.value, 10);
    if (pendingPwmTimeout) {
      clearTimeout(pendingPwmTimeout);
      pendingPwmTimeout = null;
    }
    sendPwmCommand(pwmValue);
  }
});

document
  .getElementById("led-on")
  .addEventListener("click", () => sendLedCommand("/led/on", "Sending LED ON command..."));
document
  .getElementById("led-off")
  .addEventListener("click", () => sendLedCommand("/led/off", "Sending LED OFF command..."));

refreshStatus();
setInterval(refreshStatus, 5000);
