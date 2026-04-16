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

function setButtonsDisabled(disabled) {
  buttons.forEach((button) => {
    button.disabled = disabled;
  });
  brightnessSlider.disabled = disabled;
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

  wifiSignalElement.textContent = `${data.label.toUpperCase()} (${data.rssi} dBm)`;
  wifiSignalElement.classList.remove("status-off");
  wifiSignalElement.classList.add("status-on");
}

async function sendPwmCommand(pwmValue) {
  // Fire-and-forget: send PWM update without waiting for response
  // This keeps the slider UI responsive
  fetch(`/led/pwm/${pwmValue}`, {
    method: "GET",
    keepalive: true,  // Reuse connection for faster requests
  }).catch(() => {
    // Silently fail - user will see in next status refresh
  });
}

async function sendPwmCommandWithFeedback(pwmValue) {
  // For buttons: wait for response and show feedback
  try {
    const response = await fetch(`/led/pwm/${pwmValue}`, {
      method: "GET",
      keepalive: true,
    });
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
    const response = await fetch(path, { keepalive: true });
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
      fetch("/led/status", { keepalive: true }),
      fetch("/wifi/signal", { keepalive: true }),
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
});

brightnessSlider.addEventListener("touchend", () => {
  isUserDragging = false;
});

brightnessSlider.addEventListener("input", (event) => {
  // Only allow slider changes when LED is on
  if (!isLedOn) {
    return;
  }
  
  const sliderValue = parseInt(event.target.value, 10);
  const pwmValue = sliderValue;  // Linear mapping: slider value = PWM value
  
  updateBrightnessDisplay(sliderValue);
  messageElement.textContent = "Adjusting brightness...";
  
  sendPwmCommand(pwmValue);
});

brightnessSlider.addEventListener("change", (event) => {
  // Only allow slider changes when LED is on
  if (!isLedOn) {
    return;
  }
  
  // Ensure final value is sent
  const sliderValue = parseInt(event.target.value, 10);
  const pwmValue = sliderValue;  // Linear mapping: slider value = PWM value
  
  sendPwmCommand(pwmValue);
});

document
  .getElementById("led-on")
  .addEventListener("click", () => sendLedCommand("/led/on", "Sending LED ON command..."));
document
  .getElementById("led-off")
  .addEventListener("click", () => sendLedCommand("/led/off", "Sending LED OFF command..."));

refreshStatus();
setInterval(refreshStatus, 5000);
