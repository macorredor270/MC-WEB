cmake_minimum_required(VERSION 3.25)
project(mcweb VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MCWEB_BUILD_TESTS "Compilar los tests" ON)
option(MCWEB_WEB_THREADS "Build web con pthreads (requiere COOP/COEP)" ON)

if(EMSCRIPTEN)
  set(MCWEB_PLATFORM_WEB ON)
  # Todo (también las dependencias) con excepciones de wasm y, si se piden, hilos
  add_compile_options(-fwasm-exceptions)
  add_link_options(-fwasm-exceptions)
  if(MCWEB_WEB_THREADS)
    add_compile_options(-pthread)
    add_link_options(-pthread)
  endif()
endif()

include(cmake/deps.cmake)
include(cmake/warnings.cmake)

add_subdirectory(src)
add_subdirectory(apps/mcweb)

if(MCWEB_BUILD_TESTS AND NOT EMSCRIPTEN)
  enable_testing()
  add_subdirectory(tests)
endif()
