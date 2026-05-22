"""
Compute full 3x3 accelerometer calibration (matrix M and bias b) from labeled
static-pose captures. Saves `accel_full_calibration.json` with M (3x3) and b (3x1).

Usage:
  python accel_full_fit.py test/BNO085/data/accel_static_poses.csv

Model: a_true = M @ a_meas + b

The script builds a linear system and solves for M and b using least-squares.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import numpy as np
import csv
import math

G = 9.80665

POSE_TO_VECTOR = {
    'X+': np.array([G, 0.0, 0.0]),
    'X-': np.array([-G, 0.0, 0.0]),
    'Y+': np.array([0.0, G, 0.0]),
    'Y-': np.array([0.0, -G, 0.0]),
    'Z+': np.array([0.0, 0.0, G]),
    'Z-': np.array([0.0, 0.0, -G]),
}


def read_labeled_csv(path: Path):
    rows = []
    with path.open('r', newline='', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        for row in reader:
            pose = (row.get('Pose') or '').strip()
            if pose not in POSE_TO_VECTOR:
                continue
            try:
                mx = float(row['RawAccelX'])
                my = float(row['RawAccelY'])
                mz = float(row['RawAccelZ'])
            except Exception:
                continue
            rows.append((pose, np.array([mx, my, mz], dtype=float)))
    return rows


def fit_full_matrix(rows):
    # Build A (N x 4) with columns [mx, my, mz, 1]
    # and T_x, T_y, T_z targets (N)
    N = len(rows)
    A = np.zeros((N, 4), dtype=float)
    Tx = np.zeros((N,), dtype=float)
    Ty = np.zeros((N,), dtype=float)
    Tz = np.zeros((N,), dtype=float)

    for i, (pose, m) in enumerate(rows):
        A[i, :3] = m
        A[i, 3] = 1.0
        t = POSE_TO_VECTOR[pose]
        Tx[i] = t[0]
        Ty[i] = t[1]
        Tz[i] = t[2]

    # Solve least squares for each output axis
    px, *_ = np.linalg.lstsq(A, Tx, rcond=None)
    py, *_ = np.linalg.lstsq(A, Ty, rcond=None)
    pz, *_ = np.linalg.lstsq(A, Tz, rcond=None)

    # Build M (3x3) and b (3)
    M = np.vstack([px[:3], py[:3], pz[:3]])
    b = np.array([px[3], py[3], pz[3]])

    # Compute residuals and RMSE
    preds = (A[:, :3] @ M.T) + b  # (N,3)
    targets = np.vstack([Tx, Ty, Tz]).T
    residuals = preds - targets
    rmse_per_axis = np.sqrt(np.mean(residuals ** 2, axis=0))
    rmse = math.sqrt(np.mean(residuals ** 2))

    return M, b, rmse_per_axis.tolist(), float(rmse), residuals


def save_json(path: Path, M: np.ndarray, b: np.ndarray, rmse, rmse_axes, sample_count: int):
    out = {
        'M': M.tolist(),
        'b': b.tolist(),
        'rmse': rmse,
        'rmse_per_axis': rmse_axes,
        'sample_count': int(sample_count),
        'method': 'least_squares_3x3',
    }
    with path.open('w', encoding='utf-8') as f:
        json.dump(out, f, indent=2)


def main():
    parser = argparse.ArgumentParser(description='Fit full 3x3 accelerometer calibration')
    parser.add_argument('csv', type=Path, nargs='?', default=Path('test/BNO085/data/accel_static_poses.csv'))
    args = parser.parse_args()

    rows = read_labeled_csv(args.csv)
    if not rows:
        raise SystemExit('No labeled rows found in CSV')

    M, b, rmse_axes, rmse, residuals = fit_full_matrix(rows)

    out_path = args.csv.parent / 'accel_full_calibration.json'
    save_json(out_path, M, b, rmse, rmse_axes, len(rows))

    print('Saved', out_path)
    print('M =')
    print(np.array_str(M, precision=6, suppress_small=True))
    print('b =', b)
    print('RMSE per axis =', rmse_axes)
    print('Overall RMSE =', rmse)

    # Optional: show simple verification (norms close to g)
    norms = np.linalg.norm((np.array([m for _, m in rows]) @ M.T) + b, axis=1)
    print('Mean corrected magnitude =', float(np.mean(norms)), 'std =', float(np.std(norms)))


if __name__ == '__main__':
    main()
