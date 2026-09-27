cmake_minimum_required(VERSION 3.24)
project(ZuxiaIME VERSION 0.1.0 LANGUAGES CXX RC)

if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "ZuxiaNative currently requires Windows and MSVC.")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(ZUXIA_ARCH x64)
else()
  set(ZUXIA_ARCH x86)
endif()

set(ZUXIA_LIBRIME_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/librime/${ZUXIA_ARCH}/dist"
    CACHE PATH "Extracted librime SDK root for the active architecture")
set(RIME_INCLUDE_DIR "${ZUXIA_LIBRIME_ROOT}/include")
set(RIME_RUNTIME "${ZUXIA_LIBRIME_ROOT}/lib/rime.dll")

foreach(required IN ITEMS "${RIME_INCLUDE_DIR}/rime_api.h"
                          "${RIME_RUNTIME}")
  if(NOT EXISTS "${required}")
    message(FATAL_ERROR
      "Missing librime dependency: ${required}. Run scripts/fetch-librime.ps1.")
  endif()
endforeach()

set(ZUXIA_SOURCES
  src/CandidateWindow.cpp
  src/Compartment.cpp
  src/Composition.cpp
  src/Diagnostics.cpp
  src/DllMain.cpp
  src/EndComposition.cpp
  src/Globals.cpp
  src/InputMode.cpp
  src/KeyEventSink.cpp
  src/KeyHandler.cpp
  src/Register.cpp
  src/RimeEngine.cpp
  src/Server.cpp
  src/TextEditSink.cpp
  src/TextService.cpp
  src/ThreadMgrEventSink.cpp
  src/Utf.cpp
  src/ZuxiaTSF.rc
  src/ZuxiaTSF.def)

add_library(ZuxiaTSF SHARED ${ZUXIA_SOURCES})
target_compile_features(ZuxiaTSF PRIVATE cxx_std_17)
target_include_directories(ZuxiaTSF PRIVATE src "${RIME_INCLUDE_DIR}")
target_compile_definitions(ZuxiaTSF PRIVATE
  UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
  WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_compile_options(ZuxiaTSF PRIVATE
  /utf-8 /W4 /EHsc /permissive- /Zc:__cplusplus)
target_link_libraries(ZuxiaTSF PRIVATE
  ole32 oleaut32 uuid advapi32 shell32 user32 gdi32)
# Reproducible build: without this the PE header carries the build
# time, so the same source yields a different binary every time and
# nobody can verify a released DLL against this tree.
target_link_options(ZuxiaTSF PRIVATE /Brepro)
set_target_properties(ZuxiaTSF PROPERTIES
  OUTPUT_NAME "ZuxiaTSF"
  PREFIX ""
  # Static CRT keeps the installer independent of a separate VC redist.
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_custom_command(TARGET ZuxiaTSF POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${RIME_RUNTIME}" "$<TARGET_FILE_DIR:ZuxiaTSF>/rime.dll"
  COMMENT "Copying librime runtime")

install(TARGETS ZuxiaTSF RUNTIME DESTINATION "${ZUXIA_ARCH}")
install(FILES "${RIME_RUNTIME}" DESTINATION "${ZUXIA_ARCH}")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/" DESTINATION data
        FILES_MATCHING PATTERN "*.yaml")
install(FILES README.md LICENSE-CODE.txt THIRD_PARTY_NOTICES.md
        DESTINATION docs)
# Nothing else from docs/ ships. Two reasons: the maintainer's notes there
# are named in Chinese, and makecab parses the cabinet directive file in
# the system ANSI code page -- a non-ASCII member name reaches it as "????"
# and aborts the build (make-setup.ps1 now guards against that). Keeping
# payload names ASCII also keeps the build identical on a machine with a
# different code page. User-facing docs are the three files installed
# above: README.md, LICENSE-CODE.txt, THIRD_PARTY_NOTICES.md.
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/licenses/"
        DESTINATION docs/licenses)
