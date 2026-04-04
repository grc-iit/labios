include(CTest)
include(Catch)

add_executable(labios-config-test unit/config_test.cpp)
target_link_libraries(labios-config-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-config-test)

add_executable(labios-smoke-test integration/smoke_test.cpp)
target_link_libraries(labios-smoke-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-smoke-test)
