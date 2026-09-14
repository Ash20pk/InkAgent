add_executable(DitherTest DitherTest.cpp)
target_include_directories(DitherTest PRIVATE ${REPO_ROOT}/lib/Epub/Epub/converters)
target_link_libraries(DitherTest PRIVATE inkagent_test_common GTest::gtest_main)
gtest_discover_tests(DitherTest)
