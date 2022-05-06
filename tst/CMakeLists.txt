project(st_ut) 

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#set(CMAKE_BUILD_TYPE Release) 
set(CMAKE_BUILD_TYPE Debug) 
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -O3 -Wall")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -Wall")

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/googletest)

add_executable(st_ut
    simple_thread_ut.cpp
)

target_link_libraries(st_ut 
    gtest 
    gtest_main
    st)
