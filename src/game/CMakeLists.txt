add_library(mcw_game STATIC achievements.cpp anvil.cpp armor.cpp block_entity.cpp block_rules.cpp carts.cpp crafting.cpp creative_tabs.cpp enchant_effects.cpp enchanting.cpp enchantments.cpp fluids.cpp growth.cpp inventory.cpp menu.cpp minecart.cpp mobs.cpp physics.cpp player.cpp rails.cpp redstone.cpp rules.cpp session.cpp)
target_link_libraries(mcw_game PUBLIC mcw_core mcw_data mcw_world PRIVATE nlohmann_json::nlohmann_json)
mcweb_target_defaults(mcw_game)
