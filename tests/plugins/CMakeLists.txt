# Qord plugin tests. One Qt Test per plugin (tst_<id>.cpp), registered with
# qord_add_plugin_test(<id>) as ctest "plugin_<id>"; host tests use
# qord_add_host_test(<name>) for tst_host_<name>.cpp.
# tools/check_equicord_plugins.py reads the qord_add_plugin_test() calls.

function(_qord_test target source)
    qt_add_executable(${target} ${source} ${CMAKE_CURRENT_SOURCE_DIR}/PluginTestUtils.hpp)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(${target} PRIVATE
        QORD_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    set_target_properties(${target} PROPERTIES AUTOMOC ON)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8)
    endif()
endfunction()

function(qord_add_plugin_test id)
    _qord_test(TestPlugin_${id} tst_${id}.cpp)
    target_link_libraries(TestPlugin_${id} PRIVATE qord_plugin_${id} Qt::Test)
    add_test(NAME plugin_${id} COMMAND TestPlugin_${id})
endfunction()

function(qord_add_host_test name)
    _qord_test(TestPluginHost_${name} tst_host_${name}.cpp)
    target_link_libraries(TestPluginHost_${name} PRIVATE qord_plugin_host Qt::Test)
    add_test(NAME plugin_host_${name} COMMAND TestPluginHost_${name})
endfunction()

qord_add_host_test(core)
qord_add_host_test(jscompat)

qord_add_plugin_test(clearurls)
qord_add_plugin_test(textreplace)
qord_add_plugin_test(unindent)
qord_add_plugin_test(normalizemessagelinks)
qord_add_plugin_test(noreplymention)
