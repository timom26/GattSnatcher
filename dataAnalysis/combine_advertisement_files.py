import os
import re
import pandas as pd

def glue_advertisement_logs(base_path):

    for subdir, _, files in os.walk(base_path):
        csv_files = [f for f in files if f.startswith("scanner_log_") and f.endswith(".csv")]
        if not csv_files:
            continue

        # Sort files by numeric suffix
        csv_files.sort(key=lambda f: int(re.search(r'scanner_log_(\d+)\.csv', f).group(1)))

        full_paths = [os.path.join(subdir, f) for f in csv_files]
        output_file = os.path.join(subdir, "glued_advertisements.csv")

        previous_max_timestamp = 0

        with open(output_file, 'w', newline='') as outfile:
            writer = None

            for path in full_paths:
                df = pd.read_csv(path)
                if df.empty:
                    continue
                df["timestamp_us"] += previous_max_timestamp
                previous_max_timestamp = df["timestamp_us"].max() + 10_000_000  # 10 seconds buffer

                if writer is None:
                    df.to_csv(outfile, index=False)
                    writer = True
                else:
                    df.to_csv(outfile, mode='a', index=False, header=False)

        print(f"Glued {len(full_paths)} files into {output_file}")

if __name__ == "__main__":
    glue_advertisement_logs("./dataFiles/scanner/processed")
