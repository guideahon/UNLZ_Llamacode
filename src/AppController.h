#pragma once
#include "core/BinaryRegistry.h"
#include "core/ModelRootRegistry.h"
#include "core/ModelCatalog.h"
#include "core/profiles/ProfileManager.h"
#include "core/profiles/EffectiveProfileBuilder.h"
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>

class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(BinaryRegistry*     binaryRegistry  READ binaryRegistry  CONSTANT)
    Q_PROPERTY(ModelRootRegistry*  rootRegistry    READ rootRegistry    CONSTANT)
    Q_PROPERTY(ModelCatalog*       modelCatalog    READ modelCatalog    CONSTANT)
    Q_PROPERTY(ProfileManager*     profileManager  READ profileManager  CONSTANT)
    Q_PROPERTY(bool   serverRunning   READ serverRunning   NOTIFY serverRunningChanged)
    Q_PROPERTY(QString serverLog      READ serverLog       NOTIFY serverLogChanged)
    Q_PROPERTY(QString activeLaunchId READ activeLaunchId  NOTIFY activeLaunchIdChanged)
    Q_PROPERTY(QVariantMap effectiveProfile READ effectiveProfile NOTIFY effectiveProfileChanged)
    Q_PROPERTY(bool needsSetup READ needsSetup NOTIFY setupStateChanged)
    Q_PROPERTY(bool installingOfficialBinary READ installingOfficialBinary NOTIFY installingOfficialBinaryChanged)
    Q_PROPERTY(QString officialBinaryInstallStatus READ officialBinaryInstallStatus NOTIFY officialBinaryInstallStatusChanged)
    Q_PROPERTY(QString officialBinaryInstallLog READ officialBinaryInstallLog NOTIFY officialBinaryInstallLogChanged)

public:
    explicit AppController(QObject *parent = nullptr);

    BinaryRegistry    *binaryRegistry()  { return &m_binaries; }
    ModelRootRegistry *rootRegistry()    { return &m_roots; }
    ModelCatalog      *modelCatalog()    { return &m_catalog; }
    ProfileManager    *profileManager()  { return &m_profiles; }

    bool   serverRunning()   const { return m_proc && m_proc->state() != QProcess::NotRunning; }
    QString serverLog()      const { return m_log; }
    QString activeLaunchId() const { return m_activeLaunchId; }
    QVariantMap effectiveProfile() const { return m_effectiveProfile; }
    bool needsSetup() const { return m_binaries.count() == 0 && m_catalog.count() == 0; }
    bool installingOfficialBinary() const { return m_installingOfficialBinary; }
    QString officialBinaryInstallStatus() const { return m_officialBinaryInstallStatus; }
    QString officialBinaryInstallLog() const { return m_officialBinaryInstallLog; }

    Q_INVOKABLE void startServer(const QString &launchProfileId);
    Q_INVOKABLE void stopServer();
    Q_INVOKABLE void computeEffectiveProfile(const QString &launchProfileId);
    Q_INVOKABLE void clearLog();
    Q_INVOKABLE void copyToClipboard(const QString &text);
    Q_INVOKABLE void installOfficialBinary();
    Q_INVOKABLE void cancelOfficialBinaryInstall();
    Q_INVOKABLE QString version() const { return QStringLiteral("0.1.0"); }

signals:
    void serverRunningChanged();
    void serverLogChanged();
    void activeLaunchIdChanged();
    void effectiveProfileChanged();
    void setupStateChanged();
    void installingOfficialBinaryChanged();
    void officialBinaryInstallStatusChanged();
    void officialBinaryInstallLogChanged();
    void officialBinaryInstallFinished(bool success, const QString &message, const QString &binaryPath);
    void serverError(const QString &message);

private:
    void appendLog(const QString &text);
    EffectiveProfileBuilder::Context buildContext(const QString &launchProfileId);

    BinaryRegistry    m_binaries;
    ModelCatalog      m_catalog;
    ModelRootRegistry m_roots{&m_catalog};
    ProfileManager    m_profiles;

    QProcess *m_proc = nullptr;
    QProcess *m_installerProc = nullptr;
    QString   m_log;
    QString   m_activeLaunchId;
    QVariantMap m_effectiveProfile;
    bool m_installingOfficialBinary = false;
    QString m_officialBinaryInstallStatus;
    QString m_officialBinaryInstallLog;
    bool m_cancelingOfficialBinaryInstall = false;
    bool m_timeoutOfficialBinaryInstall = false;
    QDateTime m_lastInstallProgressAt;
    QTimer *m_installWatchdog = nullptr;
};
