add_library(kk_oled STATIC
    kk_oled/driver/kk_oled_driver.c
    kk_oled/graphics/kk_oled.c
    kk_oled/graphics/kk_oled_bitmap.c
    kk_oled/graphics/kk_oled_draw.c
    kk_oled/graphics/kk_oled_font.c
)

target_include_directories(kk_oled PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/kk_oled/include
    ${CMAKE_CURRENT_SOURCE_DIR}/kk_oled/driver
    ${CMAKE_CURRENT_SOURCE_DIR}/kk_oled/graphics
)

target_link_libraries(kk_oled PUBLIC stm32cubemx)
