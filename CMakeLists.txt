cmake_minimum_required(VERSION 3.20)
project(YouTubeMP3Downloader LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(YouTubeMP3Downloader WIN32
    src/main.c
)

target_compile_definitions(YouTubeMP3Downloader PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    target_compile_options(YouTubeMP3Downloader PRIVATE /W4 /utf-8)
else()
    target_compile_options(YouTubeMP3Downloader PRIVATE -Wall -Wextra)
endif()

target_link_libraries(YouTubeMP3Downloader PRIVATE
    comctl32
    comdlg32
    shell32
    ole32
    uxtheme
)
