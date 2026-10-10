add_executable(mcweb_tests
  main.cpp
  test_buffer.cpp
  test_data.cpp
  test_world.cpp
  test_assets.cpp
  test_mesher.cpp
  test_game.cpp
  test_audio.cpp
  test_settings.cpp
  test_save.cpp
  test_net.cpp
  test_skin.cpp
  test_enchant.cpp
  test_enchant_effects.cpp
  test_anvil.cpp
  test_rails.cpp
  test_carts.cpp
  test_creative.cpp
  test_touch.cpp
  test_arena.cpp
  test_quality.cpp
  test_tiles.cpp
  test_fluids.cpp
  test_growth.cpp
  test_fire.cpp
)
target_link_libraries(mcweb_tests PRIVATE mcw_core mcw_data mcw_world mcw_assets mcw_client mcw_game mcw_save mcw_net doctest::doctest nlohmann_json::nlohmann_json)
target_compile_definitions(mcweb_tests PRIVATE MCWEB_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
mcweb_target_defaults(mcweb_tests)
add_test(NAME mcweb_tests COMMAND mcweb_tests)
