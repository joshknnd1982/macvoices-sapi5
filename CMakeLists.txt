cmake_minimum_required(VERSION 3.20)
project(ClassicMacVoicesSAPI VERSION 1.0.0 LANGUAGES CXX)

# The engine host (panthera_host.exe) is a prebuilt 32-bit process that both SAPI DLLs
# drive over pipes, so the two architectures build the same sources: configure once with
# -A Win32 and once with -A x64 into separate build directories.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(CMV_X64 TRUE)
    message(STATUS "Classic Mac Voices SAPI5: building x64")
else()
    set(CMV_X64 FALSE)
    message(STATUS "Classic Mac Voices SAPI5: building x86")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set(CMV_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# --------------------------------------------------------------------------------------
# Shared by every binary: logging, path discovery, the voice catalogue, settings, the
# text pipeline and the host protocol client.
# --------------------------------------------------------------------------------------
add_library(cmv_common STATIC
    ${CMV_SRC}/cmv_com.cpp
    ${CMV_SRC}/cmv_host.cpp
    ${CMV_SRC}/cmv_log.cpp
    ${CMV_SRC}/cmv_paths.cpp
    ${CMV_SRC}/cmv_registry.cpp
    ${CMV_SRC}/cmv_settings.cpp
    ${CMV_SRC}/cmv_textproc.cpp
    ${CMV_SRC}/cmv_voices.cpp
)
target_include_directories(cmv_common PUBLIC ${CMV_SRC})
target_link_libraries(cmv_common PUBLIC ole32 oleaut32 uuid advapi32 shell32 user32)

set(CMV_TARGETS cmv_common)

# --------------------------------------------------------------------------------------
# The SAPI 5 engine DLL, identical sources for both architectures.
# --------------------------------------------------------------------------------------
add_library(ClassicMacSAPI5 SHARED
    ${CMV_SRC}/cmv_main.cpp
    ${CMV_SRC}/cmv_datakey.cpp
    ${CMV_SRC}/cmv_token.cpp
    ${CMV_SRC}/cmv_enum_tokens.cpp
    ${CMV_SRC}/cmv_engine.cpp
    ${CMV_SRC}/classicmac_sapi.def
)
target_link_libraries(ClassicMacSAPI5 PRIVATE cmv_common sapi)
list(APPEND CMV_TARGETS ClassicMacSAPI5)

# --------------------------------------------------------------------------------------
# Renders every registered Classic Mac voice to WAV through the full SAPI stack: the
# x86 build exercises the 32-bit DLL, the x64 build the 64-bit one.
# --------------------------------------------------------------------------------------
add_executable(cmv_sapitest
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/cmv_sapitest.cpp
)
target_link_libraries(cmv_sapitest PRIVATE sapi ole32 oleaut32 uuid)
list(APPEND CMV_TARGETS cmv_sapitest)

# Drives the DLL through its class objects without registering it, so the whole engine
# can be tested without administrator rights.
add_executable(cmv_directtest
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/cmv_directtest.cpp
)
target_link_libraries(cmv_directtest PRIVATE sapi ole32 oleaut32 uuid)
list(APPEND CMV_TARGETS cmv_directtest)

# Checks the C++ text pipeline against outputs recorded from the Python originals.
add_executable(cmv_textproc_test
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/cmv_textproc_test.cpp
)
target_link_libraries(cmv_textproc_test PRIVATE cmv_common)
list(APPEND CMV_TARGETS cmv_textproc_test)

# Speaks out loud through the registered SAPI 5 stack - the post-install check.
add_executable(cmv_speak
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/cmv_speak.cpp
)
target_link_libraries(cmv_speak PRIVATE sapi ole32 oleaut32 uuid)
list(APPEND CMV_TARGETS cmv_speak)

# --------------------------------------------------------------------------------------
# The configuration utility. One build is enough - the x86 binary runs everywhere - but
# building it in both trees costs nothing; the installer takes the x86 one.
# --------------------------------------------------------------------------------------
add_executable(ClassicMacConfig WIN32
    ${CMV_SRC}/config/cmv_config.cpp
    ${CMV_SRC}/config/cmv_config.rc
)
target_include_directories(ClassicMacConfig PRIVATE ${CMV_SRC}/config)
target_link_libraries(ClassicMacConfig PRIVATE cmv_common sapi comctl32)
list(APPEND CMV_TARGETS ClassicMacConfig)

foreach(target ${CMV_TARGETS})
    target_compile_definitions(${target} PRIVATE
        UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN
    )
    if(MSVC)
        target_compile_options(${target} PRIVATE /W3 /MP /utf-8)
        set_property(TARGET ${target} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endforeach()
