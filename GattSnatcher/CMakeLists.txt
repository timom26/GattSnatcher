# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Espressif CMake configuration
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# This project uses common functions for Bluetooth HCI layer.
set(EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/examples/bluetooth/hci/hci_common_component)

project(ble-ad-scanner)
