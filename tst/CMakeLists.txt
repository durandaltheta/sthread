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
    reply_tst.cpp
    task_tst.cpp 
    message_passing_ex.cpp
    message_payloads_ex.cpp
    payload_data_type_checking_ex.cpp
    closing_channels_ex.cpp 
    abstracting_message_replies_ex.cpp
    )


add_executable(${TEST_BINARY} ${TEST_HEADER_FILES} ${TEST_SOURCE_FILES})
target_include_directories(${TEST_BINARY} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../inc)
target_link_libraries(${TEST_BINARY} PUBLIC gtest gtest_main)
