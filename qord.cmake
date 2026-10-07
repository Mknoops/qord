# Qord additions to the Acheron build: the native plugin host, the plugins,
# their tests, and the client glue that wires them in. Kept in one file so
# the edits to Acheron's CMakeLists.txt stay a single include().

# Same Qt 5 + current MSVC shim Acheron applies to its own targets
if(MSVC AND QT_VERSION_MAJOR LESS 6)
    add_compile_options("/FI${CMAKE_CURRENT_SOURCE_DIR}/src/qt5_compat.h")
endif()

add_subdirectory(src/Plugins)
add_subdirectory(plugins)

target_sources(${PROJECT_NAME} PRIVATE
    src/Plugins/Client/ClientPlugins.hpp
    src/Plugins/Client/ClientPlugins.cpp
    src/UI/Settings/PluginsPage.hpp
    src/UI/Settings/PluginsPage.cpp
)
target_link_libraries(${PROJECT_NAME} PRIVATE qord_builtin_plugins qord_builtin_plugin_widgets)

if(BUILD_TESTS)
    add_subdirectory(tests/plugins)
endif()
