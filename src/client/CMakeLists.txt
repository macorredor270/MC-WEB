add_library(mcw_client STATIC
  environment.cpp
  game.cpp
  gl.cpp
  hud.cpp
  item_renderer.cpp
  mesher.cpp
  terrain.cpp
  touch.cpp
  ui.cpp
)
target_link_libraries(mcw_client PUBLIC mcw_core mcw_data mcw_world mcw_assets mcw_game SDL3::SDL3-static)
if(NOT EMSCRIPTEN)
  target_link_libraries(mcw_client PUBLIC glad)
endif()
mcweb_target_defaults(mcw_client)
