cmake_minimum_required(VERSION 3.5)

if(DEFINED ENV{RMAKER_PATH})
  set(RMAKER_PATH $ENV{RMAKER_PATH})
else()
  set(RMAKER_PATH /opt/espressif/esp-rainmaker)
endif()

set(PROJECT_VER "1.0")
set(PROJECT_VER_NUMBER 1)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS
    "${RMAKER_PATH}/components"
    "${RMAKER_PATH}/examples/common/rmaker_app_network")

project(esp_rainmaker_gateway)

idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)
