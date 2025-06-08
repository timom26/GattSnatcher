import struct
import csv
import os
import shutil

INPUT_DIR = "./dataFiles/scanner/unprocessed"
PROCESSED_DIR = "./dataFiles/scanner/processed"

def mac_bytes_to_str(mac_bytes):
    return ':'.join(f'{b:02X}' for b in reversed(mac_bytes))

def parse_single_file(input_path, output_path):
    with open(input_path, "rb") as bin_file, open(output_path, "w", newline="") as csv_file:
        writer = csv.writer(csv_file)
        writer.writerow([
            "timestamp_us", "adv_event_type", "addr_type",
            "mac_address", "adv_data_length", "rssi"
        ])

        while True:
            hdr = bin_file.read(16)
            if len(hdr) < 16:
                break

            ts_bytes = hdr[0:6] + b'\x00\x00'
            timestamp = struct.unpack("<Q", ts_bytes)[0]
            adv_event_type = hdr[6]
            addr_type = hdr[7]
            mac_address = mac_bytes_to_str(hdr[8:14])
            adv_data_length = hdr[14]
            rssi = struct.unpack("b", bytes([hdr[15]]))[0]

            writer.writerow([
                timestamp, adv_event_type, addr_type,
                mac_address, adv_data_length, rssi
            ])
            # print(f"Record: ts={timestamp}, event={adv_event_type}, type={addr_type}, mac={mac_address}, len={adv_data_length}, rssi={rssi}")

if __name__ == "__main__":
    print("start")
    if not os.path.exists(PROCESSED_DIR):
        os.makedirs(PROCESSED_DIR)

    for root, _, files in os.walk(INPUT_DIR):
        for filename in files:
            if not filename.endswith(".bin"):
                continue
            print(f"Processing file: {filename}")
            rel_path = os.path.relpath(root, INPUT_DIR)
            output_dir = os.path.join(PROCESSED_DIR, rel_path)
            os.makedirs(output_dir, exist_ok=True)
            input_path = os.path.join(root, filename)
            output_path = os.path.join(output_dir, filename.replace(".bin", ".csv"))
            if os.path.exists(output_path):
                print(f"Already processed: {filename}")
                continue
            parse_single_file(input_path, output_path)
            print(f"Processed (original kept): {filename}")
    print("end")
