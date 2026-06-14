cmake_minimum_required(VERSION 3.21)
project(LlamaCode VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(BUILD_TESTS "Build unit tests (LlamaCodeTests)" OFF)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Quick Sql Concurrent Network Widgets)

qt_standard_project_setup(REQUIRES 6.4)

set(CORE_SOURCES
    src/AppController.cpp
    src/ThemeProvider.cpp
    src/core/LlamaBinary.cpp
    src/core/BinaryRegistry.cpp
    src/core/CapabilityDetector.cpp
    src/core/ModelRoot.cpp
    src/core/ModelRootRegistry.cpp
    src/core/CatalogModel.cpp
    src/core/ModelCatalog.cpp
    src/core/GGUFScanner.cpp
    src/core/DocumentExtractor.cpp
    src/core/ControlApi.cpp
    src/core/profiles/ProfileTypes.cpp
    src/core/profiles/ProfileManager.cpp
    src/core/profiles/EffectiveProfileBuilder.cpp
    src/core/agent/OpencodeBackend.cpp
    src/core/agent/RawChatBackend.cpp
    src/core/agent/LlamaAgentBackend.cpp
    src/core/agent/McpClient.cpp
    src/core/agent/AgentToolRunner.cpp
    src/core/agent/SubAgentRunner.cpp
    src/core/agent/MemoryStore.cpp
    src/core/agent/GraphStore.cpp
    src/core/agent/MasterCli.cpp
    src/core/eval/EvalSuite.cpp
    src/core/tuner/AutoTuner.cpp
    src/core/tuner/TunerEngine.cpp
    src/core/tuner/TunerWorker.cpp
)

set(HEADERS
    src/AppController.h
    src/ThemeProvider.h
    src/core/LlamaBinary.h
    src/core/BinaryRegistry.h
    src/core/CapabilityDetector.h
    src/core/ModelRoot.h
    src/core/ModelRootRegistry.h
    src/core/CatalogModel.h
    src/core/ModelCatalog.h
    src/core/GGUFScanner.h
    src/core/DocumentExtractor.h
    src/core/ControlApi.h
    src/core/profiles/ProfileTypes.h
    src/core/profiles/ProfileManager.h
    src/core/profiles/EffectiveProfileBuilder.h
    src/core/agent/AgentTypes.h
    src/core/agent/IAgentBackend.h
    src/core/agent/OpencodeBackend.h
    src/core/agent/RawChatBackend.h
    src/core/agent/LlamaAgentBackend.h
    src/core/agent/McpClient.h
    src/core/agent/AgentToolRunner.h
    src/core/agent/SubAgentRunner.h
    src/core/agent/MemoryStore.h
    src/core/agent/GraphStore.h
    src/core/eval/EvalSuite.h
    src/core/tuner/AutoTuner.h
    src/core/tuner/TunerEngine.h
    src/core/tuner/TunerWorker.h
)

# ── Libreria core compartida (app + tests) ──
# Toda la logica del nucleo vive aca para que los tests linkeen contra los mismos
# objetos que el app, sin recompilar ni mantener listas de fuentes duplicadas.
add_library(llamacode_core STATIC
    ${CORE_SOURCES}
    ${HEADERS}
)
target_include_directories(llamacode_core PUBLIC src)
target_link_libraries(llamacode_core PUBLIC
    Qt6::Core
    Qt6::Quick
    Qt6::Sql
    Qt6::Concurrent
    Qt6::Network
    Qt6::Widgets
)
if (MSVC)
    target_compile_options(llamacode_core PRIVATE /utf-8)
endif()

qt_add_executable(LlamaCode
    src/main.cpp
)
target_link_libraries(LlamaCode PRIVATE llamacode_core)

if (WIN32)
    target_sources(LlamaCode PRIVATE app_icon.rc)
    target_link_libraries(LlamaCode PRIVATE shell32)  # SetCurrentProcessExplicitAppUserModelID
    # Debug builds embed the red debug_icon.ico; Release uses app_icon.ico.
    set_source_files_properties(app_icon.rc PROPERTIES
        COMPILE_OPTIONS "$<$<CONFIG:Debug>:/dLC_DEBUG_ICON>")
endif()

qt_add_qml_module(LlamaCode
    URI LlamaCode
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/pages/BinariesPage.qml
        qml/pages/ModelRootsPage.qml
        qml/pages/ProfilesPage.qml
        qml/pages/LaunchPage.qml
        qml/pages/ChatPage.qml
        qml/components/NavBar.qml
        qml/components/PageHeader.qml
        qml/components/CommandPreview.qml
        qml/components/LcButton.qml
        qml/components/LcComboBox.qml
        qml/components/LcTextField.qml
        qml/components/LcDialog.qml
        qml/components/LcScrollBar.qml
        qml/components/OpencodeConfigDialog.qml
        qml/pages/AgentPage.qml
        qml/pages/BenchmarkPage.qml
        qml/pages/ResearchPage.qml
        qml/pages/SettingsPage.qml
)

if (MSVC)
    target_compile_options(LlamaCode PRIVATE /utf-8)
endif()

qt_add_resources(LlamaCode "llamacode_assets"
    PREFIX "/"
    FILES
        assets/app_icon.png
        assets/app_icon.ico
        assets/debug_icon.ico
        assets/hwfit/hf_models.json
        assets/benchmarks/aa_intelligence.json
)

target_include_directories(LlamaCode PRIVATE src)

target_link_libraries(LlamaCode
    PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::Sql
    Qt6::Concurrent
    Qt6::Network
    Qt6::Widgets
)

set_target_properties(LlamaCode PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

# ── Tests unit + integration ──
# Un executable por subsistema (QtTest = 1 QTEST_MAIN por binario). Cada uno
# linkea la lib core (mismos objetos que el app) + Qt6::Test.
# Para agregar tests de una feature nueva: crear tests/test_<area>.cpp y sumar
# una linea add_lc_test(<area> tests/test_<area>.cpp). Ver CLAUDE.md.
if (BUILD_TESTS)
    enable_testing()
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)

    function(add_lc_test name file)
        qt_add_executable(${name} ${file})
        # Subsistema consola: si no, QtTest no imprime a stdout (output perdido).
        set_target_properties(${name} PROPERTIES WIN32_EXECUTABLE FALSE)
        if (MSVC)
            target_compile_options(${name} PRIVATE /utf-8)
            target_link_options(${name} PRIVATE /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup)
        endif()
        target_include_directories(${name} PRIVATE src tests)
        target_link_libraries(${name} PRIVATE llamacode_core Qt6::Test)
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    add_lc_test(test_gguf_profiles      tests/test_gguf_profiles.cpp)
    add_lc_test(test_profiles           tests/test_profiles.cpp)
    add_lc_test(test_registries         tests/test_registries.cpp)
    add_lc_test(test_catalog            tests/test_catalog.cpp)
    add_lc_test(test_capability         tests/test_capability.cpp)
    add_lc_test(test_document_extractor tests/test_document_extractor.cpp)
    add_lc_test(test_memory_graph       tests/test_memory_graph.cpp)
    add_lc_test(test_tuner              tests/test_tuner.cpp)
    add_lc_test(test_eval               tests/test_eval.cpp)
    add_lc_test(test_control_api        tests/test_control_api.cpp)
    add_lc_test(test_agent_tools        tests/test_agent_tools.cpp)
    add_lc_test(test_master_cli         tests/test_master_cli.cpp)
    add_lc_test(test_backends_net       tests/test_backends_net.cpp)
endif()
