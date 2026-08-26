const stateElement = document.getElementById("motor-state");
const wifiSignalElement = document.getElementById("wifi-signal");
const wifiBarsElement = document.getElementById("wifi-bars");
const messageElement = document.getElementById("message");
const motorSpeedSlider = document.getElementById("motor-speed-slider");
const motorSpeedValue = document.getElementById("motor-speed-value");
const motorControl = document.querySelector(".motor-control");
const buttons = Array.from(document.querySelectorAll(".action-button"));
const pageWindow = document.getElementById("page-window");
const pageTrack = document.getElementById("page-track");
const pageDots = Array.from(document.querySelectorAll(".page-dot"));

let currentSpeed = 0;
let isUserDragging = false;
let ws = null;
let speedSendTimer = null;
let pendingSpeedValue = null;
let currentPage = 0;
let swipeStartX = 0;
let swipeStartY = 0;
let isSwiping = false;

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
  if (!isSwiping) {
    return;
  }

  const point = event.changedTouches ? event.changedTouches[0] : event;
  const deltaX = point.clientX - swipeStartX;
  const deltaY = point.clientY - swipeStartY;
  isSwiping = false;

  if (Math.abs(deltaX) < 50 || Math.abs(deltaX) < Math.abs(deltaY)) {
    return;
  }

  if (deltaX < 0) {
    showPage(currentPage + 1);
  } else {
    showPage(currentPage - 1);
  }
}

function setButtonsDisabled(disabled) {
  buttons.forEach((button) => {
    button.disabled = disabled;
  });
}

function updateMotorSpeedDisplay(speed) {
  currentSpeed = Math.max(-255, Math.min(255, speed));
  const percentage = Math.round((Math.abs(currentSpeed) / 255) * 100);
  let direction = "STOP";

  if (currentSpeed > 0) {
    direction = "CW";
  } else if (currentSpeed < 0) {
    direction = "CCW";
  }

  motorSpeedValue.textContent =
    currentSpeed === 0 ? "STOP" : `${direction} ${percentage}%`;

  if (!isUserDragging) {
    motorSpeedSlider.value = currentSpeed;
  }

  motorControl.dataset.direction = direction.toLowerCase();
}

function updateState(state, speed) {
  const enabled = state === "on";

  if (!enabled) {
    stateElement.textContent = "OFF";
  } else if (speed > 0) {
    stateElement.textContent = "CW";
  } else if (speed < 0) {
    stateElement.textContent = "CCW";
  } else {
    stateElement.textContent = "STOPPED";
  }

  stateElement.classList.toggle("status-on", enabled);
  stateElement.classList.toggle("status-off", !enabled);
  updateMotorSpeedDisplay(Number.isFinite(speed) ? speed : 0);
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
    const response = await fetch("/wifi/signal", { keepalive: true });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }

    const data = await response.json();
    updateWifiSignal(data);
  } catch (error) {
    wifiBarsElement.dataset.quality = "0";
    wifiSignalElement.textContent = "Unavailable";
    wifiSignalElement.classList.remove("status-on");
    wifiSignalElement.classList.add("status-off");
  }
}

function connectWebSocket() {
  const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
  ws = new WebSocket(`${protocol}//${window.location.host}/ws`);

  ws.onopen = () => {
    messageElement.textContent = "Connected to ESP32 via WebSocket";
    setButtonsDisabled(false);
    ws.send(JSON.stringify({ cmd: "status" }));
  };

  ws.onmessage = (event) => {
    try {
      const data = JSON.parse(event.data);

      if (data.state !== undefined && data.speed !== undefined) {
        updateState(data.state, data.speed);
      }
    } catch (error) {
      console.error("Failed to parse message:", error);
    }
  };

  ws.onerror = () => {
    messageElement.textContent = "WebSocket connection error";
  };

  ws.onclose = () => {
    messageElement.textContent = "Disconnected from ESP32. Reconnecting...";
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

  if (cmd.cmd === "speed") {
    endpoint = `/motor/speed?value=${encodeURIComponent(cmd.value)}`;
  } else if (cmd.cmd === "on") {
    endpoint = "/motor/on";
  } else if (cmd.cmd === "off") {
    endpoint = "/motor/off";
  } else if (cmd.cmd === "status") {
    endpoint = "/motor/status";
  }

  if (!endpoint) {
    return;
  }

  try {
    const response = await fetch(endpoint, { cache: "no-store" });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }

    const data = await response.json();
    if (data.state !== undefined && data.speed !== undefined) {
      updateState(data.state, data.speed);
    }
  } catch (error) {
    messageElement.textContent = "Motor command failed";
    console.error("Motor command failed:", error);
  }
}

function sendMotorCommand(cmd) {
  if (sendWebSocketCommand(cmd)) {
    return;
  }

  if (cmd.cmd === "speed") {
    messageElement.textContent = "Using HTTP motor control";
  } else {
    messageElement.textContent = "WebSocket not connected, using HTTP";
  }
  sendHttpCommand(cmd);
}

function sendSpeedValue(speedValue) {
  pendingSpeedValue = speedValue;

  if (speedSendTimer) {
    return;
  }

  speedSendTimer = setTimeout(() => {
    speedSendTimer = null;

    if (pendingSpeedValue === null) {
      return;
    }

    const valueToSend = pendingSpeedValue;
    pendingSpeedValue = null;
    sendMotorCommand({ cmd: "speed", value: valueToSend });
  }, 30);
}

function sendOnCommand() {
  setButtonsDisabled(true);
  messageElement.textContent = "Enabling motor...";
  sendMotorCommand({ cmd: "on" });
  setTimeout(() => {
    setButtonsDisabled(false);
  }, 500);
}

function sendOffCommand() {
  setButtonsDisabled(true);
  messageElement.textContent = "Stopping motor...";
  updateMotorSpeedDisplay(0);
  sendMotorCommand({ cmd: "off" });
  setTimeout(() => {
    setButtonsDisabled(false);
  }, 500);
}

motorSpeedSlider.addEventListener("mousedown", () => {
  isUserDragging = true;
});

motorSpeedSlider.addEventListener("touchstart", () => {
  isUserDragging = true;
});

motorSpeedSlider.addEventListener("mouseup", () => {
  isUserDragging = false;
});

motorSpeedSlider.addEventListener("touchend", () => {
  isUserDragging = false;
});

motorSpeedSlider.addEventListener("input", (event) => {
  const sliderValue = parseInt(event.target.value, 10);
  updateMotorSpeedDisplay(sliderValue);
  messageElement.textContent = "Adjusting motor speed...";
  sendSpeedValue(sliderValue);
});

motorSpeedSlider.addEventListener("change", (event) => {
  const sliderValue = parseInt(event.target.value, 10);
  sendSpeedValue(sliderValue);
});

document.getElementById("motor-on").addEventListener("click", sendOnCommand);
document.getElementById("motor-off").addEventListener("click", sendOffCommand);

pageWindow.addEventListener("touchstart", handleSwipeStart, { passive: true });
pageWindow.addEventListener("touchend", handleSwipeEnd);
pageWindow.addEventListener("mousedown", handleSwipeStart);
pageWindow.addEventListener("mouseup", handleSwipeEnd);

pageDots.forEach((dot) => {
  dot.addEventListener("click", () => {
    showPage(parseInt(dot.dataset.page, 10));
  });
});

showPage(0);
connectWebSocket();
refreshWifiSignal();
setInterval(refreshWifiSignal, 5000);
