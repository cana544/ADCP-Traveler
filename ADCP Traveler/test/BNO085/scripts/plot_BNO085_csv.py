"""Lightweight offline plotting utilities for BNO085 CSV captures.

Usage:
  python plot_BNO085_csv.py BNO085_data.csv
"""

import sys
from pathlib import Path
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt


def _load_df(path: Path):
    df = pd.read_csv(path)
    if 'Time_s' not in df.columns:
        raise ValueError(f'CSV missing Time_s column: {path}')
    df = df.apply(pd.to_numeric, errors='coerce')
    df = df.dropna(subset=['Time_s'])
    return df, (df['Time_s'] - df['Time_s'].iloc[0])


def plot_accel(path: Path, out_path: Path):
    df, t = _load_df(path)
    plots_dir = out_path.parent
    plots_dir.mkdir(parents=True, exist_ok=True)
    fig, axes = plt.subplots(3, 1, figsize=(10, 9), sharex=True)
    if 'RawAccelX' in df.columns:
        axes[0].plot(t, df['RawAccelX'], label='RawAccelX')
    if 'AccelX' in df.columns:
        axes[0].plot(t, df['AccelX'], label='AccelX')
    axes[0].set_ylabel('m/s^2')
    axes[0].legend()

    if 'RawAccelY' in df.columns:
        axes[1].plot(t, df['RawAccelY'], label='RawAccelY')
    if 'AccelY' in df.columns:
        axes[1].plot(t, df['AccelY'], label='AccelY')
    axes[1].set_ylabel('m/s^2')
    axes[1].legend()

    if 'RawAccelZ' in df.columns:
        axes[2].plot(t, df['RawAccelZ'], label='RawAccelZ')
    if 'AccelZ' in df.columns:
        axes[2].plot(t, df['AccelZ'], label='AccelZ')
    axes[2].set_ylabel('m/s^2')
    axes[2].set_xlabel('Time (s)')
    axes[2].legend()
    fig.tight_layout()
    fig.savefig(out_path)
    plt.close(fig)


def plot_movement(path: Path, out_path: Path):
    df, t = _load_df(path)
    plots_dir = out_path.parent
    plots_dir.mkdir(parents=True, exist_ok=True)
    fig, axes = plt.subplots(3, 1, figsize=(10, 9), sharex=True)
    dt = np.diff(t.fillna(0).to_numpy())
    dt = np.concatenate(([dt[0] if len(dt) else 0.02], dt))
    for i, axis in enumerate(['X', 'Y', 'Z']):
        lin_key = f'Lin{axis}'
        vel_key = f'Vel{axis}'
        disp_key = f'Disp{axis}'
        if lin_key in df.columns:
            lin = df[lin_key]
            axes[i].plot(t, lin, label=f'Lin{axis}')
            if vel_key in df.columns:
                vel = df[vel_key]
            else:
                vel = np.cumsum(lin.to_numpy() * dt)
                vel = pd.Series(vel, index=df.index)
            axes[i].plot(t, vel, label=f'Vel{axis}')
            if disp_key in df.columns:
                disp = df[disp_key]
            else:
                disp = np.cumsum(vel.to_numpy() * dt)
                disp = pd.Series(disp, index=df.index)
            axes[i].plot(t, disp, label=f'Disp{axis}')
        else:
            axes[i].text(0.5, 0.5, f'No {lin_key} data', ha='center')
        axes[i].legend()
    axes[2].set_xlabel('Time (s)')
    fig.tight_layout()
    fig.savefig(out_path)
    plt.close(fig)


def plot_rotation(path: Path, out_path: Path):
    df, t = _load_df(path)
    plots_dir = out_path.parent
    plots_dir.mkdir(parents=True, exist_ok=True)
    fig, axes = plt.subplots(3, 1, figsize=(10, 9), sharex=True)
    for i, axis in enumerate(['X', 'Y', 'Z']):
        gyro_key = f'Gyro{axis}_rad_s'
        angle_key = f'Angle{axis}_rad'
        if gyro_key in df.columns:
            axes[i].plot(t, df[gyro_key], label=f'Gyro{axis} (rad/s)')
        else:
            axes[i].text(0.5, 0.6, f'No {gyro_key} data', ha='center')
        if angle_key in df.columns:
            axes[i].plot(t, df[angle_key], label=f'Angle{axis} (rad)')
        else:
            axes[i].text(0.5, 0.4, f'No {angle_key} data', ha='center')
        axes[i].legend()
        axes[i].set_ylabel('rad / rad/s')
    axes[2].set_xlabel('Time (s)')
    fig.tight_layout()
    fig.savefig(out_path)
    plt.close(fig)


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print('Usage: python plot_BNO085_csv.py BNO085_data.csv')
        sys.exit(1)
    base = Path(sys.argv[1])
    parent = base.parent

    accel_csv = parent / 'BNO085_data_acceleration.csv'
    movement_csv = parent / 'BNO085_data_movement.csv'
    rotation_csv = parent / 'BNO085_data_rotation.csv'

    accel_src = accel_csv if accel_csv.exists() else base
    movement_src = movement_csv if movement_csv.exists() else base
    rotation_src = rotation_csv if rotation_csv.exists() else base

    plots_dir = parent / 'plots'
    plots_dir.mkdir(parents=True, exist_ok=True)

    print('Using accel source:', accel_src)
    print('Using movement source:', movement_src)
    print('Using rotation source:', rotation_src)

    plot_accel(accel_src, plots_dir / 'plot_raw_and_calibrated_accel.png')
    plot_movement(movement_src, plots_dir / 'plot_lin_vel_disp.png')
    plot_rotation(rotation_src, plots_dir / 'plot_gyro_and_angle.png')

    print('Saved plots to', str(plots_dir))
