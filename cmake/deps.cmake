# Todas las dependencias externas, fijadas a una versión concreta y descargadas por git.
include(FetchContent)
set(FETCHCONTENT_QUIET ON)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
# CMake 4 ya no acepta dependencias que piden una versión mínima menor que la 3.5 (miniz, glm, doctest...): se les
# deja configurar como si pidieran la 3.5. Con CMake 3.x no cambia nada.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# --- SDL3 (zlib) -----------------------------------------------------------
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.2.30
  GIT_SHALLOW TRUE)

# --- glm (MIT) -------------------------------------------------------------
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.1
  GIT_SHALLOW TRUE)

# --- miniz (MIT) -----------------------------------------------------------
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(INSTALL_PROJECT OFF CACHE BOOL "" FORCE)
FetchContent_Declare(miniz
  GIT_REPOSITORY https://github.com/richgel999/miniz.git
  GIT_TAG 3.0.2
  GIT_SHALLOW TRUE)

# --- nlohmann/json (MIT) ---------------------------------------------------
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  GIT_SHALLOW TRUE)

# --- stb (dominio público / MIT) ------------------------------------------
FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20)

# --- mbedTLS (Apache-2.0): HTTPS para descargar los recursos oficiales; solo escritorio -------------------
if(NOT EMSCRIPTEN)
  set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
  set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
  set(GEN_FILES OFF CACHE BOOL "" FORCE)
  set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
  set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
  FetchContent_Declare(mbedtls
    GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
    GIT_TAG mbedtls-3.6.2
    GIT_SHALLOW TRUE
    GIT_SUBMODULES_RECURSE TRUE)
  FetchContent_MakeAvailable(mbedtls)
endif()

FetchContent_MakeAvailable(SDL3 glm miniz nlohmann_json stb)

add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE ${stb_SOURCE_DIR})

# --- glad (GL 3.3 core, generado; solo escritorio) -------------------------
if(NOT EMSCRIPTEN)
  add_library(glad STATIC ${PROJECT_SOURCE_DIR}/third_party/glad/src/gl.c)
  target_include_directories(glad SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/third_party/glad/include)
endif()

# --- doctest (MIT), solo tests ---------------------------------------------
if(MCWEB_BUILD_TESTS AND NOT EMSCRIPTEN)
  FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(doctest)
endif()
