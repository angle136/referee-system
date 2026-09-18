cmake_minimum_required(VERSION 3.22)
project(base)

# MCU flags / 链接脚本见本目录 cmake/gcc-arm-none-eabi.cmake

# 本板差异
# ThreadX configuration for Cortex-M3 / GCC
set(THREADX_ARCH cortex_m3)
# 本板无 USB（CubeMX 未启用），关闭 BSP USB 外设，DWC2 端口自动联动关闭
set(BSP_USB_ENABLE OFF)
set(BSP_SPI_ENABLE OFF)
set(BSP_CAN_ENABLE OFF)
set(BSP_PWM_ENABLE OFF)
set(BSP_BEEP_ENABLE OFF)
# 公共构建逻辑
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/board_common.cmake)

# Board-local display runtime. The Skill installation directory is not used
# as a compiler input; this repository owns and may edit the copied sources.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../third_party
                 ${CMAKE_CURRENT_BINARY_DIR}/third_party)
target_link_libraries(app PUBLIC kk_ui)
target_link_libraries(${CMAKE_PROJECT_NAME} kk_oled kk_ui)

# 板级专属 sources / includes / defines / libraries 在此追加
# target_sources(${CMAKE_PROJECT_NAME} PRIVATE ...)
