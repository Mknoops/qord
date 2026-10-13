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

# --- Batch 3 ---------------------------------------------------------------
qord_add_plugin(messagelogger chat SOURCES MessageLogger.hpp MessageLogger.cpp)
qord_add_plugin(noblockedmessages chat SOURCES NoBlockedMessages.hpp NoBlockedMessages.cpp)
qord_add_plugin(viewraw developer SOURCES ViewRaw.hpp ViewRaw.cpp)
qord_add_plugin(reverseimagesearch media SOURCES ReverseImageSearch.hpp ReverseImageSearch.cpp)

# Batch 4
qord_add_plugin(silenttyping privacy SOURCES SilentTyping.hpp SilentTyping.cpp)
qord_add_plugin(alwaystrust utility SOURCES AlwaysTrust.cpp)
qord_add_plugin(onepingperdm chat SOURCES OnePingPerDM.hpp OnePingPerDM.cpp)
qord_add_plugin(unsuppressembeds chat SOURCES UnsuppressEmbeds.cpp)
qord_add_plugin(sendtimestamps chat SOURCES SendTimestamps.hpp SendTimestamps.cpp)
qord_add_plugin(showmeyourname chat SOURCES ShowMeYourName.hpp ShowMeYourName.cpp)
qord_add_plugin(dontroundmytimestamps appearance SOURCES DontRoundMyTimestamps.cpp)

# Batch 5
qord_add_plugin(viewicons media SOURCES ViewIcons.hpp ViewIcons.cpp)
qord_add_plugin(whoreacted chat SOURCES WhoReacted.hpp WhoReacted.cpp)
qord_add_plugin(readallnotificationsbutton utility SOURCES ReadAllNotificationsButton.cpp)
qord_add_plugin(membercount utility SOURCES MemberCount.cpp)
qord_add_plugin(platformindicators appearance SOURCES PlatformIndicators.hpp PlatformIndicators.cpp)

# Batch 6
qord_add_plugin(calltimer utility SOURCES CallTimer.hpp CallTimer.cpp)
qord_add_plugin(messageclickactions chat SOURCES MessageClickActions.hpp MessageClickActions.cpp)

# Batch 7
qord_add_plugin(fakenitro chat SOURCES FakeNitro.hpp FakeNitro.cpp ApngToGif.hpp ApngToGif.cpp)
target_link_libraries(qord_plugin_fakenitro PUBLIC Qt::Gui) # QImage/QPainter for animated stickers

# Batch 8
qord_add_plugin(permissionsviewer utility SOURCES PermissionsViewer.hpp PermissionsViewer.cpp)
qord_add_plugin(showhiddenchannels utility SOURCES ShowHiddenChannels.hpp ShowHiddenChannels.cpp)
qord_add_plugin(copyfilecontents utility SOURCES CopyFileContents.hpp CopyFileContents.cpp)
qord_add_plugin(serverinfo utility SOURCES ServerInfo.hpp ServerInfo.cpp)
qord_add_plugin(typingindicator appearance SOURCES TypingIndicator.hpp TypingIndicator.cpp)
qord_add_plugin(validuser chat SOURCES ValidUser.hpp ValidUser.cpp)
qord_add_plugin(quickreply chat SOURCES QuickReply.hpp QuickReply.cpp)
qord_add_plugin(translate utility SOURCES Translate.hpp Translate.cpp Languages.cpp)
qord_add_plugin(forceownercrown appearance SOURCES ForceOwnerCrown.cpp)
qord_add_plugin(rolecoloreverywhere appearance SOURCES RoleColorEverywhere.cpp)
qord_add_plugin(mutualgroupdms utility SOURCES MutualGroupDMs.cpp)
qord_add_plugin(volumebooster voice SOURCES VolumeBooster.cpp)
qord_add_plugin(pindms chat SOURCES PinDMs.hpp PinDMs.cpp)
qord_add_plugin(imagezoom media SOURCES ImageZoom.cpp)
qord_add_plugin(betterfolders appearance SOURCES BetterFolders.cpp)
qord_add_plugin(messagelinkembeds chat SOURCES MessageLinkEmbeds.hpp MessageLinkEmbeds.cpp)
qord_add_plugin(favoriteemojifirst chat SOURCES FavoriteEmojiFirst.hpp FavoriteEmojiFirst.cpp
    UI_SOURCES AliasListWidget.cpp)

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
