cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)
cmake_policy(SET CMP0069 NEW)
project(SeowolYTMP3Downloader LANGUAGES C RC)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

file(STRINGS "${CMAKE_SOURCE_DIR}/VERSION" APP_VERSION LIMIT_COUNT 1)
string(STRIP "${APP_VERSION}" APP_VERSION)
if(NOT APP_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    message(FATAL_ERROR "VERSION must use the x.y.z format")
endif()
set(APP_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(APP_VERSION_MINOR "${CMAKE_MATCH_2}")
set(APP_VERSION_PATCH "${CMAKE_MATCH_3}")
configure_file(src/version.h.in generated/version.h @ONLY)

# Ship a self-contained MSVC runtime so the EXE does not require
# VCRUNTIME140.dll / the Visual C++ Redistributable on the target PC.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(SeowolYTMP3Downloader WIN32
    src/app.rc
    src/command_line.c
    src/filename.c
    src/main.c
)

target_include_directories(SeowolYTMP3Downloader PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR LANGUAGES C)
if(IPO_SUPPORTED)
    set_property(TARGET SeowolYTMP3Downloader PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
else()
    message(WARNING "Interprocedural optimization is unavailable: ${IPO_ERROR}")
endif()

target_compile_definitions(SeowolYTMP3Downloader PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    target_compile_options(SeowolYTMP3Downloader PRIVATE
        $<$<COMPILE_LANGUAGE:C>:/W4>
        $<$<COMPILE_LANGUAGE:C>:/utf-8>
        $<$<COMPILE_LANGUAGE:C>:/O2>
        $<$<COMPILE_LANGUAGE:C>:/Gy>
        $<$<COMPILE_LANGUAGE:RC>:/nologo>
    )
    target_link_options(SeowolYTMP3Downloader PRIVATE
        $<$<CONFIG:Release>:/OPT:REF /OPT:ICF /INCREMENTAL:NO>
    )
else()
    target_compile_options(SeowolYTMP3Downloader PRIVATE -Wall -Wextra)
endif()

target_link_libraries(SeowolYTMP3Downloader PRIVATE
    comctl32
    comdlg32
    shell32
    ole32
    uxtheme
    winhttp
    advapi32
)
