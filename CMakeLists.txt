cmake_minimum_required(VERSION 3.20)
project(sthread)
set(LIBRARY ${CMAKE_PROJECT_NAME})
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release) 
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -O3 -Wall")

set(HEADER_FILES 
    ${CMAKE_CURRENT_LIST_DIR}/inc/sthread
    ${CMAKE_CURRENT_LIST_DIR}/inc/simple_thread.hpp
    )

set(SOURCE_FILES
    ${CMAKE_CURRENT_LIST_DIR}/src/simple_thread.cpp)

add_library(${LIBRARY} STATIC ${SOURCE_FILES})
target_include_directories(${LIBRARY} PUBLIC inc) 

install(FILES ${HEADER_FILES} DESTINATION include/${LIBRARY})
install(TARGETS ${LIBRARY} DESTINATION lib/)

add_subdirectory(tst EXCLUDE_FROM_ALL)
