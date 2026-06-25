idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        espressif__arduino-esp32
        esp_driver_i2s
        GxEPD2
)
