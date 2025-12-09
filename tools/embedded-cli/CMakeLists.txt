set(TARGET llama-embedded-cli)

if (NOT LLAMA_HTTPLIB)
    message(FATAL_ERROR "LLAMA_HTTPLIB is OFF, cannot build llama-embedded-cli. Enable it or turn off LLAMA_BUILD_SERVER.")
endif()

set(SERVER_SOURCES
    ../server/server-common.cpp
    ../server/server-http.cpp
    ../server/server-task.cpp
    ../server/server-queue.cpp
    ../server/server-context.cpp
)

set(PUBLIC_ASSETS
    ../server/public/index.html.gz
    ../server/public/loading.html
)

foreach(asset ${PUBLIC_ASSETS})
    get_filename_component(asset_name ${asset} NAME)
    set(output "${CMAKE_CURRENT_BINARY_DIR}/${asset_name}.hpp")
    list(APPEND SERVER_SOURCES ${output})
    add_custom_command(
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${asset}"
        OUTPUT  "${output}"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/${asset}" "-DOUTPUT=${output}" -P "${PROJECT_SOURCE_DIR}/scripts/xxd.cmake"
    )
    set_source_files_properties(${output} PROPERTIES GENERATED TRUE)
endforeach()

add_executable(${TARGET}
    embedded-cli.cpp
    ${SERVER_SOURCES}
)

target_include_directories(${TARGET} PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../server
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(${TARGET} PRIVATE
    common
    mtmd
    cpp-httplib
    ${CMAKE_THREAD_LIBS_INIT}
)

if (WIN32)
    target_link_libraries(${TARGET} PRIVATE ws2_32)
endif()

target_compile_features(${TARGET} PRIVATE cxx_std_17)

install(TARGETS ${TARGET} RUNTIME)
