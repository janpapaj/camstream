cmake_minimum_required(VERSION 3.5)
project(camstream)

set(CMAKE_AUTOMOC ON)
find_package(Qt5 REQUIRED COMPONENTS Core WebSockets Gui Network Widgets)

find_package(PkgConfig REQUIRED)

pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
    libswscale
)

find_package(OpenCV REQUIRED)

cmake_policy(SET CMP0071 NEW)
find_package(Protobuf REQUIRED)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS
    ${CMAKE_CURRENT_SOURCE_DIR}/cmd.proto
)

# client
file(GLOB CLIENT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/client/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/client/*.hpp"
)

add_executable(
    client
    ${CLIENT_SOURCES}
    ${PROTO_SRCS}
    ${PROTO_HDRS})

target_include_directories(client PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/client/
    ${CMAKE_CURRENT_BINARY_DIR}
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_libraries(client PRIVATE
    Qt5::Widgets
    Qt5::Core
    Qt5::WebSockets
    Qt5::Gui
    Qt5::Network
    PkgConfig::FFMPEG
    protobuf::libprotobuf)

# server
file(GLOB SERVER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/server/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/server/*.hpp"
)

add_executable(
    server
    ${SERVER_SOURCES}
    ${PROTO_SRCS}
    ${PROTO_HDRS})

target_link_libraries(server PRIVATE
    Qt5::Widgets
    Qt5::Core
    Qt5::WebSockets
    Qt5::Gui
    Qt5::Network
    PkgConfig::FFMPEG
    ${OpenCV_LIBS}
    protobuf::libprotobuf)

target_include_directories(server PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/server
    ${CMAKE_CURRENT_BINARY_DIR}
    ${FFMPEG_INCLUDE_DIRS}
)
