# Adafruit Bus IO Library
# https://github.com/adafruit/Adafruit_BusIO
# MIT License

cmake_minimum_required(VERSION 3.5)

# REQUIRES the arduino-esp32 managed component (espressif__arduino-esp32).
idf_component_register(SRCS "Adafruit_I2CDevice.cpp" "Adafruit_BusIO_Register.cpp" "Adafruit_SPIDevice.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES espressif__arduino-esp32)
