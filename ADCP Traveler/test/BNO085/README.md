# BNO085 Arduino IDE Sensor Test

This folder is for testing the BNO085 with the Arduino IDE.

## Files

- `BNO085.ino`: Arduino sketch to flash to the ESP32.
- `BNO085_data.csv`: empty data file with the expected column headers.
- `plot_BNO085_csv.py`: Python plot script.
- `plot_BNO085_csv.m`: MATLAB plot script.

## Workflow

1. Open `BNO085.ino` in the Arduino IDE.
2. Install the `Adafruit BNO08x` library if needed.
3. Select your ESP32 board and port.
4. Upload the sketch.
5. Close Serial Monitor if it is open.
6. Save the CSV output into `BNO085_data.csv`. The sketch calibrates gyro bias for 2 seconds, then records for 15 seconds and prints `END`.
7. Run either plot script.

Python:

```sh
python plot_BNO085_csv.py
```

This saves `BNO085_data_plot.png` and opens the PNG automatically.
Use `--no-open` if you only want to save the file:

```sh
python plot_BNO085_csv.py --no-open
```

MATLAB:

```matlab
plot_BNO085_csv
```
