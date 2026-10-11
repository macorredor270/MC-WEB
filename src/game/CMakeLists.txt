add_library(mcw_game STATIC achievements.cpp anvil.cpp armor.cpp block_entity.cpp block_rules.cpp carts.cpp containers.cpp crafting.cpp creative_tabs.cpp enchant_effects.cpp enchanting.cpp enchantments.cpp fire.cpp fluids.cpp growth.cpp inventory.cpp menu.cpp minecart.cpp mobs.cpp mobs_extra.cpp loot.cpp physics.cpp player.cpp portals.cpp rails.cpp redstone.cpp rules.cpp session.cpp thrown.cpp)
target_link_libraries(mcw_game PUBLIC mcw_core mcw_data mcw_world PRIVATE nlohmann_json::nlohmann_json)
mcweb_target_defaults(mcw_game)
