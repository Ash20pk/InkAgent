add_executable(calibre_wireless_test
  CalibreWirelessTest.cpp
  ${REPO_ROOT}/lib/CalibreWireless/CalibreWireless.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)
target_link_libraries(calibre_wireless_test PRIVATE inkagent_test_common gtest_main)
gtest_discover_tests(calibre_wireless_test)
