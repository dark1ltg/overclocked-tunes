set(TEST_SOURCES
    test_parsers.cpp
    ../src/HtmlParsers.cpp
    ../src/OfficialTorrentPolicy.cpp
)

add_executable(test_parsers ${TEST_SOURCES})
target_include_directories(test_parsers PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_parsers PRIVATE
    OCR_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
target_link_libraries(test_parsers PRIVATE Qt6::Core)

if (TARGET Qt6::Test)
    target_link_libraries(test_parsers PRIVATE Qt6::Test)
    target_compile_definitions(test_parsers PRIVATE OCR_HAS_QT_TEST)
endif()

add_test(NAME parser_tests COMMAND test_parsers)
