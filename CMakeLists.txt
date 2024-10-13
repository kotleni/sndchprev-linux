cmake_minimum_required(VERSION 3.15)
project(sounddir)

include(cmake/FindPipeWire.cmake)
include(cmake/FindX11.cmake)
include(cmake/FindCairo.cmake)

add_executable(${PROJECT_NAME} main.c)

target_include_directories(${PROJECT_NAME} PRIVATE ${PipeWire_INCLUDE_DIRS} ${PKG_Spa_INCLUDE_DIRS} ${X11_INCLUDE_DIR} ${CAIRO_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE m ${PipeWire_LIBRARIES} ${X11_LIBRARIES} ${CAIRO_LIBRARIES} Xfixes Xcomposite)