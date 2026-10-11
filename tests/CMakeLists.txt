find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(quantMicrostructure_tests
  testBuildInfo.cpp
  testFeed.cpp
  testOrderBook.cpp
  testMatchingEngine.cpp
  testBookProperties.cpp
  testRng.cpp
  testZeroIntelligence.cpp
  testHawkes.cpp
  testToyModels.cpp
  testQueueReactive.cpp
  testTape.cpp
  testMeasures.cpp
  testBacktest.cpp
  testImpact.cpp
  testExecution.cpp
)
target_link_libraries(quantMicrostructure_tests
  PRIVATE
    quantMicrostructure
    GTest::gtest_main
)
# tests/support holds what tests and benchmarks share.
target_include_directories(quantMicrostructure_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
qm_set_warnings(quantMicrostructure_tests)
gtest_discover_tests(quantMicrostructure_tests)
