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
    src/Core/LogRedaction.hpp
    src/Core/LogRedaction.cpp
    src/Qord/DevMockAccount.cpp
    src/Qord/ClientExtras.cpp
    src/Core/Markdown/QordRules.cpp
    src/UI/Qord/UserPanel.hpp
    src/UI/Qord/UserPanel.cpp
    src/UI/Qord/Toast.hpp
    src/UI/Qord/Toast.cpp
    src/UI/Qord/FlowLayout.hpp
    src/UI/Qord/FlowLayout.cpp
    src/UI/Qord/DevicesPage.hpp
    src/UI/Qord/DevicesPage.cpp
    src/UI/Qord/NotificationCards.hpp
    src/UI/Qord/NotificationCards.cpp
    src/UI/Qord/ListDialog.hpp
    src/UI/Qord/ListDialog.cpp
    src/UI/Qord/PermissionsView.hpp
    src/UI/Qord/PermissionsView.cpp
    src/UI/Qord/HiddenChannelScreen.hpp
    src/UI/Qord/HiddenChannelScreen.cpp
    src/UI/Qord/TextFileDialog.hpp
    src/UI/Qord/TextFileDialog.cpp
    src/UI/Qord/ServerInfoDialog.hpp
    src/UI/Qord/ServerInfoDialog.cpp
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
    src/Qord/Video/Pacer.hpp
    src/Qord/Video/Pacer.cpp
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
        src/Qord/Audio/StreamAudio.hpp
        src/Qord/Audio/StreamAudio.cpp
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
        target_sources(${PROJECT_NAME} PRIVATE src/Qord/Video/WgcCapture.hpp src/Qord/Video/WgcCapture.cpp
            src/Qord/Audio/WasapiLoopback.hpp src/Qord/Audio/WasapiLoopback.cpp)
        target_link_libraries(${PROJECT_NAME} PRIVATE d3d11 dxgi dwmapi windowsapp mf mfplat mfreadwrite mfuuid mmdevapi)
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

    qt_add_executable(TestQordUi tests/qord/tst_qord_ui.cpp src/UI/Qord/FlowLayout.cpp)
    target_link_libraries(TestQordUi PRIVATE Qt::Core Qt::Gui Qt::Widgets Qt::Test)
    target_include_directories(TestQordUi PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    set_target_properties(TestQordUi PROPERTIES AUTOMOC ON)
    add_test(NAME QordUiTests COMMAND TestQordUi)

    qt_add_executable(TestSlashCommands tests/qord/tst_slash_commands.cpp src/Qord/SlashCommands.cpp)
    target_link_libraries(TestSlashCommands PRIVATE Qt::Core Qt::Test)
    target_include_directories(TestSlashCommands PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    set_target_properties(TestSlashCommands PROPERTIES AUTOMOC ON)
    if(MSVC)
        target_compile_options(TestSlashCommands PRIVATE /utf-8)
    endif()
    add_test(NAME QordSlashCommandTests COMMAND TestSlashCommands)

    qt_add_executable(TestMockTokenStore tests/qord/tst_mock_token_store.cpp
        tests/qord/fake_keychain/qtkeychain/keychain.h src/Core/TokenStore.cpp
        src/Core/Snowflake.cpp src/Qord/DevMock.cpp)
    target_link_libraries(TestMockTokenStore PRIVATE Qt::Core Qt::Network Qt::Test)
    target_include_directories(TestMockTokenStore PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/qord/fake_keychain ${CMAKE_CURRENT_SOURCE_DIR}/src)
    set_target_properties(TestMockTokenStore PROPERTIES AUTOMOC ON)
    add_test(NAME QordMockTokenStoreTests COMMAND TestMockTokenStore)
    set_tests_properties(QordMockTokenStoreTests PROPERTIES TIMEOUT 10)

    qt_add_executable(TestPluginNetworkPolicy tests/qord/tst_network_policy.cpp src/Qord/DevMock.cpp)
    target_link_libraries(TestPluginNetworkPolicy PRIVATE Qt::Core Qt::Network Qt::Test)
    target_include_directories(TestPluginNetworkPolicy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    set_target_properties(TestPluginNetworkPolicy PROPERTIES AUTOMOC ON)
    add_test(NAME QordPluginNetworkPolicyTests COMMAND TestPluginNetworkPolicy)
    set_tests_properties(QordPluginNetworkPolicyTests PROPERTIES TIMEOUT 20)

    qt_add_executable(TestLogRedaction tests/qord/tst_log_redaction.cpp src/Core/LogRedaction.cpp)
    target_link_libraries(TestLogRedaction PRIVATE Qt::Core Qt::Test)
    target_include_directories(TestLogRedaction PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    set_target_properties(TestLogRedaction PROPERTIES AUTOMOC ON)
    add_test(NAME QordLogRedactionTests COMMAND TestLogRedaction)

    find_package(Threads REQUIRED)
    qt_add_executable(TestIngest tests/qord/tst_ingest.cpp src/Discord/IngestThread.cpp)
    target_link_libraries(TestIngest PRIVATE Qt::Core Qt::Test Threads::Threads)
    if(TARGET Qt::ZlibPrivate)
        target_link_libraries(TestIngest PRIVATE Qt::ZlibPrivate)
    else()
        target_link_libraries(TestIngest PRIVATE QtZlibCompat)
    endif()
    target_include_directories(TestIngest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    set_target_properties(TestIngest PROPERTIES AUTOMOC ON)
    add_test(NAME QordIngestTests COMMAND TestIngest)
    set_tests_properties(QordIngestTests PROPERTIES TIMEOUT 30)

    qt_add_executable(TestPacer tests/qord/tst_pacer.cpp src/Qord/Video/Pacer.cpp)
    target_link_libraries(TestPacer PRIVATE Qt::Core Qt::Test)
    target_include_directories(TestPacer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    set_target_properties(TestPacer PROPERTIES AUTOMOC ON)
    add_test(NAME QordPacerTests COMMAND TestPacer)

    qt_add_executable(TestH264Rtp tests/qord/tst_h264_rtp.cpp src/Qord/Video/H264Rtp.cpp)
    target_link_libraries(TestH264Rtp PRIVATE Qt::Core Qt::Test)
    target_include_directories(TestH264Rtp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    set_target_properties(TestH264Rtp PROPERTIES AUTOMOC ON)
    add_test(NAME QordH264RtpTests COMMAND TestH264Rtp)

    if(ENABLE_VOICE)
        qt_add_executable(TestVoiceEncryption tests/qord/tst_voice_encryption.cpp
            src/Discord/Voice/VoiceEncryption.cpp)
        target_link_libraries(TestVoiceEncryption PRIVATE Qt::Core Qt::Test ${SODIUM_TARGET})
        target_include_directories(TestVoiceEncryption PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        set_target_properties(TestVoiceEncryption PROPERTIES AUTOMOC ON)
        add_test(NAME QordVoiceEncryptionTests COMMAND TestVoiceEncryption)
        set_tests_properties(QordVoiceEncryptionTests PROPERTIES TIMEOUT 10)
    endif()

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

        # Go Live's sound: frames and Opus (capture needs a sound server)
        qt_add_executable(TestStreamAudio tests/qord/tst_stream_audio.cpp src/Qord/Audio/StreamAudio.cpp
            src/Core/Audio/OpusEncoder.cpp src/Core/Audio/OpusDecoder.cpp src/Core/Audio/MiniaudioImpl.cpp
            src/Core/Logging.cpp)
        target_link_libraries(TestStreamAudio PRIVATE Qt::Core Qt::Test ${OPUS_TARGET})
        target_include_directories(TestStreamAudio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/vendor/miniaudio)
        set_target_properties(TestStreamAudio PROPERTIES AUTOMOC ON)
        if(WIN32)
            target_sources(TestStreamAudio PRIVATE src/Qord/Audio/WasapiLoopback.cpp)
            target_link_libraries(TestStreamAudio PRIVATE mmdevapi ole32)
        else()
            target_link_libraries(TestStreamAudio PRIVATE ${CMAKE_DL_LIBS} Threads::Threads)
        endif()
        add_test(NAME QordStreamAudioTests COMMAND TestStreamAudio)
    endif()
    add_subdirectory(tests/plugins)
endif()
