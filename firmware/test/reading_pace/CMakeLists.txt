add_executable(ReadingPaceTest ReadingPaceTest.cpp)
target_include_directories(ReadingPaceTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(ReadingPaceTest PRIVATE inkagent_test_common GTest::gtest_main)
gtest_discover_tests(ReadingPaceTest)
