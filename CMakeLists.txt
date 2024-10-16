cmake_minimum_required(VERSION 3.15)
project(sndchprev)

include(cmake/FindPipeWire.cmake)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED sdl3)

add_executable(${PROJECT_NAME} main.c)

target_include_directories(${PROJECT_NAME} PRIVATE 
    ${PipeWire_INCLUDE_DIRS} 
    ${PKG_Spa_INCLUDE_DIRS}
    ${SDL3_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE 
    m
    ${PipeWire_LIBRARIES}
    ${SDL3_LIBRARIES}
)