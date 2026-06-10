idf_component_register(
    SRCS "buttons.cpp" "serial_cmd.cpp" "lua_api.cpp" "sound.cpp" "subghz.cpp" "mqtt.cpp" "espnow.cpp" "http.cpp" "wifi.cpp" "ui.cpp" "display.cpp" "solana.cpp" "atecc.cpp" "badge_state.cpp" "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES espressif__arduino-esp32 GxEPD2 mqtt json esp_http_client esp-tls esp_wifi esp_driver_i2s lua espressif__libsodium espressif__esp-cryptoauthlib mbedtls
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE BOARD_HAS_PSRAM LUA_32BITS)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=format-truncation)
