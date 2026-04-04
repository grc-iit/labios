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

add_executable(labios-catalog-manager-test unit/catalog_manager_test.cpp)
target_link_libraries(labios-catalog-manager-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-catalog-manager-test TEST_PREFIX "unit/" PROPERTIES LABELS "unit")

add_executable(labios-smoke-test integration/smoke_test.cpp)
target_link_libraries(labios-smoke-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-smoke-test
    TEST_PREFIX "smoke/"
    PROPERTIES LABELS "smoke"
)

add_executable(labios-content-manager-test unit/content_manager_test.cpp)
target_link_libraries(labios-content-manager-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-content-manager-test TEST_PREFIX "unit/" PROPERTIES LABELS "unit")

add_executable(labios-data-path-test integration/data_path_test.cpp)
target_link_libraries(labios-data-path-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-data-path-test TEST_PREFIX "smoke/" PROPERTIES LABELS "smoke")

add_executable(labios-label-manager-test unit/label_manager_test.cpp)
target_link_libraries(labios-label-manager-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-label-manager-test TEST_PREFIX "unit/" PROPERTIES LABELS "unit")

add_executable(labios-fd-table-test unit/fd_table_test.cpp)
target_link_libraries(labios-fd-table-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-fd-table-test TEST_PREFIX "unit/" PROPERTIES LABELS "unit")

add_executable(labios-intercept-test integration/intercept_test.cpp)
target_link_libraries(labios-intercept-test PRIVATE Catch2::Catch2WithMain)

add_executable(labios-benchmark-test integration/benchmark_test.cpp)
target_link_libraries(labios-benchmark-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-benchmark-test TEST_PREFIX "bench/" PROPERTIES LABELS "bench")

add_executable(labios-shuffler-test unit/shuffler_test.cpp)
target_link_libraries(labios-shuffler-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-shuffler-test TEST_PREFIX "unit/" PROPERTIES LABELS "unit")
