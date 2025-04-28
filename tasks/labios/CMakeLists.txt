# ------------------------------------------------------------------------------
# Build labios module
# ------------------------------------------------------------------------------
include_directories(include)
add_subdirectory(src)
add_subdirectory(test)
# -----------------------------------------------------------------------------
# Install labios headers
# -----------------------------------------------------------------------------
install(DIRECTORY include DESTINATION ${CMAKE_INSTALL_PREFIX})
