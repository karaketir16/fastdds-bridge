cmake_minimum_required(VERSION 3.27)

project(fastdds_bridge LANGUAGES CXX)

find_package(fastdds CONFIG REQUIRED)
find_package(ixwebsocket CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(fastdds_bridge_core
    src/config.cpp
    src/dds_topic.cpp
)
target_compile_features(fastdds_bridge_core PUBLIC cxx_std_17)
target_include_directories(fastdds_bridge_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(fastdds_bridge_core PUBLIC
    fastdds
    yaml-cpp::yaml-cpp
    nlohmann_json::nlohmann_json
)

add_executable(fastdds_bridge
    src/main.cpp
    src/rosbridge_protocol.cpp
    src/websocket_server.cpp
)
target_compile_features(fastdds_bridge PRIVATE cxx_std_17)
target_link_libraries(fastdds_bridge PRIVATE
    fastdds_bridge_core
    ixwebsocket::ixwebsocket
)

add_executable(fastdds_bridge_example_publisher examples/publisher/main.cpp)
target_compile_features(fastdds_bridge_example_publisher PRIVATE cxx_std_17)
target_link_libraries(fastdds_bridge_example_publisher PRIVATE fastdds_bridge_core)
