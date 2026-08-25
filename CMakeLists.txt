cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)
project(SeowolYTMP3Downloader LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Ship a self-contained MSVC runtime so the EXE does not require
# VCRUNTIME140.dll / the Visual C++ Redistributable on the target PC.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(SeowolYTMP3Downloader WIN32
    src/command_line.c
    src/filename.c
    src/main.c
)

target_compile_definitions(SeowolYTMP3Downloader PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    target_compile_options(SeowolYTMP3Downloader PRIVATE /W4 /utf-8)
else()
    target_compile_options(SeowolYTMP3Downloader PRIVATE -Wall -Wextra)
endif()

target_link_libraries(SeowolYTMP3Downloader PRIVATE
    comctl32
    comdlg32
    shell32
    ole32
    uxtheme
)
