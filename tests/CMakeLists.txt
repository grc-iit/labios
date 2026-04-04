include(CTest)
include(Catch)

add_executable(labios-config-test unit/config_test.cpp)
target_link_libraries(labios-config-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-config-test
    TEST_PREFIX "unit/"
    PROPERTIES LABELS "unit"
)

add_executable(labios-label-test unit/label_test.cpp)
target_link_libraries(labios-label-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-label-test
    TEST_PREFIX "unit/"
    PROPERTIES LABELS "unit"
)

add_executable(labios-solver-test unit/solver_test.cpp)
target_link_libraries(labios-solver-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-solver-test
    TEST_PREFIX "unit/"
    PROPERTIES LABELS "unit"
)

add_executable(labios-smoke-test integration/smoke_test.cpp)
target_link_libraries(labios-smoke-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-smoke-test
    TEST_PREFIX "smoke/"
    PROPERTIES LABELS "smoke"
)
