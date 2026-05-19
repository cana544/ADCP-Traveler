"""Plot BNO085 accelerometer, linear acceleration, and gyro angle/rate data.

Save the Arduino Serial Monitor output into BNO085_data.csv, then run:
    python plot_BNO085_csv.py

You can also pass a different file:
    python plot_BNO085_csv.py my_capture.csv

The plot is saved as a PNG and opened automatically.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import sys
import traceback

import pandas as pd
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


DEFAULT_CSV = Path("BNO085_data.csv")
BASE_COLUMNS = ["Time_s", "AccelX", "AccelY", "AccelZ", "LinX", "LinY", "LinZ"]
ANGLE_COLUMNS = ["AngleX_rad", "AngleY_rad", "AngleZ_rad"]
GYRO_COLUMNS = ["GyroX", "GyroY", "GyroZ"]


def read_bno085_csv(csv_path: Path) -> pd.DataFrame:
    if not csv_path.exists():
        raise FileNotFoundError(f"Cannot find CSV file: {csv_path}")

    df = pd.read_csv(csv_path, sep=r"[\t,]+", engine="python")
    df.columns = [column.strip() for column in df.columns]

    missing_columns = [column for column in BASE_COLUMNS if column not in df.columns]
    if missing_columns:
        raise ValueError(f"Missing columns in {csv_path}: {', '.join(missing_columns)}")

    has_angle_columns = all(column in df.columns for column in ANGLE_COLUMNS)
    has_gyro_columns = all(column in df.columns for column in GYRO_COLUMNS)
    if has_angle_columns:
        motion_columns = ANGLE_COLUMNS
    elif has_gyro_columns:
        motion_columns = GYRO_COLUMNS
    else:
        raise ValueError(
            f"Missing gyro/angle columns in {csv_path}. Expected either "
            f"{', '.join(ANGLE_COLUMNS)} or {', '.join(GYRO_COLUMNS)}."
        )

    clean_df = df[BASE_COLUMNS + motion_columns].apply(pd.to_numeric, errors="coerce").dropna()
    if clean_df.empty:
        raise ValueError(f"No numeric data rows found in {csv_path}")

    return clean_df


def default_png_path(csv_path: Path) -> Path:
    return csv_path.with_name(f"{csv_path.stem}_plot.png")


def open_png(output_path: Path) -> None:
    if sys.platform.startswith("win"):
        os.startfile(output_path.resolve())  # type: ignore[attr-defined]
        return

    print(f"Open this file to view the plot: {output_path}")


def plot_bno085_data(
    df: pd.DataFrame,
    csv_path: Path,
    output_path: Path,
    open_output: bool,
) -> None:
    fig, axes = plt.subplots(3, 1, sharex=True, figsize=(11, 8))
    fig.suptitle(f"BNO085 sensor test: {csv_path.name}")

    axes[0].plot(df["Time_s"], df[["AccelX", "AccelY", "AccelZ"]])
    axes[0].set_title("Accelerometer")
    axes[0].set_ylabel("m/s^2")
    axes[0].legend(["X", "Y", "Z"], loc="best")
    axes[0].grid(True, alpha=0.3)

    axes[1].plot(df["Time_s"], df[["LinX", "LinY", "LinZ"]])
    axes[1].set_title("Linear acceleration")
    axes[1].set_ylabel("m/s^2")
    axes[1].legend(["X", "Y", "Z"], loc="best")
    axes[1].grid(True, alpha=0.3)

    if all(column in df.columns for column in ANGLE_COLUMNS):
        axes[2].plot(df["Time_s"], df[ANGLE_COLUMNS])
        axes[2].set_title("Integrated gyro angle")
        axes[2].set_ylabel("rad")
    else:
        axes[2].plot(df["Time_s"], df[GYRO_COLUMNS])
        axes[2].set_title("Gyroscope")
        axes[2].set_ylabel("rad/s")

    axes[2].set_xlabel("Time (s)")
    axes[2].legend(["X", "Y", "Z"], loc="best")
    axes[2].grid(True, alpha=0.3)

    fig.tight_layout()
    fig.savefig(output_path, dpi=300, bbox_inches="tight")
    print(f"Saved plot to {output_path}")
    plt.close(fig)

    if open_output:
        open_png(output_path)


def main() -> None:
    parser = argparse.ArgumentParser(description="Plot BNO085 CSV data.")
    parser.add_argument("csv_path", nargs="?", type=Path, default=DEFAULT_CSV)
    parser.add_argument("--output", type=Path, help="PNG output path")
    parser.add_argument("--no-open", action="store_true", help="Save the PNG without opening it")
    args = parser.parse_args()

    output_path = args.output or default_png_path(args.csv_path)
    plot_bno085_data(
        read_bno085_csv(args.csv_path),
        args.csv_path,
        output_path,
        open_output=not args.no_open,
    )


if __name__ == "__main__":
    try:
        main()
    except Exception:
        traceback.print_exc()
        input("Press Enter to close...")
