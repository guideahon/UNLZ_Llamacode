cmake_minimum_required(VERSION 3.21)
project(LlamaCode VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(BUILD_TESTS "Build unit tests (LlamaCodeTests)" OFF)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Quick Sql Concurrent Network Widgets)

qt_standard_project_setup(REQUIRES 6.4)

set(SOURCES
    src/main.cpp
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
)

qt_add_executable(LlamaCode
    ${SOURCES}
    ${HEADERS}
)

if (WIN32)
    target_sources(LlamaCode PRIVATE app_icon.rc)
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
    PREFIX "/assets"
    FILES
        assets/app_icon.png
        assets/hwfit/hf_models.json
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

# ── Tests unitarios (lógica pura del núcleo) ──
if (BUILD_TESTS)
    enable_testing()
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)

    qt_add_executable(LlamaCodeTests
        tests/test_core.cpp
        src/core/GGUFScanner.cpp
        src/core/CatalogModel.cpp
        src/core/ModelRoot.cpp
        src/core/LlamaBinary.cpp
        src/core/profiles/ProfileTypes.cpp
        src/core/profiles/EffectiveProfileBuilder.cpp
    )
    if (MSVC)
        target_compile_options(LlamaCodeTests PRIVATE /utf-8)
    endif()
    target_include_directories(LlamaCodeTests PRIVATE src)
    target_link_libraries(LlamaCodeTests PRIVATE
        Qt6::Core Qt6::Test)
    add_test(NAME CoreTests COMMAND LlamaCodeTests)
endif()
