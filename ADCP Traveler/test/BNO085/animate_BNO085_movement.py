"""Create a 3D movement animation from BNO085 displacement CSV data.

Run from the repo root or this folder:
    python test/BNO085/animate_BNO085_movement.py

Or pass a different CSV:
    python test/BNO085/animate_BNO085_movement.py test/BNO085/BNO085_data.csv
"""

from __future__ import annotations

import argparse
from pathlib import Path
import traceback

import pandas as pd
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation, PillowWriter


DEFAULT_CSV = Path(__file__).with_name("BNO085_data.csv")
DISPLACEMENT_COLUMNS = ["DispX", "DispY", "DispZ"]
TIME_COLUMN = "Time_s"


def read_displacement_csv(csv_path: Path) -> pd.DataFrame:
    if not csv_path.exists():
        raise FileNotFoundError(f"Cannot find CSV file: {csv_path}")

    df = pd.read_csv(csv_path, sep=r"[\t,]+", engine="python")
    df.columns = [column.strip() for column in df.columns]

    required_columns = [TIME_COLUMN] + DISPLACEMENT_COLUMNS
    missing_columns = [column for column in required_columns if column not in df.columns]
    if missing_columns:
        raise ValueError(f"Missing columns in {csv_path}: {', '.join(missing_columns)}")

    clean_df = df[required_columns].apply(pd.to_numeric, errors="coerce").dropna()
    if clean_df.empty:
        raise ValueError(f"No numeric displacement rows found in {csv_path}")

    clean_df[TIME_COLUMN] -= clean_df[TIME_COLUMN].iloc[0]
    return clean_df.reset_index(drop=True)


def default_output_path(csv_path: Path) -> Path:
    return csv_path.with_name(f"{csv_path.stem}_movement.gif")


def set_equal_3d_limits(ax, xs: pd.Series, ys: pd.Series, zs: pd.Series) -> None:
    mid_x = (xs.min() + xs.max()) / 2
    mid_y = (ys.min() + ys.max()) / 2
    mid_z = (zs.min() + zs.max()) / 2
    max_range = max(xs.max() - xs.min(), ys.max() - ys.min(), zs.max() - zs.min())
    radius = max(max_range / 2, 0.05)

    ax.set_xlim(mid_x - radius, mid_x + radius)
    ax.set_ylim(mid_y - radius, mid_y + radius)
    ax.set_zlim(mid_z - radius, mid_z + radius)


def animate_movement(
    df: pd.DataFrame,
    csv_path: Path,
    output_path: Path,
    fps: int,
    trail: int | None,
    frame_step: int,
) -> None:
    plot_df = df.iloc[::frame_step].reset_index(drop=True)
    xs = plot_df["DispX"]
    ys = plot_df["DispY"]
    zs = plot_df["DispZ"]
    times = plot_df[TIME_COLUMN]

    fig = plt.figure(figsize=(8, 7))
    ax = fig.add_subplot(111, projection="3d")
    ax.set_title(f"BNO085 movement: {csv_path.name}")
    ax.set_xlabel("X displacement (m)")
    ax.set_ylabel("Y displacement (m)")
    ax.set_zlabel("Z displacement (m)")
    set_equal_3d_limits(ax, xs, ys, zs)
    ax.view_init(elev=24, azim=-58)

    full_path, = ax.plot(xs, ys, zs, color="0.75", linewidth=1.0, label="Full path")
    trail_line, = ax.plot([], [], [], color="#1f77b4", linewidth=2.2, label="Travelled path")
    current_point, = ax.plot([], [], [], marker="o", color="#d62728", markersize=7, label="Current")
    start_point, = ax.plot([xs.iloc[0]], [ys.iloc[0]], [zs.iloc[0]], marker="o", color="#2ca02c", markersize=6, label="Start")
    time_text = ax.text2D(0.04, 0.94, "", transform=ax.transAxes)
    ax.legend(loc="upper right")

    def update(frame: int):
        start = 0 if trail is None else max(0, frame - trail)
        end = frame + 1

        trail_line.set_data(xs.iloc[start:end], ys.iloc[start:end])
        trail_line.set_3d_properties(zs.iloc[start:end])

        current_point.set_data([xs.iloc[frame]], [ys.iloc[frame]])
        current_point.set_3d_properties([zs.iloc[frame]])

        time_text.set_text(f"t = {times.iloc[frame]:.2f} s")
        return full_path, trail_line, current_point, start_point, time_text

    animation = FuncAnimation(fig, update, frames=len(plot_df), interval=1000 / fps, blit=False)
    writer = PillowWriter(fps=fps)
    animation.save(output_path, writer=writer)
    plt.close(fig)
    print(f"Saved movement animation to {output_path}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Animate BNO085 displacement from CSV.")
    parser.add_argument("csv_path", nargs="?", type=Path, default=DEFAULT_CSV)
    parser.add_argument("--output", type=Path, help="GIF output path")
    parser.add_argument("--fps", type=int, default=20, help="Animation frames per second")
    parser.add_argument("--trail", type=int, help="Number of recent points to show; default shows all history")
    parser.add_argument("--frame-step", type=int, default=1, help="Use every Nth CSV row as an animation frame")
    args = parser.parse_args()

    if args.fps <= 0:
        raise ValueError("--fps must be greater than 0")
    if args.frame_step <= 0:
        raise ValueError("--frame-step must be greater than 0")

    csv_path = args.csv_path
    animate_movement(
        read_displacement_csv(csv_path),
        csv_path,
        args.output or default_output_path(csv_path),
        fps=args.fps,
        trail=args.trail,
        frame_step=args.frame_step,
    )


if __name__ == "__main__":
    try:
        main()
    except Exception:
        traceback.print_exc()
        input("Press Enter to close...")
