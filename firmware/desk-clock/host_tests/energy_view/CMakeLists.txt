cmake_minimum_required(VERSION 3.16)
project(energy_view_test LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
get_filename_component(FIRMWARE "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${FIRMWARE}/managed_components/lvgl__lvgl" lvgl)
add_executable(energy_view_test main.cpp
    "${FIRMWARE}/components/clock_ui/energy_view.cpp"
    "${FIRMWARE}/components/clock_ui/fonts/clock_cjk_16.c"
    "${FIRMWARE}/components/clock_ui/fonts/clock_cjk_24.c"
    "${FIRMWARE}/components/clock_ui/fonts/clock_cjk_32.c"
    "${FIRMWARE}/components/clock_ui/fonts/energy_digits_64.c")
target_include_directories(energy_view_test PRIVATE "${FIRMWARE}/components/clock_ui/include")
target_link_libraries(energy_view_test PRIVATE lvgl)


add_executable(devices_view_test devices_main.cpp
    "${FIRMWARE}/components/clock_ui/devices_view.cpp"
    "${FIRMWARE}/components/ha_devices/device_power.cpp"
    "${FIRMWARE}/components/clock_ui/fonts/clock_cjk_16.c"
    "${FIRMWARE}/components/clock_ui/fonts/clock_cjk_24.c")
target_include_directories(devices_view_test PRIVATE "${FIRMWARE}/components/clock_ui/include" "${FIRMWARE}/components/ha_devices/include")
target_link_libraries(devices_view_test PRIVATE lvgl)
