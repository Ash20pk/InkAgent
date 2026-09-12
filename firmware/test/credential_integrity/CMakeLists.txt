add_executable(CredentialIntegrityTest
  CredentialIntegrityTest.cpp
)

target_link_libraries(CredentialIntegrityTest PRIVATE
  inkagent_test_common
  GTest::gtest_main
)

gtest_discover_tests(CredentialIntegrityTest)
