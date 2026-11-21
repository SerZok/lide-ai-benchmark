cmake_minimum_required(VERSION 3.19)
project(lide-ai-benchmark VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
find_package(QT NAMES Qt6 REQUIRED COMPONENTS Core Network Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Network Widgets)

add_executable(lide-ai-benchmark
    src/main.cpp
    src/code_extractor.cpp
    src/code_extractor.h
    src/harness_generator.cpp
    src/harness_generator.h
    src/ollama_client.cpp
    src/ollama_client.h
    src/benchmark_window.cpp
    src/benchmark_window.h
    src/sbcl_sandbox.cpp
    src/sbcl_sandbox.h
)
target_link_libraries(lide-ai-benchmark PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Widgets
)
target_include_directories(lide-ai-benchmark PRIVATE src)

if(WIN32)
    target_link_libraries(lide-ai-benchmark PRIVATE advapi32 userenv)
endif()

add_custom_command(TARGET lide-ai-benchmark POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/aggregator.py"
        "$<TARGET_FILE_DIR:lide-ai-benchmark>/aggregator.py"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmark.example.json"
        "$<TARGET_FILE_DIR:lide-ai-benchmark>/benchmark.example.json"
)

if(WIN32)
    find_program(BENCHMARK_WINDEPLOYQT windeployqt)
    if(BENCHMARK_WINDEPLOYQT)
        add_custom_command(TARGET lide-ai-benchmark POST_BUILD
            COMMAND ${BENCHMARK_WINDEPLOYQT} $<TARGET_FILE:lide-ai-benchmark>
                --verbose 0
                --no-network
                --no-opengl
                --no-opengl-sw
                --no-compiler-runtime
                --no-system-d3d-compiler
                --no-system-dxc-compiler
            COMMENT "Deploying Qt dependencies for lide-ai-benchmark"
        )
    endif()
endif()

include(CTest)
find_package(Python3 COMPONENTS Interpreter)
if(BUILD_TESTING AND Python3_Interpreter_FOUND)
    add_test(
        NAME benchmark_aggregator
        COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/tests -v
    )
    set_tests_properties(benchmark_aggregator PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
if(BUILD_TESTING)
    add_executable(code-extractor-test
        tests/code_extractor_test.cpp
        src/code_extractor.cpp
        src/code_extractor.h
    )
    target_link_libraries(code-extractor-test PRIVATE Qt${QT_VERSION_MAJOR}::Core)
    target_include_directories(code-extractor-test PRIVATE src)
    add_test(NAME code_extractor COMMAND code-extractor-test)

    add_executable(harness-generator-test
        tests/harness_generator_test.cpp
        src/harness_generator.cpp
        src/harness_generator.h
    )
    target_link_libraries(harness-generator-test PRIVATE Qt${QT_VERSION_MAJOR}::Core)
    target_include_directories(harness-generator-test PRIVATE src)
    add_test(NAME harness_generator COMMAND harness-generator-test)
endif()
