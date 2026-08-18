cmake_minimum_required(VERSION 3.22.1)
project("shenzhou_player")

include(FetchContent)

# 云端自动下载官方 SDL2 release-2.28.5 源码并编译
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-2.28.5
)
FetchContent_MakeAvailable(SDL2)

# 编译你的神舟游戏包装层
add_library(main SHARED
    main.c
    lvm.c
)

find_library(log-lib log)
find_library(android-lib android)
find_library(GLESv2-lib GLESv2)

target_link_libraries(main
    SDL2::SDL2
    ${log-lib}
    ${android-lib}
    ${GLESv2-lib}
)
