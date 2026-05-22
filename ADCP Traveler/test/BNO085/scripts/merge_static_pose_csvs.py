"""Merge the six still-pose capture files into one calibration CSV.

The input files are expected in test/BNO085/data/:
  X+.csv, X-.csv, Y+.csv, Y-.csv, Z+.csv, Z-.csv

The output is a single comma-separated CSV with a Pose label column.
"""

from __future__ import annotations

from pathlib import Path
import csv


POSE_FILES = ["X+.csv", "X-.csv", "Y+.csv", "Y-.csv", "Z+.csv", "Z-.csv"]
OUTPUT_NAME = "accel_static_poses.csv"
OUTPUT_HEADER = ["Pose", "Time_s", "RawAccelX", "RawAccelY", "RawAccelZ"]


def merge_static_pose_csvs(data_dir: Path, output_path: Path) -> int:
    rows_written = 0

    with output_path.open("w", newline="", encoding="utf-8") as out_file:
        writer = csv.writer(out_file)
        writer.writerow(OUTPUT_HEADER)

        for filename in POSE_FILES:
            input_path = data_dir / filename
            if not input_path.exists():
                raise FileNotFoundError(f"Missing input capture: {input_path}")

            pose = input_path.stem
            with input_path.open("r", newline="", encoding="utf-8-sig") as in_file:
                reader = csv.DictReader(in_file, delimiter="\t")
                expected_header = ["Time_s", "RawAccelX", "RawAccelY", "RawAccelZ"]
                actual_header = [column.strip() for column in (reader.fieldnames or [])]
                if actual_header != expected_header:
                    raise ValueError(
                        f"Unexpected header in {input_path.name}: {actual_header}"
                    )

                for row in reader:
                    time_value = (row.get("Time_s") or "").strip()
                    if not time_value:
                        continue

                    writer.writerow([
                        pose,
                        time_value,
                        (row.get("RawAccelX") or "").strip(),
                        (row.get("RawAccelY") or "").strip(),
                        (row.get("RawAccelZ") or "").strip(),
                    ])
                    rows_written += 1

    return rows_written


def main() -> None:
    script_dir = Path(__file__).resolve().parent
    data_dir = script_dir.parent / "data"
    output_path = data_dir / OUTPUT_NAME

    rows_written = merge_static_pose_csvs(data_dir, output_path)
    print(f"Wrote {rows_written} rows to {output_path}")


if __name__ == "__main__":
    main()
