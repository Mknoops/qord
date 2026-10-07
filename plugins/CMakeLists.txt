# Native Qord plugins. Each plugin lives in plugins/<category>/<id>/ with a
# plugin.json manifest and its C++ sources, and is added here with
#
#   qord_add_plugin(<id> <category> SOURCES ... [UI_SOURCES ...])
#
# SOURCES must not depend on Qt Widgets so the plugin can be unit tested;
# UI_SOURCES (settings widgets) are compiled into the client only.
# tools/check_equicord_plugins.py reads the qord_add_plugin() calls below.

set(QORD_PLUGIN_IDS "" CACHE INTERNAL "")
set(QORD_PLUGIN_UI_IDS "" CACHE INTERNAL "")

function(qord_add_plugin id category)
    cmake_parse_arguments(ARG "" "" "SOURCES;UI_SOURCES" ${ARGN})
    set(dir ${CMAKE_CURRENT_SOURCE_DIR}/${category}/${id})
    list(TRANSFORM ARG_SOURCES PREPEND ${dir}/)
    add_library(qord_plugin_${id} STATIC ${ARG_SOURCES})
    set_target_properties(qord_plugin_${id} PROPERTIES AUTOMOC ON)
    target_link_libraries(qord_plugin_${id} PUBLIC qord_plugin_host)
    target_include_directories(qord_plugin_${id} PUBLIC ${dir})
    if(MSVC)
        target_compile_options(qord_plugin_${id} PRIVATE /utf-8 /W4)
    endif()
    # Embed plugin.json as qord_manifest_<id>() (works the same on Qt 5 and 6,
    # and needs no resource initialization in static libraries). Editing the
    # manifest re-runs CMake.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${dir}/plugin.json)
    file(READ ${dir}/plugin.json _hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "'\\\\x\\1'," _bytes "${_hex}")
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/manifest_${id}.cpp
        "// Generated from ${category}/${id}/plugin.json. Do not edit.\n"
        "#include <QByteArray>\n"
        "namespace Qord { namespace Plugins {\n"
        "QByteArray qord_manifest_${id}()\n{\n"
        "    static const char data[] = { ${_bytes} };\n"
        "    return QByteArray::fromRawData(data, int(sizeof(data)));\n}\n"
        "} }\n")
    target_sources(qord_plugin_${id} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/manifest_${id}.cpp)

    set(QORD_PLUGIN_IDS ${QORD_PLUGIN_IDS} ${id} CACHE INTERNAL "")
    if(ARG_UI_SOURCES)
        list(TRANSFORM ARG_UI_SOURCES PREPEND ${dir}/)
        add_library(qord_plugin_${id}_ui STATIC ${ARG_UI_SOURCES})
        set_target_properties(qord_plugin_${id}_ui PROPERTIES AUTOMOC ON)
        target_link_libraries(qord_plugin_${id}_ui PUBLIC qord_plugin_${id} qord_plugin_ui Qt::Widgets)
        if(MSVC)
            target_compile_options(qord_plugin_${id}_ui PRIVATE /utf-8)
        endif()
        set(QORD_PLUGIN_UI_IDS ${QORD_PLUGIN_UI_IDS} ${id} CACHE INTERNAL "")
    endif()
endfunction()

# Shared widget-side plugin API (settings widget factories)
add_library(qord_plugin_ui INTERFACE)
target_link_libraries(qord_plugin_ui INTERFACE qord_plugin_host Qt::Widgets)

# --- Batch 1 ---------------------------------------------------------------
qord_add_plugin(clearurls privacy SOURCES ClearUrls.hpp ClearUrls.cpp ClearUrlsPlugin.hpp ClearUrlsPlugin.cpp)
qord_add_plugin(textreplace chat SOURCES TextReplace.hpp TextReplace.cpp TextReplacePlugin.cpp
    UI_SOURCES TextReplaceSettingsWidget.hpp TextReplaceSettingsWidget.cpp)
qord_add_plugin(unindent chat SOURCES Unindent.hpp Unindent.cpp)
qord_add_plugin(normalizemessagelinks chat SOURCES NormalizeMessageLinks.hpp NormalizeMessageLinks.cpp)
qord_add_plugin(noreplymention chat SOURCES NoReplyMention.hpp NoReplyMention.cpp)

# --- Batch 2 ---------------------------------------------------------------
qord_add_plugin(writeuppercase chat SOURCES WriteUpperCase.hpp WriteUpperCase.cpp)
qord_add_plugin(polishwording chat SOURCES PolishWording.hpp PolishWording.cpp)
qord_add_plugin(talkinreverse chat SOURCES TalkInReverse.hpp TalkInReverse.cpp)
qord_add_plugin(silentmessagetoggle chat SOURCES SilentMessageToggle.hpp SilentMessageToggle.cpp)
qord_add_plugin(ingtoninator chat SOURCES Ingtoninator.hpp Ingtoninator.cpp)
qord_add_plugin(copyuserurls utility SOURCES CopyUserUrls.hpp CopyUserUrls.cpp)
qord_add_plugin(copyusermention chat SOURCES CopyUserMention.hpp CopyUserMention.cpp)

# --- Generated registration -------------------------------------------------
set(_decls "")
set(_calls "")
foreach(id IN LISTS QORD_PLUGIN_IDS)
    string(APPEND _decls "void qord_register_${id}(Registry &registry);\n")
    string(APPEND _calls "    qord_register_${id}(registry);\n")
endforeach()
set(_ui_decls "")
set(_ui_calls "")
foreach(id IN LISTS QORD_PLUGIN_UI_IDS)
    string(APPEND _ui_decls "void qord_register_${id}_ui(SettingsWidgetRegistry &registry);\n")
    string(APPEND _ui_calls "    qord_register_${id}_ui(registry);\n")
endforeach()
configure_file(BuiltinPlugins.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/BuiltinPlugins.cpp @ONLY)
configure_file(BuiltinPluginWidgets.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/BuiltinPluginWidgets.cpp @ONLY)

add_library(qord_builtin_plugins STATIC ${CMAKE_CURRENT_BINARY_DIR}/BuiltinPlugins.cpp)
foreach(id IN LISTS QORD_PLUGIN_IDS)
    target_link_libraries(qord_builtin_plugins PUBLIC qord_plugin_${id})
endforeach()

add_library(qord_builtin_plugin_widgets STATIC ${CMAKE_CURRENT_BINARY_DIR}/BuiltinPluginWidgets.cpp)
target_include_directories(qord_builtin_plugin_widgets PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(qord_builtin_plugin_widgets PUBLIC qord_plugin_ui)
foreach(id IN LISTS QORD_PLUGIN_UI_IDS)
    target_link_libraries(qord_builtin_plugin_widgets PUBLIC qord_plugin_${id}_ui)
endforeach()
