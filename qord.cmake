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
    src/Qord/Video/H264Rtp.hpp
    src/Qord/Video/H264Rtp.cpp
)
target_link_libraries(${PROJECT_NAME} PRIVATE qord_builtin_plugins qord_builtin_plugin_widgets)
# Video (Go Live, cameras) needs ffmpeg's H.264 encoders and decoder, and voice
if(TARGET ffmpeg AND ENABLE_VOICE)
    target_sources(${PROJECT_NAME} PRIVATE
        src/Qord/Video/VideoCodec.hpp
        src/Qord/Video/VideoCodec.cpp
        src/Qord/Video/ScreenCapture.hpp
        src/Qord/Video/ScreenCapture.cpp
        src/Qord/Video/GoLive.hpp
        src/Qord/Video/GoLive.cpp
        src/Qord/Video/FrameSource.hpp
        src/Qord/Video/VideoSender.hpp
        src/Qord/Video/VideoSender.cpp
        src/Qord/Video/CameraCapture.hpp
        src/Qord/Video/CameraCapture.cpp
        src/Qord/Video/Camera.hpp
        src/Qord/Video/Camera.cpp
        src/UI/Qord/GoLiveUi.hpp
        src/UI/Qord/GoLiveUi.cpp
        src/UI/Qord/CameraUi.hpp
        src/UI/Qord/CameraUi.cpp)
    target_compile_definitions(${PROJECT_NAME} PRIVATE QORD_HAVE_GO_LIVE)
    if(WIN32)
        target_sources(${PROJECT_NAME} PRIVATE src/Qord/Video/WgcCapture.hpp src/Qord/Video/WgcCapture.cpp)
        target_link_libraries(${PROJECT_NAME} PRIVATE d3d11 dxgi dwmapi windowsapp mf mfplat mfreadwrite mfuuid)
    endif()
endif()

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

    qt_add_executable(TestH264Rtp tests/qord/tst_h264_rtp.cpp src/Qord/Video/H264Rtp.cpp)
    target_link_libraries(TestH264Rtp PRIVATE Qt::Core Qt::Test)
    target_include_directories(TestH264Rtp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    set_target_properties(TestH264Rtp PROPERTIES AUTOMOC ON)
    add_test(NAME QordH264RtpTests COMMAND TestH264Rtp)

    if(TARGET ffmpeg)
        qt_add_executable(TestVideoCodec tests/qord/tst_video_codec.cpp src/Qord/Video/VideoCodec.cpp
            src/Qord/Video/H264Rtp.cpp)
        target_link_libraries(TestVideoCodec PRIVATE Qt::Core Qt::Gui Qt::Test ffmpeg)
        target_include_directories(TestVideoCodec PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        set_target_properties(TestVideoCodec PROPERTIES AUTOMOC ON)
        add_test(NAME QordVideoCodecTests COMMAND TestVideoCodec)

        # Cameras and the sender they share with Go Live
        qt_add_executable(TestCamera tests/qord/tst_camera.cpp src/Qord/Video/CameraCapture.cpp
            src/Qord/Video/VideoSender.cpp src/Qord/Video/VideoCodec.cpp src/Qord/Video/H264Rtp.cpp
            src/Qord/DevMock.cpp src/Core/Logging.cpp)
        target_link_libraries(TestCamera PRIVATE Qt::Core Qt::Gui Qt::Network Qt::Test ffmpeg)
        target_include_directories(TestCamera PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        set_target_properties(TestCamera PROPERTIES AUTOMOC ON)
        if(WIN32)
            target_link_libraries(TestCamera PRIVATE mf mfplat mfreadwrite mfuuid)
        endif()
        add_test(NAME QordCameraTests COMMAND TestCamera)
    endif()
    add_subdirectory(tests/plugins)
endif()
