# Proxy WebSocket <-> TCP: la versión web lo usa para entrar a servidores de Minecraft 1.8
add_executable(mcweb-wsproxy main.cpp)
target_link_libraries(mcweb-wsproxy PRIVATE mcw_net)
if(NOT WIN32)
  find_package(Threads REQUIRED)
  target_link_libraries(mcweb-wsproxy PRIVATE Threads::Threads)
endif()
mcweb_target_defaults(mcweb-wsproxy)
