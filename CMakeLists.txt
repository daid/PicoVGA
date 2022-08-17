cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)
project(my_project)
pico_sdk_init()

add_executable(vga
    main.cpp
)
pico_generate_pio_header(vga ${CMAKE_CURRENT_LIST_DIR}/vga.pio)

pico_enable_stdio_usb(vga 1)
pico_enable_stdio_uart(vga 0)

target_link_libraries(vga pico_stdlib hardware_pio hardware_dma)
pico_add_extra_outputs(vga)