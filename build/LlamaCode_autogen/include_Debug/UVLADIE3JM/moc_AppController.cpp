/****************************************************************************
** Meta object code from reading C++ file 'AppController.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../src/AppController.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'AppController.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.8.3. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN13AppControllerE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN13AppControllerE = QtMocHelpers::stringData(
    "AppController",
    "serverRunningChanged",
    "",
    "serverLogChanged",
    "activeLaunchIdChanged",
    "effectiveProfileChanged",
    "setupStateChanged",
    "installingOfficialBinaryChanged",
    "officialBinaryInstallStatusChanged",
    "officialBinaryInstallLogChanged",
    "officialBinaryInstallFinished",
    "success",
    "message",
    "binaryPath",
    "serverError",
    "smokeTestFinished",
    "passed",
    "output",
    "startServer",
    "launchProfileId",
    "stopServer",
    "computeEffectiveProfile",
    "clearLog",
    "copyToClipboard",
    "text",
    "installOfficialBinary",
    "cancelOfficialBinaryInstall",
    "smokeTestServer",
    "smokeTestRunning",
    "resolveFlag",
    "binaryId",
    "flag",
    "version",
    "binaryRegistry",
    "BinaryRegistry*",
    "rootRegistry",
    "ModelRootRegistry*",
    "modelCatalog",
    "ModelCatalog*",
    "profileManager",
    "ProfileManager*",
    "serverRunning",
    "serverLog",
    "activeLaunchId",
    "effectiveProfile",
    "QVariantMap",
    "needsSetup",
    "installingOfficialBinary",
    "officialBinaryInstallStatus",
    "officialBinaryInstallLog"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN13AppControllerE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
      22,   14, // methods
      12,  192, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
      11,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    0,  146,    2, 0x06,   13 /* Public */,
       3,    0,  147,    2, 0x06,   14 /* Public */,
       4,    0,  148,    2, 0x06,   15 /* Public */,
       5,    0,  149,    2, 0x06,   16 /* Public */,
       6,    0,  150,    2, 0x06,   17 /* Public */,
       7,    0,  151,    2, 0x06,   18 /* Public */,
       8,    0,  152,    2, 0x06,   19 /* Public */,
       9,    0,  153,    2, 0x06,   20 /* Public */,
      10,    3,  154,    2, 0x06,   21 /* Public */,
      14,    1,  161,    2, 0x06,   25 /* Public */,
      15,    2,  164,    2, 0x06,   27 /* Public */,

 // methods: name, argc, parameters, tag, flags, initial metatype offsets
      18,    1,  169,    2, 0x02,   30 /* Public */,
      20,    0,  172,    2, 0x02,   32 /* Public */,
      21,    1,  173,    2, 0x02,   33 /* Public */,
      22,    0,  176,    2, 0x02,   35 /* Public */,
      23,    1,  177,    2, 0x02,   36 /* Public */,
      25,    0,  180,    2, 0x02,   38 /* Public */,
      26,    0,  181,    2, 0x02,   39 /* Public */,
      27,    1,  182,    2, 0x02,   40 /* Public */,
      28,    0,  185,    2, 0x102,   42 /* Public | MethodIsConst  */,
      29,    2,  186,    2, 0x102,   43 /* Public | MethodIsConst  */,
      32,    0,  191,    2, 0x102,   46 /* Public | MethodIsConst  */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool, QMetaType::QString, QMetaType::QString,   11,   12,   13,
    QMetaType::Void, QMetaType::QString,   12,
    QMetaType::Void, QMetaType::Bool, QMetaType::QString,   16,   17,

 // methods: parameters
    QMetaType::Void, QMetaType::QString,   19,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   19,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   24,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   19,
    QMetaType::Bool,
    QMetaType::QString, QMetaType::QString, QMetaType::QString,   30,   31,
    QMetaType::QString,

 // properties: name, type, flags, notifyId, revision
      33, 0x80000000 | 34, 0x00015409, uint(-1), 0,
      35, 0x80000000 | 36, 0x00015409, uint(-1), 0,
      37, 0x80000000 | 38, 0x00015409, uint(-1), 0,
      39, 0x80000000 | 40, 0x00015409, uint(-1), 0,
      41, QMetaType::Bool, 0x00015001, uint(0), 0,
      42, QMetaType::QString, 0x00015001, uint(1), 0,
      43, QMetaType::QString, 0x00015001, uint(2), 0,
      44, 0x80000000 | 45, 0x00015009, uint(3), 0,
      46, QMetaType::Bool, 0x00015001, uint(4), 0,
      47, QMetaType::Bool, 0x00015001, uint(5), 0,
      48, QMetaType::QString, 0x00015001, uint(6), 0,
      49, QMetaType::QString, 0x00015001, uint(7), 0,

       0        // eod
};

Q_CONSTINIT const QMetaObject AppController::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_ZN13AppControllerE.offsetsAndSizes,
    qt_meta_data_ZN13AppControllerE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN13AppControllerE_t,
        // property 'binaryRegistry'
        QtPrivate::TypeAndForceComplete<BinaryRegistry*, std::true_type>,
        // property 'rootRegistry'
        QtPrivate::TypeAndForceComplete<ModelRootRegistry*, std::true_type>,
        // property 'modelCatalog'
        QtPrivate::TypeAndForceComplete<ModelCatalog*, std::true_type>,
        // property 'profileManager'
        QtPrivate::TypeAndForceComplete<ProfileManager*, std::true_type>,
        // property 'serverRunning'
        QtPrivate::TypeAndForceComplete<bool, std::true_type>,
        // property 'serverLog'
        QtPrivate::TypeAndForceComplete<QString, std::true_type>,
        // property 'activeLaunchId'
        QtPrivate::TypeAndForceComplete<QString, std::true_type>,
        // property 'effectiveProfile'
        QtPrivate::TypeAndForceComplete<QVariantMap, std::true_type>,
        // property 'needsSetup'
        QtPrivate::TypeAndForceComplete<bool, std::true_type>,
        // property 'installingOfficialBinary'
        QtPrivate::TypeAndForceComplete<bool, std::true_type>,
        // property 'officialBinaryInstallStatus'
        QtPrivate::TypeAndForceComplete<QString, std::true_type>,
        // property 'officialBinaryInstallLog'
        QtPrivate::TypeAndForceComplete<QString, std::true_type>,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<AppController, std::true_type>,
        // method 'serverRunningChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'serverLogChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'activeLaunchIdChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'effectiveProfileChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'setupStateChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'installingOfficialBinaryChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'officialBinaryInstallStatusChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'officialBinaryInstallLogChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'officialBinaryInstallFinished'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'serverError'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'smokeTestFinished'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'startServer'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'stopServer'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'computeEffectiveProfile'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'clearLog'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'copyToClipboard'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'installOfficialBinary'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'cancelOfficialBinaryInstall'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'smokeTestServer'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'smokeTestRunning'
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'resolveFlag'
        QtPrivate::TypeAndForceComplete<QString, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'version'
        QtPrivate::TypeAndForceComplete<QString, std::false_type>
    >,
    nullptr
} };

void AppController::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AppController *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->serverRunningChanged(); break;
        case 1: _t->serverLogChanged(); break;
        case 2: _t->activeLaunchIdChanged(); break;
        case 3: _t->effectiveProfileChanged(); break;
        case 4: _t->setupStateChanged(); break;
        case 5: _t->installingOfficialBinaryChanged(); break;
        case 6: _t->officialBinaryInstallStatusChanged(); break;
        case 7: _t->officialBinaryInstallLogChanged(); break;
        case 8: _t->officialBinaryInstallFinished((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[3]))); break;
        case 9: _t->serverError((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 10: _t->smokeTestFinished((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 11: _t->startServer((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 12: _t->stopServer(); break;
        case 13: _t->computeEffectiveProfile((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 14: _t->clearLog(); break;
        case 15: _t->copyToClipboard((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 16: _t->installOfficialBinary(); break;
        case 17: _t->cancelOfficialBinaryInstall(); break;
        case 18: _t->smokeTestServer((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 19: { bool _r = _t->smokeTestRunning();
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 20: { QString _r = _t->resolveFlag((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast< QString*>(_a[0]) = std::move(_r); }  break;
        case 21: { QString _r = _t->version();
            if (_a[0]) *reinterpret_cast< QString*>(_a[0]) = std::move(_r); }  break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (AppController::*)();
            if (_q_method_type _q_method = &AppController::serverRunningChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _q_method_type = void (AppController::*)();
            if (_q_method_type _q_method = &AppController::serverLogChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _q_method_type = void (AppController::*)();
            if (_q_method_type _q_method = &AppController::activeLaunchIdChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _q_method_type = void (AppController::*)();
            if (_q_method_type _q_method = &AppController::effectiveProfileChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
        {
            using _q_method_type = void (AppController::*)();
            if (_q_method_type _q_method = &AppController::setupStateChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 4;
                return;
            }
        }
        {
            using _q_method_type = void (AppController::*)();
            if (_q_method_type _q_method = &AppController::installingOfficialBinaryChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 5;
                return;
            }
        }
        {
            using _q_method_type = void (AppController::*)();
            if (_q_method_type _q_method = &AppController::officialBinaryInstallStatusChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 6;
                return;
            }
        }
        {
            using _q_method_type = void (AppController::*)();
            if (_q_method_type _q_method = &AppController::officialBinaryInstallLogChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 7;
                return;
            }
        }
        {
            using _q_method_type = void (AppController::*)(bool , const QString & , const QString & );
            if (_q_method_type _q_method = &AppController::officialBinaryInstallFinished; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 8;
                return;
            }
        }
        {
            using _q_method_type = void (AppController::*)(const QString & );
            if (_q_method_type _q_method = &AppController::serverError; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 9;
                return;
            }
        }
        {
            using _q_method_type = void (AppController::*)(bool , const QString & );
            if (_q_method_type _q_method = &AppController::smokeTestFinished; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 10;
                return;
            }
        }
    }
    if (_c == QMetaObject::RegisterPropertyMetaType) {
        switch (_id) {
        default: *reinterpret_cast<int*>(_a[0]) = -1; break;
        case 0:
            *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< BinaryRegistry* >(); break;
        case 2:
            *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< ModelCatalog* >(); break;
        case 1:
            *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< ModelRootRegistry* >(); break;
        case 3:
            *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< ProfileManager* >(); break;
        }
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< BinaryRegistry**>(_v) = _t->binaryRegistry(); break;
        case 1: *reinterpret_cast< ModelRootRegistry**>(_v) = _t->rootRegistry(); break;
        case 2: *reinterpret_cast< ModelCatalog**>(_v) = _t->modelCatalog(); break;
        case 3: *reinterpret_cast< ProfileManager**>(_v) = _t->profileManager(); break;
        case 4: *reinterpret_cast< bool*>(_v) = _t->serverRunning(); break;
        case 5: *reinterpret_cast< QString*>(_v) = _t->serverLog(); break;
        case 6: *reinterpret_cast< QString*>(_v) = _t->activeLaunchId(); break;
        case 7: *reinterpret_cast< QVariantMap*>(_v) = _t->effectiveProfile(); break;
        case 8: *reinterpret_cast< bool*>(_v) = _t->needsSetup(); break;
        case 9: *reinterpret_cast< bool*>(_v) = _t->installingOfficialBinary(); break;
        case 10: *reinterpret_cast< QString*>(_v) = _t->officialBinaryInstallStatus(); break;
        case 11: *reinterpret_cast< QString*>(_v) = _t->officialBinaryInstallLog(); break;
        default: break;
        }
    }
}

const QMetaObject *AppController::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AppController::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN13AppControllerE.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int AppController::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 22)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 22;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 22)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 22;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 12;
    }
    return _id;
}

// SIGNAL 0
void AppController::serverRunningChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void AppController::serverLogChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void AppController::activeLaunchIdChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void AppController::effectiveProfileChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void AppController::setupStateChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}

// SIGNAL 5
void AppController::installingOfficialBinaryChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 5, nullptr);
}

// SIGNAL 6
void AppController::officialBinaryInstallStatusChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 6, nullptr);
}

// SIGNAL 7
void AppController::officialBinaryInstallLogChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 7, nullptr);
}

// SIGNAL 8
void AppController::officialBinaryInstallFinished(bool _t1, const QString & _t2, const QString & _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 8, _a);
}

// SIGNAL 9
void AppController::serverError(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 9, _a);
}

// SIGNAL 10
void AppController::smokeTestFinished(bool _t1, const QString & _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 10, _a);
}
QT_WARNING_POP
