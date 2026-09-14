add_executable(ToneMapperTest ToneMapperTest.cpp
               ${REPO_ROOT}/freeink-sdk/libs/assets/ImageToneMapper/src/ImageToneMapper.cpp)
target_include_directories(ToneMapperTest PRIVATE
                           ${REPO_ROOT}/freeink-sdk/libs/assets/ImageToneMapper/include)
target_link_libraries(ToneMapperTest PRIVATE inkagent_test_common GTest::gtest_main)
gtest_discover_tests(ToneMapperTest)
