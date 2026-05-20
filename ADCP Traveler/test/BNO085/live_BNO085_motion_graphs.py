"""Live BNO085 motion plots.

Shows linear acceleration, velocity, and displacement in real time.

Serial mode, reading directly from the ESP32 on COM3:
    python live_BNO085_motion_graphs.py

CSV tail mode, if you are saving Serial Monitor output to BNO085_data.csv:
    python live_BNO085_motion_graphs.py --csv BNO085_data.csv

The script is built to stay responsive by draining all available input each
update and plotting only a rolling time window. Serial mode uses the known
BNO085.ino CSV column order, so it does not wait for the header line.
"""

from __future__ import annotations

import argparse
import csv
from collections import deque
from pathlib import Path
import time
from typing import TextIO

import matplotlib.pyplot as plt


DEFAULT_PORT = "COM3"
DEFAULT_BAUD = 115200
DEFAULT_CSV = Path(__file__).with_name("BNO085_data.csv")

EXPECTED_COLUMNS = [
    "Time_s",
    "RawAccelX",
    "RawAccelY",
    "RawAccelZ",
    "AccelX",
    "AccelY",
    "AccelZ",
    "LinX",
    "LinY",
    "LinZ",
    "VelX",
    "VelY",
    "VelZ",
    "DispX",
    "DispY",
    "DispZ",
    "GyroX_rad_s",
    "GyroY_rad_s",
    "GyroZ_rad_s",
    "AngleX_rad",
    "AngleY_rad",
    "AngleZ_rad",
]

PLOT_GROUPS = [
    ("Linear acceleration", ["LinX", "LinY", "LinZ"], "m/s^2"),
    ("Velocity", ["VelX", "VelY", "VelZ"], "m/s"),
    ("Displacement", ["DispX", "DispY", "DispZ"], "m"),
]

AXIS_LABELS = ["X", "Y", "Z"]
AXIS_COLORS = ["tab:red", "tab:green", "tab:blue"]


def parse_csv_line(line: str) -> list[str]:
    return next(csv.reader([line], skipinitialspace=True))


def make_row(header: list[str], line: str) -> dict[str, float] | None:
    if not line or line == "END" or line.startswith("Time_s"):
        return None

    parts = [part.strip() for part in parse_csv_line(line)]
    if len(parts) < len(header):
        return None

    try:
        return {name: float(parts[index]) for index, name in enumerate(header)}
    except ValueError:
        return None


class SerialLineSource:
    def __init__(self, port: str, baud: int) -> None:
        try:
            import serial
        except ImportError as exc:
            raise SystemExit("Install pyserial first: python -m pip install pyserial") from exc

        try:
            self.serial = serial.Serial(port, baud, timeout=0)
        except serial.SerialException as exc:
            raise SystemExit(
                f"Could not open {port}. Close Arduino Serial Monitor or any other program using it, "
                f"then try again.\nWindows/pyserial error: {exc}"
            ) from exc

        print(f"Connected directly to ESP32 serial output on {port} at {baud} baud.")
        time.sleep(2.0)
        self.header = EXPECTED_COLUMNS
        self.serial.reset_input_buffer()
        print("Using known BNO085.ino CSV format; reading numeric data now.")

    def _read_line(self) -> str:
        return self.serial.readline().decode(errors="ignore").strip()

    def read_available(self) -> list[str]:
        lines = []
        while True:
            line = self._read_line()
            if not line:
                break
            lines.append(line)
        return lines

    def close(self) -> None:
        self.serial.close()


class CsvTailLineSource:
    def __init__(self, csv_path: Path) -> None:
        self.csv_path = csv_path
        self.file: TextIO | None = None
        self.header: list[str] = []
        self._open_and_find_header()

    def _open_and_find_header(self) -> None:
        if not self.csv_path.exists():
            raise FileNotFoundError(f"Cannot find CSV file: {self.csv_path}")

        self.file = self.csv_path.open("r", encoding="utf-8", errors="ignore", newline="")
        for line in self.file:
            line = line.strip()
            if line.startswith("Time_s"):
                self.header = [column.strip() for column in parse_csv_line(line)]
                break

        if not self.header:
            raise ValueError(f"Could not find CSV header in {self.csv_path}")

    def read_available(self) -> list[str]:
        assert self.file is not None
        lines = []
        while True:
            line = self.file.readline()
            if not line:
                break
            lines.append(line.strip())
        return lines

    def close(self) -> None:
        if self.file is not None:
            self.file.close()


def check_columns(header: list[str]) -> None:
    missing = [
        column
        for _title, columns, _ylabel in PLOT_GROUPS
        for column in columns
        if column not in header
    ]
    if missing:
        raise ValueError(f"Missing required columns: {', '.join(missing)}")

    if header != EXPECTED_COLUMNS:
        print("Warning: CSV header is not the exact expected BNO085.ino header, but required columns exist.")
        print("Using header:")
        print(",".join(header))


def trim_to_window(data: dict[str, deque[float]], window_s: float) -> None:
    times = data["Time_s"]
    if not times:
        return

    cutoff = times[-1] - window_s
    while len(times) > 2 and times[0] < cutoff:
        for values in data.values():
            values.popleft()


def update_y_limits(axes: list[plt.Axes], data: dict[str, deque[float]]) -> None:
    for axis, (_title, columns, _ylabel) in zip(axes, PLOT_GROUPS):
        values = [value for column in columns for value in data[column]]
        if not values:
            continue

        y_min = min(values)
        y_max = max(values)
        if y_min == y_max:
            margin = max(abs(y_min) * 0.1, 0.01)
        else:
            margin = (y_max - y_min) * 0.1
        axis.set_ylim(y_min - margin, y_max + margin)


def main() -> None:
    parser = argparse.ArgumentParser(description="Live BNO085 linear acceleration, velocity, and displacement plots.")
    parser.add_argument("--port", default=DEFAULT_PORT, help=f"ESP32 serial port, default {DEFAULT_PORT}")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help=f"Serial baud rate, default {DEFAULT_BAUD}")
    parser.add_argument("--csv", type=Path, help="Optional: tail a CSV file instead of opening serial")
    parser.add_argument("--window", type=float, default=10.0, help="Seconds of data visible on screen")
    parser.add_argument("--ui-interval", type=float, default=0.03, help="Plot refresh interval in seconds")
    args = parser.parse_args()

    source = CsvTailLineSource(args.csv) if args.csv else SerialLineSource(args.port, args.baud)
    check_columns(source.header)

    data_columns = ["Time_s"] + [column for _title, columns, _ylabel in PLOT_GROUPS for column in columns]
    data: dict[str, deque[float]] = {column: deque() for column in data_columns}

    plt.ion()
    fig, axes_array = plt.subplots(3, 1, sharex=True, figsize=(11, 8))
    axes = list(axes_array)
    lines = {}

    for axis, (title, columns, ylabel) in zip(axes, PLOT_GROUPS):
        for column, label, color in zip(columns, AXIS_LABELS, AXIS_COLORS):
            (line,) = axis.plot([], [], label=label, color=color, linewidth=1.3)
            lines[column] = line
        axis.set_title(title)
        axis.set_ylabel(ylabel)
        axis.grid(True, alpha=0.3)
        axis.legend(loc="upper right", ncol=3)

    axes[-1].set_xlabel("Time (s)")
    status = fig.text(0.01, 0.01, "Waiting for data...", fontsize=9)
    fig.tight_layout(rect=(0, 0.03, 1, 1))
    fig.show()

    last_limits_update = 0.0
    raw_lines_seen = 0
    rows_seen = 0
    skipped_rows = 0
    printed_skipped_examples = 0

    try:
        while plt.fignum_exists(fig.number):
            for raw_line in source.read_available():
                raw_lines_seen += 1
                row = make_row(source.header, raw_line)
                if row is None:
                    skipped_rows += 1
                    if printed_skipped_examples < 3 and raw_line and not raw_line.startswith("Time_s"):
                        print(f"Skipped serial line: {raw_line}")
                        printed_skipped_examples += 1
                    continue

                for column in data_columns:
                    data[column].append(row[column])
                trim_to_window(data, args.window)
                rows_seen += 1

            if data["Time_s"]:
                times = list(data["Time_s"])
                for _title, columns, _ylabel in PLOT_GROUPS:
                    for column in columns:
                        lines[column].set_data(times, list(data[column]))

                axes[-1].set_xlim(max(times[-1] - args.window, times[0]), times[-1] + 0.001)

                now = time.monotonic()
                if now - last_limits_update > 0.25:
                    update_y_limits(axes, data)
                    last_limits_update = now

                status.set_text(
                    f"latest t={times[-1]:.3f}s | parsed={rows_seen} | "
                    f"received={raw_lines_seen} | skipped={skipped_rows} | window={args.window:g}s"
                )
            else:
                status.set_text(f"Waiting for numeric data... received={raw_lines_seen} skipped={skipped_rows}")

            fig.canvas.draw_idle()
            fig.canvas.flush_events()
            plt.pause(args.ui_interval)
    finally:
        source.close()


if __name__ == "__main__":
    main()
