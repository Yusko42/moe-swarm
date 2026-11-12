find_package(GTest REQUIRED)

function(swarm_add_test)
    cmake_parse_arguments(
        ARG
        ""
        "NAME"
        "SOURCES;LIBS"
        ${ARGN}
    )

    if(NOT ARG_NAME)
        message(FATAL_ERROR "swarm_add_test: NAME is required")
    endif()
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "swarm_add_test(${ARG_NAME}): SOURCES is required")
    endif()

    add_executable(${ARG_NAME} ${ARG_SOURCES})

    target_link_libraries(${ARG_NAME} PRIVATE
        GTest::gtest_main
        GTest::gmock
        ${ARG_LIBS}
    )

    set_target_properties(${ARG_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/tests"
    )

    include(GoogleTest)
    gtest_discover_tests(${ARG_NAME}
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        PROPERTIES
            TIMEOUT 30
    )
endfunction()

swarm_add_test(
    NAME    dummy-test
    SOURCES dummy-test.cpp
)