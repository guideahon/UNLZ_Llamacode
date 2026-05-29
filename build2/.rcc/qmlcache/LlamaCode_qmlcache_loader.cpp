#include <QtQml/qqmlprivate.h>
#include <QtCore/qdir.h>
#include <QtCore/qurl.h>
#include <QtCore/qhash.h>
#include <QtCore/qstring.h>

namespace QmlCacheGeneratedCode {
namespace _0x5f_LlamaCode_qml_Main_qml { 
    extern const unsigned char qmlData[];
    extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];
    const QQmlPrivate::CachedQmlUnit unit = {
        reinterpret_cast<const QV4::CompiledData::Unit*>(&qmlData), &aotBuiltFunctions[0], nullptr
    };
}
namespace _0x5f_LlamaCode_qml_pages_BinariesPage_qml { 
    extern const unsigned char qmlData[];
    extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];
    const QQmlPrivate::CachedQmlUnit unit = {
        reinterpret_cast<const QV4::CompiledData::Unit*>(&qmlData), &aotBuiltFunctions[0], nullptr
    };
}
namespace _0x5f_LlamaCode_qml_pages_ModelRootsPage_qml { 
    extern const unsigned char qmlData[];
    extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];
    const QQmlPrivate::CachedQmlUnit unit = {
        reinterpret_cast<const QV4::CompiledData::Unit*>(&qmlData), &aotBuiltFunctions[0], nullptr
    };
}
namespace _0x5f_LlamaCode_qml_pages_ProfilesPage_qml { 
    extern const unsigned char qmlData[];
    extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];
    const QQmlPrivate::CachedQmlUnit unit = {
        reinterpret_cast<const QV4::CompiledData::Unit*>(&qmlData), &aotBuiltFunctions[0], nullptr
    };
}
namespace _0x5f_LlamaCode_qml_pages_LaunchPage_qml { 
    extern const unsigned char qmlData[];
    extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];
    const QQmlPrivate::CachedQmlUnit unit = {
        reinterpret_cast<const QV4::CompiledData::Unit*>(&qmlData), &aotBuiltFunctions[0], nullptr
    };
}
namespace _0x5f_LlamaCode_qml_components_NavBar_qml { 
    extern const unsigned char qmlData[];
    extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];
    const QQmlPrivate::CachedQmlUnit unit = {
        reinterpret_cast<const QV4::CompiledData::Unit*>(&qmlData), &aotBuiltFunctions[0], nullptr
    };
}
namespace _0x5f_LlamaCode_qml_components_PageHeader_qml { 
    extern const unsigned char qmlData[];
    extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];
    const QQmlPrivate::CachedQmlUnit unit = {
        reinterpret_cast<const QV4::CompiledData::Unit*>(&qmlData), &aotBuiltFunctions[0], nullptr
    };
}
namespace _0x5f_LlamaCode_qml_components_CommandPreview_qml { 
    extern const unsigned char qmlData[];
    extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];
    const QQmlPrivate::CachedQmlUnit unit = {
        reinterpret_cast<const QV4::CompiledData::Unit*>(&qmlData), &aotBuiltFunctions[0], nullptr
    };
}
namespace _0x5f_LlamaCode_qml_components_LcButton_qml { 
    extern const unsigned char qmlData[];
    extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];
    const QQmlPrivate::CachedQmlUnit unit = {
        reinterpret_cast<const QV4::CompiledData::Unit*>(&qmlData), &aotBuiltFunctions[0], nullptr
    };
}
namespace _0x5f_LlamaCode_qml_components_LcTextField_qml { 
    extern const unsigned char qmlData[];
    extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];
    const QQmlPrivate::CachedQmlUnit unit = {
        reinterpret_cast<const QV4::CompiledData::Unit*>(&qmlData), &aotBuiltFunctions[0], nullptr
    };
}
namespace _0x5f_LlamaCode_qml_components_LcDialog_qml { 
    extern const unsigned char qmlData[];
    extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];
    const QQmlPrivate::CachedQmlUnit unit = {
        reinterpret_cast<const QV4::CompiledData::Unit*>(&qmlData), &aotBuiltFunctions[0], nullptr
    };
}

}
namespace {
struct Registry {
    Registry();
    ~Registry();
    QHash<QString, const QQmlPrivate::CachedQmlUnit*> resourcePathToCachedUnit;
    static const QQmlPrivate::CachedQmlUnit *lookupCachedUnit(const QUrl &url);
};

Q_GLOBAL_STATIC(Registry, unitRegistry)


Registry::Registry() {
    resourcePathToCachedUnit.insert(QStringLiteral("/LlamaCode/qml/Main.qml"), &QmlCacheGeneratedCode::_0x5f_LlamaCode_qml_Main_qml::unit);
    resourcePathToCachedUnit.insert(QStringLiteral("/LlamaCode/qml/pages/BinariesPage.qml"), &QmlCacheGeneratedCode::_0x5f_LlamaCode_qml_pages_BinariesPage_qml::unit);
    resourcePathToCachedUnit.insert(QStringLiteral("/LlamaCode/qml/pages/ModelRootsPage.qml"), &QmlCacheGeneratedCode::_0x5f_LlamaCode_qml_pages_ModelRootsPage_qml::unit);
    resourcePathToCachedUnit.insert(QStringLiteral("/LlamaCode/qml/pages/ProfilesPage.qml"), &QmlCacheGeneratedCode::_0x5f_LlamaCode_qml_pages_ProfilesPage_qml::unit);
    resourcePathToCachedUnit.insert(QStringLiteral("/LlamaCode/qml/pages/LaunchPage.qml"), &QmlCacheGeneratedCode::_0x5f_LlamaCode_qml_pages_LaunchPage_qml::unit);
    resourcePathToCachedUnit.insert(QStringLiteral("/LlamaCode/qml/components/NavBar.qml"), &QmlCacheGeneratedCode::_0x5f_LlamaCode_qml_components_NavBar_qml::unit);
    resourcePathToCachedUnit.insert(QStringLiteral("/LlamaCode/qml/components/PageHeader.qml"), &QmlCacheGeneratedCode::_0x5f_LlamaCode_qml_components_PageHeader_qml::unit);
    resourcePathToCachedUnit.insert(QStringLiteral("/LlamaCode/qml/components/CommandPreview.qml"), &QmlCacheGeneratedCode::_0x5f_LlamaCode_qml_components_CommandPreview_qml::unit);
    resourcePathToCachedUnit.insert(QStringLiteral("/LlamaCode/qml/components/LcButton.qml"), &QmlCacheGeneratedCode::_0x5f_LlamaCode_qml_components_LcButton_qml::unit);
    resourcePathToCachedUnit.insert(QStringLiteral("/LlamaCode/qml/components/LcTextField.qml"), &QmlCacheGeneratedCode::_0x5f_LlamaCode_qml_components_LcTextField_qml::unit);
    resourcePathToCachedUnit.insert(QStringLiteral("/LlamaCode/qml/components/LcDialog.qml"), &QmlCacheGeneratedCode::_0x5f_LlamaCode_qml_components_LcDialog_qml::unit);
    QQmlPrivate::RegisterQmlUnitCacheHook registration;
    registration.structVersion = 0;
    registration.lookupCachedQmlUnit = &lookupCachedUnit;
    QQmlPrivate::qmlregister(QQmlPrivate::QmlUnitCacheHookRegistration, &registration);
}

Registry::~Registry() {
    QQmlPrivate::qmlunregister(QQmlPrivate::QmlUnitCacheHookRegistration, quintptr(&lookupCachedUnit));
}

const QQmlPrivate::CachedQmlUnit *Registry::lookupCachedUnit(const QUrl &url) {
    if (url.scheme() != QLatin1String("qrc"))
        return nullptr;
    QString resourcePath = QDir::cleanPath(url.path());
    if (resourcePath.isEmpty())
        return nullptr;
    if (!resourcePath.startsWith(QLatin1Char('/')))
        resourcePath.prepend(QLatin1Char('/'));
    return unitRegistry()->resourcePathToCachedUnit.value(resourcePath, nullptr);
}
}
int QT_MANGLE_NAMESPACE(qInitResources_qmlcache_LlamaCode)() {
    ::unitRegistry();
    return 1;
}
Q_CONSTRUCTOR_FUNCTION(QT_MANGLE_NAMESPACE(qInitResources_qmlcache_LlamaCode))
int QT_MANGLE_NAMESPACE(qCleanupResources_qmlcache_LlamaCode)() {
    return 1;
}
