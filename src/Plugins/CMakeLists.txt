# Qord plugin host: UI-free so it can be unit tested on its own.
add_library(qord_plugin_host STATIC
    Types.hpp
    Permission.hpp Permission.cpp
    Settings.hpp Settings.cpp
    Manifest.hpp Manifest.cpp
    Plugin.hpp
    Services.hpp
    PluginContext.hpp PluginContext.cpp
    Registry.hpp
    PluginHost.hpp PluginHost.cpp
    JsCompat.hpp JsCompat.cpp
    TextDiff.hpp TextDiff.cpp
)
set_target_properties(qord_plugin_host PROPERTIES AUTOMOC ON)
target_include_directories(qord_plugin_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(qord_plugin_host PUBLIC Qt::Core)
target_compile_features(qord_plugin_host PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(qord_plugin_host PRIVATE /utf-8 /W4)
endif()
