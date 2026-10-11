find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(quantMicrostructure_tests
  testBuildInfo.cpp
)
target_link_libraries(quantMicrostructure_tests
  PRIVATE
    quantMicrostructure
    GTest::gtest_main
)
qm_set_warnings(quantMicrostructure_tests)
gtest_discover_tests(quantMicrostructure_tests)
