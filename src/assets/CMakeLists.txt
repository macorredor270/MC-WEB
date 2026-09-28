add_library(mcw_assets STATIC image.cpp pack.cpp models.cpp textures.cpp cc0_pack.cpp cc0_blocks.cpp cc0_items.cpp item_models.cpp)
target_link_libraries(mcw_assets PUBLIC mcw_core mcw_data nlohmann_json::nlohmann_json PRIVATE stb)
mcweb_target_defaults(mcw_assets)
