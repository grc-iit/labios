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
catch_discover_tests(labios-catalog-manager-test TEST_PREFIX "smoke/" PROPERTIES LABELS "smoke")

add_executable(labios-smoke-test integration/smoke_test.cpp)
target_link_libraries(labios-smoke-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-smoke-test
    TEST_PREFIX "smoke/"
    PROPERTIES LABELS "smoke"
)

add_executable(labios-content-manager-test unit/content_manager_test.cpp)
target_link_libraries(labios-content-manager-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-content-manager-test TEST_PREFIX "smoke/" PROPERTIES LABELS "smoke")

add_executable(labios-data-path-test integration/data_path_test.cpp)
target_link_libraries(labios-data-path-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-data-path-test TEST_PREFIX "smoke/" PROPERTIES LABELS "smoke")

add_executable(labios-label-manager-test unit/label_manager_test.cpp)
target_link_libraries(labios-label-manager-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-label-manager-test TEST_PREFIX "smoke/" PROPERTIES LABELS "smoke")

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

add_executable(labios-solver-random-test unit/solver_random_test.cpp)
target_link_libraries(labios-solver-random-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-solver-random-test TEST_PREFIX "unit/" PROPERTIES LABELS "unit")

add_executable(labios-worker-manager-test unit/worker_manager_test.cpp)
target_link_libraries(labios-worker-manager-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-worker-manager-test TEST_PREFIX "unit/" PROPERTIES LABELS "unit")

add_executable(labios-solver-constraint-test unit/solver_constraint_test.cpp)
target_link_libraries(labios-solver-constraint-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-solver-constraint-test TEST_PREFIX "unit/" PROPERTIES LABELS "unit")

add_executable(labios-solver-minmax-test unit/solver_minmax_test.cpp)
target_link_libraries(labios-solver-minmax-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-solver-minmax-test TEST_PREFIX "unit/" PROPERTIES LABELS "unit")

add_executable(labios-scheduling-test integration/scheduling_test.cpp)
target_link_libraries(labios-scheduling-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-scheduling-test TEST_PREFIX "integration/" PROPERTIES LABELS "integration")

add_executable(labios-elastic-decision-test unit/elastic_decision_test.cpp)
target_link_libraries(labios-elastic-decision-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-elastic-decision-test TEST_PREFIX "unit/" PROPERTIES LABELS "unit")

add_executable(labios-docker-client-test unit/docker_client_test.cpp)
target_link_libraries(labios-docker-client-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-docker-client-test TEST_PREFIX "unit/" PROPERTIES LABELS "unit")

add_executable(labios-elastic-orchestrator-test unit/elastic_orchestrator_test.cpp)
target_link_libraries(labios-elastic-orchestrator-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-elastic-orchestrator-test TEST_PREFIX "unit/" PROPERTIES LABELS "unit")

add_executable(labios-client-async-test unit/client_async_test.cpp)
target_link_libraries(labios-client-async-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-client-async-test TEST_PREFIX "unit/" PROPERTIES LABELS "unit")

add_executable(labios-elastic-flood-test integration/elastic_flood_test.cpp)
target_link_libraries(labios-elastic-flood-test PRIVATE labios)

add_executable(labios-kernel-cm1-test integration/kernel_cm1_test.cpp)
target_link_libraries(labios-kernel-cm1-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-kernel-cm1-test TEST_PREFIX "kernel/" PROPERTIES LABELS "kernel")
