# 包含头文件目录
include_directories(
        ${PROJECT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/doctest
)

# 设置测试源文件列表
set(TEST_SOURCES
        task_test.cpp
        thread_pool_test.cpp
)

# 遍历每个测试源文件，创建可执行文件并添加测试
foreach(test_source ${TEST_SOURCES})
    # 获取测试名称（不含扩展名）
    get_filename_component(test_name ${test_source} NAME_WE)

    # 创建测试可执行文件
    add_executable(${test_name} ${test_source})

    # 设置可执行文件的属性
    set_target_properties(${test_name} PROPERTIES
            CXX_STANDARD 20
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
    )

    # 包含头文件目录
    target_include_directories(${test_name} PRIVATE
            ${PROJECT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/doctest
    )

    # 如果需要链接库，在此处添加
    # target_link_libraries(${test_name} PRIVATE libcoroutine)

    # 添加测试到 CTest
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
