

import json
from bluetooth_numbers import service as services
from bluetooth_numbers import characteristic as characteristics
from bluetooth_numbers import company
from uuid import UUID
import sys
import os

# Add the directory containing custom_uuid.py to sys.path
script_dir = os.path.dirname(__file__)
includes_dir = os.path.join(script_dir, "includes")
sys.path.insert(0, includes_dir)

from custom_uuid import custom_uuid_company as custom_uuid

INPUT_DIR = "./dataFiles/questioner/unprocessed"
PROCESSED_DIR = "./dataFiles/questioner/processed"

# Helper to format 128-bit UUIDs from byte lists
def bytes_to_uuid(byte_list):
    """
    Convert a list of 16 integer byte values into a standard UUID string.
    """
    hex_str = ''.join(f'{b:02x}' for b in byte_list)
    # Format: 8-4-4-4-12
    return f'{hex_str[0:8]}-{hex_str[8:12]}-{hex_str[12:16]}-{hex_str[16:20]}-{hex_str[20:32]}'

def decode_uuid(uuid_entry):
    """
    Decode a GATT service UUID string to human-readable name.
    Handles both 16-bit (short) and full 128-bit UUIDs.
    """
    if isinstance(uuid_entry, str):
        # Short 16-bit UUIDs like "0x1800"
        if uuid_entry.startswith("0x"):
            try:
                uid = int(uuid_entry, 16)
            except ValueError:
                return uuid_entry
            # 1) Standard Bluetooth SIG services
            name = services.get(uid)
            if name:
                return name
            # 2) Vendor assignment via bluetooth_numbers.company
            comp_name = company.get(uid)
            if comp_name:
                return comp_name
            # 3) Manual vendor-specific services (fallback)
            # vendor_name = vendor_services.get(uid)
            # if vendor_name:
            #     return vendor_name
            # Try custom mapping before fallback
            custom = decode_custom_uuid(uuid_entry)
            if custom:
                return custom
            # 4) Fallback to raw
            return uuid_entry
        # Full 128-bit UUID strings
        try:
            u = UUID(uuid_entry)
        except ValueError:
            return uuid_entry
        hex_str = u.hex  # 32 hex digits without hyphens
        # Check for Bluetooth Base UUID pattern: 0000xxxx-0000-1000-8000-00805f9b34fb
        if hex_str.startswith("0000") and hex_str[8:] == "00001000800000805f9b34fb":
            short = int(hex_str[4:8], 16)
            return services.get(short, "Custom Service")
        custom = decode_custom_uuid(uuid_entry)
        if custom:
            return custom
        return "Custom Service"
    return "Custom Service"

def decode_custom_uuid(uuid_entry):
    """
    Map a 16-bit or 128-bit UUID into a custom service name using custom_uuid dict.
    """
    # Normalize to lowercase
    entry = uuid_entry.lower()
    # If given as 0xXXXX, convert to full 128-bit form
    if entry.startswith("0x"):
        short = entry[2:].rjust(4, "0")
        full = f"0000{short.upper()}-0000-1000-8000-00805f9b34fb"
    else:
        full = entry
    try:
        return custom_uuid[full]
    except:
        return None

def decode_characteristic(uuid_entry):
    if isinstance(uuid_entry, str) and uuid_entry.startswith("0x"):
        try:
            uid = int(uuid_entry, 16)
            return f"{characteristics.get(uid, uuid_entry)}"
        except ValueError:
            return uuid_entry
    return "Custom Characteristic"


# Helper: decode properties bitmask to human-readable flags
def decode_properties(props):
    """
    Decode the GATT characteristic properties bitmask into human-readable flags.
    """
    flags = []
    if props & 0x01: flags.append("Broadcast")
    if props & 0x02: flags.append("Read")
    if props & 0x04: flags.append("Write Without Response")
    if props & 0x08: flags.append("Write")
    if props & 0x10: flags.append("Notify")
    if props & 0x20: flags.append("Indicate")
    if props & 0x40: flags.append("Authenticated Signed Writes")
    if props & 0x80: flags.append("Extended Properties")
    return "|".join(flags) if flags else "None"

def bytes_to_unicode(byte_list):
    try:
        b = bytes(byte_list)
        # replaces invalid sequences with the Unicode replacement character
        return b.decode('utf-8', errors='replace')
    except Exception:
        # Fallback: map printable ASCII directly, and use � for non-printable bytes
        return ''.join(chr(x) if 32 <= x < 127 else '\ufffd' for x in byte_list)

def parse_gatt_json(file_path, output_file):
    def split_json_objects(raw_text):
        decoder = json.JSONDecoder()
        idx = 0
        while idx < len(raw_text):
            try:
                obj, offset = decoder.raw_decode(raw_text[idx:])
                yield obj
                idx += offset
                # Skip whitespace or newlines after object
                while idx < len(raw_text) and raw_text[idx] in [' ', '\n', '\r']:
                    idx += 1
            except json.JSONDecodeError as e:
                print(f"Skipping invalid JSON block at index {idx}: {e}")
                break

    with open(file_path, "r") as f:
        raw = f.read()

    with open(output_file, "w") as out:
        for idx, profile in enumerate(split_json_objects(raw)):#Lord have mercy neni casu
            print("==================================================================================================")
            out.write("==================================================================================================\n")
            
            print(f"Remote MAC: {profile.get('remote_bda', 'N/A')}")
            out.write(f"Remote MAC: {profile.get('remote_bda', 'N/A')}\n")
            
            print("Services:\n")
            out.write("Services:\n\n")
            

            for service in profile.get("services", []):
                if service.get("uuid"):
                    uuid_str = service["uuid"]
                else:
                    uuid_str = bytes_to_uuid(service.get("uuid128", []))
                print("    .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .")
                out.write("    .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .\n")
                print(f"  Service UUID: {uuid_str} \"{decode_uuid(uuid_str)}\".  StartH: {service['start_handle']} EndH: {service['end_handle']}")
                out.write(f"  Service UUID: {uuid_str} \"{decode_uuid(uuid_str)}\".  StartH: {service['start_handle']} EndH: {service['end_handle']}\n")

                for char in service.get("characteristics", []):
                    if char.get("uuid"):
                        cuuid = char["uuid"]
                    else:
                        cuuid = bytes_to_uuid(char.get("uuid128", []))
                    props = char.get("properties", 0)
                    handle = char.get("handle")
                    val_raw = char.get("value", [])
                    val_unicode = bytes_to_unicode(val_raw)

                    print(f"    Characteristic UUID: {cuuid} \"{decode_characteristic(cuuid)}\"")
                    out.write(f"    Characteristic UUID: {cuuid} \"{decode_characteristic(cuuid)}\"\n")

                    print(f"      Handle:          {handle}")
                    out.write(f"      Handle:          {handle}\n")
                    props_str = decode_properties(props)
                    print(f"      Properties:      {props_str} (0x{props:02x})")
                    out.write(f"      Properties:      {props_str} (0x{props:02x})\n")
                    print(f"      Value (unicode): {val_unicode}")
                    out.write(f"      Value (unicode): {val_unicode}\n")
                    print(f"      Value (raw):     {val_raw}")
                    out.write(f"      Value (raw):     {val_raw}\n")
                print()

import os
import shutil

if __name__ == '__main__':
    if not os.path.exists(PROCESSED_DIR):
        os.makedirs(PROCESSED_DIR)

    for root, _, files in os.walk(INPUT_DIR):
        for filename in files:
            if not filename.endswith(".bin"):
                continue
            else:
                print(f"Processing file: {filename}")

            rel_path = os.path.relpath(root, INPUT_DIR)
            output_dir = os.path.join(PROCESSED_DIR, rel_path)
            os.makedirs(output_dir, exist_ok=True)

            input_path = os.path.join(root, filename)
            output_path = os.path.join(output_dir, filename)

            # Skip if already processed
            if os.path.exists(output_path):
                print(f"Already processed: {filename}")
                continue

            try:
                parse_gatt_json(input_path, output_path)
                # shutil.copy(input_path, output_path)
                print(f"Processed (original kept): {filename}")
            except Exception as e:
                print(f"Error processing {filename}: {e}")
    # parse_gatt_json("./dataFiles/questioner/glued_gatt_log_0.bin", "./dataFiles/questioner/glued_gatt_log_0.txt")
