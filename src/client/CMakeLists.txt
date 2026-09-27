add_library(mcw_client STATIC
  environment.cpp
  game.cpp
  gl.cpp
  mesher.cpp
  terrain.cpp
  ui.cpp
)
target_link_libraries(mcw_client PUBLIC mcw_core mcw_data mcw_world mcw_assets SDL3::SDL3-static)
if(NOT EMSCRIPTEN)
  target_link_libraries(mcw_client PUBLIC glad)
endif()
mcweb_target_defaults(mcw_client)
