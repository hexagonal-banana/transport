cmake_minimum_required(VERSION 3.16)
project(transport_agent LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic)

include_directories(include)

add_executable(agent
    src/agent_main.cpp
    src/peer_buffer.cpp
)

target_link_libraries(agent PRIVATE pthread)

add_executable(client
    src/client_main.cpp
)

add_executable(sender
    src/sender_main.cpp
)

add_executable(receiver
    src/receiver_main.cpp
)
