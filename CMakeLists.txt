cmake_minimum_required(VERSION 3.29)

project(libcoroutine)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 启用导出编译命令。这通常用于生成 compile_commands.json 文件，便于使用 clangd、ccls 等工具进行代码分析。
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 查找线程库
find_package(Threads REQUIRED)

# 添加 src 和 test 子目录
add_subdirectory(src)
enable_testing()
add_subdirectory(test)
