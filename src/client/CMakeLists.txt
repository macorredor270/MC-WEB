# Mallador aparte (sin GL ni SDL): lo comparten el cliente y el Web Worker del build web
add_library(mcw_mesher STATIC mesher.cpp)
target_link_libraries(mcw_mesher PUBLIC mcw_core mcw_data mcw_world mcw_assets)
mcweb_target_defaults(mcw_mesher)

add_library(mcw_client STATIC
  environment.cpp
  game.cpp
  gl.cpp
  hud.cpp
  item_renderer.cpp
  terrain.cpp
  touch.cpp
  settings.cpp
  ui.cpp
  worker_pool.cpp
)
target_link_libraries(mcw_client PUBLIC mcw_core mcw_data mcw_world mcw_assets mcw_mesher mcw_game SDL3::SDL3-static)
if(NOT EMSCRIPTEN)
  target_link_libraries(mcw_client PUBLIC glad)
endif()
mcweb_target_defaults(mcw_client)
