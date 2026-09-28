add_library(mcw_game STATIC achievements.cpp block_rules.cpp crafting.cpp inventory.cpp menu.cpp mobs.cpp physics.cpp player.cpp redstone.cpp rules.cpp session.cpp)
target_link_libraries(mcw_game PUBLIC mcw_core mcw_data mcw_world PRIVATE nlohmann_json::nlohmann_json)
mcweb_target_defaults(mcw_game)
