set(TEST_BINARY ${CMAKE_PROJECT_NAME}_tst)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/googletest EXCLUDE_FROM_ALL)

set(TEST_HEADER_FILES
    simple_thread_test_utils.hpp
    )

set(TEST_SOURCE_FILES 
    simple_thread_test_utils.cpp
    data_tst.cpp
    context_tst.cpp
    message_tst.cpp
    channel_tst.cpp 
    task_tst.cpp
    )

add_executable(${TEST_BINARY} ${TEST_HEADER_FILES} ${TEST_SOURCE_FILES})
target_include_directories(${TEST_BINARY} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../inc)
target_link_libraries(${TEST_BINARY} PUBLIC gtest gtest_main)
