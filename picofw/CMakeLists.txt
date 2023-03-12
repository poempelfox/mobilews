cmake_minimum_required(VERSION 3.12)
include(pico_sdk_import.cmake)
project(mobilews CXX C ASM)
pico_sdk_init()

# Waveshare ePaper Library
# add a compilation subdirectory
# currently none
# add a header directory
# currently none

# our main source directory
include_directories(./src)

# generate an executable file
add_executable(mobilews 
src/main.c
)

# enable usb output, disable uart output
pico_enable_stdio_usb(mobilews 1)
pico_enable_stdio_uart(mobilews 0)

# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(mobilews)

target_link_libraries(mobilews pico_stdlib hardware_spi hardware_adc hardware_gpio hardware_rtc)

