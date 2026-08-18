cmake_minimum_required(VERSION 3.22.1)
project("shenzhou_player")

add_library(main SHARED
    main.c
    lvm.c
)

find_library(log-lib log)
find_library(android-lib android)
find_library(GLESv2-lib GLESv2)

target_link_libraries(main
    ${log-lib}
    ${android-lib}
    ${GLESv2-lib}
)
