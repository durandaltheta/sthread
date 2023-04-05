cmake_minimum_required(VERSION 3.20)
project(sthread)
set(LIBRARY ${CMAKE_PROJECT_NAME})
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release) 
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -O3 -Wall")

set(HEADER_FILES 
    ${CMAKE_CURRENT_LIST_DIR}/inc/utility.hpp
    ${CMAKE_CURRENT_LIST_DIR}/inc/context.hpp
    ${CMAKE_CURRENT_LIST_DIR}/inc/data.hpp
    ${CMAKE_CURRENT_LIST_DIR}/inc/message.hpp
    ${CMAKE_CURRENT_LIST_DIR}/inc/channel.hpp
    ${CMAKE_CURRENT_LIST_DIR}/inc/reply.hpp
    ${CMAKE_CURRENT_LIST_DIR}/inc/threadpool.hpp
    ${CMAKE_CURRENT_LIST_DIR}/inc/sthread
    )

set(SOURCE_FILES
    ${CMAKE_CURRENT_LIST_DIR}/src/utility.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/channel.cpp
    )

add_library(${LIBRARY} STATIC ${SOURCE_FILES})
target_include_directories(${LIBRARY} PUBLIC inc) 

install(FILES ${HEADER_FILES} DESTINATION include/${LIBRARY})
install(TARGETS ${LIBRARY} DESTINATION lib/)

add_subdirectory(tst EXCLUDE_FROM_ALL)
