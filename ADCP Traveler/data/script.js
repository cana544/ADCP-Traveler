const stateElement = document.getElementById("led-state");
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
    const response = await fetch("/led/status");
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }

    const data = await response.json();
    updateState(data.state);
    messageElement.textContent = "Connected to ESP32 control page.";
  } catch (error) {
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
