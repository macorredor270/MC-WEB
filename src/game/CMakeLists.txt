add_library(mcw_game STATIC achievements.cpp armor.cpp block_rules.cpp crafting.cpp creative_tabs.cpp enchant_effects.cpp enchanting.cpp enchantments.cpp inventory.cpp menu.cpp mobs.cpp physics.cpp player.cpp redstone.cpp rules.cpp session.cpp)
target_link_libraries(mcw_game PUBLIC mcw_core mcw_data mcw_world PRIVATE nlohmann_json::nlohmann_json)
mcweb_target_defaults(mcw_game)
