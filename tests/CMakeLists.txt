add_executable(mcweb_tests
  main.cpp
  test_buffer.cpp
  test_data.cpp
  test_world.cpp
  test_assets.cpp
  test_mesher.cpp
  test_game.cpp
)
target_link_libraries(mcweb_tests PRIVATE mcw_core mcw_data mcw_world mcw_assets mcw_client mcw_game doctest::doctest nlohmann_json::nlohmann_json)
target_compile_definitions(mcweb_tests PRIVATE MCWEB_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
mcweb_target_defaults(mcweb_tests)
add_test(NAME mcweb_tests COMMAND mcweb_tests)
