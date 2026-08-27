cmake_minimum_required(VERSION 3.16)
project(rocksmith_psarc LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Emscripten (wasm): zlib/ogg/vorbis come from Emscripten ports built into the toolchain
# sysroot (one-time: embuilder build zlib ogg vorbis). The vorbis port bundles vorbisfile
# symbols inside libvorbis.a, so both imported targets point at the same archive.
if(EMSCRIPTEN)
  execute_process(
    COMMAND "${EMSCRIPTEN_ROOT_PATH}/em-config" CACHE
    OUTPUT_VARIABLE RS_EM_CACHE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE RS_EM_CACHE_RC
  )
  if(NOT RS_EM_CACHE_RC EQUAL 0 OR RS_EM_CACHE STREQUAL "")
    message(FATAL_ERROR "em-config CACHE failed; cannot locate Emscripten sysroot")
  endif()
  set(RS_EM_SYSROOT "${RS_EM_CACHE}/sysroot")
  set(RS_EM_LIBDIR "${RS_EM_SYSROOT}/lib/wasm32-emscripten")
  foreach(_lib z ogg vorbis)
    if(NOT EXISTS "${RS_EM_LIBDIR}/lib${_lib}.a")
      message(FATAL_ERROR "Missing Emscripten port lib${_lib}.a in ${RS_EM_LIBDIR}. Run: embuilder build zlib ogg vorbis")
    endif()
  endforeach()

  # Preseed FindZLIB / vgmstream's FindOgg / FindVorbis / FindVorbisFile caches so no
  # host libraries are searched and vgmstream does not FetchDependency from git.
  set(ZLIB_INCLUDE_DIR "${RS_EM_SYSROOT}/include" CACHE PATH "" FORCE)
  set(ZLIB_LIBRARY "${RS_EM_LIBDIR}/libz.a" CACHE FILEPATH "" FORCE)
  set(OGG_INCLUDE_DIR "${RS_EM_SYSROOT}/include" CACHE PATH "" FORCE)
  set(OGG_LIBRARY "${RS_EM_LIBDIR}/libogg.a" CACHE FILEPATH "" FORCE)
  set(VORBIS_INCLUDE_DIR "${RS_EM_SYSROOT}/include" CACHE PATH "" FORCE)
  set(VORBIS_LIBRARY "${RS_EM_LIBDIR}/libvorbis.a" CACHE FILEPATH "" FORCE)
  set(VORBISFILE_INCLUDE_DIR "${RS_EM_SYSROOT}/include" CACHE PATH "" FORCE)
  set(VORBISFILE_LIBRARY "${RS_EM_LIBDIR}/libvorbis.a" CACHE FILEPATH "" FORCE)

  # vgmstream's setup_target links bare names (vorbisfile vorbis ogg); define them as
  # GLOBAL imported targets so CMake resolves them instead of emitting -lvorbisfile.
  add_library(ogg STATIC IMPORTED GLOBAL)
  set_target_properties(ogg PROPERTIES IMPORTED_LOCATION "${RS_EM_LIBDIR}/libogg.a")
  add_library(vorbis STATIC IMPORTED GLOBAL)
  set_target_properties(vorbis PROPERTIES IMPORTED_LOCATION "${RS_EM_LIBDIR}/libvorbis.a")
  add_library(vorbisfile STATIC IMPORTED GLOBAL)
  set_target_properties(vorbisfile PROPERTIES IMPORTED_LOCATION "${RS_EM_LIBDIR}/libvorbis.a")

  # Node test harness link flags — only when rocksmith-psarc is the top-level project
  # (the browser GUI build adds its own link options for its own executables).
  if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_link_options(-sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -sSTACK_SIZE=1048576
                     "SHELL:--pre-js ${CMAKE_CURRENT_SOURCE_DIR}/wasm/node_env_pre.js")
  endif()
endif()

find_package(ZLIB REQUIRED)

# vgmstream: decodes Wwise .wem (needs libvorbis; FFmpeg disabled)
set(RS_VGM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/vgmstream")
if(NOT EXISTS "${RS_VGM_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "Missing vgmstream at ${RS_VGM_DIR}. Clone: git clone --depth 1 https://github.com/vgmstream/vgmstream.git ${RS_VGM_DIR}")
endif()
set(USE_FFMPEG OFF CACHE BOOL "rocksmith-psarc: vgmstream without ffmpeg" FORCE)
set(USE_MPEG OFF CACHE BOOL "" FORCE)
set(USE_G719 OFF CACHE BOOL "" FORCE)
set(USE_ATRAC9 OFF CACHE BOOL "" FORCE)
set(USE_CELT OFF CACHE BOOL "" FORCE)
set(USE_SPEEX OFF CACHE BOOL "" FORCE)
set(BUILD_CLI OFF CACHE BOOL "" FORCE)
set(BUILD_V123 OFF CACHE BOOL "" FORCE)
set(BUILD_AUDACIOUS OFF CACHE BOOL "" FORCE)
# Windows MSVC defaults BUILD_FB2K ON and requires FB2K_SDK_PATH / WTL; we only need libvgmstream.
if(MSVC)
  set(BUILD_FB2K OFF CACHE BOOL "rocksmith-psarc: vgmstream without foobar2000 component" FORCE)
endif()
if(WIN32)
  set(BUILD_WINAMP OFF CACHE BOOL "rocksmith-psarc: vgmstream without Winamp plugin" FORCE)
  set(BUILD_XMPLAY OFF CACHE BOOL "rocksmith-psarc: vgmstream without XMPlay plugin" FORCE)
endif()
# Keep vgmstream artifacts outside $(BUILD_DIR) so `make clean` (rm -rf build/) does not force a full vgmstream rebuild.
# The wasm build uses its own vgmstream object tree (different compiler/ABI than native).
if(EMSCRIPTEN)
  set(RS_VGM_BINARY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.vgmstream-cmake-build-wasm" CACHE PATH "vgmstream CMake binary dir (delete with: make clean-vgmstream)")
else()
  set(RS_VGM_BINARY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.vgmstream-cmake-build" CACHE PATH "vgmstream CMake binary dir (delete with: make clean-vgmstream)")
endif()
add_subdirectory("${RS_VGM_DIR}" "${RS_VGM_BINARY_DIR}")
if(MSVC AND TARGET libvgmstream)
  # Third-party; thousands of C4267 size_t warnings drown out rocksmith/rocknroller diagnostics in CI.
  target_compile_options(libvgmstream PRIVATE /w)
endif()

add_library(rocksmith_psarc STATIC
  third_party/tiny-aes/aes.c
  src/aes_ctr.c
  src/psarc_crypto.c
  src/psarc.c
  src/rs_temp_file.c
  src/rs_audio_wem.c
  "${RS_VGM_DIR}/cli/wav_utils.c"
  src/sng_unpack.c
  src/sng_read.c
  src/chart.c
)

target_include_directories(rocksmith_psarc PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(rocksmith_psarc PRIVATE
  "${RS_VGM_DIR}/src"
  "${RS_VGM_DIR}/cli"
  "${RS_VGM_DIR}/ext_includes"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tiny-aes"
)

# tiny-AES-c: 256-bit key + ECB block primitive only (CTR/CBC loops live in src/).
target_compile_definitions(rocksmith_psarc PRIVATE AES256=1 ECB=1 CTR=0 CBC=0)

# vgmstream WEM decode links libvorbis (vcpkg on Windows; Homebrew/paths on Unix; Emscripten ports on wasm).
if(EMSCRIPTEN)
  target_link_libraries(rocksmith_psarc PUBLIC ZLIB::ZLIB libvgmstream vorbisfile vorbis ogg)
else()
  if(WIN32)
    find_library(RS_VORBISFILE_LIB NAMES vorbisfile)
    find_library(RS_VORBIS_LIB NAMES vorbis)
    find_library(RS_OGG_LIB NAMES ogg)
  else()
    find_library(RS_VORBISFILE_LIB NAMES vorbisfile PATHS /opt/homebrew/lib /usr/local/lib /usr/lib)
    find_library(RS_VORBIS_LIB NAMES vorbis PATHS /opt/homebrew/lib /usr/local/lib /usr/lib)
    find_library(RS_OGG_LIB NAMES ogg PATHS /opt/homebrew/lib /usr/local/lib /usr/lib)
  endif()
  if(NOT RS_VORBISFILE_LIB OR NOT RS_VORBIS_LIB OR NOT RS_OGG_LIB)
    message(FATAL_ERROR "libvorbis not found (e.g. brew install libvorbis libogg; Windows: vcpkg install libogg libvorbis)")
  endif()

  target_link_libraries(rocksmith_psarc PUBLIC ZLIB::ZLIB libvgmstream)
  if(WIN32)
    target_link_libraries(rocksmith_psarc PUBLIC ${RS_VORBISFILE_LIB} ${RS_VORBIS_LIB} ${RS_OGG_LIB})
  elseif(UNIX)
    if(NOT APPLE)
      target_link_libraries(rocksmith_psarc PUBLIC ${RS_VORBISFILE_LIB} ${RS_VORBIS_LIB} ${RS_OGG_LIB} m)
    else()
      target_link_libraries(rocksmith_psarc PUBLIC ${RS_VORBISFILE_LIB} ${RS_VORBIS_LIB} ${RS_OGG_LIB})
    endif()
  endif()
endif()

if(MSVC)
  target_compile_definitions(rocksmith_psarc PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(rocksmith_psarc PRIVATE -Wall)
endif()

# Tests are only for a standalone rocksmith-psarc checkout.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  return()
endif()

add_executable(rs_psarc_smoke tests/rs_psarc_smoke.c)
target_link_libraries(rs_psarc_smoke PRIVATE rocksmith_psarc)
if(NOT MSVC)
  target_compile_options(rs_psarc_smoke PRIVATE -Wall)
endif()

add_executable(rs_psarc_test tests/rs_psarc_test.c)
target_link_libraries(rs_psarc_test PRIVATE rocksmith_psarc)
if(NOT MSVC)
  target_compile_options(rs_psarc_test PRIVATE -Wall)
endif()

add_executable(rs_wem_smoke tests/rs_wem_smoke.c)
target_link_libraries(rs_wem_smoke PRIVATE rocksmith_psarc)
if(NOT MSVC)
  target_compile_options(rs_wem_smoke PRIVATE -Wall)
endif()

add_executable(rs_temp_file_test tests/rs_temp_file_test.c)
target_link_libraries(rs_temp_file_test PRIVATE rocksmith_psarc)
if(NOT MSVC)
  target_compile_options(rs_temp_file_test PRIVATE -Wall)
endif()
add_test(NAME rs_temp_file_test COMMAND rs_temp_file_test)

add_executable(rs_psarc_official_test tests/rs_psarc_official_test.c)
target_link_libraries(rs_psarc_official_test PRIVATE rocksmith_psarc)
if(NOT MSVC)
  target_compile_options(rs_psarc_official_test PRIVATE -Wall)
endif()

add_executable(sng_sections_test tests/sng_sections_test.c)
target_link_libraries(sng_sections_test PRIVATE rocksmith_psarc)
target_include_directories(sng_sections_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(NOT MSVC)
  target_compile_options(sng_sections_test PRIVATE -Wall)
endif()
