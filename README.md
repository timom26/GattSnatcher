# GattSnatcher - Diploma thesis of Bc. Timotej Kamensky
2025-05-28
Brno University of Technology, Faculty of Information Technology

## Basic outline
The diploma thesis focuses on trying to break the privacy of Bluetooth LE devices, utilising GATT profiles and a statistical approach to the advertisements. The whole work is separated into two parts:
1. ESP-32 Program (firmware) serving the data collection, and
2. Data analysis using Python libraries.


## ESP-32 Data collection
ESP-32 is composed of two chips, one serves advertisement collection, the other serves GATT collection. 
The ESP-32 program is written as the same program for both chips, with the difference of a single idf.py menuconfig parameter.

To compile the program, you will need to install ESP-IDF 5.1.1 (follow the guide on the Espressif website). Do not forget to import the environment before use.  Then, replace the libbtdm_app library  with the Espressif customized version (now in the includes directory): `mv vendor/libbtdm_app.a ${IDF_PATH}/components/bt/controller/lib_esp32/esp32`

In idf.py menuconfig, find the project settings. For one chip, set the Chip role to Questioner, save, build, and flash. For the other chip, set the Chip role to Scanner, save, build, and flash.


There are a few Python scripts with different tasks. We will look at them one by one.

- littlefsDownloader.py - To download the files from the chip, use littlefsDownloader.py script. This will download all data from both of the chips (if set to appropriate interfaces in the script itself) as binary blobs, walk over them, create actual files from them, and then erase the storage flash memory. There are two important parameters that are not loaded automatically: the ports of the connected devices.

- process_interrogator_files.py and process_scanner_files.py - automatically walk over all of the files that are still unprocessed, and process them. In case of the scanner, it means decoding the binary structure into a CSV file; in case of the interrogator, it's a case of making it human-readable. 

- combine_advertisement_files.py and combine_gatt_files.py - one file == one bootup, one folder == one measurement session. To process the whole session, we combine the files into a single file. 

- analysis_gatt.py - Computes and displays the similarity of GATT profiles. The name of the file is hardcoded in the code, though.

- analysis_advertisement.py - Computes and displays the advertisements. Also calls for analysis_gatt, to show the similarity of profiles of paired MAC addresses (as a form of verification). 

The required Python packages are mostly standard ones: 
json, matplotlib,numpy, pandas, bluetooth_numbers, uuid.
