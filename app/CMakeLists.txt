cmake_minimum_required(VERSION 3.16)
project(demucs_mlx_app LANGUAGES C)
enable_testing()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_definitions(-DPLATFORM_DESKTOP -DGRAPHICS_API_OPENGL_33)

set(APP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(RS_PSARC_DIR "${APP_ROOT}/external/rocksmith-psarc")
set(RAYLIB_DIR "${APP_ROOT}/external/raylib-master")
set(TINYFD_DIR "${APP_ROOT}/external/tinyfiledialogs")

if(NOT EXISTS "${RS_PSARC_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "Missing rocksmith-psarc at ${RS_PSARC_DIR}")
endif()
if(NOT EXISTS "${RAYLIB_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "Missing raylib at ${RAYLIB_DIR}. Symlink rocknroller-gui/external/raylib-master")
endif()
if(NOT EXISTS "${TINYFD_DIR}/tinyfiledialogs.c")
  message(FATAL_ERROR "Missing tinyfiledialogs at ${TINYFD_DIR}")
endif()

find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

add_subdirectory("${RS_PSARC_DIR}" "${CMAKE_BINARY_DIR}/rocksmith-psarc")

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
add_subdirectory("${RAYLIB_DIR}" "${CMAKE_BINARY_DIR}/raylib-build")

add_executable(demucs_mlx_app
  src/main.c
  src/app_state.c
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
  "${RS_PSARC_DIR}/include"
)

target_link_libraries(demucs_mlx_app PRIVATE
  raylib
  rocksmith_psarc
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

# Dev convenience: copy models + branding resources next to binary
add_custom_command(TARGET demucs_mlx_app POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:demucs_mlx_app>/models"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${APP_ROOT}/models" "$<TARGET_FILE_DIR:demucs_mlx_app>/models"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:demucs_mlx_app>/resources"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${REPO_RESOURCES_DIR}" "$<TARGET_FILE_DIR:demucs_mlx_app>/resources"
  COMMENT "Copying bundled MLX models and branding resources next to binary"
)
