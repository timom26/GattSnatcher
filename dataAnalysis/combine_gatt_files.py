import os
import re
import json

def glue_logs(base_path):
    pattern = re.compile(r'interrogator_log_(\d{1,4})\.bin$')

    for subdir, _, files in os.walk(base_path):
        matching_files = [f for f in files if pattern.match(f)]
        if not matching_files:
            continue

        full_paths = [os.path.join(subdir, f) for f in matching_files]
        output_filename = os.path.join(subdir, "glued_file.bin")

        with open(output_filename, 'wb') as outfile:
            for f in sorted(full_paths):
                with open(f, 'rb') as infile:
                    content = infile.read()
                    lines = content.decode("utf-8").splitlines()
                    buffer = ""
                    for line in lines:
                        if line.strip() == "":
                            continue
                        buffer += line
                        try:
                            obj = json.loads(buffer)
                            if not obj.get("services"):
                                buffer = ""
                                continue
                            outfile.write(json.dumps(obj).encode("utf-8") + b'\n')
                            buffer = ""
                        except json.JSONDecodeError:
                            buffer += "\n"

        print(f"Glued {len(full_paths)} files into {output_filename}")

# Example usage:
# glue_logs("/your/target/path")

if __name__ == '__main__':
    glue_logs("./dataFiles/questioner/unprocessed")