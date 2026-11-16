cmake_minimum_required(VERSION 3.16)

# -------------------------------
# CMake Setup
# -------------------------------
# Application Version
set(APP_VERSION_MAJOR 0)
set(APP_VERSION_MINOR 0)
set(APP_VERSION_PATCH 0)
set(APP_NAME "zeal-xonix")
set(target_name "zeal-xonix")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/bin)

# Ensure the bin directory exists
file(MAKE_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

# -------------------------------
# Toolchain
# -------------------------------
set(ZOS_TOOLCHAIN sdcc)
include($ENV{ZOS_PATH}/cmake/zos_init.cmake)

# -------------------------------
# Project
# -------------------------------
project(${target_name} C ASM)

# -------------------------------
# Blocking screens and startup code do not need speed-optimized code, so
# compile them for size.  The playfield, game and spider stay optimized for
# speed because they run every frame.
set_source_files_properties(
  src/main.c
  src/splash.c
  src/highscore.c
  src/sound.c
  src/levels.c
  PROPERTIES COMPILE_OPTIONS "--opt-code-size")

# -------------------------------
# Include and link SDKs
# -------------------------------
zos_use(zvb)
find_package(ZVB_SDK REQUIRED)

zos_use(zgdk)
find_package(ZGDK REQUIRED)


# -------------------------------
# Executable
# -------------------------------
add_executable(${target_name}
  src/main.c
  src/field.c
  src/game.c
  src/spider.c
  src/levels.c
  src/splash.c
  src/highscore.c
  src/sound.c
)

# -------------------------------
# Linking/Libraries/Includes
# -------------------------------
# Include ZVB includes, even if not linking to ZVB libs to use zvb_hardware.h
target_include_directories(${target_name} PRIVATE $ENV{ZVB_SDK_PATH}/include)
# Projects /include dir
target_include_directories(${target_name} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include)
target_include_directories(${target_name} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)
# Link to zvb_gfx and zgdk
target_link_libraries(${target_name} PUBLIC zvb_gfx zvb_sound zvb_dma zvb_crc zgdk)


# -------------------------------
# Assets
# -------------------------------
gif2zeal(${target_name} FILES
    "${CMAKE_CURRENT_LIST_DIR}/assets/field.gif"
    BIT 4
    COMPRESSED lz)

embed_assets(${target_name} FILES
    "${CMAKE_CURRENT_LIST_DIR}/assets/field.gif")

# -------------------------------
# Optional splash screen
# -------------------------------
# The splash files are ready-made and loaded at runtime from
# assets/ next to the binary.  Copy them to the output directory when present;
# the game itself still warns and runs without them when they are missing.
set(SPLASH_ASSETS
    "${CMAKE_CURRENT_LIST_DIR}/assets/splash.zts"
    "${CMAKE_CURRENT_LIST_DIR}/assets/splash.ztp"
    "${CMAKE_CURRENT_LIST_DIR}/assets/splash.ztm")
set(SPLASH_EXISTING "")
foreach(splash_file ${SPLASH_ASSETS})
    if(EXISTS "${splash_file}")
        list(APPEND SPLASH_EXISTING "${splash_file}")
    endif()
endforeach()
if(SPLASH_EXISTING)
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${SPLASH_EXISTING}
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets/"
        COMMENT "Copying splash assets next to the binary")
endif()

# -------------------------------
# Convert to binary
# -------------------------------
zos_add_outputs(${target_name})
