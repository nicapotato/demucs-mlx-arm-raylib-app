cmake_minimum_required(VERSION 3.16)

# Must be set before project() so the binary's LC_BUILD_VERSION minos matches
# Info.plist LSMinimumSystemVersion (and does not inherit the build machine's OS).
if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL "")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version for DemucsMLX")
endif()

project(demucs_mlx_app LANGUAGES C)
enable_testing()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_definitions(-DPLATFORM_DESKTOP -DGRAPHICS_API_OPENGL_33)

set(APP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(REPO_ROOT "${APP_ROOT}/..")
set(RS_PREBUILT_DIR "${APP_ROOT}/external/rocksmith-psarc-prebuilt")
set(RAYLIB_DIR "${APP_ROOT}/external/raylib-master")
set(TINYFD_DIR "${APP_ROOT}/external/tinyfiledialogs")

# Version from repo-root project.conf (single source of truth for itch / GH releases)
set(DMX_APP_VERSION "0.0.0")
set(DMX_PROJECT_CONF "${REPO_ROOT}/project.conf")
if(EXISTS "${DMX_PROJECT_CONF}")
  file(READ "${DMX_PROJECT_CONF}" _dmx_project_conf)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${DMX_PROJECT_CONF}")
  if(_dmx_project_conf MATCHES "VERSION=([^\r\n]+)")
    set(DMX_APP_VERSION "${CMAKE_MATCH_1}")
  endif()
endif()
message(STATUS "DemucsMLX version: ${DMX_APP_VERSION}")

set(RS_PREBUILT_LIB "${RS_PREBUILT_DIR}/lib/librocksmith_psarc.a")
set(RS_VGMSTREAM_LIB "${RS_PREBUILT_DIR}/lib/libvgmstream.a")
set(RS_PREBUILT_INCLUDE "${RS_PREBUILT_DIR}/include")
if(NOT EXISTS "${RS_PREBUILT_LIB}" OR NOT EXISTS "${RS_VGMSTREAM_LIB}")
  message(FATAL_ERROR
    "Missing prebuilt rocksmith-psarc libs under ${RS_PREBUILT_DIR}/lib.\n"
    "Refresh with: bash app/scripts/refresh_rocksmith_prebuilt.sh")
endif()
if(NOT EXISTS "${RS_PREBUILT_INCLUDE}/rocksmith_psarc.h")
  message(FATAL_ERROR "Missing ${RS_PREBUILT_INCLUDE}/rocksmith_psarc.h")
endif()
if(NOT EXISTS "${RAYLIB_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "Missing raylib at ${RAYLIB_DIR}. Run: bash app/scripts/vendor_raylib.sh")
endif()
if(NOT EXISTS "${TINYFD_DIR}/tinyfiledialogs.c")
  message(FATAL_ERROR "Missing tinyfiledialogs at ${TINYFD_DIR}")
endif()

find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

# Prebuilt macOS arm64 static libs (no rocknroller checkout required).
add_library(rocksmith_psarc STATIC IMPORTED GLOBAL)
set_target_properties(rocksmith_psarc PROPERTIES
  IMPORTED_LOCATION "${RS_PREBUILT_LIB}"
  INTERFACE_INCLUDE_DIRECTORIES "${RS_PREBUILT_INCLUDE}"
)

add_library(libvgmstream STATIC IMPORTED GLOBAL)
set_target_properties(libvgmstream PROPERTIES
  IMPORTED_LOCATION "${RS_VGMSTREAM_LIB}"
)

# Prefer static libs so the shipped .app does not depend on Homebrew dylibs
# at /opt/homebrew/... (itch users do not have those installed).
set(_dmx_find_lib_suffixes "${CMAKE_FIND_LIBRARY_SUFFIXES}")
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
find_library(RS_VORBISFILE_LIB NAMES vorbisfile
  PATHS /opt/homebrew/opt/libvorbis/lib /opt/homebrew/lib /usr/local/lib /usr/lib)
find_library(RS_VORBIS_LIB NAMES vorbis
  PATHS /opt/homebrew/opt/libvorbis/lib /opt/homebrew/lib /usr/local/lib /usr/lib)
find_library(RS_OGG_LIB NAMES ogg
  PATHS /opt/homebrew/opt/libogg/lib /opt/homebrew/lib /usr/local/lib /usr/lib)
set(CMAKE_FIND_LIBRARY_SUFFIXES "${_dmx_find_lib_suffixes}")
unset(_dmx_find_lib_suffixes)
if(NOT RS_VORBISFILE_LIB OR NOT RS_VORBIS_LIB OR NOT RS_OGG_LIB)
  message(FATAL_ERROR "static libvorbis/libogg not found (brew install libvorbis libogg)")
endif()
message(STATUS "libvorbisfile (static): ${RS_VORBISFILE_LIB}")
message(STATUS "libvorbis (static): ${RS_VORBIS_LIB}")
message(STATUS "libogg (static): ${RS_OGG_LIB}")

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
add_subdirectory("${RAYLIB_DIR}" "${CMAKE_BINARY_DIR}/raylib-build")

add_executable(demucs_mlx_app
  src/main.c
  src/app_state.c
  src/prefs.c
  src/job_queue.c
  src/worker_run.c
  src/psarc_input.c
  src/ui.c
  src/platform/threads_pthread.c
  "${TINYFD_DIR}/tinyfiledialogs.c"
)

target_include_directories(demucs_mlx_app PRIVATE
  "${APP_ROOT}/src"
  "${TINYFD_DIR}"
  "${RS_PREBUILT_INCLUDE}"
)

target_compile_definitions(demucs_mlx_app PRIVATE
  "DMX_APP_VERSION=\"${DMX_APP_VERSION}\""
)

# Link order: app glue -> vgmstream -> vorbis/ogg -> zlib
target_link_libraries(demucs_mlx_app PRIVATE
  raylib
  rocksmith_psarc
  libvgmstream
  ${RS_VORBISFILE_LIB}
  ${RS_VORBIS_LIB}
  ${RS_OGG_LIB}
  ZLIB::ZLIB
  Threads::Threads
)

if(APPLE)
  target_link_libraries(demucs_mlx_app PRIVATE
    "-framework Cocoa"
    "-framework IOKit"
    "-framework OpenGL"
    "-framework CoreAudio"
    "-framework AudioToolbox"
    "-framework CoreVideo"
  )
endif()

if(NOT MSVC)
  target_compile_options(demucs_mlx_app PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

set(REPO_RESOURCES_DIR "${APP_ROOT}/../resources")

# Dev convenience: copy models + branding resources next to binary.
# app/models/ may be empty in CI (*.pkl gitignored); create it so copy_directory succeeds.
add_custom_command(TARGET demucs_mlx_app POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_ROOT}/models"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:demucs_mlx_app>/models"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${APP_ROOT}/models" "$<TARGET_FILE_DIR:demucs_mlx_app>/models"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:demucs_mlx_app>/resources"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${REPO_RESOURCES_DIR}" "$<TARGET_FILE_DIR:demucs_mlx_app>/resources"
  COMMENT "Copying bundled MLX models and branding resources next to binary"
)
