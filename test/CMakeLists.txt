# 包含头文件目录
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/doctest)

# 收集测试源文件
file(GLOB TEST_SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

# 创建测试可执行文件
add_executable(coro_tests ${TEST_SRC_FILES})

# 链接库
target_link_libraries(coro_tests PRIVATE coro Threads::Threads)

# 设置测试目标的C++标准
set_target_properties(coro_tests PROPERTIES CXX_STANDARD 20)

# 添加测试
add_test(NAME CoroTests COMMAND coro_tests)
