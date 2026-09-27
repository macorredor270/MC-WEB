add_library(mcw_world STATIC chunk.cpp noise.cpp generator.cpp light.cpp world.cpp)
target_link_libraries(mcw_world PUBLIC mcw_core mcw_data)
mcweb_target_defaults(mcw_world)
