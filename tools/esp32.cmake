cmake_minimum_required(VERSION 3.30)

# Run with cmake [-DPORT=COM4] [-DPY32_BUILD=...] -P tools/esp32.cmake.
get_filename_component(repo "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(local "${repo}/.local/esp32")
if(NOT DEFINED DRIVE)
  set(DRIVE "X:")
endif()
if(NOT DRIVE MATCHES "^[A-Za-z]:$")
  message(FATAL_ERROR "DRIVE must be a Windows drive letter such as X:.")
endif()
if(DEFINED PORT AND NOT PORT MATCHES "^[Cc][Oo][Mm][1-9][0-9]*$")
  message(FATAL_ERROR "Set PORT to the ESP32 serial port, such as COM4.")
endif()
if(DEFINED PY32_BUILD)
  get_filename_component(PY32_BUILD "${PY32_BUILD}" ABSOLUTE)
endif()
if(NOT EXISTS "${local}/cli/arduino-cli.exe")
  message(FATAL_ERROR "Arduino CLI is missing from ${local}/cli. See docs/esp32-build.md.")
endif()

function(build_esp32 result)
  set(cli "${DRIVE}/cli/arduino-cli.exe")
  set(ENV{ARDUINO_DIRECTORIES_DATA} "${DRIVE}/data")
  set(demo "${DRIVE}/7.0inch_ESP32-8048S070/1-Demo/Demo_Arduino")
  set(build "${DRIVE}/build-manual")
  set(board "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=huge_app")
  set(prepare_args -DemoArduinoRoot "${demo}")
  if(DEFINED PY32_BUILD)
    list(APPEND prepare_args -Py32Build "${PY32_BUILD}")
  endif()
  execute_process(
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
      -File "${CMAKE_CURRENT_LIST_DIR}/prepare-esp32-demo.ps1" ${prepare_args}
    OUTPUT_VARIABLE sketch OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE status)
  if(status STREQUAL "0")
    execute_process(
      COMMAND "${cli}" compile --fqbn "${board}" --libraries "${demo}/Libraries"
        --build-path "${build}" "${sketch}"
      RESULT_VARIABLE status)
  endif()
  if(status STREQUAL "0" AND DEFINED PORT)
    execute_process(
      COMMAND "${cli}" upload --fqbn "${board}" --port "${PORT}" --input-dir "${build}" "${sketch}"
      RESULT_VARIABLE status)
  endif()
  set(${result} "${status}" PARENT_SCOPE)
endfunction()

execute_process(COMMAND subst.exe "${DRIVE}" "${local}" RESULT_VARIABLE mapped)
if(NOT mapped STREQUAL "0")
  message(FATAL_ERROR "Could not map ${DRIVE}. Pass -DDRIVE= followed by an unused drive letter and colon.")
endif()
build_esp32(result)
execute_process(COMMAND subst.exe "${DRIVE}" /D RESULT_VARIABLE unmapped)
if(NOT unmapped STREQUAL "0")
  message(WARNING "Could not remove the temporary mapping for ${DRIVE}.")
endif()
if(NOT result STREQUAL "0")
  message(FATAL_ERROR "ESP32 workflow failed (${result}). See the tool output above.")
endif()
