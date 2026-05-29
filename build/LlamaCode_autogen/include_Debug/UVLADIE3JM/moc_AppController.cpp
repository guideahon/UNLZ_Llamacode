/****************************************************************************
** Meta object code from reading C++ file 'AppController.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../src/AppController.h"
#include <QtNetwork/QSslError>
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
    "languageChanged",
    "harnessStatusChanged",
    "harnessInstallFinished",
    "adapter",
    "chatSessionsChanged",
    "chatMessagesChanged",
    "chatGeneratingChanged",
    "agentRunningChanged",
    "agentLogChanged",
    "agentMessagesChanged",
    "agentSessionsChanged",
    "newChatSession",
    "newChatSessionInProject",
    "projectId",
    "projectName",
    "switchChatSession",
    "id",
    "sendChatMessage",
    "text",
    "stopChatGeneration",
    "startServer",
    "launchProfileId",
    "stopServer",
    "computeEffectiveProfile",
    "clearLog",
    "copyToClipboard",
    "installOfficialBinary",
    "cancelOfficialBinaryInstall",
    "smokeTestServer",
    "smokeTestRunning",
    "resolveFlag",
    "binaryId",
    "flag",
    "version",
    "l",
    "key",
    "lf",
    "arg1",
    "readSetting",
    "QVariant",
    "defaultValue",
    "writeSetting",
    "value",
    "isHarnessInstalled",
    "installHarness",
    "startAgent",
    "stopAgent",
    "sendToAgent",
    "clearAgentLog",
    "agentNativeLogDir",
    "openAgentLogDir",
    "newOpencodeSession",
    "switchOpencodeSession",
    "sessionId",
    "refreshOpencodeSessionList",
    "pickDirectory",
    "title",
    "changeAgentProject",
    "directory",
    "binaryRegistry",
    "BinaryRegistry*",
    "rootRegistry",
    "ModelRootRegistry*",
    "modelCatalog",
    "ModelCatalog*",
    "profileManager",
    "ProfileManager*",
    "chatSessions",
    "QVariantList",
    "chatMessages",
    "chatSessionId",
    "chatSessionTitle",
    "chatGenerating",
    "serverRunning",
    "serverStopping",
    "serverLog",
    "activeLaunchId",
    "effectiveProfile",
    "QVariantMap",
    "needsSetup",
    "serverBaseUrl",
    "installingOfficialBinary",
    "officialBinaryInstallStatus",
    "officialBinaryInstallLog",
    "language",
    "langV",
    "agentRunning",
    "agentLog",
    "agentMessages",
    "agentSessions",
    "opencodeSessionId",
    "opencodeSessionTitle",
    "activeAgentAdapter",
    "agentInTerminal",
    "installingHarness",
    "harnessInstallStatus",
    "harnessCheckV"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN13AppControllerE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
      56,   14, // methods
      32,  478, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
      21,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    0,  350,    2, 0x06,   33 /* Public */,
       3,    0,  351,    2, 0x06,   34 /* Public */,
       4,    0,  352,    2, 0x06,   35 /* Public */,
       5,    0,  353,    2, 0x06,   36 /* Public */,
       6,    0,  354,    2, 0x06,   37 /* Public */,
       7,    0,  355,    2, 0x06,   38 /* Public */,
       8,    0,  356,    2, 0x06,   39 /* Public */,
       9,    0,  357,    2, 0x06,   40 /* Public */,
      10,    3,  358,    2, 0x06,   41 /* Public */,
      14,    1,  365,    2, 0x06,   45 /* Public */,
      15,    2,  368,    2, 0x06,   47 /* Public */,
      18,    0,  373,    2, 0x06,   50 /* Public */,
      19,    0,  374,    2, 0x06,   51 /* Public */,
      20,    3,  375,    2, 0x06,   52 /* Public */,
      22,    0,  382,    2, 0x06,   56 /* Public */,
      23,    0,  383,    2, 0x06,   57 /* Public */,
      24,    0,  384,    2, 0x06,   58 /* Public */,
      25,    0,  385,    2, 0x06,   59 /* Public */,
      26,    0,  386,    2, 0x06,   60 /* Public */,
      27,    0,  387,    2, 0x06,   61 /* Public */,
      28,    0,  388,    2, 0x06,   62 /* Public */,

 // methods: name, argc, parameters, tag, flags, initial metatype offsets
      29,    0,  389,    2, 0x02,   63 /* Public */,
      30,    2,  390,    2, 0x02,   64 /* Public */,
      33,    1,  395,    2, 0x02,   67 /* Public */,
      35,    1,  398,    2, 0x02,   69 /* Public */,
      37,    0,  401,    2, 0x02,   71 /* Public */,
      38,    1,  402,    2, 0x02,   72 /* Public */,
      40,    0,  405,    2, 0x02,   74 /* Public */,
      41,    1,  406,    2, 0x02,   75 /* Public */,
      42,    0,  409,    2, 0x02,   77 /* Public */,
      43,    1,  410,    2, 0x02,   78 /* Public */,
      44,    0,  413,    2, 0x02,   80 /* Public */,
      45,    0,  414,    2, 0x02,   81 /* Public */,
      46,    1,  415,    2, 0x02,   82 /* Public */,
      47,    0,  418,    2, 0x102,   84 /* Public | MethodIsConst  */,
      48,    2,  419,    2, 0x102,   85 /* Public | MethodIsConst  */,
      51,    0,  424,    2, 0x102,   88 /* Public | MethodIsConst  */,
      52,    1,  425,    2, 0x102,   89 /* Public | MethodIsConst  */,
      54,    2,  428,    2, 0x102,   91 /* Public | MethodIsConst  */,
      56,    2,  433,    2, 0x102,   94 /* Public | MethodIsConst  */,
      56,    1,  438,    2, 0x122,   97 /* Public | MethodCloned | MethodIsConst  */,
      59,    2,  441,    2, 0x02,   99 /* Public */,
      61,    1,  446,    2, 0x102,  102 /* Public | MethodIsConst  */,
      62,    1,  449,    2, 0x02,  104 /* Public */,
      63,    1,  452,    2, 0x02,  106 /* Public */,
      64,    0,  455,    2, 0x02,  108 /* Public */,
      65,    1,  456,    2, 0x02,  109 /* Public */,
      66,    0,  459,    2, 0x02,  111 /* Public */,
      67,    1,  460,    2, 0x102,  112 /* Public | MethodIsConst  */,
      68,    1,  463,    2, 0x102,  114 /* Public | MethodIsConst  */,
      69,    0,  466,    2, 0x02,  116 /* Public */,
      70,    1,  467,    2, 0x02,  117 /* Public */,
      72,    0,  470,    2, 0x02,  119 /* Public */,
      73,    1,  471,    2, 0x02,  120 /* Public */,
      73,    0,  474,    2, 0x22,  122 /* Public | MethodCloned */,
      75,    1,  475,    2, 0x02,  123 /* Public */,

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
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool, QMetaType::QString, QMetaType::QString,   11,   21,   12,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

 // methods: parameters
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString, QMetaType::QString,   31,   32,
    QMetaType::Void, QMetaType::QString,   34,
    QMetaType::Void, QMetaType::QString,   36,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   39,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   39,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   36,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   39,
    QMetaType::Bool,
    QMetaType::QString, QMetaType::QString, QMetaType::QString,   49,   50,
    QMetaType::QString,
    QMetaType::QString, QMetaType::QString,   53,
    QMetaType::QString, QMetaType::QString, QMetaType::QString,   53,   55,
    0x80000000 | 57, QMetaType::QString, 0x80000000 | 57,   53,   58,
    0x80000000 | 57, QMetaType::QString,   53,
    QMetaType::Void, QMetaType::QString, 0x80000000 | 57,   53,   60,
    QMetaType::Bool, QMetaType::QString,   21,
    QMetaType::Void, QMetaType::QString,   21,
    QMetaType::Void, QMetaType::QString,   39,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   36,
    QMetaType::Void,
    QMetaType::QString, QMetaType::QString,   21,
    QMetaType::Void, QMetaType::QString,   21,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   71,
    QMetaType::Void,
    QMetaType::QString, QMetaType::QString,   74,
    QMetaType::QString,
    QMetaType::Void, QMetaType::QString,   76,

 // properties: name, type, flags, notifyId, revision
      77, 0x80000000 | 78, 0x00015409, uint(-1), 0,
      79, 0x80000000 | 80, 0x00015409, uint(-1), 0,
      81, 0x80000000 | 82, 0x00015409, uint(-1), 0,
      83, 0x80000000 | 84, 0x00015409, uint(-1), 0,
      85, 0x80000000 | 86, 0x00015009, uint(14), 0,
      87, 0x80000000 | 86, 0x00015009, uint(15), 0,
      88, QMetaType::QString, 0x00015001, uint(14), 0,
      89, QMetaType::QString, 0x00015001, uint(14), 0,
      90, QMetaType::Bool, 0x00015001, uint(16), 0,
      91, QMetaType::Bool, 0x00015001, uint(0), 0,
      92, QMetaType::Bool, 0x00015001, uint(0), 0,
      93, QMetaType::QString, 0x00015001, uint(1), 0,
      94, QMetaType::QString, 0x00015001, uint(2), 0,
      95, 0x80000000 | 96, 0x00015009, uint(3), 0,
      97, QMetaType::Bool, 0x00015001, uint(4), 0,
      98, QMetaType::QString, 0x00015001, uint(0), 0,
      99, QMetaType::Bool, 0x00015001, uint(5), 0,
     100, QMetaType::QString, 0x00015001, uint(6), 0,
     101, QMetaType::QString, 0x00015001, uint(7), 0,
     102, QMetaType::QString, 0x00015103, uint(11), 0,
     103, QMetaType::Int, 0x00015001, uint(11), 0,
     104, QMetaType::Bool, 0x00015001, uint(17), 0,
     105, QMetaType::QString, 0x00015001, uint(18), 0,
     106, 0x80000000 | 86, 0x00015009, uint(19), 0,
     107, 0x80000000 | 86, 0x00015009, uint(20), 0,
     108, QMetaType::QString, 0x00015001, uint(20), 0,
     109, QMetaType::QString, 0x00015001, uint(20), 0,
     110, QMetaType::QString, 0x00015001, uint(17), 0,
     111, QMetaType::Bool, 0x00015001, uint(17), 0,
     112, QMetaType::Bool, 0x00015001, uint(12), 0,
     113, QMetaType::QString, 0x00015001, uint(12), 0,
     114, QMetaType::Int, 0x00015001, uint(12), 0,

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
        // property 'chatSessions'
        QtPrivate::TypeAndForceComplete<QVariantList, std::true_type>,
        // property 'chatMessages'
        QtPrivate::TypeAndForceComplete<QVariantList, std::true_type>,
        // property 'chatSessionId'
        QtPrivate::TypeAndForceComplete<QString, std::true_type>,
        // property 'chatSessionTitle'
        QtPrivate::TypeAndForceComplete<QString, std::true_type>,
        // property 'chatGenerating'
        QtPrivate::TypeAndForceComplete<bool, std::true_type>,
        // property 'serverRunning'
        QtPrivate::TypeAndForceComplete<bool, std::true_type>,
        // property 'serverStopping'
        QtPrivate::TypeAndForceComplete<bool, std::true_type>,
        // property 'serverLog'
        QtPrivate::TypeAndForceComplete<QString, std::true_type>,
        // property 'activeLaunchId'
        QtPrivate::TypeAndForceComplete<QString, std::true_type>,
        // property 'effectiveProfile'
        QtPrivate::TypeAndForceComplete<QVariantMap, std::true_type>,
        // property 'needsSetup'
        QtPrivate::TypeAndForceComplete<bool, std::true_type>,
        // property 'serverBaseUrl'
        QtPrivate::TypeAndForceComplete<QString, std::true_type>,
        // property 'installingOfficialBinary'
        QtPrivate::TypeAndForceComplete<bool, std::true_type>,
        // property 'officialBinaryInstallStatus'
        QtPrivate::TypeAndForceComplete<QString, std::true_type>,
        // property 'officialBinaryInstallLog'
        QtPrivate::TypeAndForceComplete<QString, std::true_type>,
        // property 'language'
        QtPrivate::TypeAndForceComplete<QString, std::true_type>,
        // property 'langV'
        QtPrivate::TypeAndForceComplete<int, std::true_type>,
        // property 'agentRunning'
        QtPrivate::TypeAndForceComplete<bool, std::true_type>,
        // property 'agentLog'
        QtPrivate::TypeAndForceComplete<QString, std::true_type>,
        // property 'agentMessages'
        QtPrivate::TypeAndForceComplete<QVariantList, std::true_type>,
        // property 'agentSessions'
        QtPrivate::TypeAndForceComplete<QVariantList, std::true_type>,
        // property 'opencodeSessionId'
        QtPrivate::TypeAndForceComplete<QString, std::true_type>,
        // property 'opencodeSessionTitle'
        QtPrivate::TypeAndForceComplete<QString, std::true_type>,
        // property 'activeAgentAdapter'
        QtPrivate::TypeAndForceComplete<QString, std::true_type>,
        // property 'agentInTerminal'
        QtPrivate::TypeAndForceComplete<bool, std::true_type>,
        // property 'installingHarness'
        QtPrivate::TypeAndForceComplete<bool, std::true_type>,
        // property 'harnessInstallStatus'
        QtPrivate::TypeAndForceComplete<QString, std::true_type>,
        // property 'harnessCheckV'
        QtPrivate::TypeAndForceComplete<int, std::true_type>,
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
        // method 'languageChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'harnessStatusChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'harnessInstallFinished'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'chatSessionsChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'chatMessagesChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'chatGeneratingChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'agentRunningChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'agentLogChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'agentMessagesChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'agentSessionsChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'newChatSession'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'newChatSessionInProject'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'switchChatSession'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'sendChatMessage'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'stopChatGeneration'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
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
        QtPrivate::TypeAndForceComplete<QString, std::false_type>,
        // method 'l'
        QtPrivate::TypeAndForceComplete<QString, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'lf'
        QtPrivate::TypeAndForceComplete<QString, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'readSetting'
        QtPrivate::TypeAndForceComplete<QVariant, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QVariant &, std::false_type>,
        // method 'readSetting'
        QtPrivate::TypeAndForceComplete<QVariant, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'writeSetting'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QVariant &, std::false_type>,
        // method 'isHarnessInstalled'
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'installHarness'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'startAgent'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'stopAgent'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'sendToAgent'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'clearAgentLog'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'agentNativeLogDir'
        QtPrivate::TypeAndForceComplete<QString, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'openAgentLogDir'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'newOpencodeSession'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'switchOpencodeSession'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'refreshOpencodeSessionList'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'pickDirectory'
        QtPrivate::TypeAndForceComplete<QString, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'pickDirectory'
        QtPrivate::TypeAndForceComplete<QString, std::false_type>,
        // method 'changeAgentProject'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>
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
        case 11: _t->languageChanged(); break;
        case 12: _t->harnessStatusChanged(); break;
        case 13: _t->harnessInstallFinished((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[3]))); break;
        case 14: _t->chatSessionsChanged(); break;
        case 15: _t->chatMessagesChanged(); break;
        case 16: _t->chatGeneratingChanged(); break;
        case 17: _t->agentRunningChanged(); break;
        case 18: _t->agentLogChanged(); break;
        case 19: _t->agentMessagesChanged(); break;
        case 20: _t->agentSessionsChanged(); break;
        case 21: _t->newChatSession(); break;
        case 22: _t->newChatSessionInProject((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 23: _t->switchChatSession((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 24: _t->sendChatMessage((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 25: _t->stopChatGeneration(); break;
        case 26: _t->startServer((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 27: _t->stopServer(); break;
        case 28: _t->computeEffectiveProfile((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 29: _t->clearLog(); break;
        case 30: _t->copyToClipboard((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 31: _t->installOfficialBinary(); break;
        case 32: _t->cancelOfficialBinaryInstall(); break;
        case 33: _t->smokeTestServer((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 34: { bool _r = _t->smokeTestRunning();
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 35: { QString _r = _t->resolveFlag((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast< QString*>(_a[0]) = std::move(_r); }  break;
        case 36: { QString _r = _t->version();
            if (_a[0]) *reinterpret_cast< QString*>(_a[0]) = std::move(_r); }  break;
        case 37: { QString _r = _t->l((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast< QString*>(_a[0]) = std::move(_r); }  break;
        case 38: { QString _r = _t->lf((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast< QString*>(_a[0]) = std::move(_r); }  break;
        case 39: { QVariant _r = _t->readSetting((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QVariant>>(_a[2])));
            if (_a[0]) *reinterpret_cast< QVariant*>(_a[0]) = std::move(_r); }  break;
        case 40: { QVariant _r = _t->readSetting((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast< QVariant*>(_a[0]) = std::move(_r); }  break;
        case 41: _t->writeSetting((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QVariant>>(_a[2]))); break;
        case 42: { bool _r = _t->isHarnessInstalled((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 43: _t->installHarness((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 44: _t->startAgent((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 45: _t->stopAgent(); break;
        case 46: _t->sendToAgent((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 47: _t->clearAgentLog(); break;
        case 48: { QString _r = _t->agentNativeLogDir((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast< QString*>(_a[0]) = std::move(_r); }  break;
        case 49: _t->openAgentLogDir((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 50: _t->newOpencodeSession(); break;
        case 51: _t->switchOpencodeSession((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 52: _t->refreshOpencodeSessionList(); break;
        case 53: { QString _r = _t->pickDirectory((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast< QString*>(_a[0]) = std::move(_r); }  break;
        case 54: { QString _r = _t->pickDirectory();
            if (_a[0]) *reinterpret_cast< QString*>(_a[0]) = std::move(_r); }  break;
        case 55: _t->changeAgentProject((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
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
        {
            using _q_method_type = void (AppController::*)();
            if (_q_method_type _q_method = &AppController::languageChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 11;
                return;
            }
        }
        {
            using _q_method_type = void (AppController::*)();
            if (_q_method_type _q_method = &AppController::harnessStatusChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 12;
                return;
            }
        }
        {
            using _q_method_type = void (AppController::*)(bool , const QString & , const QString & );
            if (_q_method_type _q_method = &AppController::harnessInstallFinished; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 13;
                return;
            }
        }
        {
            using _q_method_type = void (AppController::*)();
            if (_q_method_type _q_method = &AppController::chatSessionsChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 14;
                return;
            }
        }
        {
            using _q_method_type = void (AppController::*)();
            if (_q_method_type _q_method = &AppController::chatMessagesChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 15;
                return;
            }
        }
        {
            using _q_method_type = void (AppController::*)();
            if (_q_method_type _q_method = &AppController::chatGeneratingChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 16;
                return;
            }
        }
        {
            using _q_method_type = void (AppController::*)();
            if (_q_method_type _q_method = &AppController::agentRunningChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 17;
                return;
            }
        }
        {
            using _q_method_type = void (AppController::*)();
            if (_q_method_type _q_method = &AppController::agentLogChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 18;
                return;
            }
        }
        {
            using _q_method_type = void (AppController::*)();
            if (_q_method_type _q_method = &AppController::agentMessagesChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 19;
                return;
            }
        }
        {
            using _q_method_type = void (AppController::*)();
            if (_q_method_type _q_method = &AppController::agentSessionsChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 20;
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
        case 4: *reinterpret_cast< QVariantList*>(_v) = _t->chatSessions(); break;
        case 5: *reinterpret_cast< QVariantList*>(_v) = _t->chatMessages(); break;
        case 6: *reinterpret_cast< QString*>(_v) = _t->chatSessionId(); break;
        case 7: *reinterpret_cast< QString*>(_v) = _t->chatSessionTitle(); break;
        case 8: *reinterpret_cast< bool*>(_v) = _t->chatGenerating(); break;
        case 9: *reinterpret_cast< bool*>(_v) = _t->serverRunning(); break;
        case 10: *reinterpret_cast< bool*>(_v) = _t->serverStopping(); break;
        case 11: *reinterpret_cast< QString*>(_v) = _t->serverLog(); break;
        case 12: *reinterpret_cast< QString*>(_v) = _t->activeLaunchId(); break;
        case 13: *reinterpret_cast< QVariantMap*>(_v) = _t->effectiveProfile(); break;
        case 14: *reinterpret_cast< bool*>(_v) = _t->needsSetup(); break;
        case 15: *reinterpret_cast< QString*>(_v) = _t->serverBaseUrl(); break;
        case 16: *reinterpret_cast< bool*>(_v) = _t->installingOfficialBinary(); break;
        case 17: *reinterpret_cast< QString*>(_v) = _t->officialBinaryInstallStatus(); break;
        case 18: *reinterpret_cast< QString*>(_v) = _t->officialBinaryInstallLog(); break;
        case 19: *reinterpret_cast< QString*>(_v) = _t->language(); break;
        case 20: *reinterpret_cast< int*>(_v) = _t->langV(); break;
        case 21: *reinterpret_cast< bool*>(_v) = _t->agentRunning(); break;
        case 22: *reinterpret_cast< QString*>(_v) = _t->agentLog(); break;
        case 23: *reinterpret_cast< QVariantList*>(_v) = _t->agentMessages(); break;
        case 24: *reinterpret_cast< QVariantList*>(_v) = _t->agentSessions(); break;
        case 25: *reinterpret_cast< QString*>(_v) = _t->opencodeSessionId(); break;
        case 26: *reinterpret_cast< QString*>(_v) = _t->opencodeSessionTitle(); break;
        case 27: *reinterpret_cast< QString*>(_v) = _t->activeAgentAdapter(); break;
        case 28: *reinterpret_cast< bool*>(_v) = _t->agentInTerminal(); break;
        case 29: *reinterpret_cast< bool*>(_v) = _t->installingHarness(); break;
        case 30: *reinterpret_cast< QString*>(_v) = _t->harnessInstallStatus(); break;
        case 31: *reinterpret_cast< int*>(_v) = _t->harnessCheckV(); break;
        default: break;
        }
    }
    if (_c == QMetaObject::WriteProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 19: _t->setLanguage(*reinterpret_cast< QString*>(_v)); break;
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
        if (_id < 56)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 56;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 56)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 56;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 32;
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

// SIGNAL 11
void AppController::languageChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 11, nullptr);
}

// SIGNAL 12
void AppController::harnessStatusChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 12, nullptr);
}

// SIGNAL 13
void AppController::harnessInstallFinished(bool _t1, const QString & _t2, const QString & _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 13, _a);
}

// SIGNAL 14
void AppController::chatSessionsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 14, nullptr);
}

// SIGNAL 15
void AppController::chatMessagesChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 15, nullptr);
}

// SIGNAL 16
void AppController::chatGeneratingChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 16, nullptr);
}

// SIGNAL 17
void AppController::agentRunningChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 17, nullptr);
}

// SIGNAL 18
void AppController::agentLogChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 18, nullptr);
}

// SIGNAL 19
void AppController::agentMessagesChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 19, nullptr);
}

// SIGNAL 20
void AppController::agentSessionsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 20, nullptr);
}
QT_WARNING_POP
