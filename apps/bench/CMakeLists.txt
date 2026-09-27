# Medidor de rendimiento de la carga del mundo (generar, luz, mallar)
add_executable(mcweb-bench main.cpp)
target_link_libraries(mcweb-bench PRIVATE mcw_client)
mcweb_target_defaults(mcweb-bench)
if(EMSCRIPTEN)
  # En web solo se compila a petición (cmake --build ... --target mcweb-bench) y se ejecuta con node
  set_target_properties(mcweb-bench PROPERTIES SUFFIX ".js" EXCLUDE_FROM_ALL ON)
  target_link_options(mcweb-bench PRIVATE -sENVIRONMENT=node -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=67108864
                      -sMAXIMUM_MEMORY=1073741824 -sNODERAWFS=1)
endif()
