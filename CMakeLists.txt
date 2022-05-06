cmake_minimum_required(VERSION 3.20)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#set(CMAKE_BUILD_TYPE Release) 
set(CMAKE_BUILD_TYPE Debug) 
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -O3 -Wall")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -Wall")

project(libst)

set(ST_HEADER_FILES 
    ${CMAKE_CURRENT_LIST_DIR}/inc/simple_thread.hpp
    )

add_library(st INTERFACE)
target_include_directories(st INTERFACE inc) 

install(TARGETS st DESTINATION lib)
install(FILES ${ST_HEADER_FILES} DESTINATION include/st)

add_subdirectory(tst EXCLUDE_FROM_ALL)
