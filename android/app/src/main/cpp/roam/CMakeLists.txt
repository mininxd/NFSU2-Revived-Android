cmake_minimum_required(VERSION 3.22)
project(nfsu2_roam C)

add_library(nfsu2_roam SHARED
    roam_core.c
    roam_game.c
    roam_android.c
)

target_include_directories(nfsu2_roam PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(nfsu2_roam PRIVATE
    ROAM_HOST_ANDROID
    ROAM_HOST_WIN=1
)

target_compile_options(nfsu2_roam PRIVATE
    -std=c11
    -O3
    -fomit-frame-pointer
    -ffp-contract=off
    -fno-strict-aliasing
    -Wall
    -Wextra
    -Wno-unused-parameter
)

target_link_libraries(nfsu2_roam PRIVATE
    log
    dl
)
