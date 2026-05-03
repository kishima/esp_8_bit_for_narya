cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(narya_core)

# Bake NES ROMs from data/nofrendo/ into the 'storage' LittleFS partition.
littlefs_create_partition_image(storage data/nofrendo FLASH_IN_PROJECT)
