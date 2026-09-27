add_executable(mcweb WIN32 main.cpp)
target_link_libraries(mcweb PRIVATE mcw_client)
mcweb_target_defaults(mcweb)

if(EMSCRIPTEN)
  set_target_properties(mcweb PROPERTIES SUFFIX ".js")
  # Sin hilos (páginas sin COOP/COEP, p. ej. alojamientos estáticos): mcweb-st.js / mcweb-st.wasm
  if(NOT MCWEB_WEB_THREADS)
    set_target_properties(mcweb PROPERTIES OUTPUT_NAME "mcweb-st")
  endif()
  target_link_options(mcweb PRIVATE
    -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 -sMAXIMUM_MEMORY=2147483648
    -sSTACK_SIZE=1048576
    -sEXPORTED_RUNTIME_METHODS=FS,callMain -sMODULARIZE=0
    -sFORCE_FILESYSTEM=1 -lidbfs.js -sDYNAMIC_EXECUTION=0
    -sINVOKE_RUN=0)
  add_custom_command(TARGET mcweb POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROJECT_SOURCE_DIR}/web/index.html $<TARGET_FILE_DIR:mcweb>/index.html)
  if(MCWEB_WEB_THREADS)
    target_link_options(mcweb PRIVATE -pthread -sPTHREAD_POOL_SIZE=6 -sPTHREAD_POOL_SIZE_STRICT=0)
  endif()
endif()
