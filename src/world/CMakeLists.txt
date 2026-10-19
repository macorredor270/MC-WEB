add_library(mcw_world STATIC chunk.cpp chunk_codec.cpp noise.cpp legacy_gen.cpp generator.cpp dimension_gen.cpp structures.cpp villages.cpp temples.cpp mineshafts.cpp light.cpp tree_grow.cpp world.cpp)
target_link_libraries(mcw_world PUBLIC mcw_core mcw_data)
mcweb_target_defaults(mcw_world)
