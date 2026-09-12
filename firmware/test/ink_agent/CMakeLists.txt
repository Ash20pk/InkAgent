add_executable(InkAgentProtocolTest
  InkAgentProtocolTest.cpp
  ${REPO_ROOT}/lib/InkAgent/InkAgentProtocol.cpp
)
target_include_directories(InkAgentProtocolTest PRIVATE ${REPO_ROOT}/lib/InkAgent)
target_link_libraries(InkAgentProtocolTest PRIVATE inkagent_test_common GTest::gtest_main)
gtest_discover_tests(InkAgentProtocolTest)
