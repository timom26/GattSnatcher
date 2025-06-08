

import json
import matplotlib.pyplot as plt
import numpy as np
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

def parse_gatt_json(profiles, macs):
    for idx, features in enumerate(profiles):
        print("==================================================================================================")
        print(f"Remote MAC: {macs[idx]}")
        print("Services:\n")

        services = {}
        for feature in features:
            parts = feature.split(":")
            if len(parts) != 4:
                continue
            service_uuid, char_uuid, props, value = parts
            if service_uuid not in services:
                services[service_uuid] = []
            services[service_uuid].append((char_uuid, props, value))

        for service_uuid in sorted(services.keys()):
            print("    .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .")
            print(f"  Service UUID: {service_uuid} \"{decode_uuid(service_uuid)}\"")

            for char_uuid, props, value in services[service_uuid]:
                print(f"    Characteristic UUID: {char_uuid} \"{decode_characteristic(char_uuid)}\"")
                print(f"      Properties:      {props}")
                print(f"      Value (unicode): {value}")
                print()




# --- Feature extraction and Jaccard similarity for GATT profile analysis ---
def profile_to_feature_set(profile):
    features = set()
    for service in profile.get("services", []):
        service_uuid = service.get("uuid") or bytes_to_uuid(service.get("uuid128", []))
        for char in service.get("characteristics", []):
            char_uuid = char.get("uuid") or bytes_to_uuid(char.get("uuid128", []))
            props = decode_properties(char.get("properties", 0))
            value = bytes_to_unicode(char.get("value", []))
            feature = f"{service_uuid}:{char_uuid}:{props}:{value}"
            features.add(feature)
    return features

def jaccard_similarity(set1, set2):
    intersection = set1 & set2
    union = set1 | set2
    if not union:
        return 0.0
    return len(intersection) / len(union)

def process_all_gatt_profiles(file_path):
    profiles = []
    macs = []

    def split_json_objects(raw_text):
        decoder = json.JSONDecoder()
        idx = 0
        while idx < len(raw_text):
            try:
                obj, offset = decoder.raw_decode(raw_text[idx:])
                yield obj
                idx += offset
                while idx < len(raw_text) and raw_text[idx] in [' ', '\n', '\r']:
                    idx += 1
            except json.JSONDecodeError as e:
                print(f"Skipping invalid JSON block at index {idx}: {e}")
                break

    with open(file_path, "r") as f:
        raw = f.read()

    for profile in split_json_objects(raw):
        features = profile_to_feature_set(profile)

        #FILTERS 

        if not features:#skip empty profiles
            continue
        # elif (test_for_apple(profile) == True):
        #     continue  # Skip Apple devices
        else:
            profiles.append(features)
            macs.append(profile.get("remote_bda", "N/A"))

    return macs, profiles

def plot_gatt_profile_similarity_matrix(macs, profiles):
    n = len(profiles)
    matrix = np.zeros((n, n))
    for i in range(n):
        for j in range(i, n):
            sim = jaccard_similarity(profiles[i], profiles[j])
            matrix[i, j] = sim
            matrix[j, i] = sim 
    fig, ax = plt.subplots(figsize=(10, 8))
    cax = ax.matshow(np.triu(matrix), cmap='viridis')
    plt.colorbar(cax)
    ax.set_xticks(range(n))
    ax.set_yticks(range(n))
    ax.set_xticklabels(macs, rotation=90, fontsize=14)
    ax.set_yticklabels(macs, fontsize=14)
    plt.title("Jaccard Similarity Matrix of BLE GATT Profiles", fontsize=14)
    plt.tight_layout()
    plt.show()

def test_for_apple(profile) -> bool:
    apple_keywords = ["Apple", "AirPods", "iPhone", "iPad"]#Watch missing
    apple_service_uuids = ["fca0", "fcb2", "fd43", "fd44", "fe13"]  # Assigned to Apple Inc.
    apple_manufacturer_ids = [76]   # Apple Inc. (0x004C)
    for svc in profile.get("services", []):
        service_uuid = svc.get("uuid") or bytes_to_uuid(svc.get("uuid128", []))
        if service_uuid and service_uuid.lower().startswith("fd") and service_uuid.lower() in apple_service_uuids:
            return True

        for char in svc.get("characteristics", []):
            val = bytes_to_unicode(char.get("value", []))
            for keyword in apple_keywords:
                if keyword.lower() in val.lower():
                    return True

            raw = char.get("value", [])
            if len(raw) >= 2 and raw[1] in apple_manufacturer_ids:
                return True

    return False


# Filter function: keep only MACs and their profiles that are in filter_list
def filter_changed_profiles(macs, profiles, filter_list):
    """
    Return only those MACs and their profiles where the MAC is in filter_list.
    """
    filtered_macs = []
    filtered_profiles = []
    for mac, prof in zip(macs, profiles):
        if mac.upper() in filter_list:
            filtered_macs.append(mac)
            filtered_profiles.append(prof)
    return filtered_macs, filtered_profiles


if __name__ == '__main__':
    file_path = "./dataFiles/questioner/unprocessed/2025-05-27-16-45-00/glued_file.bin"
    changed_macs = [

    ]
    macs, profiles = process_all_gatt_profiles(file_path)
        # Keep only changed MACs
    # macs, profiles = filter_changed_profiles(macs, profiles, changed_macs)
    parse_gatt_json(profiles=profiles,macs=macs)
    plot_gatt_profile_similarity_matrix(macs, profiles)