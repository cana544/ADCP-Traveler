import serial
import time
import matplotlib.pyplot as plt
from collections import deque

# ---------------- USER SETTINGS ----------------
PORT = "COM3"      # Change this to your ESP32 port
BAUD = 115200
MAX_POINTS = 1000
# ------------------------------------------------

def main():
    ser = serial.Serial(PORT, BAUD, timeout=1)
    time.sleep(2)

    print("Connected to", PORT)
    print("Waiting for CSV header...")

    header_columns = []
    while True:
        line = ser.readline().decode(errors="ignore").strip()
        if line.startswith("Time_s"):
            header_columns = [column.strip() for column in line.split(",")]
            print("Header found:")
            print(line)
            break

    xs = deque(maxlen=MAX_POINTS)
    ys = deque(maxlen=MAX_POINTS)
    zs = deque(maxlen=MAX_POINTS)

    plt.ion()
    fig = plt.figure()
    ax = fig.add_subplot(111, projection="3d")

    trajectory_line, = ax.plot([], [], [], linewidth=1.5)
    current_point, = ax.plot([], [], [], marker="o", markersize=6)

    ax.set_title("Live ADCP Traveller 3D Displacement")
    ax.set_xlabel("X displacement [m]")
    ax.set_ylabel("Y displacement [m]")
    ax.set_zlabel("Z displacement [m]")

    print("Reading live IMU data...")

    while True:
        raw = ser.readline().decode(errors="ignore").strip()

        if not raw:
            continue

        if raw == "END":
            print("ESP32 sent END")
            break

        if raw.startswith("Time_s"):
            continue

        parts = [part.strip() for part in raw.split(",")]

        if len(parts) != len(header_columns):
            print("Bad line:", raw)
            continue

        try:
            row = dict(zip(header_columns, parts))

            t = float(row["Time_s"])

            lin_x = float(row["LinX"])
            lin_y = float(row["LinY"])
            lin_z = float(row["LinZ"])

            vel_x = float(row["VelX"])
            vel_y = float(row["VelY"])
            vel_z = float(row["VelZ"])

            disp_x = float(row["DispX"])
            disp_y = float(row["DispY"])
            disp_z = float(row["DispZ"])

            gyro_x = float(row["GyroX_rad_s"])
            gyro_y = float(row["GyroY_rad_s"])
            gyro_z = float(row["GyroZ_rad_s"])

            angle_x = float(row["AngleX_rad"])
            angle_y = float(row["AngleY_rad"])
            angle_z = float(row["AngleZ_rad"])

        except (KeyError, ValueError):
            print("Parse error:", raw)
            continue

        xs.append(disp_x)
        ys.append(disp_y)
        zs.append(disp_z)

        trajectory_line.set_data(list(xs), list(ys))
        trajectory_line.set_3d_properties(list(zs))

        current_point.set_data([disp_x], [disp_y])
        current_point.set_3d_properties([disp_z])

        margin = 0.2

        if len(xs) > 1:
            ax.set_xlim(min(xs) - margin, max(xs) + margin)
            ax.set_ylim(min(ys) - margin, max(ys) + margin)
            ax.set_zlim(min(zs) - margin, max(zs) + margin)
        else:
            ax.set_xlim(-margin, margin)
            ax.set_ylim(-margin, margin)
            ax.set_zlim(-margin, margin)

        fig.canvas.draw()
        fig.canvas.flush_events()

        print(
            f"t={t:.2f}s | "
            f"lin=({lin_x:.4f}, {lin_y:.4f}, {lin_z:.4f}) m/s^2 | "
            f"vel=({vel_x:.4f}, {vel_y:.4f}, {vel_z:.4f}) m/s | "
            f"disp=({disp_x:.4f}, {disp_y:.4f}, {disp_z:.4f}) m | "
            f"gyro=({gyro_x:.4f}, {gyro_y:.4f}, {gyro_z:.4f}) rad/s | "
            f"angle=({angle_x:.4f}, {angle_y:.4f}, {angle_z:.4f}) rad"
        )

    ser.close()


if __name__ == "__main__":
    main()
