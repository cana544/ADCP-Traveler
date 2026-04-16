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
  // Convert PWM back to slider position using reverse gamma correction
  // Dead zone: PWM 0-10 maps to slider 0 (prevents ghost brightness)
  let sliderValue = 0;
  
  if (pwm > 10) {
    const gamma = 2.2;
    const normalizedPwm = pwm / 255;
    sliderValue = Math.round(Math.pow(normalizedPwm, 1 / gamma) * 255);
  }
  
  updateBrightnessDisplay(sliderValue);
}

function updateState(state, pwm) {
  const isOn = state === "on";
  stateElement.textContent = isOn ? "ON" : "OFF";
  stateElement.classList.toggle("status-on", isOn);
  stateElement.classList.toggle("status-off", !isOn);

  if (Number.isFinite(pwm)) {
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
  const sliderValue = parseInt(event.target.value, 10);
  
  // Dead zone: 0-2% slider maps to PWM 0
  let pwmValue = 0;
  if (sliderValue > 5) {
    // Apply gamma correction for perceptually accurate brightness
    // Gamma of 2.2 matches typical display brightness perception
    const gamma = 2.2;
    const normalizedValue = sliderValue / 255;
    const gammaAdjusted = Math.pow(normalizedValue, gamma);
    pwmValue = Math.round(gammaAdjusted * 255);
  }
  
  updateBrightnessDisplay(sliderValue);  // Display uses slider position, not PWM
  messageElement.textContent = "Adjusting brightness...";
  
  // Send gamma-corrected PWM command
  sendPwmCommand(pwmValue);
});

brightnessSlider.addEventListener("change", (event) => {
  // Ensure final value is sent
  const sliderValue = parseInt(event.target.value, 10);
  
  let pwmValue = 0;
  if (sliderValue > 5) {
    const gamma = 2.2;
    const normalizedValue = sliderValue / 255;
    const gammaAdjusted = Math.pow(normalizedValue, gamma);
    pwmValue = Math.round(gammaAdjusted * 255);
  }
  
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
