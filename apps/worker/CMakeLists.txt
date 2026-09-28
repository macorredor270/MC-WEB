# Módulo de los Web Workers (solo en el build web sin hilos): generación de chunks y mallado
add_executable(mcweb-worker main.cpp)
target_link_libraries(mcweb-worker PRIVATE mcw_world mcw_assets mcw_mesher)
mcweb_target_defaults(mcweb-worker)
set_target_properties(mcweb-worker PROPERTIES SUFFIX ".js")
target_link_options(mcweb-worker PRIVATE
  -sENVIRONMENT=worker -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=33554432 -sMAXIMUM_MEMORY=536870912
  -sSTACK_SIZE=1048576 -sEXPORTED_FUNCTIONS=_malloc,_free "-sDEFAULT_LIBRARY_FUNCS_TO_INCLUDE=$stringToNewUTF8" -sDYNAMIC_EXECUTION=0 -sFILESYSTEM=0
  --pre-js ${PROJECT_SOURCE_DIR}/web/worker-pre.js)
set_property(TARGET mcweb-worker APPEND PROPERTY LINK_DEPENDS ${PROJECT_SOURCE_DIR}/web/worker-pre.js)
# Junto a mcweb-st.js, para que la página lo encuentre con una ruta relativa
set_target_properties(mcweb-worker PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/apps/mcweb)
