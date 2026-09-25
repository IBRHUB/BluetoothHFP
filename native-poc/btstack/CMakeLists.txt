cmake_minimum_required(VERSION 3.20)
project(ax201_headset LANGUAGES C CXX)
include(../cmake/ControllerProfiles.cmake)
if(NOT BTSTACK_ROOT)
  message(FATAL_ERROR "Supply pinned BTSTACK_ROOT using Build-Headset.ps1")
endif()
set(R ${BTSTACK_ROOT})
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Static codec dependency" FORCE)
add_subdirectory(${FDK_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/fdk-aac EXCLUDE_FROM_ALL)
configure_file(${R}/port/windows-winusb-intel/btstack_config.h ${CMAKE_CURRENT_BINARY_DIR}/btstack_config.h COPYONLY)
file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/btstack_config.h "\n#ifndef ENABLE_HFP_SUPER_WIDE_BAND_SPEECH\n#define ENABLE_HFP_SUPER_WIDE_BAND_SPEECH\n#endif\n")
file(GLOB LC3 ${R}/3rd-party/lc3-google/src/*.c)
file(GLOB CORE ${R}/src/*.c ${R}/src/classic/*.c ${R}/src/ble/*.c ${R}/src/ble/gatt-service/*.c)
list(REMOVE_ITEM CORE ${R}/src/ble/le_device_db_memory.c ${R}/src/classic/hfp.c)
file(GLOB WINDOWS ${R}/platform/windows/*.c)
list(REMOVE_ITEM WINDOWS ${R}/platform/windows/hci_transport_h2_winusb.c)
file(GLOB SBC ${R}/3rd-party/bluedroid/encoder/srce/*.c ${R}/3rd-party/bluedroid/decoder/srce/*.c)
file(GLOB MOD ${R}/3rd-party/hxcmod-player/*.c ${R}/3rd-party/hxcmod-player/mods/*.c)
add_library(btstack STATIC ${CORE} ${WINDOWS} ${SBC} ${MOD} ${LC3}
  ${R}/3rd-party/micro-ecc/uECC.c ${R}/3rd-party/md5/md5.c
  ${R}/3rd-party/rijndael/rijndael.c ${R}/3rd-party/yxml/yxml.c
  ${R}/chipset/csr/btstack_chipset_csr.c ${R}/chipset/zephyr/btstack_chipset_zephyr.c
  ${CMAKE_CURRENT_BINARY_DIR}/hci_transport_h2_winusb.c ${CMAKE_CURRENT_BINARY_DIR}/hfp.c)
target_include_directories(btstack PUBLIC ${CMAKE_CURRENT_BINARY_DIR} ${R}/src
  ${R}/platform/windows ${R}/platform/posix ${R}/platform/embedded
  ${R}/chipset/csr ${R}/chipset/zephyr ${R}/3rd-party/micro-ecc
  ${R}/3rd-party/md5 ${R}/3rd-party/rijndael ${R}/3rd-party/yxml
  ${R}/3rd-party/bluedroid/encoder/include ${R}/3rd-party/bluedroid/decoder/include
  ${R}/3rd-party/lc3-google/include ${R}/3rd-party/hxcmod-player)
target_compile_definitions(btstack PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_features(btstack PRIVATE c_std_11)
if(MSVC)
  target_include_directories(btstack PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/msvc)
endif()
add_executable(ax201_headset ../app/headset_main.c ../app/control.cpp ../bluetooth/hfp_hf.c ../bluetooth/hfp_audio_codec.c
  ../bluetooth/a2dp_sink.c ../audio/media_render.cpp
  ../audio/audio_bridge.cpp ../audio/wasapi_endpoint.cpp ../audio/wasapi_capture.cpp ../audio/wasapi_render.cpp)
target_compile_features(ax201_headset PRIVATE cxx_std_17)
target_compile_definitions(ax201_headset PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_include_directories(ax201_headset PRIVATE ..)
target_link_libraries(ax201_headset PRIVATE btstack fdk-aac setupapi winusb ole32 uuid)
add_executable(lc3_quality_tests ../tests/lc3_quality_tests.c)
target_link_libraries(lc3_quality_tests PRIVATE btstack setupapi winusb)
enable_testing()
add_test(NAME lc3_speech_roundtrip COMMAND lc3_quality_tests)
