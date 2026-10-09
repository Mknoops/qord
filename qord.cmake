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
    src/Plugins/Client/MessageContexts.hpp
    src/Plugins/Client/MessageContexts.cpp
    src/UI/Settings/PluginsPage.hpp
    src/UI/Settings/PluginsPage.cpp
    src/UI/Input/PluginChatButtons.hpp
    src/UI/Input/PluginChatButtons.cpp
    src/Qord/DevMock.hpp
    src/Qord/DevMock.cpp
    src/Qord/DevMockAccount.cpp
    src/Qord/ClientExtras.cpp
    src/Core/Markdown/QordRules.cpp
    src/UI/Qord/UserPanel.hpp
    src/UI/Qord/UserPanel.cpp
    src/UI/Qord/MessageListPopup.hpp
    src/UI/Qord/MessageListPopup.cpp
    src/UI/Qord/MainWindowQord.cpp
    src/UI/Qord/EmojiPicker.hpp
    src/UI/Qord/EmojiPicker.cpp
    src/UI/Qord/FriendsPage.hpp
    src/UI/Qord/FriendsPage.cpp
    src/UI/Qord/AddServerDialog.hpp
    src/UI/Qord/AddServerDialog.cpp
    src/UI/Qord/DesktopNotifier.hpp
    src/UI/Qord/DesktopNotifier.cpp
    src/UI/Qord/NameDecorations.hpp
    src/UI/Qord/NameDecorations.cpp
    src/UI/Qord/PluginSurfaces.hpp
    src/UI/Qord/PluginSurfaces.cpp
    src/UI/Qord/NotificationSound.hpp
    src/UI/Qord/NotificationSound.cpp
    src/UI/Qord/ExpressionPicker.hpp
    src/UI/Qord/ExpressionPicker.cpp
    src/UI/Qord/SlashCommandPopup.hpp
    src/UI/Qord/SlashCommandPopup.cpp
    src/Qord/EmojiPickerData.cpp
    src/Qord/SlashCommands.hpp
    src/Qord/SlashCommands.cpp
)
target_link_libraries(${PROJECT_NAME} PRIVATE qord_builtin_plugins qord_builtin_plugin_widgets)

if(BUILD_TESTS)
    # ImageManager builds requests through Core::networkRequest, which honours mock mode
    if(TARGET TestProxy)
        target_sources(TestProxy PRIVATE src/Qord/DevMock.cpp)
    endif()
    # The parser now registers Qord's Discord rules too
    target_sources(TestMarkdown PRIVATE src/Core/Markdown/QordRules.cpp)
    target_link_libraries(TestMarkdown PRIVATE Qt::Gui) # QColor in role mentions

    qt_add_executable(TestQordMarkdown tests/qord/tst_qord_markdown.cpp src/Core/Markdown/Parser.cpp
        src/Core/Markdown/QordRules.cpp src/Core/EmojiSegmenter.cpp src/Core/Snowflake.cpp)
    target_link_libraries(TestQordMarkdown PRIVATE Qt::Core Qt::Gui Qt::Test)
    target_include_directories(TestQordMarkdown PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/emoji-segmenter)
    set_target_properties(TestQordMarkdown PROPERTIES AUTOMOC ON)
    if(MSVC)
        target_compile_options(TestQordMarkdown PRIVATE /utf-8)
    endif()
    target_compile_definitions(TestQordMarkdown PRIVATE QORD_MARKDOWN_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/qord")
    add_test(NAME QordMarkdownTests COMMAND TestQordMarkdown)

    qt_add_executable(TestSlashCommands tests/qord/tst_slash_commands.cpp src/Qord/SlashCommands.cpp)
    target_link_libraries(TestSlashCommands PRIVATE Qt::Core Qt::Test)
    target_include_directories(TestSlashCommands PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    set_target_properties(TestSlashCommands PROPERTIES AUTOMOC ON)
    if(MSVC)
        target_compile_options(TestSlashCommands PRIVATE /utf-8)
    endif()
    add_test(NAME QordSlashCommandTests COMMAND TestSlashCommands)
    add_subdirectory(tests/plugins)
endif()
