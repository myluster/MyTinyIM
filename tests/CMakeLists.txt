add_executable(integration_tests
    integration_test.cpp
)

target_link_libraries(integration_tests
    GTest::gtest_main
    Boost::system
    Boost::thread
    spdlog::spdlog
)

# 确保能找到 common 里的库或者直接使用 HTTP/WS client 代码
# 这里为了简化，我们直接在测试代码里写简易的 Beast Client
