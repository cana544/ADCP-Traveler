"""Plot selected BNO085 CSV values.

Save the Arduino Serial Monitor output into BNO085_data.csv, then run:
    python plot_BNO085_csv.py

Edit the DEFAULT_*_PLOTS lists below, or choose from the command line:
    python plot_BNO085_csv.py --accel raw conditioned
    python plot_BNO085_csv.py --motion linear_acceleration velocity displacement --rotation gyro angle
    python plot_BNO085_csv.py --interactive
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


DEFAULT_CSV = Path(__file__).with_name("BNO085_data.csv")

# Pick what gets plotted when no --accel/--motion/--rotation options are supplied.
# Accel choices: "raw", "conditioned"
# Motion choices: "linear_acceleration", "velocity", "displacement"
# Rotation choices: "gyro", "angle"
DEFAULT_ACCEL_PLOTS = ["raw", "conditioned"]
DEFAULT_MOTION_PLOTS = ["linear_acceleration", "velocity", "displacement"]
DEFAULT_ROTATION_PLOTS = ["gyro", "angle"]

AXES = ["X", "Y", "Z"]
TIME_COLUMN = "Time_s"
LINEAR_ACCEL_COLUMNS = ["LinX", "LinY", "LinZ"]
RAW_ACCEL_COLUMNS = ["RawAccelX", "RawAccelY", "RawAccelZ"]
CONDITIONED_ACCEL_COLUMNS = ["AccelX", "AccelY", "AccelZ"]
VELOCITY_COLUMNS = ["VelX", "VelY", "VelZ"]
DISPLACEMENT_COLUMNS = ["DispX", "DispY", "DispZ"]
ANGLE_COLUMNS = ["AngleX_rad", "AngleY_rad", "AngleZ_rad"]
GYRO_COLUMNS = ["GyroX_rad_s", "GyroY_rad_s", "GyroZ_rad_s"]
LEGACY_GYRO_COLUMNS = ["GyroX", "GyroY", "GyroZ"]

ACCEL_PLOTS = {
    "raw": {
        "title": "Raw accelerometer",
        "columns": RAW_ACCEL_COLUMNS,
        "ylabel": "m/s^2",
    },
    "conditioned": {
        "title": "Signal-conditioned accelerometer",
        "columns": CONDITIONED_ACCEL_COLUMNS,
        "ylabel": "m/s^2",
    },
}

MOTION_PLOTS = {
    "linear_acceleration": {
        "title": "Linear acceleration (gravity removed)",
        "columns": LINEAR_ACCEL_COLUMNS,
        "ylabel": "m/s^2",
    },
    "velocity": {
        "title": "Velocity",
        "columns": VELOCITY_COLUMNS,
        "ylabel": "m/s",
    },
    "displacement": {
        "title": "Displacement",
        "columns": DISPLACEMENT_COLUMNS,
        "ylabel": "m",
    },
}

ROTATION_PLOTS = {
    "gyro": {
        "title": "Gyroscope",
        "columns": GYRO_COLUMNS,
        "legacy_columns": LEGACY_GYRO_COLUMNS,
        "ylabel": "rad/s",
    },
    "angle": {
        "title": "Integrated gyro angle",
        "columns": ANGLE_COLUMNS,
        "ylabel": "rad",
    },
}


def read_bno085_csv(csv_path: Path) -> pd.DataFrame:
    if not csv_path.exists():
        raise FileNotFoundError(f"Cannot find CSV file: {csv_path}")

    df = pd.read_csv(csv_path, sep=r"[\t,]+", engine="python")
    df.columns = [column.strip() for column in df.columns]

    if TIME_COLUMN not in df.columns:
        raise ValueError(f"Missing {TIME_COLUMN} column in {csv_path}")

    numeric_df = df.apply(pd.to_numeric, errors="coerce")
    numeric_df = numeric_df.dropna(subset=[TIME_COLUMN])
    if numeric_df.empty:
        raise ValueError(f"No numeric data rows found in {csv_path}")

    return numeric_df


def default_motion_png_path(csv_path: Path) -> Path:
    return csv_path.with_name(f"{csv_path.stem}_motion.png")


def default_accel_png_path(csv_path: Path) -> Path:
    return csv_path.with_name(f"{csv_path.stem}_accelerometer.png")


def default_rotation_png_path(csv_path: Path) -> Path:
    return csv_path.with_name(f"{csv_path.stem}_rotation.png")


def open_png(output_path: Path) -> None:
    if sys.platform.startswith("win"):
        os.startfile(output_path.resolve())  # type: ignore[attr-defined]
        return

    print(f"Open this file to view the plot: {output_path}")


def clean_plot_names(plot_names: list[str] | None, valid_names: set[str], label: str) -> list[str]:
    if plot_names is None:
        return []

    clean_names = []
    for name in plot_names:
        normalized = name.lower().replace("-", "_")
        if normalized in ("none", "off", "no"):
            return []
        if normalized not in valid_names:
            raise ValueError(
                f"Unknown {label} plot '{name}'. Choose from: {', '.join(sorted(valid_names))}"
            )
        if normalized not in clean_names:
            clean_names.append(normalized)

    return clean_names


def columns_for_plot(df: pd.DataFrame, config: dict[str, object]) -> list[str] | None:
    columns = config["columns"]
    assert isinstance(columns, list)
    if all(column in df.columns for column in columns):
        return columns

    legacy_columns = config.get("legacy_columns")
    if isinstance(legacy_columns, list) and all(column in df.columns for column in legacy_columns):
        return legacy_columns

    return None


def available_plots(
    df: pd.DataFrame,
    requested: list[str],
    configs: dict[str, dict[str, object]],
    group_label: str,
) -> list[tuple[str, dict[str, object], list[str]]]:
    available = []
    for plot_name in requested:
        config = configs[plot_name]
        columns = columns_for_plot(df, config)
        if columns is None:
            print(f"Skipping {group_label} plot '{plot_name}' because its CSV columns are missing.")
            continue
        available.append((plot_name, config, columns))
    return available


def plot_group(
    df: pd.DataFrame,
    csv_path: Path,
    output_path: Path,
    selected_plots: list[tuple[str, dict[str, object], list[str]]],
    title: str,
    open_output: bool,
) -> None:
    if not selected_plots:
        return

    fig, axes = plt.subplots(len(selected_plots), 1, sharex=True, figsize=(11, 2.8 * len(selected_plots)))
    if len(selected_plots) == 1:
        axes = [axes]

    fig.suptitle(f"{title}: {csv_path.name}")

    for axis, (_plot_name, config, columns) in zip(axes, selected_plots):
        plot_df = df[[TIME_COLUMN] + columns].dropna()
        axis.plot(plot_df[TIME_COLUMN], plot_df[columns])
        axis.set_title(str(config["title"]))
        axis.set_ylabel(str(config["ylabel"]))
        axis.legend(AXES, loc="best")
        axis.grid(True, alpha=0.3)

    axes[-1].set_xlabel("Time (s)")
    fig.tight_layout()
    fig.savefig(output_path, dpi=300, bbox_inches="tight")
    print(f"Saved {title.lower()} plot to {output_path}")
    plt.close(fig)

    if open_output:
        open_png(output_path)


def prompt_for_plots(label: str, choices: list[str], defaults: list[str]) -> list[str]:
    print(f"\n{label} plot choices: {', '.join(choices)}")
    print(f"Default: {', '.join(defaults) if defaults else 'none'}")
    response = input("Enter choices separated by spaces, or 'none': ").strip()
    if not response:
        return defaults
    return response.split()


def plot_bno085_data(
    df: pd.DataFrame,
    csv_path: Path,
    accel_output_path: Path,
    motion_output_path: Path,
    rotation_output_path: Path,
    accel_plots: list[str],
    motion_plots: list[str],
    rotation_plots: list[str],
    open_output: bool,
) -> None:
    selected_accel = available_plots(df, accel_plots, ACCEL_PLOTS, "accelerometer")
    selected_motion = available_plots(df, motion_plots, MOTION_PLOTS, "motion")
    selected_rotation = available_plots(df, rotation_plots, ROTATION_PLOTS, "rotation")

    if not selected_accel and not selected_motion and not selected_rotation:
        raise ValueError("No requested plots are available in this CSV.")

    plot_group(df, csv_path, accel_output_path, selected_accel, "BNO085 accelerometer", open_output)
    plot_group(df, csv_path, motion_output_path, selected_motion, "BNO085 motion", open_output)
    plot_group(df, csv_path, rotation_output_path, selected_rotation, "BNO085 rotation", open_output)


def main() -> None:
    parser = argparse.ArgumentParser(description="Plot selected BNO085 CSV data.")
    parser.add_argument("csv_path", nargs="?", type=Path, default=DEFAULT_CSV)
    parser.add_argument(
        "--accel",
        nargs="*",
        choices=sorted(ACCEL_PLOTS.keys()) + ["none"],
        help="Accelerometer plots to save in one PNG.",
    )
    parser.add_argument(
        "--motion",
        nargs="*",
        choices=sorted(MOTION_PLOTS.keys()) + ["none"],
        help="Motion plots to save in one PNG.",
    )
    parser.add_argument(
        "--rotation",
        nargs="*",
        choices=sorted(ROTATION_PLOTS.keys()) + ["none"],
        help="Rotation plots to save in one PNG.",
    )
    parser.add_argument("--accel-output", type=Path, help="Accelerometer PNG output path")
    parser.add_argument("--motion-output", type=Path, help="Motion PNG output path")
    parser.add_argument("--rotation-output", type=Path, help="Rotation PNG output path")
    parser.add_argument("--interactive", action="store_true", help="Prompt for graph choices")
    parser.add_argument("--no-open", action="store_true", help="Save PNGs without opening them")
    args = parser.parse_args()

    accel_plots = DEFAULT_ACCEL_PLOTS if args.accel is None else args.accel
    motion_plots = DEFAULT_MOTION_PLOTS if args.motion is None else args.motion
    rotation_plots = DEFAULT_ROTATION_PLOTS if args.rotation is None else args.rotation

    if args.interactive:
        accel_plots = prompt_for_plots("Accelerometer", sorted(ACCEL_PLOTS.keys()), accel_plots)
        motion_plots = prompt_for_plots("Motion", sorted(MOTION_PLOTS.keys()), motion_plots)
        rotation_plots = prompt_for_plots("Rotation", sorted(ROTATION_PLOTS.keys()), rotation_plots)

    accel_plots = clean_plot_names(accel_plots, set(ACCEL_PLOTS.keys()), "accelerometer")
    motion_plots = clean_plot_names(motion_plots, set(MOTION_PLOTS.keys()), "motion")
    rotation_plots = clean_plot_names(rotation_plots, set(ROTATION_PLOTS.keys()), "rotation")

    plot_bno085_data(
        read_bno085_csv(args.csv_path),
        args.csv_path,
        args.accel_output or default_accel_png_path(args.csv_path),
        args.motion_output or default_motion_png_path(args.csv_path),
        args.rotation_output or default_rotation_png_path(args.csv_path),
        accel_plots,
        motion_plots,
        rotation_plots,
        open_output=not args.no_open,
    )


if __name__ == "__main__":
    try:
        main()
    except Exception:
        traceback.print_exc()
        input("Press Enter to close...")
