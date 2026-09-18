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

add_library(kk_ui STATIC
    kk_ui/src/kk_ui.c
    kk_ui/src/kk_ui_anim.c
    kk_ui/src/kk_ui_dialog.c
    kk_ui/src/kk_ui_draw.c
    kk_ui/src/kk_ui_home.c
    kk_ui/src/kk_ui_menu.c
    kk_ui/src/kk_ui_nav.c
    kk_ui/src/kk_ui_page.c
)

target_include_directories(kk_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/kk_ui/include
)

target_compile_definitions(kk_ui PUBLIC
    KK_UI_REFRESH_MODE=KK_UI_REFRESH_BLOCKING
    KK_UI_FRAME_INTERVAL_MS=100U
    KK_UI_ENABLE_HOME=0
    KK_UI_ENABLE_MENU=0
    KK_UI_ENABLE_INFO=0
    KK_UI_ENABLE_CUSTOM=1
    KK_UI_ENABLE_INT_EDITOR=0
    KK_UI_ENABLE_BOOL_EDITOR=0
    KK_UI_ENABLE_CONFIRM=0
    KK_UI_ENABLE_MESSAGE=0
    KK_UI_ENABLE_TOAST=0
)

target_link_libraries(kk_ui PUBLIC kk_oled)
