cmake_minimum_required(VERSION 3.20)

project(
    sthread
    VERSION 1.0.0
    DESCRIPTION "A header-only c++ library enabling simple interthread operations"
)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release) 
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -O3 -Wall")

set(HEADER_FILES 
    ${CMAKE_CURRENT_LIST_DIR}/inc/utility.hpp
    ${CMAKE_CURRENT_LIST_DIR}/inc/context.hpp
    ${CMAKE_CURRENT_LIST_DIR}/inc/data.hpp
    ${CMAKE_CURRENT_LIST_DIR}/inc/message.hpp
    ${CMAKE_CURRENT_LIST_DIR}/inc/task.hpp
    ${CMAKE_CURRENT_LIST_DIR}/inc/channel.hpp
    ${CMAKE_CURRENT_LIST_DIR}/inc/reply.hpp
    ${CMAKE_CURRENT_LIST_DIR}/inc/sthread
    )

add_library(${CMAKE_PROJECT_NAME} INTERFACE)

target_include_directories(
    ${CMAKE_PROJECT_NAME} 
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/inc 
) 

install(FILES ${HEADER_FILES} DESTINATION include/${CMAKE_PROJECT_NAME})

add_subdirectory(tst EXCLUDE_FROM_ALL)
