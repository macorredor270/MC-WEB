# Servidor dedicado (sin ventana): partida de Minecraft 1.8 en modo offline
add_executable(mcweb-server main.cpp)
target_link_libraries(mcweb-server PRIVATE mcw_net)
mcweb_target_defaults(mcweb-server)
