cmake_minimum_required(VERSION 3.20)
project(simple_thread)
set(LIBRARY ${CMAKE_PROJECT_NAME})
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release) 
set(ENABLE_SIMPLE_THREAD_LOGGING 1)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -O3 -Wall")

set(HEADER_FILES 
    ${CMAKE_CURRENT_LIST_DIR}/inc/sthread
    ${CMAKE_CURRENT_LIST_DIR}/inc/simple_thread.hpp
    )

add_library(${LIBRARY} INTERFACE)
target_include_directories(${LIBRARY} INTERFACE inc) 

install(FILES ${HEADER_FILES} DESTINATION include/${LIBRARY})

add_subdirectory(tst EXCLUDE_FROM_ALL)
