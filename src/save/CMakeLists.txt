add_library(mcw_save STATIC nbt.cpp region.cpp anvil.cpp world_save.cpp chunk_io.cpp)
target_link_libraries(mcw_save PUBLIC mcw_core mcw_data mcw_world mcw_game)
mcweb_target_defaults(mcw_save)
