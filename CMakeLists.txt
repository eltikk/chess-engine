cmake_minimum_required(VERSION 3.16)

project(ChessEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(chess_engine_core
    src/Board.cpp
    src/MagicBitboards.cpp
    src/MoveGenerator.cpp
)

target_include_directories(chess_engine_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(chess_engine
    src/main.cpp
)

target_link_libraries(chess_engine PRIVATE chess_engine_core)

include(CTest)

if(BUILD_TESTING)
    file(GLOB TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*Tests.cpp"
    )

    add_executable(chess_engine_tests
        tests/TestMain.cpp
        ${TEST_SOURCES}
    )

    target_link_libraries(chess_engine_tests PRIVATE chess_engine_core)

    target_include_directories(chess_engine_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(
        NAME chess_engine_tests
        COMMAND chess_engine_tests
    )
endif()
