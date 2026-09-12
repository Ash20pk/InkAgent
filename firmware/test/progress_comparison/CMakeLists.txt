add_executable(ProgressComparisonTest
  ProgressComparisonTest.cpp
  ${REPO_ROOT}/lib/KOReaderSync/ProgressComparison.cpp
)

target_include_directories(ProgressComparisonTest PRIVATE
  ${REPO_ROOT}/lib/KOReaderSync
)

target_link_libraries(ProgressComparisonTest PRIVATE
  inkagent_test_common
  GTest::gtest_main
)

gtest_discover_tests(ProgressComparisonTest)
