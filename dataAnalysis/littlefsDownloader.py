from littlefs import LittleFS
import subprocess
import os
import argparse
import sys
import datetime
import math

def load_params_from_sdkconfig(path='sdkconfig'):
    """Parse CONFIG_LITTLEFS_* lines from an ESP-IDF sdkconfig file."""
    params = {}
    try:
        with open(path, 'r') as f:
            for line in f:
                if line.startswith('CONFIG_LITTLEFS_BLOCK_SIZE='):
                    params['block_size'] = int(line.split('=',1)[1])
                elif line.startswith('CONFIG_LITTLEFS_BLOCK_COUNT='):
                    params['block_count'] = int(line.split('=',1)[1])
                elif line.startswith('CONFIG_LITTLEFS_READ_SIZE='):
                    params['read_size'] = int(line.split('=',1)[1])
                elif line.startswith('CONFIG_LITTLEFS_WRITE_SIZE='):
                    # SPI program/write granularity in ESP-IDF is called WRITE_SIZE
                    params['prog_size'] = int(line.split('=',1)[1])
    except FileNotFoundError:
        print(f"Warning: sdkconfig not found at '{path}', falling back to interactive input.", file=sys.stderr)
    return params

def erase_partition(port_path, params):
    """
    Erase the 'storage' partition on the given port using esptool.py,
    sourcing offset and size from the params dict returned by parsevariables().
    """
    try:
        storage_offset = params['storage_offset']
        storage_size = params['storage_size']
    except KeyError:
        sys.exit("Error: params dict missing 'storage_offset' or 'storage_size'")
    subprocess.run([
        'esptool.py',
        '--port', port_path,
        'erase_region',
        hex(storage_offset), hex(storage_size)
    ], check=True)

def downloadbinary(binary_name, port_path, offset,size):
    subprocess.run([
        'esptool.py',
        '--port',port_path,
        'read_flash',
        hex(offset), hex(size), binary_name
    ], check=True)

def createoutputfolder():
    output_dir = os.path.join(os.path.dirname(__file__), 'littlefs_out')
    os.makedirs(output_dir, exist_ok=True)
    return output_dir

def mountfilesystem(params, img_data):
    # 5) Mount the filesystem in-memory
    fs = LittleFS(
        block_size=params['block_size'],
        block_count=params['block_count'],
        read_size=params['read_size'],
        prog_size=params['prog_size'],
        mount=False
    )
    # Populate the filesystem’s underlying buffer:
    fs.context.buffer = bytearray(img_data)
    # Now mount it
    fs.mount()
    return fs

def get_rounded_timestamp():
    now = datetime.datetime.now()
    rounded_minute = int(math.floor(now.minute / 5.0) * 5)
    rounded = now.replace(minute=0, second=0, microsecond=0) + datetime.timedelta(minutes=rounded_minute)
    return rounded.strftime('%Y-%m-%d-%H-%M-00')
def extracteachfile(fs, output_dir):
    rounded_ts = get_rounded_timestamp()
    # 4) Walk the filesystem and extract every directory and file
    for root, dirs, files in fs.walk('.'):
        # Make any sub-directories locally
        for d in dirs:
            os.makedirs(os.path.join(output_dir, root, d), exist_ok=True)
        # Dump each file
        for fname in files:
            remote_path = os.path.join(root, fname)
            print(f"[DEBUG] Extracting '{remote_path}'")
            with fs.open(remote_path, 'rb') as f:
                data = f.read()
                print(f"[DEBUG]   -> {len(data)} bytes")
            if fname.startswith('scanner_log_'):
                local_path = os.path.join('dataFiles', 'scanner', 'unprocessed', rounded_ts, fname)
            elif fname.startswith('interrogator_log_'):
                local_path = os.path.join('dataFiles', 'questioner', 'unprocessed', rounded_ts, fname)
            else:
                local_path = os.path.join(output_dir, root, fname)
            os.makedirs(os.path.dirname(local_path), exist_ok=True)
            with open(local_path, 'wb') as out:
                out.write(data)

def parsevariables():
    # Parse LFS parameters via args or sdkconfig
    parser = argparse.ArgumentParser(description="LittleFS downloader and extractor")
    parser.add_argument('--block-size', type=int, help='LFS block size in bytes')
    parser.add_argument('--block-count', type=int, help='Total number of blocks')
    parser.add_argument('--read-size', type=int, help='Read granularity in bytes')
    parser.add_argument('--prog-size', type=int, help='Prog granularity in bytes')
    parser.add_argument('--sdkconfig', default='./../GattSnatcher/sdkconfig', help='Path to sdkconfig file')
    parser.add_argument('--partitions', default='./../GattSnatcher/partitions.csv', help='Path to partitions CSV file')
    args = parser.parse_args()

    # Read storage offset & size from partition table
    part_file = args.partitions
    storage_offset = None
    storage_size = None
    with open(part_file, 'r') as f:
        for ln in f:
            if ln.strip().startswith('storage,'):
                cols = [c.strip() for c in ln.split(',')]
                storage_offset = int(cols[3], 16)
                storage_size   = int(cols[4], 16)
                break
    if storage_offset is None:
        sys.exit(f"Error: 'storage' entry not found in {part_file}")


    # Determine where the params are sourced from
    if any(v is not None for v in (args.block_size, args.block_count, args.read_size, args.prog_size)):
        # override from input: must supply all four params
        if None in (args.block_size, args.block_count, args.read_size, args.prog_size):
            sys.exit('Error: when specifying parameters via arguments, all of --block-size, --block-count, --read-size, --prog-size are required')
        params = {
            'block_size': args.block_size,
            'block_count': args.block_count,
            'read_size': args.read_size,
            'prog_size': args.prog_size
          }
        params['storage_offset'] = storage_offset
        params['storage_size']   = storage_size
    else:
        # default: read read_size/prog_size from sdkconfig,
        # block_size=4096, block_count from partitions.csv
        sc = load_params_from_sdkconfig(args.sdkconfig)
        # ensure we have read and write sizes
        if 'read_size' not in sc or 'prog_size' not in sc:
            print(sc)
            sys.exit('Error: Missing CONFIG_LITTLEFS_READ_SIZE or CONFIG_LITTLEFS_WRITE_SIZE in sdkconfig')

        block_size = 4096
        params = {
            'block_size': block_size,
            'block_count': storage_size // block_size,
            'read_size' : sc['read_size'],
            'prog_size' : sc['prog_size']
             }
        params['storage_offset'] = storage_offset
        params['storage_size']   = storage_size
    return params
def mount_and_read_files(binary_name,params):
    output_dir = createoutputfolder()
    with open(binary_name, 'rb') as img:
        img_data = img.read()
        print(f"[DEBUG] Read {len(img_data)} bytes from '{binary_name}'")
    fs = mountfilesystem(params, img_data)
    print("[DEBUG] Filesystem structure:")
    for root, dirs, files in fs.walk('.'):
        print(f"  {root}/ -> dirs: {dirs}, files: {files}")
    extracteachfile(fs, output_dir)
    # 6) Clean up
    fs.unmount()
def main():
    params = parsevariables()
    print(f"[DEBUG] Loaded parameters: {params}")
    downloadbinary(binary_name,port_path, params['storage_offset'],params['storage_size'])
    downloadbinary(binary_name_gatt,port_path_gatt, params['storage_offset'],params['storage_size'])
    mount_and_read_files(binary_name=binary_name,params=params)
    mount_and_read_files(binary_name=binary_name_gatt,params=params)
    # erase_partition(port_path=port_path,params=params)
    # erase_partition(port_path=port_path_gatt,params=params)


if __name__ == '__main__':
    binary_name = 'storage2.bin'
    port_path =  '/dev/tty.usbserial-0001'
    port_path_gatt =  '/dev/tty.usbserial-3'
    binary_name_gatt = 'storage_gatt.bin'
    main()