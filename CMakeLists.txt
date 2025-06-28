cmake_minimum_required(VERSION 3.25)
project(
  labios
  VERSION 0.0.0
  DESCRIPTION "Labios: A Label-based I/O system")

add_subdirectory(tasks)

jarvis_repo_add("${CMAKE_SOURCE_DIR}/tasks/jarvis_labios" "${CMAKE_SOURCE_DIR}/tasks/jarvis_labios/pipelines")