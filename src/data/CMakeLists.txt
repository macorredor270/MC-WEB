add_library(mcw_data STATIC blocks.cpp blockstates.cpp biomes.cpp items.cpp)
target_link_libraries(mcw_data PUBLIC mcw_core)
mcweb_target_defaults(mcw_data)
