cmake_minimum_required(VERSION 3.21)
project(LlamaCode VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

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
    src/core/profiles/ProfileTypes.cpp
    src/core/profiles/ProfileManager.cpp
    src/core/profiles/EffectiveProfileBuilder.cpp
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
    src/core/profiles/ProfileTypes.h
    src/core/profiles/ProfileManager.h
    src/core/profiles/EffectiveProfileBuilder.h
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
        qml/pages/AgentPage.qml
        qml/pages/BenchmarkPage.qml
        qml/pages/SettingsPage.qml
)

if (MSVC)
    target_compile_options(LlamaCode PRIVATE /utf-8)
endif()

qt_add_resources(LlamaCode "llamacode_assets"
    PREFIX "/assets"
    FILES
        assets/app_icon.png
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
