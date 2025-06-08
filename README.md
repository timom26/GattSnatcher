# GattSnatcher - Diploma thesis of Bc. Timotej Kamensky
2025-05-28
Brno University of Technology, Faculty of Information technology

## Basic outline
The diploma thesis focuses on trying to break privacy of Bluetooth LE devices, utilising GATT profiles and statistical approach to the advertisements. The whole work is separated into two parts:
1. ESP-32 Program (firmware) serving the data collection
2. Data analysis using python libraries.


## ESP-32 Data collection
ESP-32 is composed of two chips, one serves advertisement collection, the other serves GATT collection. 
The ESP-32 program is written as the same program for both chips, with the difference of a single idf.py menuconfig parameter

to compile it, install ESP-IDF 5.1.1 (follow the guide on the Espressif website). Do not forget to install and then to import the ESP-IDF before use.  Then, Replace the libbtdm_app library  with Espressif customized version (now in includes directory): `mv vendor/libbtdm_app.a ${IDF_PATH}/components/bt/controller/lib_esp32/esp32`

in idf.py menuconfig, find profile settings. For one chip, set the Chip role to Questioner, save, build and flash. For the other chip, set the Chip role to Scanner, save, build and flash.


there are a few python scripts with different tasks. We will look at them one by one.

- littlefsDownloader.py - To download the files from the chip, use littlefsDownloader.py script. This will download all data from both of the chips (if set to appropriate interfaces) as binary blobs, walk over them, create actual files from them, and then erase the storage flash memory. There are two important parameters that are not loaded automatically: the ports of the connected devices.

- process_interrogator_files.py and process_scanner_files.py - automatically walk over all of the files that are still unprocessed, and process them. In case of scanner, it means decoding binary struct into a csv, in case of the interrogator, its a case of making it human readable. 

- combine_advertisement_files.py and combine_gatt_files.py - one file == one bootup, one folder == one measurement session. To process the whole session, we combine the files into a single file. 

- analysis_gatt.py - computes and displays similarity of gatt profiles. Name of the file is hardcoded in the code.

- analysis_advertisement.py - computes and displays the advertisements. Also calls for analysis_gatt, to show similarity of profiles of paired mac addresses (as a form of verification). 

required python packages are mostly standard ones: 
json, matplotlib,numpy, pandas, bluetooth_numbers, uuid.