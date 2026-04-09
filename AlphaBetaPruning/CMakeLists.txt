cmake_minimum_required(VERSION 3.14)
project(BroncosGambit C CXX)

set(CMAKE_CXX_STANDARD 17)

enable_testing()
find_package(GTest REQUIRED)

add_executable(test_engine test_engine.cpp)
target_link_libraries(test_engine GTest::GTest GTest::Main)
set_target_properties(test_engine PROPERTIES
    BUILD_RPATH "/opt/anaconda3/lib"
    INSTALL_RPATH "/opt/anaconda3/lib"
)

include(GoogleTest)
gtest_discover_tests(test_engine)
