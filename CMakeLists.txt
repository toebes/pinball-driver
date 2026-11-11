cmake_minimum_required(VERSION 3.30)
project(PinballDriver LANGUAGES C ASM)
option(PINBALL_ENABLE_HALL_ADC "Automatically sample Hall sensors for ball detection" ON)
set(PUYA_SDK_ROOT "C:/Tools/Puya/PY32F002B_Firmware" CACHE PATH "Official PY32F002B firmware library")
set(template "${PUYA_SDK_ROOT}/Templates/PY32F002Bxx_Templates")
set(startup "${template}/EIDE/startup_py32f002bxx.s")
set(linker "${template}/EIDE/py32f002bx5.ld")
set(system_source "${template}/Src/system_py32f002b.c")
foreach(required IN ITEMS "${startup}" "${linker}" "${system_source}")
  if(NOT EXISTS "${required}")
    message(FATAL_ERROR "SDK file missing: ${required}. Set PUYA_SDK_ROOT to the extracted official SDK.")
  endif()
endforeach()

# 24 KiB flash: 4 KiB resident, 8 KiB application, 8 KiB staging, 4 KiB metadata.
set(app_base 0x08001000)
set(app_end 0x08003000)
set(app_vector_offset 0x1000)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${linker}" "${system_source}")
file(READ "${linker}" vendor_linker)
set(flash_region "FLASH \\(rx\\)[ \t]*:[ \t]*ORIGIN[ \t]*=[ \t]*0x0?8000000,[ \t]*LENGTH[ \t]*=[ \t]*24K")
if(NOT vendor_linker MATCHES "${flash_region}")
  message(FATAL_ERROR "Expected PY32F002B flash region missing from ${linker}")
endif()
string(REGEX REPLACE "${flash_region}" "FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 4K" boot_linker "${vendor_linker}")
string(REGEX REPLACE "${flash_region}" "FLASH (rx) : ORIGIN = ${app_base}, LENGTH = 8K" app_linker "${vendor_linker}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/py32-boot.ld" CONTENT "${boot_linker}" @ONLY)
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/py32-app.ld" CONTENT "${app_linker}" @ONLY)
file(READ "${system_source}" vendor_system)
set(vector_define "#define VECT_TAB_OFFSET  0x00")
string(FIND "${vendor_system}" "${vector_define}" vector_define_index)
if(vector_define_index EQUAL -1)
  message(FATAL_ERROR "Expected vector offset definition missing from ${system_source}")
endif()
string(REPLACE "${vector_define}" "#define VECT_TAB_OFFSET  ${app_vector_offset}" app_system "${vendor_system}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/system_py32_app.c" CONTENT "${app_system}" @ONLY)

set_source_files_properties("${startup}" PROPERTIES COMPILE_FLAGS "-x assembler-with-cpp")
function(configure_py32 target script)
  set_target_properties(${target} PROPERTIES SUFFIX ".elf" C_STANDARD 11 C_STANDARD_REQUIRED YES)
  target_compile_definitions(${target} PRIVATE PY32F002Bx5 PINBALL_ENABLE_HALL_ADC=$<BOOL:${PINBALL_ENABLE_HALL_ADC}>)
  target_include_directories(${target} SYSTEM PRIVATE
    "${PUYA_SDK_ROOT}/Drivers/CMSIS/Include"
    "${PUYA_SDK_ROOT}/Drivers/CMSIS/Device/PY32F0xx/Include"
    "${PUYA_SDK_ROOT}/Drivers/PY32F002B_HAL_Driver/Inc")
  target_compile_options(${target} PRIVATE -mcpu=cortex-m0plus -mthumb -Os -g3 -Wall -Wextra -ffunction-sections -fdata-sections)
  target_link_options(${target} PRIVATE -mcpu=cortex-m0plus -mthumb --specs=nano.specs --specs=nosys.specs
    "-T${script}" -Wl,--gc-sections "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${target}.map")
  set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${script}")
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -O ihex "$<TARGET_FILE:${target}>" "${CMAKE_CURRENT_BINARY_DIR}/${target}.hex"
    COMMAND "${CMAKE_OBJCOPY}" -O binary "$<TARGET_FILE:${target}>" "${CMAKE_CURRENT_BINARY_DIR}/${target}.bin"
    COMMAND "${CMAKE_SIZE}" "$<TARGET_FILE:${target}>" VERBATIM)
endfunction()

set(PINBALL_VERSION_MAJOR 0 CACHE STRING "Firmware major version")
set(PINBALL_VERSION_MINOR 4 CACHE STRING "Firmware minor version")
set(flash_sources firmware/py32/src/update_flash.c
    "${PUYA_SDK_ROOT}/Drivers/PY32F002B_HAL_Driver/Src/py32f002b_hal_flash.c")
function(enable_flash target)
  target_sources(${target} PRIVATE ${flash_sources})
  target_include_directories(${target} SYSTEM PRIVATE "${template}/Inc")
endfunction()
set(app_sources firmware/py32/src/main.c firmware/py32/src/sk6812.S)
add_executable(py32_smoke ${app_sources} "${startup}" "${system_source}")
configure_py32(py32_smoke "${linker}")

add_executable(py32_bootloader firmware/py32/bootloader/main.c "${startup}" "${system_source}")
configure_py32(py32_bootloader "${CMAKE_CURRENT_BINARY_DIR}/py32-boot.ld")
target_link_options(py32_bootloader PRIVATE
  "-Wl,--defsym=__pinball_app_start=${app_base}" "-Wl,--defsym=__pinball_app_end=${app_end}")

add_executable(py32_app ${app_sources} "${startup}" "${CMAKE_CURRENT_BINARY_DIR}/system_py32_app.c")
configure_py32(py32_app "${CMAKE_CURRENT_BINARY_DIR}/py32-app.ld")

enable_flash(py32_bootloader)
enable_flash(py32_app)
target_compile_definitions(py32_app PRIVATE PINBALL_UPDATE_ENABLED=1
    PINBALL_VERSION_MAJOR=${PINBALL_VERSION_MAJOR} PINBALL_VERSION_MINOR=${PINBALL_VERSION_MINOR})
