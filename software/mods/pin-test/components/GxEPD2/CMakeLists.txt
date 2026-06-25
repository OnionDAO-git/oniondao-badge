# GxEPD2 — https://github.com/ZinggJM/GxEPD2 (v1.5.9, vendored)
# Wrapped as an ESP-IDF component on top of arduino-esp32 + Adafruit_GFX.
# All display drivers under src/ are globbed (mirrors the Arduino build, which
# compiles the whole library); the GxEPD2_BW/3C templates select the one in use.
file(GLOB_RECURSE GXEPD2_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

idf_component_register(
    SRCS ${GXEPD2_SRCS}
    INCLUDE_DIRS "src"
    REQUIRES espressif__arduino-esp32 Adafruit_GFX
)
