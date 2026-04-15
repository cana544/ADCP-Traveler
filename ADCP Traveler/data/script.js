const stateElement = document.getElementById("led-state");
const wifiSignalElement = document.getElementById("wifi-signal");
const wifiBarsElement = document.getElementById("wifi-bars");
const messageElement = document.getElementById("message");
const buttons = Array.from(document.querySelectorAll(".action-button"));

function setButtonsDisabled(disabled) {
  buttons.forEach((button) => {
    button.disabled = disabled;
  });
}

function updateState(state) {
  const isOn = state === "on";
  stateElement.textContent = isOn ? "ON" : "OFF";
  stateElement.classList.toggle("status-on", isOn);
  stateElement.classList.toggle("status-off", !isOn);
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

async function sendLedCommand(path, pendingText) {
  setButtonsDisabled(true);
  messageElement.textContent = pendingText;

  try {
    const response = await fetch(path);
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }

    const data = await response.json();
    updateState(data.state);
    messageElement.textContent = `LED turned ${data.state.toUpperCase()}.`;
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
    updateState(ledData.state);
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

document
  .getElementById("led-on")
  .addEventListener("click", () => sendLedCommand("/led/on", "Sending LED ON command..."));
document
  .getElementById("led-off")
  .addEventListener("click", () => sendLedCommand("/led/off", "Sending LED OFF command..."));

refreshStatus();
setInterval(refreshStatus, 5000);
