"""
Compute 6-sided accelerometer bias and scale from BNO085 CSV data.
Saves results to accel_calibration.json in the same folder.

Usage:
    python accel_calibrate.py data/accel_static_poses.csv

The script prefers labeled static pose captures with a Pose column containing
X+, X-, Y+, Y-, Z+, Z-. It uses the steady-state mean of each pose instead of
raw extrema, which is more stable for still calibration data.

Calibration model:
    bias_i  = 0.5 * (mean_i(+face) + mean_i(-face))
    scale_i = (mean_i(+face) - mean_i(-face)) / (2*g)

If Pose is not available, it falls back to the old min/max method.
"""

import csv
import json
import sys

import numpy as np
import matplotlib.pyplot as plt

G = 9.80665


def read_csv(path):
    rows = []
    with open(path, newline='') as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append(row)
    return rows


def to_float_list(rows, key):
    vals = []
    for r in rows:
        try:
            vals.append(float(r[key]))
        except Exception:
            vals.append(np.nan)
    return np.array(vals)


def _pose_means(rows):
    grouped = {}
    for row in rows:
        pose = (row.get('Pose') or '').strip()
        if not pose:
            continue
        try:
            x = float(row['RawAccelX'])
            y = float(row['RawAccelY'])
            z = float(row['RawAccelZ'])
        except Exception:
            continue
        grouped.setdefault(pose, {'x': [], 'y': [], 'z': []})
        grouped[pose]['x'].append(x)
        grouped[pose]['y'].append(y)
        grouped[pose]['z'].append(z)
    if not grouped:
        return None

    means = {}
    for pose, axes in grouped.items():
        means[pose] = {
            'x': float(np.mean(axes['x'])),
            'y': float(np.mean(axes['y'])),
            'z': float(np.mean(axes['z'])),
            'count': int(len(axes['x'])),
        }
    return means


def _fit_from_pose_means(means, g=G):
    required = ['X+', 'X-', 'Y+', 'Y-', 'Z+', 'Z-']
    missing = [pose for pose in required if pose not in means]
    if missing:
        raise ValueError(f'Missing required poses: {", ".join(missing)}')

    bx = 0.5 * (means['X+']['x'] + means['X-']['x'])
    by = 0.5 * (means['Y+']['y'] + means['Y-']['y'])
    bz = 0.5 * (means['Z+']['z'] + means['Z-']['z'])

    sx = (means['X+']['x'] - means['X-']['x']) / (2.0 * g)
    sy = (means['Y+']['y'] - means['Y-']['y']) / (2.0 * g)
    sz = (means['Z+']['z'] - means['Z-']['z']) / (2.0 * g)

    return {
        'bias': [float(bx), float(by), float(bz)],
        'scale': [float(sx), float(sy), float(sz)],
        'pose_means': means,
        'method': 'pose_means_6_face',
    }


def compute_accel_calibration(rows, g=G):
    means = _pose_means(rows)
    if means is not None:
        try:
            return _fit_from_pose_means(means, g=g)
        except Exception:
            pass

    raw_x = to_float_list(rows, 'RawAccelX')
    raw_y = to_float_list(rows, 'RawAccelY')
    raw_z = to_float_list(rows, 'RawAccelZ')

    # drop NaNs
    raw_x = raw_x[~np.isnan(raw_x)]
    raw_y = raw_y[~np.isnan(raw_y)]
    raw_z = raw_z[~np.isnan(raw_z)]

    max_x, min_x = np.max(raw_x), np.min(raw_x)
    max_y, min_y = np.max(raw_y), np.min(raw_y)
    max_z, min_z = np.max(raw_z), np.min(raw_z)

    bx = 0.5 * (max_x + min_x)
    by = 0.5 * (max_y + min_y)
    bz = 0.5 * (max_z + min_z)

    sx = (max_x - min_x) / (2.0 * g)
    sy = (max_y - min_y) / (2.0 * g)
    sz = (max_z - min_z) / (2.0 * g)

    return {
        'bias': [float(bx), float(by), float(bz)],
        'scale': [float(sx), float(sy), float(sz)],
        'method': 'raw_min_max_fallback',
        'raw_extremes': {
            'max': [float(max_x), float(max_y), float(max_z)],
            'min': [float(min_x), float(min_y), float(min_z)],
        }
    }


def apply_calibration(rows, calib):
    bx, by, bz = calib['bias']
    sx, sy, sz = calib['scale']
    out = []
    for r in rows:
        try:
            rx = float(r['RawAccelX'])
            ry = float(r['RawAccelY'])
            rz = float(r['RawAccelZ'])
        except Exception:
            out.append((np.nan, np.nan, np.nan))
            continue
        ax = (rx - bx) / sx if sx != 0 else np.nan
        ay = (ry - by) / sy if sy != 0 else np.nan
        az = (rz - bz) / sz if sz != 0 else np.nan
        out.append((ax, ay, az))
    return np.array(out)


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print('Usage: python accel_calibrate.py BNO085_data.csv')
        sys.exit(1)

    csv_path = sys.argv[1]
    rows = read_csv(csv_path)
    calib = compute_accel_calibration(rows)

    out_path = 'accel_calibration.json'
    with open(out_path, 'w') as f:
        json.dump(calib, f, indent=2)

    print('Saved calibration to', out_path)
    print('Method:', calib.get('method', 'unknown'))
    print('Bias:', calib['bias'])
    print('Scale:', calib['scale'])

    # Plot raw vs corrected for a short window
    corrected = apply_calibration(rows, calib)
    raw_x = to_float_list(rows, 'RawAccelX')
    raw_y = to_float_list(rows, 'RawAccelY')
    raw_z = to_float_list(rows, 'RawAccelZ')

    t = np.arange(len(raw_x)) * 0.02  # approximate sample time from sketch (20 ms)

    plt.figure(figsize=(10,6))
    plt.subplot(3,1,1)
    plt.plot(t, raw_x, label='raw X')
    plt.plot(t, corrected[:,0], label='corrected X')
    plt.legend()
    plt.subplot(3,1,2)
    plt.plot(t, raw_y, label='raw Y')
    plt.plot(t, corrected[:,1], label='corrected Y')
    plt.legend()
    plt.subplot(3,1,3)
    plt.plot(t, raw_z, label='raw Z')
    plt.plot(t, corrected[:,2], label='corrected Z')
    plt.legend()
    plt.tight_layout()
    plt.show()
