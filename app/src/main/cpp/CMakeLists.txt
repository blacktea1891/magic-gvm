cmake_minimum_required(VERSION 3.22.1)
project("shenzhou_player")

# 引入 SDL2 源码或预编译库
add_library(main SHARED
    main.c
    lvm.c
)

# 链接 Android 原生系统日志与图形库
find_library(log-lib log)
find_library(android-lib android)

target_link_libraries(main
    ${log-lib}
    ${android-lib}
)
