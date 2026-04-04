add_executable(labios-dispatcher labios-dispatcher.cpp)
target_link_libraries(labios-dispatcher PRIVATE labios)

add_executable(labios-worker labios-worker.cpp)
target_link_libraries(labios-worker PRIVATE labios)

add_executable(labios-manager labios-manager.cpp)
target_link_libraries(labios-manager PRIVATE labios)
