#include "AppController.h"
#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  define VC_EXTRA_LEAN
#  include <windows.h>
#else
#  include <signal.h>
#endif
#include <QProcess>
#include <QDateTime>
#include <QClipboard>
#include <QGuiApplication>
#include <QApplication>
#include <QWidget>
#include <QImage>
#include <QClipboard>
#include "core/agent/OpencodeBackend.h"
#include "core/agent/RawChatBackend.h"
#include "core/agent/LlamaAgentBackend.h"
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QDesktopServices>
#include <QFileDialog>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QCoreApplication>
#include <QUuid>
#include <functional>
#include <QSet>
#include <memory>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

AppController::AppController(QObject *parent) : QObject(parent)
{
    QSettings s;
    m_language = s.value(QStringLiteral("language"), QStringLiteral("es")).toString();
    m_agentApprovalMode = s.value(QStringLiteral("agent/approvalMode"), QStringLiteral("ask")).toString();
    m_agentThinkingEnabled = s.value(QStringLiteral("agent/thinkingEnabled"), false).toBool();
    m_agentSystemPrompt = s.value(QStringLiteral("agent/systemPrompt")).toString();
    m_agentTemperature  = s.value(QStringLiteral("agent/temperature"), -1.0).toDouble();

    killManagedOrphans();
    createJobObject();
    ensureChatBackend();
    const QString logDir = runtimeLogDir();
    m_serverLogFilePath = logDir + QStringLiteral("/server.log");
    m_agentLogFilePath = logDir + QStringLiteral("/agent.log");
    appendFileLog(m_serverLogFilePath, QStringLiteral("\n=== session %1 ===\n")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs)));
    appendFileLog(m_agentLogFilePath, QStringLiteral("\n=== session %1 ===\n")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs)));

    m_binaries.refresh();
    m_roots.refresh();
    connect(&m_binaries, &BinaryRegistry::countChanged, this, &AppController::setupStateChanged);
    connect(&m_catalog, &ModelCatalog::countChanged, this, &AppController::setupStateChanged);

    // If folders are registered but no models were found yet (e.g. a root added
    // in "manual" mode), scan them once on startup so the catalog is populated.
    if (m_roots.count() > 0 && m_catalog.count() == 0)
        m_roots.scanAll();
}

void AppController::startServer(const QString &launchProfileId)
{
    if (serverRunning()) {
        emit serverError("Server already running. Stop it first.");
        return;
    }

    // Repoblar flags soportados del binario justo antes de armar el perfil.
    // Si el registro quedó con flags stale/parciales, EffectiveProfileBuilder::addFlag
    // dropea flags de runtime (--flash-attn/--mlock/--cache-type-k/...) → server roto.
    {
        const auto launch  = m_profiles.resolveLaunch(launchProfileId);
        const auto backend = m_profiles.resolveBackend(launch.backendProfileId);
        if (!backend.binaryId.isEmpty()) {
            if (!m_binaries.detectCapabilitiesSync(backend.binaryId))
                appendServerEvent(QStringLiteral("lifecycle"),
                                  QStringLiteral("Aviso: no se pudieron redetectar flags del binario; uso los guardados."));
        }
    }

    computeEffectiveProfile(launchProfileId);

    if (!m_effectiveProfile.value("isValid", false).toBool()) {
        const QStringList errors = m_effectiveProfile.value("blockingErrors").toStringList();
        emit serverError("Cannot start: " + errors.join("; "));
        return;
    }

    const QString binaryPath = m_effectiveProfile["binaryPath"].toString();
    const QStringList args = m_effectiveProfile["effectiveArgs"].toStringList();
    const QVariantMap envMap = m_effectiveProfile["effectiveEnv"].toMap();

    m_proc = new QProcess(this);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (auto it = envMap.begin(); it != envMap.end(); ++it)
        env.insert(it.key(), it.value().toString());
    env.insert(QStringLiteral("LLAMACODE_MANAGED"), QStringLiteral("1"));
    env.insert(QStringLiteral("LLAMACODE_ROLE"),    QStringLiteral("server"));
    env.insert(QStringLiteral("LLAMACODE_APP_PID"), QString::number(QCoreApplication::applicationPid()));
    m_proc->setProcessEnvironment(env);

    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this]() {
        appendServerEvent(QStringLiteral("stdout"), QString::fromUtf8(m_proc->readAllStandardOutput()));
    });
    connect(m_proc, &QProcess::readyReadStandardError, this, [this]() {
        appendServerEvent(QStringLiteral("stderr"), QString::fromUtf8(m_proc->readAllStandardError()));
    });
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
        appendServerEvent(QStringLiteral("lifecycle"),
                          QStringLiteral("Server exited with code %1").arg(code));
        if (code == -1073740791) {
            emit serverError(QStringLiteral(
                "llama-server crasheó (0xC0000409). Revisá el perfil de lanzamiento activo (args/runtime)."));
        }
        clearServiceState(QStringLiteral("server"));
        if (m_stopKillTimer) { m_stopKillTimer->stop(); m_stopKillTimer->deleteLater(); m_stopKillTimer = nullptr; }
        stopHealthPolling();
        m_serverStopping = false;
        m_serverReady    = false;
        m_proc->deleteLater();
        m_proc = nullptr;
        emit serverRunningChanged();
        emit serverReadyChanged();
    });

    m_activeLaunchId = launchProfileId;
    writeSetting(QStringLiteral("lastLaunchId"), launchProfileId);  // recordar último usado
    appendServerEvent(QStringLiteral("lifecycle"),
                      QStringLiteral("Starting: %1 %2").arg(binaryPath, args.join(' ')));

    m_serverReady = false;
    m_proc->start(binaryPath, args);
    if (m_proc->waitForStarted(5000)) {
        assignToJobObject(m_proc->processId());
        const int portIdx = args.indexOf(QStringLiteral("--port"));
        QVariantMap extra;
        extra[QStringLiteral("launch_id")] = launchProfileId;
        extra[QStringLiteral("port")] = (portIdx >= 0 && portIdx + 1 < args.size())
                                            ? args[portIdx + 1].toInt() : 8080;
        writeServiceState(QStringLiteral("server"), m_proc->processId(), extra);
        startHealthPolling();
    }
    emit serverRunningChanged();
    emit serverReadyChanged();
    emit activeLaunchIdChanged();
}

void AppController::startHealthPolling()
{
    stopHealthPolling();
    m_healthPollTimer = new QTimer(this);
    m_healthPollTimer->setInterval(2000);
    connect(m_healthPollTimer, &QTimer::timeout, this, [this]() {
        if (!serverRunning()) { stopHealthPolling(); return; }
        if (!m_nam) m_nam = new QNetworkAccessManager(this);
        auto *reply = m_nam->get(QNetworkRequest(QUrl(serverBaseUrl() + "/health")));
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const bool ok = reply->error() == QNetworkReply::NoError && status == 200;
            reply->deleteLater();
            if (!ok) {
                appendServerEvent(QStringLiteral("health"),
                                  QStringLiteral("health fail status=%1 error=%2")
                                      .arg(status).arg(reply->errorString()));
            }
            if (ok && !m_serverReady) {
                m_serverReady = true;
                appendServerEvent(QStringLiteral("health"), QStringLiteral("Server ready - model loaded"));
                emit serverReadyChanged();
                stopHealthPolling();
                fetchChatThinkingSupport();
            }
        });
    });
    m_healthPollTimer->start();
}

void AppController::fetchChatThinkingSupport()
{
    if (!m_nam) m_nam = new QNetworkAccessManager(this);
    auto *reply = m_nam->get(QNetworkRequest(QUrl(serverBaseUrl() + "/props")));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        bool supported = false;
        if (reply->error() == QNetworkReply::NoError) {
            const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
            const QJsonObject params = root.value(QStringLiteral("default_generation_settings"))
                                           .toObject().value(QStringLiteral("params")).toObject();
            const QString chatFormat = params.value(QStringLiteral("chat_format")).toString();
            // Content-only = sin template (jinja) → no soporta thinking.
            supported = !chatFormat.isEmpty()
                        && chatFormat.compare(QStringLiteral("Content-only"), Qt::CaseInsensitive) != 0;
        }
        if (supported != m_chatThinkingSupported) {
            m_chatThinkingSupported = supported;
            emit chatThinkingSupportedChanged();
        }
    });
}

void AppController::stopHealthPolling()
{
    if (m_healthPollTimer) {
        m_healthPollTimer->stop();
        m_healthPollTimer->deleteLater();
        m_healthPollTimer = nullptr;
    }
}

void AppController::stopServer()
{
    if (!m_proc || m_serverStopping) return;
    stopHealthPolling();
    m_serverReady    = false;
    m_serverStopping = true;
    if (m_chatThinkingSupported) { m_chatThinkingSupported = false; emit chatThinkingSupportedChanged(); }
    emit serverReadyChanged();
    emit serverRunningChanged();
    appendServerEvent(QStringLiteral("lifecycle"), QStringLiteral("Stopping server..."));
    m_proc->terminate();
    // Kill after 5s if process hasn't exited
    m_stopKillTimer = new QTimer(this);
    m_stopKillTimer->setSingleShot(true);
    connect(m_stopKillTimer, &QTimer::timeout, this, [this]() {
        if (m_proc) m_proc->kill();
        m_stopKillTimer->deleteLater();
        m_stopKillTimer = nullptr;
    });
    m_stopKillTimer->start(5000);
}

void AppController::computeEffectiveProfile(const QString &launchProfileId)
{
    const auto ctx = buildContext(launchProfileId);
    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);

    m_effectiveProfile.clear();
    m_effectiveProfile["launchId"] = launchProfileId;
    m_effectiveProfile["isValid"] = ep.isValid();
    m_effectiveProfile["binaryPath"] = ep.binaryPath;
    m_effectiveProfile["commandLine"] = ep.commandLine;
    m_effectiveProfile["effectiveArgs"] = ep.effectiveArgs;
    m_effectiveProfile["warnings"] = ep.warnings;
    m_effectiveProfile["blockingErrors"] = ep.blockingErrors;

    QVariantMap envMap;
    for (auto it = ep.effectiveEnv.begin(); it != ep.effectiveEnv.end(); ++it)
        envMap[it.key()] = it.value();
    m_effectiveProfile["effectiveEnv"] = envMap;

    emit effectiveProfileChanged();
}

void AppController::computeEffectiveProfilePreview(const QString &launchProfileId,
                                                   const QVariantMap &overrides)
{
    EffectiveProfileBuilder::Context ctx;

    ctx.backend.host = overrides.value("host", "127.0.0.1").toString();
    ctx.backend.port = overrides.value("port", 8080).toInt();
    const QString binaryId = overrides.value("binaryId").toString();
    ctx.backend.binaryId = binaryId;
    ctx.binary = m_binaries.findById(binaryId);

    ctx.model.modelId    = overrides.value("modelId").toString();
    ctx.model.mmprojId   = overrides.value("mmprojId").toString();
    ctx.model.draftModelId = overrides.value("draftModelId").toString();
    ctx.catalogModel = m_catalog.findById(ctx.model.modelId);
    ctx.mmprojModel  = m_catalog.findById(ctx.model.mmprojId);
    ctx.draftModel   = m_catalog.findById(ctx.model.draftModelId);

    ctx.runtime.ctx           = overrides.value("ctx", 4096).toInt();
    ctx.runtime.batch         = overrides.value("batch", 512).toInt();
    ctx.runtime.ubatch        = overrides.value("ubatch", 512).toInt();
    ctx.runtime.threads       = overrides.value("threads", -1).toInt();
    ctx.runtime.gpuLayers     = overrides.value("gpuLayers", -1).toInt();
    ctx.runtime.flashAttention= overrides.value("flashAttention", false).toBool();
    ctx.runtime.mmap          = overrides.value("mmap", true).toBool();
    ctx.runtime.mlock         = overrides.value("mlock", false).toBool();
    ctx.runtime.contBatching  = overrides.value("contBatching", true).toBool();
    ctx.runtime.cacheType     = overrides.value("cacheType", "f16").toString();
    ctx.runtime.parallelSlots = overrides.value("parallelSlots", 1).toInt();

    ctx.launch.extraArgs = overrides.value("extraArgs").toStringList();

    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);

    m_effectiveProfile.clear();
    m_effectiveProfile["launchId"] = launchProfileId;
    m_effectiveProfile["isValid"] = ep.isValid();
    m_effectiveProfile["binaryPath"] = ep.binaryPath;
    m_effectiveProfile["commandLine"] = ep.commandLine;
    m_effectiveProfile["effectiveArgs"] = ep.effectiveArgs;
    m_effectiveProfile["warnings"] = ep.warnings;
    m_effectiveProfile["blockingErrors"] = ep.blockingErrors;

    QVariantMap envMap;
    for (auto it = ep.effectiveEnv.begin(); it != ep.effectiveEnv.end(); ++it)
        envMap[it.key()] = it.value();
    m_effectiveProfile["effectiveEnv"] = envMap;

    emit effectiveProfileChanged();
}

void AppController::clearLog()
{
    m_log.clear();
    emit serverLogChanged();
}

void AppController::copyToClipboard(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

void AppController::openContainingFolder(const QString &path)
{
    if (path.trimmed().isEmpty()) return;
    const QFileInfo fi(path);
    const QString dir = fi.absolutePath();
    if (dir.isEmpty()) return;

#ifdef Q_OS_WIN
    // explorer /select,<archivo> abre la carpeta y selecciona el archivo.
    if (fi.exists()) {
        const QString nativeFile = QDir::toNativeSeparators(fi.absoluteFilePath());
        if (QProcess::startDetached(QStringLiteral("explorer.exe"),
                                    {QStringLiteral("/select,") + nativeFile}))
            return;
    }
#endif

    bool ok = QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
#ifdef Q_OS_WIN
    if (!ok)
        ok = QProcess::startDetached(QStringLiteral("explorer.exe"),
                                     {QDir::toNativeSeparators(dir)});
#endif
    if (!ok)
        emit serverError(QStringLiteral("No se pudo abrir la carpeta: %1")
                             .arg(QDir::toNativeSeparators(dir)));
}

void AppController::installOfficialBinary()
{
    if (m_installingOfficialBinary || m_installerProc) {
        emit serverError("Binary install already in progress.");
        return;
    }

    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString toolsDir = appData + "/tools";
    QDir().mkpath(toolsDir);

    const QString script = QStringLiteral(R"PS(
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
try {
    $headers = @{ 'User-Agent' = 'LlamaCode' }
    Write-Output 'STATUS: Consultando release latest de llama.cpp...'
    $api = 'https://api.github.com/repos/ggml-org/llama.cpp/releases/latest'
    $rel = Invoke-RestMethod -Uri $api -Headers $headers
    $assets = @($rel.assets)
    $pick = $assets | Where-Object { $_.name -match 'bin-win-cuda.*x64.*\.zip$' -and $_.name -notmatch '^cudart-' } | Select-Object -First 1
    if (-not $pick) { $pick = $assets | Where-Object { $_.name -match 'bin-win-(avx2|cpu|openblas).*x64.*\.zip$' -and $_.name -notmatch '^cudart-' } | Select-Object -First 1 }
    if (-not $pick) { $pick = $assets | Where-Object { $_.name -match 'win.*x64.*\.zip$' -and $_.name -notmatch '^cudart-' } | Select-Object -First 1 }
    if (-not $pick) { throw 'No suitable Windows x64 binary asset found in latest release.' }

    Write-Output ('STATUS: Descargando asset ' + $pick.name + ' ...')
    $dest = '%1'
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    $runId = [DateTime]::UtcNow.ToString('yyyyMMddHHmmssfff')
    $runDir = Join-Path $dest ('llama.cpp-install-' + $runId)
    New-Item -ItemType Directory -Force -Path $runDir | Out-Null
    $zip = Join-Path $runDir $pick.name
    $extract = Join-Path $runDir 'extract'
    if (Test-Path $extract) { Remove-Item -LiteralPath $extract -Recurse -Force -ErrorAction SilentlyContinue }
    try {
        Invoke-WebRequest -Uri $pick.browser_download_url -Headers $headers -OutFile $zip
    } catch {
        Start-Sleep -Milliseconds 500
        Invoke-WebRequest -Uri $pick.browser_download_url -Headers $headers -OutFile $zip
    }
    Write-Output 'STATUS: Extrayendo binarios...'
    Expand-Archive -LiteralPath $zip -DestinationPath $extract -Force
    $exe = Get-ChildItem -Path $extract -Recurse -Filter 'llama-server.exe' | Select-Object -First 1
    if (-not $exe) { throw 'llama-server.exe not found after extraction.' }
    Write-Output 'STATUS: Registrando binario en LlamaCode...'
    Write-Output $exe.FullName
} catch {
    Write-Output ('ERROR: ' + $_.Exception.Message)
    exit 1
}
)PS").arg(QDir::toNativeSeparators(toolsDir).replace("'", "''"));

    m_installerProc = new QProcess(this);
    m_installingOfficialBinary = true;
    m_cancelingOfficialBinaryInstall = false;
    m_timeoutOfficialBinaryInstall = false;
    m_lastInstallProgressAt = QDateTime::currentDateTimeUtc();
    m_officialBinaryInstallStatus = "Iniciando instalación...";
    m_officialBinaryInstallLog.clear();
    emit installingOfficialBinaryChanged();
    emit officialBinaryInstallStatusChanged();
    emit officialBinaryInstallLogChanged();

    if (!m_installWatchdog) {
        m_installWatchdog = new QTimer(this);
        m_installWatchdog->setInterval(5000);
        connect(m_installWatchdog, &QTimer::timeout, this, [this]() {
            if (!m_installingOfficialBinary || !m_installerProc)
                return;
            if (m_lastInstallProgressAt.secsTo(QDateTime::currentDateTimeUtc()) > 900) {
                m_timeoutOfficialBinaryInstall = true;
                m_officialBinaryInstallStatus = "Sin avance por 15 minutos. Cancelando instalación...";
                emit officialBinaryInstallStatusChanged();
                m_installerProc->kill();
            }
        });
    }
    m_installWatchdog->start();

    connect(m_installerProc, &QProcess::readyReadStandardOutput, this, [this]() {
        const QString chunk = QString::fromUtf8(m_installerProc->readAllStandardOutput());
        if (!chunk.trimmed().isEmpty())
            m_lastInstallProgressAt = QDateTime::currentDateTimeUtc();
        if (!chunk.isEmpty()) {
            m_officialBinaryInstallLog.append(chunk);
            emit officialBinaryInstallLogChanged();
        }
        const QStringList lines = chunk.split('\n', Qt::SkipEmptyParts);
        for (const QString &rawLine : lines) {
            const QString line = rawLine.trimmed();
            if (line.startsWith("STATUS: ")) {
                m_officialBinaryInstallStatus = line.mid(8).trimmed();
                emit officialBinaryInstallStatusChanged();
            }
        }
    });
    connect(m_installerProc, &QProcess::readyReadStandardError, this, [this]() {
        const QString chunk = QString::fromUtf8(m_installerProc->readAllStandardError());
        if (!chunk.trimmed().isEmpty())
            m_lastInstallProgressAt = QDateTime::currentDateTimeUtc();
        if (!chunk.isEmpty()) {
            m_officialBinaryInstallLog.append(chunk);
            emit officialBinaryInstallLogChanged();
        }
    });

    connect(m_installerProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus) {
        // stdout was already consumed by readyReadStandardOutput into m_officialBinaryInstallLog
        const QString stdOut = m_officialBinaryInstallLog;
        const QString stdErr = QString::fromUtf8(m_installerProc->readAllStandardError()).trimmed();

        bool ok = false;
        QString installedPath;
        QString message;
        if (exitCode == 0) {
            // Last non-empty line that isn't a STATUS/ERROR prefix is the exe path
            const QStringList lines = stdOut.split('\n', Qt::SkipEmptyParts);
            for (int i = lines.size() - 1; i >= 0; --i) {
                const QString t = lines[i].trimmed();
                if (!t.startsWith("STATUS:") && !t.startsWith("ERROR:") && !t.isEmpty()) {
                    installedPath = t;
                    break;
                }
            }
            if (QFileInfo::exists(installedPath)) {
                const QString id = m_binaries.add(installedPath, "llama-server (official latest)", "official", "cuda", "latest");
                if (!id.isEmpty()) {
                    ok = true;
                    message = "Official llama.cpp binary installed and registered.";
                }
            }
        }
        if (!ok) {
            if (m_cancelingOfficialBinaryInstall) {
                message = "Automatic install canceled by user.";
            } else if (m_timeoutOfficialBinaryInstall) {
                message = "Automatic install timed out (no progress for 15 minutes).";
            } else {
                message = "Automatic install failed.";
                const QStringList outLines = stdOut.split('\n', Qt::SkipEmptyParts);
                for (const QString &line : outLines) {
                    const QString t = line.trimmed();
                    if (t.startsWith("ERROR: ")) {
                        message += " " + t.mid(7).trimmed();
                        break;
                    }
                }
                if (!stdErr.isEmpty()) {
                    QString cleaned = stdErr;
                    const QStringList errLines = stdErr.split('\n', Qt::SkipEmptyParts);
                    if (!errLines.isEmpty()) {
                        cleaned = errLines.first().trimmed();
                        cleaned.remove(QRegularExpression("^\\s*\\+\\s*"));
                    }
                    message += " " + cleaned;
                }
            }
        }

        m_installerProc->deleteLater();
        m_installerProc = nullptr;
        if (m_installWatchdog)
            m_installWatchdog->stop();
        m_installingOfficialBinary = false;
        m_officialBinaryInstallStatus = ok ? "Instalación completada." : message;
        if (!ok && m_officialBinaryInstallLog.trimmed().isEmpty()) {
            m_officialBinaryInstallLog = message + "\n";
            emit officialBinaryInstallLogChanged();
        }
        m_cancelingOfficialBinaryInstall = false;
        m_timeoutOfficialBinaryInstall = false;
        emit installingOfficialBinaryChanged();
        emit officialBinaryInstallStatusChanged();
        emit setupStateChanged();
        emit officialBinaryInstallFinished(ok, message, installedPath);
    });

    m_installerProc->start("powershell", {"-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script});
}

void AppController::cancelOfficialBinaryInstall()
{
    if (!m_installerProc || !m_installingOfficialBinary)
        return;
    m_cancelingOfficialBinaryInstall = true;
    m_installerProc->kill();
    m_officialBinaryInstallStatus = "Instalación cancelada.";
    emit officialBinaryInstallStatusChanged();
}

QString AppController::resolveFlag(const QString &binaryId, const QString &flag) const
{
    const LlamaBinary bin = m_binaries.findById(binaryId);
    return bin.resolveFlag(flag);
}

void AppController::smokeTestServer(const QString &launchProfileId)
{
    if (m_smokeTestProc) return;

    computeEffectiveProfile(launchProfileId);
    if (!m_effectiveProfile.value("isValid", false).toBool()) {
        const QStringList errors = m_effectiveProfile.value("blockingErrors").toStringList();
        emit smokeTestFinished(false, "Perfil inválido: " + errors.join("; "));
        return;
    }

    const QString binaryPath = m_effectiveProfile["binaryPath"].toString();
    const QStringList args   = m_effectiveProfile["effectiveArgs"].toStringList();

    m_smokeTestProc = new QProcess(this);
    m_smokeTestLog.clear();
    m_smokeTestDone = false;

    connect(m_smokeTestProc, &QProcess::readyReadStandardOutput, this, [this]() {
        m_smokeTestLog += QString::fromUtf8(m_smokeTestProc->readAllStandardOutput());
    });
    connect(m_smokeTestProc, &QProcess::readyReadStandardError, this, [this]() {
        m_smokeTestLog += QString::fromUtf8(m_smokeTestProc->readAllStandardError());
    });
    connect(m_smokeTestProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
        m_smokeTestProc->deleteLater();
        m_smokeTestProc = nullptr;
        if (m_smokeTestTimer) m_smokeTestTimer->stop();
        finishSmokeTest(false,
            m_smokeTestLog.trimmed().isEmpty()
                ? QStringLiteral("Proceso terminó con código %1").arg(code)
                : m_smokeTestLog.trimmed());
    });

    m_smokeTestProc->start(binaryPath, args);

    if (!m_smokeTestTimer) {
        m_smokeTestTimer = new QTimer(this);
        m_smokeTestTimer->setSingleShot(true);
        connect(m_smokeTestTimer, &QTimer::timeout, this, [this]() {
            if (!m_smokeTestProc) return;
            QProcess *proc = m_smokeTestProc;
            m_smokeTestProc = nullptr;
            proc->disconnect(this);
            proc->terminate();
            proc->deleteLater();
            finishSmokeTest(true, "Servidor arrancó correctamente.");
        });
    }
    m_smokeTestTimer->start(5000);
}

void AppController::finishSmokeTest(bool passed, const QString &output)
{
    if (m_smokeTestDone) return;
    m_smokeTestDone = true;
    emit smokeTestFinished(passed, output);
}

void AppController::appendLog(const QString &text)
{
    m_log.append(text);
    // Keep log under 200KB
    if (m_log.size() > 200000)
        m_log = m_log.right(180000);
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    appendFileLog(m_serverLogFilePath, QStringLiteral("[%1][server/ui] %2").arg(ts, text));
    if (!text.endsWith(QLatin1Char('\n')))
        appendFileLog(m_serverLogFilePath, QStringLiteral("\n"));
    emit serverLogChanged();
}

QString AppController::runtimeLogDir() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/logs");
    QDir().mkpath(dir);
    return dir;
}

void AppController::rotateLogIfNeeded(const QString &path) const
{
    QFile f(path);
    if (!f.exists()) return;
    if (f.size() < 2 * 1024 * 1024) return;
    const QString backup = path + QStringLiteral(".1");
    QFile::remove(backup);
    QFile::rename(path, backup);
}

void AppController::appendFileLog(const QString &path, const QString &line) const
{
    rotateLogIfNeeded(path);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return;
    QTextStream ts(&f);
    ts << line;
    f.close();
}

void AppController::appendServerEvent(const QString &source, const QString &text)
{
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    const QString msg = QStringLiteral("[%1][server/%2] %3").arg(ts, source, text);
    m_log += msg;
    if (!m_log.endsWith(QLatin1Char('\n'))) m_log += QLatin1Char('\n');
    if (m_log.size() > 200000) m_log = m_log.right(180000);
    appendFileLog(m_serverLogFilePath, msg);
    if (!msg.endsWith(QLatin1Char('\n'))) appendFileLog(m_serverLogFilePath, QStringLiteral("\n"));
    emit serverLogChanged();
}

void AppController::appendAgentEvent(const QString &source, const QString &text)
{
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    const QString msg = QStringLiteral("[%1][agent/%2] %3").arg(ts, source, text);
    m_agentLog += msg;
    if (!m_agentLog.endsWith(QLatin1Char('\n'))) m_agentLog += QLatin1Char('\n');
    if (m_agentLog.size() > 300000) m_agentLog = m_agentLog.right(250000);
    appendFileLog(m_agentLogFilePath, msg);
    if (!msg.endsWith(QLatin1Char('\n'))) appendFileLog(m_agentLogFilePath, QStringLiteral("\n"));
    emit agentLogChanged();
}

EffectiveProfileBuilder::Context AppController::buildContext(const QString &launchProfileId)
{
    EffectiveProfileBuilder::Context ctx;
    ctx.launch = m_profiles.resolveLaunch(launchProfileId);
    ctx.backend = m_profiles.resolveBackend(ctx.launch.backendProfileId);
    ctx.model = m_profiles.resolveModelProfile(ctx.launch.modelProfileId);
    ctx.runtime = m_profiles.resolveRuntime(ctx.launch.runtimePresetId);
    ctx.harness = m_profiles.resolveHarness(ctx.launch.harnessProfileId);
    ctx.workspace = m_profiles.resolveWorkspace(ctx.launch.workspaceProfileId);
    ctx.binary = m_binaries.findById(ctx.backend.binaryId);
    ctx.catalogModel = m_catalog.findById(ctx.model.modelId);
    ctx.mmprojModel = m_catalog.findById(ctx.model.mmprojId);
    ctx.draftModel = m_catalog.findById(ctx.model.draftModelId);
    return ctx;
}

bool AppController::isHarnessInstalled(const QString &adapter) const
{
    if (adapter == QLatin1String("none") || adapter.isEmpty()) return true;
    if (adapter == QLatin1String("llamaagent")) return true;  // backend interno, sin binario
#ifdef Q_OS_WIN
    const QString exe = adapter + QStringLiteral(".cmd");
    if (!QStandardPaths::findExecutable(exe).isEmpty()) return true;
#endif
    return !QStandardPaths::findExecutable(adapter).isEmpty();
}

void AppController::installHarness(const QString &adapter)
{
    if (m_installingHarness || adapter.isEmpty() || adapter == QLatin1String("none")) return;

    static const QMap<QString, QString> installCmds = {
        {QStringLiteral("opencode"),  QStringLiteral("npm install -g opencode@latest")},
        {QStringLiteral("smallcode"), QStringLiteral("npm install -g smallcode@latest")},
        {QStringLiteral("pi"),        QStringLiteral("npm install -g @mariozechner/pi-coding-agent")},
    };
    const QString cmd = installCmds.value(adapter);
    if (cmd.isEmpty()) return;

    m_installingHarness = true;
    m_harnessInstallStatus = QString();
    emit harnessStatusChanged();

    m_harnessProc = new QProcess(this);
    connect(m_harnessProc, &QProcess::readyReadStandardOutput, this, [this]() {
        m_harnessInstallStatus = QString::fromUtf8(m_harnessProc->readAllStandardOutput()).trimmed();
        emit harnessStatusChanged();
    });
    connect(m_harnessProc, &QProcess::readyReadStandardError, this, [this]() {
        const QString s = QString::fromUtf8(m_harnessProc->readAllStandardError()).trimmed();
        if (!s.isEmpty()) { m_harnessInstallStatus = s; emit harnessStatusChanged(); }
    });
    connect(m_harnessProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, adapter](int code, QProcess::ExitStatus) {
        m_harnessProc->deleteLater();
        m_harnessProc = nullptr;
        m_installingHarness = false;
        const bool ok = (code == 0) && isHarnessInstalled(adapter);
        m_harnessInstallStatus = ok
            ? QStringLiteral("✓ %1 installed").arg(adapter)
            : QStringLiteral("✗ Install failed (code %1)").arg(code);
        emit harnessStatusChanged();
        emit harnessInstallFinished(ok, adapter, m_harnessInstallStatus);
    });

    // Split "npm install -g xxx" into program + args
    const QStringList parts = cmd.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    m_harnessProc->start(parts.first(), parts.mid(1));
}

static bool s_isPidRunning(qint64 pid)
{
#ifdef Q_OS_WIN
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!h) return false;
    const bool running = (WaitForSingleObject(h, 0) == WAIT_TIMEOUT);
    CloseHandle(h);
    return running;
#else
    return ::kill(static_cast<pid_t>(pid), 0) == 0;
#endif
}

void AppController::ensurePiConfig(const QString &openaiBaseUrl)
{
    // ~/.pi/agent/models.json — registra/actualiza el provider "llamacode"
    const QString dir = QDir::homePath() + QStringLiteral("/.pi/agent");
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/models.json");

    QJsonObject root;
    QFile in(path);
    if (in.open(QIODevice::ReadOnly)) {
        root = QJsonDocument::fromJson(in.readAll()).object();
        in.close();
    }

    QJsonObject providers = root.value(QStringLiteral("providers")).toObject();
    QJsonObject llamacode;
    llamacode[QStringLiteral("baseUrl")] = openaiBaseUrl;
    llamacode[QStringLiteral("api")]     = QStringLiteral("openai-completions");
    llamacode[QStringLiteral("apiKey")]  = QStringLiteral("llamacode");
    llamacode[QStringLiteral("models")]  = QJsonArray{ QJsonObject{{QStringLiteral("id"), QStringLiteral("local")}} };
    providers[QStringLiteral("llamacode")] = llamacode;
    root[QStringLiteral("providers")] = providers;

    QFile out(path);
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        out.close();
    }
}

void AppController::sendPiMessage(const QString &text)
{
    if (m_piMsgProc) {
        appendAgentEvent(QStringLiteral("pi/lifecycle"), QStringLiteral("pi ocupado - esperá la respuesta anterior"));
        return;
    }

    auto estimateTokens = [](const QString &s) -> int {
        const int n = s.trimmed().size();
        return n <= 0 ? 0 : (n + 3) / 4;
    };
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    // Mensaje del usuario + placeholder del asistente (streaming visual)
    QVariantMap userMsg{{QStringLiteral("role"), QStringLiteral("user")},
                        {QStringLiteral("content"), text},
                        {QStringLiteral("typing"), false},
                        {QStringLiteral("createdAt"), static_cast<double>(nowMs)},
                        {QStringLiteral("completedAt"), static_cast<double>(nowMs)},
                        {QStringLiteral("elapsedMs"), 0},
                        {QStringLiteral("tokens"), estimateTokens(text)},
                        {QStringLiteral("tps"), 0.0}};
    m_agentMessages.append(userMsg);
    QVariantMap asstMsg{{QStringLiteral("role"), QStringLiteral("assistant")},
                        {QStringLiteral("content"), QString()},
                        {QStringLiteral("typing"), true},
                        {QStringLiteral("createdAt"), static_cast<double>(nowMs)},
                        {QStringLiteral("elapsedMs"), 0},
                        {QStringLiteral("tokens"), 0},
                        {QStringLiteral("tps"), 0.0}};
    m_agentMessages.append(asstMsg);
    m_currentAssistantIdx = m_agentMessages.size() - 1;
    emit agentMessagesChanged();

    m_piMsgProc = new QProcess(this);
    m_piMsgProc->setProcessEnvironment(m_piEnv);
    if (!m_piCwd.isEmpty())
        m_piMsgProc->setWorkingDirectory(m_piCwd);

    // -p print mode + sesión persistente para mantener continuidad entre mensajes.
    const QStringList args{
        QStringLiteral("-p"),
        QStringLiteral("--session"),  m_piSessionPath,
        QStringLiteral("--provider"), QStringLiteral("llamacode"),
        QStringLiteral("--model"),    QStringLiteral("llamacode/local"),
        text
    };

    connect(m_piMsgProc, &QProcess::readyReadStandardOutput, this, [this, estimateTokens]() {
        if (!m_piMsgProc) return;
        const QString chunk = QString::fromUtf8(m_piMsgProc->readAllStandardOutput());
        if (chunk.isEmpty() || m_currentAssistantIdx < 0
                || m_currentAssistantIdx >= m_agentMessages.size()) return;
        QVariantMap m = m_agentMessages[m_currentAssistantIdx].toMap();
        const QString content = m.value(QStringLiteral("content")).toString() + chunk;
        m[QStringLiteral("content")] = content;
        m[QStringLiteral("typing")]  = true;
        const qint64 startedAt = static_cast<qint64>(m.value(QStringLiteral("createdAt")).toDouble());
        const qint64 elapsedMs = qMax<qint64>(0, QDateTime::currentMSecsSinceEpoch() - startedAt);
        const int toks = estimateTokens(content);
        m[QStringLiteral("tokens")] = toks;
        m[QStringLiteral("elapsedMs")] = static_cast<int>(elapsedMs);
        m[QStringLiteral("tps")] = (elapsedMs > 0 && toks > 0)
            ? (1000.0 * static_cast<double>(toks) / static_cast<double>(elapsedMs))
            : 0.0;
        m_agentMessages[m_currentAssistantIdx] = m;
        emit agentMessagesChanged();
    });
    connect(m_piMsgProc, &QProcess::readyReadStandardError, this, [this]() {
        if (!m_piMsgProc) return;
        const QString s = QString::fromUtf8(m_piMsgProc->readAllStandardError());
        if (!s.isEmpty()) appendAgentEvent(QStringLiteral("pi/stderr"), s);
    });
    connect(m_piMsgProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, estimateTokens](int code, QProcess::ExitStatus) {
        if (m_currentAssistantIdx >= 0 && m_currentAssistantIdx < m_agentMessages.size()) {
            QVariantMap m = m_agentMessages[m_currentAssistantIdx].toMap();
            if (code != 0 && m.value(QStringLiteral("content")).toString().isEmpty())
                m[QStringLiteral("content")] = QStringLiteral("[pi terminó con código %1]").arg(code);
            m[QStringLiteral("typing")] = false;
            const qint64 doneAt = QDateTime::currentMSecsSinceEpoch();
            const qint64 startedAt = static_cast<qint64>(m.value(QStringLiteral("createdAt")).toDouble());
            const qint64 elapsedMs = qMax<qint64>(0, doneAt - startedAt);
            const QString content = m.value(QStringLiteral("content")).toString();
            const int toks = estimateTokens(content);
            m[QStringLiteral("completedAt")] = static_cast<double>(doneAt);
            m[QStringLiteral("tokens")] = toks;
            m[QStringLiteral("elapsedMs")] = static_cast<int>(elapsedMs);
            m[QStringLiteral("tps")] = (elapsedMs > 0 && toks > 0)
                ? (1000.0 * static_cast<double>(toks) / static_cast<double>(elapsedMs))
                : 0.0;
            m_agentMessages[m_currentAssistantIdx] = m;
            emit agentMessagesChanged();
        }
        m_piMsgProc->deleteLater();
        m_piMsgProc = nullptr;
    });

    m_piMsgProc->start(m_piExe, args);
    if (!m_piMsgProc->waitForStarted(5000)) {
        appendAgentEvent(QStringLiteral("pi/lifecycle"), QStringLiteral("Error: no se pudo iniciar pi"));
        m_piMsgProc->deleteLater();
        m_piMsgProc = nullptr;
    }
}

IAgentBackend *AppController::ensureAgentBackend(const QString &adapter)
{
    if (m_agentBackend && m_agentBackend->adapter() == adapter)
        return m_agentBackend;
    if (m_agentBackend) { m_agentBackend->deleteLater(); m_agentBackend = nullptr; }

    IAgentBackend *b = nullptr;
    if (adapter == QLatin1String("opencode"))
        b = new OpencodeBackend(this);
    else if (adapter == QLatin1String("llamaagent"))
        b = new LlamaAgentBackend(this);
    if (!b) return nullptr;

    if (auto *cb = qobject_cast<LlamaAgentBackend *>(b)) {
        cb->setThinkingEnabled(m_agentThinkingEnabled);

        // Servers MCP: global + proyecto (el de proyecto pisa por nombre).
        const QString proj = currentAgentProjectDir();
        QMap<QString, QVariant> merged;
        for (const QVariant &v : listMcpServers(QStringLiteral("global"), QString()))
            merged.insert(v.toMap().value(QStringLiteral("name")).toString(), v);
        if (!proj.isEmpty())
            for (const QVariant &v : listMcpServers(QStringLiteral("project"), proj))
                merged.insert(v.toMap().value(QStringLiteral("name")).toString(), v);
        cb->setMcpServers(merged.values());
    }

    // Reflejar estado del backend en los mirrors expuestos a QML.
    connect(b, &IAgentBackend::messagesChanged, this, [this, b]() {
        m_agentMessages = b->messages();
        emit agentMessagesChanged();
    });
    connect(b, &IAgentBackend::sessionsChanged, this, [this, b]() {
        m_agentSessions = b->sessions();
        m_opencodeSessionId = b->currentSessionId();
        m_opencodeSessionTitle = b->currentSessionTitle();
        emit agentSessionsChanged();
    });
    connect(b, &IAgentBackend::logAppended, this, [this](const QString &chunk) {
        appendAgentEvent(QStringLiteral("backend"), chunk);
    });
    connect(b, &IAgentBackend::runningChanged, this, [this, b]() {
        if (!b->running()) {
            m_activeAgentAdapter.clear();
            if (!m_agentPendingTool.isEmpty()) {
                m_agentPendingTool.clear();
                emit agentPendingToolChanged();
            }
            m_agentContextUsed = 0;
            emit agentContextChanged();
        }
        emit agentRunningChanged();
    });
    connect(b, &IAgentBackend::errorOccurred, this, [this](const QString &m) {
        appendAgentEvent(QStringLiteral("error"), m);
        emit serverError(m);
    });
    connect(b, &IAgentBackend::toolApprovalNeeded, this, [this](const QVariantMap &toolCall) {
        m_agentPendingTool = toolCall;
        emit agentPendingToolChanged();
    });
    connect(b, &IAgentBackend::contextUsage, this, [this](int used, int limit) {
        m_agentContextUsed = used;
        if (limit > 0) m_agentContextLimit = limit;
        emit agentContextChanged();
    });

    b->setApprovalPolicy(m_agentApprovalMode);
    b->setAgentTuning(m_agentSystemPrompt, m_agentTemperature);
    m_agentBackend = b;
    return b;
}

void AppController::setAgentSystemPrompt(const QString &p)
{
    if (p == m_agentSystemPrompt) return;
    m_agentSystemPrompt = p;
    writeSetting(QStringLiteral("agent/systemPrompt"), p);
    if (m_agentBackend) m_agentBackend->setAgentTuning(m_agentSystemPrompt, m_agentTemperature);
    emit agentTuningChanged();
}

void AppController::setAgentTemperature(double t)
{
    if (qFuzzyCompare(t, m_agentTemperature)) return;
    m_agentTemperature = t;
    writeSetting(QStringLiteral("agent/temperature"), t);
    if (m_agentBackend) m_agentBackend->setAgentTuning(m_agentSystemPrompt, m_agentTemperature);
    emit agentTuningChanged();
}

void AppController::setAgentApprovalMode(const QString &mode)
{
    const QString m = mode.trimmed().isEmpty() ? QStringLiteral("ask") : mode;
    if (m == m_agentApprovalMode) return;
    m_agentApprovalMode = m;
    writeSetting(QStringLiteral("agent/approvalMode"), m);
    if (m_agentBackend) m_agentBackend->setApprovalPolicy(m);
    emit agentApprovalModeChanged();
}

void AppController::setAgentThinkingEnabled(bool enabled)
{
    if (enabled == m_agentThinkingEnabled) return;
    m_agentThinkingEnabled = enabled;
    writeSetting(QStringLiteral("agent/thinkingEnabled"), enabled);
    if (auto *cb = qobject_cast<LlamaAgentBackend *>(m_agentBackend))
        cb->setThinkingEnabled(enabled);
    emit agentThinkingChanged();
}

void AppController::approveAgentTool(const QString &id, bool always)
{
    if (m_agentBackend) m_agentBackend->approveTool(id, always);
    if (m_agentPendingTool.value(QStringLiteral("id")).toString() == id) {
        m_agentPendingTool.clear();
        emit agentPendingToolChanged();
    }
}

void AppController::rejectAgentTool(const QString &id)
{
    if (m_agentBackend) m_agentBackend->rejectTool(id);
    if (m_agentPendingTool.value(QStringLiteral("id")).toString() == id) {
        m_agentPendingTool.clear();
        emit agentPendingToolChanged();
    }
}

void AppController::revertAgentEdit(const QString &path)
{
    if (m_agentBackend) m_agentBackend->revertEdit(path);
}

QString AppController::readAgentMemory(const QString &projectDir) const
{
    const QString dir = projectDir.isEmpty() ? currentAgentProjectDir() : projectDir;
    if (dir.isEmpty()) return QString();
    QFile f(LlamaAgentBackend::memoryFilePath(dir));
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(f.read(256 * 1024));
}

bool AppController::writeAgentMemory(const QString &projectDir, const QString &text)
{
    const QString dir = projectDir.isEmpty() ? currentAgentProjectDir() : projectDir;
    if (dir.isEmpty()) { emit serverError(QStringLiteral("No hay proyecto activo para la memoria.")); return false; }
    const QString path = LlamaAgentBackend::memoryFilePath(dir);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit serverError(QStringLiteral("No se pudo escribir la memoria: %1").arg(path));
        return false;
    }
    f.write(text.toUtf8());
    return true;
}

IAgentBackend *AppController::ensureChatBackend()
{
    if (m_chatBackend) return m_chatBackend;
    auto *b = new RawChatBackend(this);
    if (auto *raw = qobject_cast<RawChatBackend *>(b)) {
        const bool thinkingEnabled = readSetting(QStringLiteral("chat/thinkingEnabled"), true).toBool();
        raw->setThinkingEnabled(thinkingEnabled);
    }
    connect(b, &IAgentBackend::messagesChanged, this, [this, b]() {
        m_chatMessages = b->messages();
        bool generating = false;
        for (const QVariant &v : std::as_const(m_chatMessages)) {
            if (v.toMap().value(QStringLiteral("typing")).toBool()) {
                generating = true;
                break;
            }
        }
        m_chatGenerating = generating;
        emit chatMessagesChanged();
        emit chatGeneratingChanged();
    });
    connect(b, &IAgentBackend::sessionsChanged, this, [this, b]() {
        m_chatSessions = b->sessions();
        m_chatSessionId = b->currentSessionId();
        m_chatSessionTitle = b->currentSessionTitle();
        emit chatSessionsChanged();
    });
    connect(b, &IAgentBackend::errorOccurred, this, [this](const QString &m) {
        emit serverError(m);
    });

    AgentContext c;
    c.adapter = QStringLiteral("raw");
    c.serverBaseUrl = serverBaseUrl();
    c.modelId = QStringLiteral("chat");
    b->start(c);
    m_chatBackend = b;
    return m_chatBackend;
}

void AppController::startAgent(const QString &launchProfileId)
{
    if (agentRunning()) return;
    m_agentStopping = false;

    const auto ctx = buildContext(launchProfileId);
    const QString adapter = ctx.harness.adapter;
    if (adapter.isEmpty() || adapter == QLatin1String("none")) {
        appendAgentEvent(QStringLiteral("lifecycle"), QStringLiteral("Error: no harness configured for this profile"));
        return;
    }
    if (adapter == QLatin1String("raw")) {
        appendAgentEvent(QStringLiteral("lifecycle"),
                         QStringLiteral("Error: 'raw' no soporta tool-calling. Usá 'opencode' para modo Agente."));
        emit serverError(QStringLiteral("El harness 'raw' no soporta tool-calling en modo Agente."));
        return;
    }

    // Backend propio: sin binario externo, corre dentro de la app.
    if (adapter == QLatin1String("llamaagent")) {
        // Necesita el llama-server corriendo (usa su API OpenAI). Sin server → refused.
        // Si está corriendo pero el modelo aún carga, igual arranca: la UI muestra
        // "Modelo cargando" y el usuario espera al ready antes de enviar.
        if (!serverRunning()) {
            const QString msg = QStringLiteral(
                "El harness 'LlamaAgent' necesita el servidor corriendo. Iniciá el modelo en 'Lanzar' primero.");
            appendAgentEvent(QStringLiteral("lifecycle"), QStringLiteral("Error: %1").arg(msg));
            emit serverError(msg);
            return;
        }
        IAgentBackend *b = ensureAgentBackend(adapter);
        if (!b) { appendAgentEvent(QStringLiteral("lifecycle"), QStringLiteral("Error: backend LlamaAgent no disponible")); return; }
        const QString agentCwd = m_agentCwdOverride.isEmpty()
            ? ctx.workspace.cwd.trimmed() : m_agentCwdOverride;
        AgentContext c;
        c.adapter       = adapter;
        c.cwd           = (!agentCwd.isEmpty() && QFileInfo(agentCwd).isDir()) ? agentCwd : QString();
        c.serverBaseUrl = serverBaseUrl();
        c.modelId       = ctx.catalogModel.id;
        m_agentCwdOverride.clear();
        m_activeAgentAdapter = adapter;
        m_agentInTerminal    = false;
        m_agentMessages.clear();
        m_currentAssistantIdx = -1;
        emit agentMessagesChanged();
        b->start(c);
        return;
    }

    QString exe;
    exe = QStandardPaths::findExecutable(adapter);
    if (exe.isEmpty()) {
        appendAgentEvent(QStringLiteral("lifecycle"),
                         QStringLiteral("Error: '%1' not found in PATH — install it first").arg(adapter));
        return;
    }

    // Build env string list for the launcher
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString baseUrl = serverBaseUrl();
    env.insert(QStringLiteral("OPENAI_BASE_URL"),   baseUrl + QStringLiteral("/v1"));
    env.insert(QStringLiteral("OPENAI_API_BASE"),   baseUrl + QStringLiteral("/v1"));
    env.insert(QStringLiteral("OPENAI_API_KEY"),    QStringLiteral("llamacode"));
    env.insert(QStringLiteral("ANTHROPIC_API_KEY"), QStringLiteral("llamacode"));
    if (adapter == QLatin1String("opencode"))
        env.insert(QStringLiteral("OPENCODE_API_URL"), baseUrl);
    else if (adapter == QLatin1String("smallcode")) {
        env.insert(QStringLiteral("SMALLCODE_API_URL"),  baseUrl);
        env.insert(QStringLiteral("SMALLCODE_BASE_URL"), baseUrl + QStringLiteral("/v1"));
    }
    else if (adapter == QLatin1String("pi")) {
        // pi usa un provider OpenAI-compatible definido en ~/.pi/agent/models.json
        ensurePiConfig(baseUrl + QStringLiteral("/v1"));
        env.insert(QStringLiteral("OPENAI_BASE_URL"), baseUrl + QStringLiteral("/v1"));
        env.insert(QStringLiteral("PI_SKIP_VERSION_CHECK"), QStringLiteral("1"));
    }
    for (auto it = ctx.harness.env.cbegin(); it != ctx.harness.env.cend(); ++it)
        env.insert(it.key(), it.value());
    env.insert(QStringLiteral("LLAMACODE_MANAGED"), QStringLiteral("1"));
    env.insert(QStringLiteral("LLAMACODE_ROLE"),    QStringLiteral("harness-") + adapter);
    env.insert(QStringLiteral("LLAMACODE_APP_PID"), QString::number(QCoreApplication::applicationPid()));

    const QString agentCwd = m_agentCwdOverride.isEmpty()
        ? ctx.workspace.cwd.trimmed() : m_agentCwdOverride;

    // pi: integración estructurada por mensaje (print mode), sin proceso persistente.
    if (adapter == QLatin1String("pi")) {
        const QString sessDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                                + QStringLiteral("/pi-sessions");
        QDir().mkpath(sessDir);
        env.insert(QStringLiteral("PI_CODING_AGENT_SESSION_DIR"), sessDir);

        m_piExe          = exe;
        m_piEnv          = env;
        m_piCwd          = (!agentCwd.isEmpty() && QFileInfo(agentCwd).isDir()) ? agentCwd : QString();
        m_piSessionPath  = sessDir + QStringLiteral("/llamacode-")
                           + QString::number(QDateTime::currentMSecsSinceEpoch()) + QStringLiteral(".json");
        m_agentCwdOverride.clear();
        m_piActive            = true;
        m_activeAgentAdapter  = adapter;
        m_agentInTerminal     = false;
        m_agentMessages.clear();
        m_currentAssistantIdx = -1;
        appendAgentEvent(QStringLiteral("pi/lifecycle"), QStringLiteral("pi listo - modo print por mensaje"));
        emit agentMessagesChanged();
        emit agentRunningChanged();
        return;
    }

    // Backends estructurados (proceso propio o sin proceso): delegados a IAgentBackend.
    if (adapter == QLatin1String("opencode")) {
        IAgentBackend *b = ensureAgentBackend(adapter);
        if (!b) { appendAgentEvent(QStringLiteral("lifecycle"), QStringLiteral("Error: backend %1 no disponible").arg(adapter)); return; }
        AgentContext c;
        c.adapter         = adapter;
        c.launchProfileId = launchProfileId;
        c.exePath         = exe;
        c.cwd             = (!agentCwd.isEmpty() && QFileInfo(agentCwd).isDir()) ? agentCwd : QString();
        c.serverBaseUrl   = baseUrl;
        c.modelId         = ctx.catalogModel.id;
        c.env             = env;
        m_agentCwdOverride.clear();
        m_activeAgentAdapter = adapter;
        m_agentInTerminal    = false;
        m_agentMessages.clear();
        m_currentAssistantIdx = -1;
        emit agentMessagesChanged();
        b->start(c);
        return;
    }

    m_activeAgentAdapter = adapter;
    m_agentInTerminal = false;
    m_agentPid = 0;
    m_agentProc = new QProcess(this);
    m_agentProc->setProcessEnvironment(env);

    const QString cwd = m_agentCwdOverride.isEmpty()
        ? ctx.workspace.cwd.trimmed() : m_agentCwdOverride;
    m_agentCwdOverride.clear();
    if (!cwd.isEmpty() && QFileInfo(cwd).isDir())
        m_agentProc->setWorkingDirectory(cwd);

    connect(m_agentProc, &QProcess::readyReadStandardOutput, this, [this]() {
        if (!m_agentProc) return;
        appendAgentEvent(QStringLiteral("proc/stdout"),
                         QString::fromUtf8(m_agentProc->readAllStandardOutput()));
    });
    connect(m_agentProc, &QProcess::readyReadStandardError, this, [this]() {
        if (!m_agentProc) return;
        appendAgentEvent(QStringLiteral("proc/stderr"),
                         QString::fromUtf8(m_agentProc->readAllStandardError()));
    });
    connect(m_agentProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
        appendAgentEvent(QStringLiteral("lifecycle"),
                         QStringLiteral("agent exited with code %1").arg(code));
        if (m_agentProc) {
            m_agentProc->deleteLater();
            m_agentProc = nullptr;
        }
        m_agentMessages.clear();
        m_currentAssistantIdx = -1;
        emit agentMessagesChanged();
        clearServiceState(QStringLiteral("harness"));
        m_activeAgentAdapter.clear();
        m_agentPid = 0;
        m_agentInTerminal = false;
        m_agentStopping = false;
        emit agentRunningChanged();
    });

    // Path genérico: adapters basados en stdin (p.ej. smallcode).
    // opencode se maneja vía OpencodeBackend (delegado arriba) y nunca llega acá.
    QString program = exe;
    QStringList programArgs = ctx.harness.args;

    appendAgentEvent(QStringLiteral("lifecycle"), QStringLiteral("starting %1").arg(adapter));
    if (!cwd.isEmpty())
        appendAgentEvent(QStringLiteral("lifecycle"), QStringLiteral("cwd: %1")
                         .arg(QDir::toNativeSeparators(cwd)));
    m_agentProc->start(program, programArgs);
    if (!m_agentProc->waitForStarted(5000)) {
        appendAgentEvent(QStringLiteral("lifecycle"), QStringLiteral("Error: failed to start agent process"));
        m_agentProc->deleteLater();
        m_agentProc = nullptr;
        m_activeAgentAdapter.clear();
        return;
    }
    m_agentPid = m_agentProc->processId();
    assignToJobObject(m_agentPid);
    QVariantMap agentExtra;
    agentExtra[QStringLiteral("adapter")] = adapter;
    writeServiceState(QStringLiteral("harness"), m_agentPid, agentExtra);
    appendAgentEvent(QStringLiteral("lifecycle"),
                     QStringLiteral("%1 running (PID %2)").arg(adapter).arg(m_agentPid));
    emit agentRunningChanged();
}

void AppController::cancelAgentGeneration()
{
    if (m_agentBackend)
        m_agentBackend->cancelGeneration();
}

void AppController::stopAgent()
{
    m_agentStopping = true;
    if (m_agentBackend && m_agentBackend->running()) {
        m_agentBackend->stop();
        return;
    }

    if (m_piActive) {
        if (m_piMsgProc) {
            m_piMsgProc->kill();
            m_piMsgProc->deleteLater();
            m_piMsgProc = nullptr;
        }
        m_piActive = false;
        m_activeAgentAdapter.clear();
        appendAgentEvent(QStringLiteral("pi/lifecycle"), QStringLiteral("pi detenido"));
        emit agentRunningChanged();
        return;
    }

    if (m_agentInTerminal && m_agentPid) {
#ifdef Q_OS_WIN
        // Kill the terminal process tree (includes child opencode process)
        QProcess::execute(QStringLiteral("taskkill"),
            {QStringLiteral("/PID"), QString::number(m_agentPid),
             QStringLiteral("/T"), QStringLiteral("/F")});
#else
        ::kill(static_cast<pid_t>(m_agentPid), SIGTERM);
#endif
        m_agentPid = 0;
        m_activeAgentAdapter.clear();
        m_agentInTerminal = false;
        if (m_agentPollTimer) m_agentPollTimer->stop();
        emit agentRunningChanged();
        return;
    }
    if (!m_agentProc) return;
#ifdef Q_OS_WIN
    // Matar el árbol completo (opencode serve lanza un hijo node que retiene el puerto).
    const qint64 pid = m_agentProc->processId();
    if (pid > 0)
        QProcess::execute(QStringLiteral("taskkill"),
            {QStringLiteral("/PID"), QString::number(pid),
             QStringLiteral("/T"), QStringLiteral("/F")});
#endif
    m_agentProc->terminate();
    if (!m_agentProc->waitForFinished(2000))
        m_agentProc->kill();
}

void AppController::sendToAgent(const QString &text)
{
    if (text.trimmed().isEmpty()) return;
    if (m_agentBackend && m_agentBackend->running()) {
        m_agentBackend->sendMessage(text);
        return;
    }
    if (m_piActive) {
        appendAgentEvent(QStringLiteral("input"), QStringLiteral("> %1").arg(text));
        sendPiMessage(text);
        return;
    }

    if (!m_agentProc || m_agentProc->state() != QProcess::Running) return;

    appendAgentEvent(QStringLiteral("input"), QStringLiteral("> %1").arg(text));

    // Adapters genéricos basados en stdin (opencode usa OpencodeBackend, delegado arriba).
    m_agentProc->write((text + QLatin1Char('\n')).toUtf8());
}

void AppController::clearAgentLog()
{
    m_agentLog.clear();
    m_agentMessages.clear();
    m_currentAssistantIdx = -1;
    emit agentLogChanged();
    emit agentMessagesChanged();
}

// Sesiones de agente: delegadas al backend activo (IAgentBackend).
// El path legacy (opencode por HTTP directo desde AppController) fue removido;
// OpencodeBackend es ahora el único dueño de sesiones/stream/proceso.
void AppController::newOpencodeSession()
{
    if (m_agentBackend) m_agentBackend->newSession();
}

void AppController::switchOpencodeSession(const QString &sessionId)
{
    if (m_agentBackend) m_agentBackend->switchSession(sessionId);
}

void AppController::refreshOpencodeSessionList()
{
    if (m_agentBackend) m_agentBackend->refreshSessions();
}

void AppController::newOpencodeSessionInProject(const QString &projectDir)
{
    if (m_agentBackend) m_agentBackend->newSessionInProject(projectDir);
}

void AppController::renameOpencodeSession(const QString &sessionId, const QString &title)
{
    if (m_agentBackend) m_agentBackend->renameSession(sessionId, title);
}

void AppController::deleteOpencodeSession(const QString &sessionId)
{
    if (m_agentBackend) m_agentBackend->deleteSession(sessionId);
}

void AppController::deleteOpencodeProject(const QString &projectDir)
{
    if (m_agentBackend) m_agentBackend->deleteProject(projectDir);
}

void AppController::forkOpencodeSession(const QString &sessionId)
{
    if (m_agentBackend) m_agentBackend->forkSession(sessionId);
}

QString AppController::currentAgentProjectDir() const
{
    if (m_agentBackend) return m_agentBackend->currentProjectDir();
    return m_agentProc ? m_agentProc->workingDirectory() : QString();
}

// ───────────────────────── Opencode config helpers ─────────────────────────
QString AppController::ocGlobalConfigDir() const
{
    const QByteArray xdg = qgetenv("XDG_CONFIG_HOME");
    const QString base = xdg.isEmpty() ? (QDir::homePath() + QStringLiteral("/.config"))
                                       : QString::fromUtf8(xdg);
    return base + QStringLiteral("/opencode");
}

QString AppController::ocConfigFilePath(const QString &scope, const QString &projectDir) const
{
    if (scope == QLatin1String("project")) {
        if (projectDir.isEmpty()) return QString();
        return QDir::cleanPath(projectDir) + QStringLiteral("/opencode.json");
    }
    return ocGlobalConfigDir() + QStringLiteral("/opencode.json");
}

QString AppController::ocCommandDir(const QString &scope, const QString &projectDir) const
{
    if (scope == QLatin1String("project")) {
        if (projectDir.isEmpty()) return QString();
        return QDir::cleanPath(projectDir) + QStringLiteral("/.opencode/command");
    }
    return ocGlobalConfigDir() + QStringLiteral("/command");
}

QJsonObject AppController::ocReadConfigObj(const QString &scope, const QString &projectDir) const
{
    const QString path = ocConfigFilePath(scope, projectDir);
    if (path.isEmpty()) return {};
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

bool AppController::ocWriteConfigObj(const QString &scope, const QString &projectDir, const QJsonObject &obj)
{
    const QString path = ocConfigFilePath(scope, projectDir);
    if (path.isEmpty()) return false;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return true;
}

QString AppController::opencodeConfigPath(const QString &scope, const QString &projectDir) const
{
    return QDir::toNativeSeparators(ocConfigFilePath(scope, projectDir));
}

QString AppController::readOpencodeConfig(const QString &scope, const QString &projectDir) const
{
    const QString path = ocConfigFilePath(scope, projectDir);
    if (path.isEmpty()) return QStringLiteral("{}");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QStringLiteral("{\n  \"$schema\": \"https://opencode.ai/config.json\"\n}");
    const QByteArray raw = f.readAll();
    if (raw.trimmed().isEmpty()) return QStringLiteral("{}");
    return QString::fromUtf8(raw);
}

bool AppController::writeOpencodeConfig(const QString &scope, const QString &projectDir, const QString &jsonText)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        emit serverError(QStringLiteral("opencode.json inválido: %1").arg(err.errorString()));
        return false;
    }
    const QString path = ocConfigFilePath(scope, projectDir);
    if (path.isEmpty()) { emit serverError(QStringLiteral("Sin proyecto seleccionado.")); return false; }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit serverError(QStringLiteral("No se pudo escribir %1").arg(path));
        return false;
    }
    f.write(doc.toJson(QJsonDocument::Indented));
    return true;
}

// ───────────────────────── MCP servers ─────────────────────────
QVariantList AppController::listMcpServers(const QString &scope, const QString &projectDir) const
{
    QVariantList out;
    const QJsonObject mcp = ocReadConfigObj(scope, projectDir).value(QStringLiteral("mcp")).toObject();
    for (auto it = mcp.begin(); it != mcp.end(); ++it) {
        const QJsonObject s = it.value().toObject();
        QVariantMap m;
        m[QStringLiteral("name")]    = it.key();
        m[QStringLiteral("type")]    = s.value(QStringLiteral("type")).toString(QStringLiteral("local"));
        m[QStringLiteral("enabled")] = s.value(QStringLiteral("enabled")).toBool(true);
        if (s.contains(QStringLiteral("url")))
            m[QStringLiteral("url")] = s.value(QStringLiteral("url")).toString();
        if (s.contains(QStringLiteral("command"))) {
            QStringList cmd;
            for (const QJsonValue &cv : s.value(QStringLiteral("command")).toArray())
                cmd << cv.toString();
            m[QStringLiteral("command")] = cmd.join(QLatin1Char(' '));
        }
        out.append(m);
    }
    return out;
}

bool AppController::setMcpServer(const QString &scope, const QString &projectDir,
                                 const QString &name, const QVariantMap &def)
{
    if (name.trimmed().isEmpty()) return false;
    QJsonObject root = ocReadConfigObj(scope, projectDir);
    QJsonObject mcp = root.value(QStringLiteral("mcp")).toObject();

    QJsonObject s;
    const QString type = def.value(QStringLiteral("type"), QStringLiteral("local")).toString();
    s[QStringLiteral("type")]    = type;
    s[QStringLiteral("enabled")] = def.value(QStringLiteral("enabled"), true).toBool();
    if (type == QLatin1String("remote")) {
        s[QStringLiteral("url")] = def.value(QStringLiteral("url")).toString();
    } else {
        QJsonArray cmd;
        const QStringList parts = def.value(QStringLiteral("command")).toString()
                                     .split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &p : parts) cmd.append(p);
        s[QStringLiteral("command")] = cmd;
    }
    mcp[name] = s;
    root[QStringLiteral("mcp")] = mcp;
    return ocWriteConfigObj(scope, projectDir, root);
}

bool AppController::removeMcpServer(const QString &scope, const QString &projectDir, const QString &name)
{
    QJsonObject root = ocReadConfigObj(scope, projectDir);
    QJsonObject mcp = root.value(QStringLiteral("mcp")).toObject();
    if (!mcp.contains(name)) return false;
    mcp.remove(name);
    root[QStringLiteral("mcp")] = mcp;
    return ocWriteConfigObj(scope, projectDir, root);
}

bool AppController::toggleMcpServer(const QString &scope, const QString &projectDir,
                                    const QString &name, bool enabled)
{
    QJsonObject root = ocReadConfigObj(scope, projectDir);
    QJsonObject mcp = root.value(QStringLiteral("mcp")).toObject();
    if (!mcp.contains(name)) return false;
    QJsonObject s = mcp.value(name).toObject();
    s[QStringLiteral("enabled")] = enabled;
    mcp[name] = s;
    root[QStringLiteral("mcp")] = mcp;
    return ocWriteConfigObj(scope, projectDir, root);
}

// ───────────────────────── Skills / comandos ─────────────────────────
QVariantList AppController::listOpencodeCommands(const QString &scope, const QString &projectDir) const
{
    QVariantList out;
    const QString dir = ocCommandDir(scope, projectDir);
    if (dir.isEmpty()) return out;
    QDir d(dir);
    const QStringList files = d.entryList({QStringLiteral("*.md")}, QDir::Files, QDir::Name);
    for (const QString &f : files) {
        QVariantMap m;
        m[QStringLiteral("name")] = QFileInfo(f).completeBaseName();
        m[QStringLiteral("path")] = QDir::toNativeSeparators(d.absoluteFilePath(f));
        out.append(m);
    }
    return out;
}

QString AppController::readOpencodeCommand(const QString &scope, const QString &projectDir, const QString &name) const
{
    const QString dir = ocCommandDir(scope, projectDir);
    if (dir.isEmpty()) return QString();
    QFile f(dir + QLatin1Char('/') + name + QStringLiteral(".md"));
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(f.readAll());
}

bool AppController::writeOpencodeCommand(const QString &scope, const QString &projectDir,
                                         const QString &name, const QString &content)
{
    const QString cleanName = name.trimmed();
    if (cleanName.isEmpty()) return false;
    const QString dir = ocCommandDir(scope, projectDir);
    if (dir.isEmpty()) { emit serverError(QStringLiteral("Sin proyecto seleccionado.")); return false; }
    QDir().mkpath(dir);
    QFile f(dir + QLatin1Char('/') + cleanName + QStringLiteral(".md"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(content.toUtf8());
    return true;
}

bool AppController::deleteOpencodeCommand(const QString &scope, const QString &projectDir, const QString &name)
{
    const QString dir = ocCommandDir(scope, projectDir);
    if (dir.isEmpty()) return false;
    return QFile::remove(dir + QLatin1Char('/') + name + QStringLiteral(".md"));
}

QString AppController::pickDirectory(const QString &title)
{
    QWidget *parent = QApplication::activeWindow();
    const QString dir = QFileDialog::getExistingDirectory(
        parent,
        title.isEmpty() ? QStringLiteral("Seleccionar carpeta del proyecto") : title,
        QDir::homePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    return dir;
}

void AppController::changeAgentProject(const QString &directory)
{
    if (directory.isEmpty()) return;
    m_agentCwdOverride = directory;

    // Con backend activo, el reinicio en el nuevo cwd lo maneja el backend.
    if (m_agentBackend && m_agentBackend->running()) {
        m_agentBackend->newSessionInProject(directory);
        return;
    }
    if (!agentRunning()) return;

    // Restart agent with new CWD — remember launch ID
    m_pendingAgentLaunchId = m_activeAgentAdapter.isEmpty()
        ? m_activeLaunchId : m_pendingAgentLaunchId;

    // When current agent stops, restart with override
    const QString restartLaunchId = m_activeLaunchId;
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(this, &AppController::agentRunningChanged, this, [this, restartLaunchId, conn]() {
        if (agentRunning()) return;  // wait until fully stopped
        QObject::disconnect(*conn);
        if (!m_agentCwdOverride.isEmpty() && !restartLaunchId.isEmpty()) {
            QTimer::singleShot(300, this, [this, restartLaunchId]() {
                startAgent(restartLaunchId);
            });
        }
    });

    stopAgent();
}

QString AppController::agentNativeLogDir(const QString &adapter) const
{
    const QString a = adapter.trimmed().isEmpty() ? QStringLiteral("agent") : adapter.trimmed();
    const QString xdgData = qEnvironmentVariable(
        "XDG_DATA_HOME",
        QDir::homePath() + QStringLiteral("/.local/share"));

    QStringList candidates;
    candidates << (xdgData + QStringLiteral("/") + a + QStringLiteral("/log"));
#ifdef Q_OS_WIN
    candidates << (QDir::homePath() + QStringLiteral("/AppData/Roaming/")
                   + a + QStringLiteral("/log"));
#endif
    candidates << (QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                   + QStringLiteral("/") + a + QStringLiteral("/log"));

    for (const QString &p : std::as_const(candidates)) {
        if (QFileInfo::exists(p) && QFileInfo(p).isDir())
            return QDir::toNativeSeparators(p);
    }
    return QDir::toNativeSeparators(candidates.first());
}

void AppController::openAgentLogDir(const QString &adapter)
{
    QString dir = agentNativeLogDir(adapter);
    if (dir.isEmpty()) return;
    QDir d(dir);
    if (!d.exists())
        QDir().mkpath(dir);

    bool ok = QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
#ifdef Q_OS_WIN
    if (!ok) {
        ok = QProcess::startDetached(QStringLiteral("explorer.exe"),
                                     {QDir::toNativeSeparators(dir)});
    }
#endif
    if (!ok) {
        emit serverError(QStringLiteral("No se pudo abrir el log nativo: %1")
                             .arg(QDir::toNativeSeparators(dir)));
    }
}

void AppController::openRuntimeLogDir()
{
    const QString dir = runtimeLogDir();
    bool ok = QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
#ifdef Q_OS_WIN
    if (!ok) {
        ok = QProcess::startDetached(QStringLiteral("explorer.exe"),
                                     {QDir::toNativeSeparators(dir)});
    }
#endif
    if (!ok) {
        emit serverError(QStringLiteral("No se pudo abrir la carpeta de logs: %1")
                             .arg(QDir::toNativeSeparators(dir)));
    }
}

void AppController::setLanguage(const QString &lang)
{
    if (m_language == lang) return;
    m_language = lang;
    QSettings s;
    s.setValue(QStringLiteral("language"), lang);
    emit languageChanged();
}

QString AppController::l(const QString &key) const
{
    const auto &t = translations();
    const auto langIt = t.find(m_language);
    if (langIt != t.end()) {
        const auto it = langIt->find(key);
        if (it != langIt->end()) return *it;
    }
    const auto esIt = t.find(QStringLiteral("es"));
    if (esIt != t.end()) {
        const auto it = esIt->find(key);
        if (it != esIt->end()) return *it;
    }
    return key;
}

QVariant AppController::readSetting(const QString &key, const QVariant &defaultValue) const
{
    QSettings s;
    return s.value(key, defaultValue);
}

void AppController::writeSetting(const QString &key, const QVariant &value)
{
    QSettings s;
    s.setValue(key, value);
}

// ── Chat sessions ─────────────────────────────────────────────────────────────

QString AppController::chatStorageDir() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/chat");
    QDir().mkpath(dir);
    return dir;
}

void AppController::loadChatSessions()
{
    QFile f(chatStorageDir() + QStringLiteral("/index.json"));
    QJsonArray arr;
    if (f.open(QIODevice::ReadOnly)) arr = QJsonDocument::fromJson(f.readAll()).array();
    m_chatSessions.clear();
    QVector<QVariantMap> sorted;
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        QVariantMap s;
        s[QStringLiteral("id")]          = o.value(QStringLiteral("id")).toString();
        s[QStringLiteral("title")]       = o.value(QStringLiteral("title")).toString();
        s[QStringLiteral("projectId")]   = o.value(QStringLiteral("projectId")).toString();
        s[QStringLiteral("projectName")] = o.value(QStringLiteral("projectName")).toString();
        s[QStringLiteral("created")]     = o.value(QStringLiteral("created")).toDouble();
        s[QStringLiteral("updated")]     = o.value(QStringLiteral("updated")).toDouble();
        sorted.append(s);
    }
    // Group by project (contiguous sections), most-recently-used project first,
    // and within a project newest chat first.
    QHash<QString, double> projLatest;
    for (const auto &s : std::as_const(sorted)) {
        const QString p = s.value(QStringLiteral("projectName")).toString();
        const double u = s.value(QStringLiteral("updated")).toDouble();
        if (u > projLatest.value(p, 0.0)) projLatest[p] = u;
    }
    std::sort(sorted.begin(), sorted.end(), [&](const QVariantMap &a, const QVariantMap &b) {
        const QString pa = a.value(QStringLiteral("projectName")).toString();
        const QString pb = b.value(QStringLiteral("projectName")).toString();
        if (pa != pb) return projLatest.value(pa) > projLatest.value(pb);
        return a.value(QStringLiteral("updated")).toDouble() > b.value(QStringLiteral("updated")).toDouble();
    });
    for (const auto &s : sorted) m_chatSessions.append(s);
    injectDraftSession();
    emit chatSessionsChanged();
}

// Active session may not be persisted yet (no messages sent). Show it in the
// list anyway so the chat / new project is visible immediately.
void AppController::injectDraftSession()
{
    if (m_chatSessionId.isEmpty()) return;
    for (const QVariant &v : std::as_const(m_chatSessions))
        if (v.toMap().value(QStringLiteral("id")).toString() == m_chatSessionId)
            return; // already persisted

    const QString projId = m_chatProjectIdOverride.isEmpty()
        ? m_activeLaunchId : m_chatProjectIdOverride;
    const QString projName = [&]() -> QString {
        if (!m_chatProjectNameOverride.isEmpty()) return m_chatProjectNameOverride;
        const auto profiles = m_profiles.launchProfiles();
        for (int i = 0; i < profiles->rowCount(); ++i) {
            const auto idx2 = profiles->index(i, 0);
            if (profiles->data(idx2, 257).toString() == projId)
                return profiles->data(idx2, Qt::DisplayRole).toString();
        }
        return projId.isEmpty() ? QStringLiteral("Sin proyecto") : projId;
    }();

    const double now = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
    QVariantMap s;
    s[QStringLiteral("id")]          = m_chatSessionId;
    s[QStringLiteral("title")]       = m_chatSessionTitle;
    s[QStringLiteral("projectId")]   = projId;
    s[QStringLiteral("projectName")] = projName;
    s[QStringLiteral("created")]     = now;
    s[QStringLiteral("updated")]     = now;
    m_chatSessions.prepend(s);
}

void AppController::saveChatSession()
{
    if (m_chatSessionId.isEmpty()) return;

    // Update index
    QFile idxFile(chatStorageDir() + QStringLiteral("/index.json"));
    QJsonArray idx;
    if (idxFile.open(QIODevice::ReadOnly)) { idx = QJsonDocument::fromJson(idxFile.readAll()).array(); idxFile.close(); }
    bool found = false;
    const double now = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
    for (int i = 0; i < idx.size(); ++i) {
        QJsonObject o = idx[i].toObject();
        if (o.value(QStringLiteral("id")).toString() == m_chatSessionId) {
            o[QStringLiteral("title")]   = m_chatSessionTitle;
            o[QStringLiteral("updated")] = now;
            idx[i] = o; found = true; break;
        }
    }
    if (!found) {
        const QString projId = m_chatProjectIdOverride.isEmpty()
            ? m_activeLaunchId : m_chatProjectIdOverride;
        const QString projName = [&]() -> QString {
            if (!m_chatProjectNameOverride.isEmpty()) return m_chatProjectNameOverride;
            const auto profiles = m_profiles.launchProfiles();
            for (int i = 0; i < profiles->rowCount(); ++i) {
                const auto idx2 = profiles->index(i, 0);
                if (profiles->data(idx2, 257).toString() == projId)
                    return profiles->data(idx2, Qt::DisplayRole).toString();
            }
            return projId.isEmpty() ? QStringLiteral("Sin proyecto") : projId;
        }();
        m_chatProjectIdOverride.clear();
        m_chatProjectNameOverride.clear();
        idx.append(QJsonObject{
            {QStringLiteral("id"),          m_chatSessionId},
            {QStringLiteral("title"),       m_chatSessionTitle},
            {QStringLiteral("projectId"),   projId},
            {QStringLiteral("projectName"), projName},
            {QStringLiteral("created"),     now},
            {QStringLiteral("updated"),     now}
        });
    }
    if (idxFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        idxFile.write(QJsonDocument(idx).toJson());
        idxFile.close();
    }

    // Save session file with messages
    QJsonArray msgsJson;
    for (const QVariant &mv : std::as_const(m_chatMessages)) {
        const QVariantMap m = mv.toMap();
        msgsJson.append(QJsonObject{
            {QStringLiteral("role"),    m.value(QStringLiteral("role")).toString()},
            {QStringLiteral("content"), m.value(QStringLiteral("content")).toString()}
        });
    }
    QFile sf(chatStorageDir() + QStringLiteral("/") + m_chatSessionId + QStringLiteral(".json"));
    if (sf.open(QIODevice::WriteOnly | QIODevice::Truncate))
        sf.write(QJsonDocument(QJsonObject{
            {QStringLiteral("id"),       m_chatSessionId},
            {QStringLiteral("title"),    m_chatSessionTitle},
            {QStringLiteral("messages"), msgsJson}
        }).toJson());

    loadChatSessions();
}

void AppController::loadChatSessionMessages(const QString &id)
{
    QFile f(chatStorageDir() + QStringLiteral("/") + id + QStringLiteral(".json"));
    if (!f.open(QIODevice::ReadOnly)) { emit chatMessagesChanged(); return; }
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    m_chatMessages.clear();
    for (const QJsonValue &v : obj.value(QStringLiteral("messages")).toArray()) {
        const QJsonObject m = v.toObject();
        QVariantMap entry;
        entry[QStringLiteral("role")]    = m.value(QStringLiteral("role")).toString();
        entry[QStringLiteral("content")] = m.value(QStringLiteral("content")).toString();
        entry[QStringLiteral("typing")]  = false;
        m_chatMessages.append(entry);
    }
    emit chatMessagesChanged();
}

void AppController::newChatSession()
{
    if (IAgentBackend *b = ensureChatBackend())
        b->newSession();
}

void AppController::newChatSessionInProject(const QString &projectId, const QString &projectName)
{
    if (IAgentBackend *b = ensureChatBackend())
        b->newSessionInProject(projectId + QStringLiteral("|") + projectName + QStringLiteral("|"));
}

void AppController::switchChatSession(const QString &id)
{
    if (IAgentBackend *b = ensureChatBackend())
        b->switchSession(id);
}

void AppController::deleteChatSession(const QString &id)
{
    if (IAgentBackend *b = ensureChatBackend())
        b->deleteSession(id);
}

void AppController::deleteChatProject(const QString &projectName)
{
    IAgentBackend *b = ensureChatBackend();
    if (!b) return;
    const QVariantList sessions = b->sessions();
    for (const QVariant &v : sessions) {
        const QVariantMap s = v.toMap();
        if (s.value(QStringLiteral("projectName")).toString() == projectName)
            b->deleteSession(s.value(QStringLiteral("id")).toString());
    }
}

QVariantList AppController::chatProjects() const
{
    if (m_chatBackend) {
        QVariantList out;
        QSet<QString> seen;
        for (const QVariant &v : m_chatBackend->sessions()) {
            const QVariantMap s = v.toMap();
            const QString name = s.value(QStringLiteral("projectName")).toString();
            if (name.isEmpty() || seen.contains(name)) continue;
            seen.insert(name);
            out.append(QVariantMap{
                {QStringLiteral("projectId"),   s.value(QStringLiteral("projectId")).toString()},
                {QStringLiteral("projectName"), name}
            });
        }
        return out;
    }
    QVariantList out;
    QSet<QString> seen;
    for (const QVariant &v : std::as_const(m_chatSessions)) {
        const QVariantMap s = v.toMap();
        const QString name = s.value(QStringLiteral("projectName")).toString();
        if (name.isEmpty() || seen.contains(name)) continue;
        seen.insert(name);
        out.append(QVariantMap{
            {QStringLiteral("projectId"),   s.value(QStringLiteral("projectId")).toString()},
            {QStringLiteral("projectName"), name}
        });
    }
    return out;
}

void AppController::moveChatToProject(const QString &id, const QString &projectId, const QString &projectName)
{
    IAgentBackend *b = ensureChatBackend();
    if (!b || id.isEmpty()) return;
    if (auto *raw = qobject_cast<RawChatBackend *>(b))
        raw->updateSessionProject(id, projectId, projectName);
}

void AppController::renameChatSession(const QString &id, const QString &title)
{
    if (IAgentBackend *b = ensureChatBackend())
        b->renameSession(id, title);
}

void AppController::renameChatProject(const QString &oldName, const QString &newName)
{
    IAgentBackend *b = ensureChatBackend();
    if (!b) return;
    const QString nn = newName.trimmed();
    if (oldName.isEmpty() || nn.isEmpty() || nn == oldName) return;
    if (auto *raw = qobject_cast<RawChatBackend *>(b))
        raw->renameProject(oldName, nn);
}

void AppController::sendChatMessage(const QString &text)
{
    if (text.trimmed().isEmpty()) return;
    IAgentBackend *b = ensureChatBackend();
    if (!b) return;
    AgentContext c;
    c.adapter = QStringLiteral("raw");
    c.serverBaseUrl = serverBaseUrl();
    c.modelId = QStringLiteral("chat");
    b->stop();
    b->start(c);
    b->sendMessage(text);
}

void AppController::sendChatMessageWithAttachments(const QString &text, const QStringList &paths)
{
    if (text.trimmed().isEmpty() && paths.isEmpty()) return;
    IAgentBackend *b = ensureChatBackend();
    if (!b) return;
    AgentContext c;
    c.adapter = QStringLiteral("raw");
    c.serverBaseUrl = serverBaseUrl();
    c.modelId = QStringLiteral("chat");
    b->stop();
    b->start(c);
    if (auto *raw = qobject_cast<RawChatBackend *>(b))
        raw->setPendingAttachments(paths);
    b->sendMessage(text);
}

QString AppController::pasteClipboardImage()
{
    const QImage img = QGuiApplication::clipboard()->image();
    if (img.isNull()) return {};
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                        + QStringLiteral("/llamacode-paste");
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/paste-")
                         + QString::number(QDateTime::currentMSecsSinceEpoch())
                         + QStringLiteral(".png");
    if (!img.save(path, "PNG")) return {};
    return path;
}

QStringList AppController::pickChatAttachments()
{
    QWidget *parent = QApplication::activeWindow();
    return QFileDialog::getOpenFileNames(
        parent, QStringLiteral("Adjuntar archivos"), QDir::homePath(),
        QStringLiteral("Soportados (*.png *.jpg *.jpeg *.webp *.gif *.bmp *.txt *.md *.json *.csv *.log *.py *.js *.ts *.cpp *.h *.qml);;Todos (*.*)"));
}

void AppController::stopChatGeneration()
{
    if (IAgentBackend *b = ensureChatBackend())
        b->cancelGeneration();
}

void AppController::setChatThinkingEnabled(bool enabled)
{
    writeSetting(QStringLiteral("chat/thinkingEnabled"), enabled);
    if (IAgentBackend *b = ensureChatBackend()) {
        if (auto *raw = qobject_cast<RawChatBackend *>(b))
            raw->setThinkingEnabled(enabled);
    }
}

// ── Managed-process lifecycle ─────────────────────────────────────────────────

QString AppController::serviceStatePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/services.json");
}

void AppController::writeServiceState(const QString &role, qint64 pid, const QVariantMap &extra)
{
    const QString path = serviceStatePath();
    QJsonObject root;
    QFile f(path);
    if (f.open(QIODevice::ReadOnly))
        root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();

    root[QStringLiteral("app_pid")] = static_cast<qint64>(QCoreApplication::applicationPid());

    QJsonObject entry;
    entry[QStringLiteral("pid")]     = pid;
    entry[QStringLiteral("started")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    for (auto it = extra.cbegin(); it != extra.cend(); ++it)
        entry[it.key()] = QJsonValue::fromVariant(it.value());
    root[role] = entry;

    QFile out(path);
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        out.write(QJsonDocument(root).toJson());
}

void AppController::clearServiceState(const QString &role)
{
    const QString path = serviceStatePath();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    root.remove(role);
    QFile out(path);
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        out.write(QJsonDocument(root).toJson());
}

void AppController::killManagedOrphans()
{
    const QString path = serviceStatePath();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();

    // If the app that spawned these is still alive, don't kill them
    const qint64 ownerPid = root.value(QStringLiteral("app_pid")).toInteger();
    if (ownerPid > 0 && ownerPid != QCoreApplication::applicationPid()) {
#ifdef Q_OS_WIN
        HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(ownerPid));
        const bool ownerAlive = h && (WaitForSingleObject(h, 0) == WAIT_TIMEOUT);
        if (h) CloseHandle(h);
#else
        const bool ownerAlive = (::kill(static_cast<pid_t>(ownerPid), 0) == 0);
#endif
        if (ownerAlive) return;  // Previous owner still running — leave its processes alone
    }

    // Owner is dead — kill orphans
    const QStringList roles = root.keys();
    for (const QString &role : roles) {
        if (role == QLatin1String("app_pid")) continue;
        const qint64 pid = root.value(role).toObject().value(QStringLiteral("pid")).toInteger();
        if (pid <= 0) continue;
#ifdef Q_OS_WIN
        HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
        if (h) {
            if (WaitForSingleObject(h, 0) == WAIT_TIMEOUT)  // still running
                TerminateProcess(h, 1);
            CloseHandle(h);
        }
#else
        ::kill(static_cast<pid_t>(pid), SIGTERM);
#endif
    }

    // Clear state — will be repopulated as new processes start
    QFile::remove(path);
}

void AppController::createJobObject()
{
#ifdef Q_OS_WIN
    m_jobObject = CreateJobObjectW(nullptr, nullptr);
    if (m_jobObject) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(static_cast<HANDLE>(m_jobObject),
                                JobObjectExtendedLimitInformation, &info, sizeof(info));
    }
#endif
}

void AppController::assignToJobObject(qint64 pid)
{
#ifdef Q_OS_WIN
    if (!m_jobObject) return;
    HANDLE h = OpenProcess(PROCESS_ALL_ACCESS, FALSE, static_cast<DWORD>(pid));
    if (h) {
        AssignProcessToJobObject(static_cast<HANDLE>(m_jobObject), h);
        CloseHandle(h);
    }
#else
    Q_UNUSED(pid)
#endif
}

struct TrEntry { const char *key, *es, *en, *zh, *fr, *it, *de; };

static const TrEntry k_tr[] = {
    // Nav
    {"nav.launch",   "Lanzar",        "Launch",       "启动",       "Lancer",        "Avvia",          "Starten"},
    {"nav.profiles", "Perfiles",       "Profiles",     "配置",       "Profils",        "Profili",        "Profile"},
    {"nav.models",   "Modelos",        "Models",       "模型",       "Modèles",   "Modelli",        "Modelle"},
    {"nav.binaries", "Binarios",       "Binaries",     "二进制", "Binaires",       "Binari",         "Binärdateien"},
    {"nav.chat",      "Chat",           "Chat",         "聊天",       "Discussion",     "Chat",           "Chat"},
    {"nav.benchmark", "Benchmark",     "Benchmark",    "基准测试",   "Benchmark",      "Benchmark",      "Benchmark"},
    {"nav.settings",  "Configuración", "Settings",     "设置",       "Paramètres",     "Impostazioni",   "Einstellungen"},
    // Launch page
    {"launch.title",       "Lanzar",          "Launch",          "启动",       "Lancer",          "Avvia",               "Starten"},
    {"launch.running",     "Servidor activo", "Server running",  "服务器运行中", "Serveur actif",   "Server in esecuzione","Server läuft"},
    {"launch.stopped",     "Servidor detenido","Server stopped", "服务器已停止", "Serveur arrêté","Server fermo",   "Server gestoppt"},
    {"launch.profile",     "Perfil de lanzamiento","Launch Profile","启动配置","Profil de lancement","Profilo di avvio","Startprofil"},
    {"launch.select",      "— seleccionar —", "— select —",      "— 选择 —",   "— sélectionner —","— seleziona —", "— auswählen —"},
    {"launch.cmdPreview",  "Vista previa del comando","Command Preview","命令预览","Aperçu de la commande","Anteprima comando","Befehlsvorschau"},
    {"launch.startServer", "Iniciar servidor","Start Server",    "启动服务器","Démarrer le serveur","Avvia server","Server starten"},
    {"launch.stopServer",  "Detener servidor","Stop Server",     "停止服务器","Arrêter le serveur","Ferma server","Server stoppen"},
    {"launch.preview",     "Vista previa",   "Preview",          "预览",        "Aperçu",     "Anteprima",          "Vorschau"},
    {"launch.showLog",     "Ver log",        "Show log",         "显示日志","Voir le journal","Mostra log",       "Log anzeigen"},
    {"launch.hideLog",     "Ocultar log",    "Hide log",         "隐藏日志","Masquer le journal","Nascondi log", "Log ausblenden"},
    {"launch.clear",       "Limpiar",        "Clear",            "清除",        "Effacer",         "Cancella",           "Leeren"},
    {"launch.copyLogs",    "Copiar logs",    "Copy logs",        "复制日志","Copier les journaux","Copia log",  "Logs kopieren"},
    {"launch.serverLog",   "Log del servidor","Server Log",      "服务器日志","Journal du serveur","Log del server","Server-Log"},
    {"launch.binary",      "Binario",        "Binary",           "二进制文件","Binaire","Binario",          "Binärdatei"},
    {"launch.valid",       "Válido",    "Valid",            "有效",        "Valide",          "Valido",             "Gültig"},
    {"launch.yes",         "Sí",        "Yes",              "是",              "Oui",             "Sì",            "Ja"},
    {"launch.no",          "No",             "No",               "否",              "Non",             "No",                 "Nein"},
    // Profiles page
    {"profiles.title",         "Perfiles",         "Profiles",           "配置文件","Profils",           "Profili",              "Profile"},
    {"profiles.subtitle",      "Editor completo de perfil","Full profile editor","完整配置编辑器","Éditeur de profil complet","Editor completo profilo","Vollständiger Profil-Editor"},
    {"profiles.smokeTesting",  "Testeando...",     "Testing...",         "测试中...", "Test en cours...",   "Test in corso...",     "Teste..."},
    {"profiles.smokeTest",     "Smoke-Test",       "Smoke Test",         "冒烟测试","Test rapide",       "Smoke Test",           "Smoke-Test"},
    {"profiles.new",           "Nuevo",            "New",                "新建",       "Nouveau",            "Nuovo",                "Neu"},
    {"profiles.duplicate",     "Duplicar",         "Duplicate",          "复制",       "Dupliquer",          "Duplica",              "Duplizieren"},
    {"profiles.rename",        "Renombrar",        "Rename",             "重命名",  "Renommer",           "Rinomina",             "Umbenennen"},
    {"profiles.cancel",        "Cancelar",         "Cancel",             "取消",       "Annuler",            "Annulla",              "Abbrechen"},
    {"profiles.save",          "Guardar",          "Save",               "保存",       "Enregistrer",        "Salva",                "Speichern"},
    {"profiles.delete",        "Eliminar",         "Delete",             "删除",       "Supprimer",          "Elimina",              "Löschen"},
    {"profiles.deleteTitle",   "Eliminar perfil",  "Delete profile",     "删除配置","Supprimer le profil","Elimina profilo",  "Profil löschen"},
    {"profiles.renameTitle",   "Renombrar perfil", "Rename profile",     "重命名配置","Renommer le profil","Rinomina profilo","Profil umbenennen"},
    {"profiles.smokeTestPassed","✓ Smoke-Test pasó","✓ Smoke Test passed","✓ 测试通过","✓ Test réussi","✓ Test superato","✓ Test bestanden"},
    {"profiles.smokeTestFailed","✗ Smoke-Test falló","✗ Smoke Test failed","✗ 测试失败","✗ Test échoué","✗ Test fallito","✗ Test fehlgeschlagen"},
    {"profiles.extraArgs",     "Args manuales adicionales","Manual extra args","手动额外参数","Arguments supplémentaires","Args aggiuntivi manuali","Manuelle Zusatzargumente"},
    {"profiles.envOverrides",  "envOverrides (JSON)","envOverrides (JSON)","环境变量覆盖","Remplacements env (JSON)","envOverrides (JSON)","envOverrides (JSON)"},
    // Binaries page
    {"binaries.title",        "Binarios",         "Binaries",            "二进制文件","Binaires",         "Binari",               "Binärdateien"},
    {"binaries.addBinary",    "Agregar binario",  "Add Binary",          "添加二进制","Ajouter un binaire","Aggiungi binario",     "Binärdatei hinzufügen"},
    {"binaries.addAction",    "+ Agregar binario","+ Add Binary",        "+ 添加二进制","+ Ajouter binaire","+ Aggiungi binario",  "+ Binärdatei hinzufügen"},
    {"binaries.path",         "Ruta",             "Path",                "路径",        "Chemin",            "Percorso",             "Pfad"},
    {"binaries.name",         "Nombre",           "Name",                "名称",        "Nom",               "Nome",                 "Name"},
    {"binaries.flavor",       "Variante",         "Flavor",              "版本",        "Variante",          "Variante",             "Variante"},
    {"binaries.backend",      "Backend",          "Backend",             "后端",        "Backend",           "Backend",              "Backend"},
    {"binaries.status",       "Estado",           "Status",              "状态",        "Statut",            "Stato",                "Status"},
    {"binaries.flags",        "Flags",            "Flags",               "标志",        "Indicateurs",       "Flag",                 "Flags"},
    {"binaries.found",        "✓ Encontrado","✓ Found",        "✓ 已找到","✓ Trouvé","✓ Trovato",   "✓ Gefunden"},
    {"binaries.notFound",     "✗ No encontrado","✗ Not found", "✗ 未找到","✗ Introuvable","✗ Non trovato","✗ Nicht gefunden"},
    {"binaries.detected",     "detectados",       "detected",            "已检测",  "détectés","rilevati",            "erkannt"},
    {"binaries.notDetected",  "No detectado",     "Not detected",        "未检测",  "Non détecté","Non rilevato",     "Nicht erkannt"},
    {"binaries.browse",       "Explorar",         "Browse",              "浏览",        "Parcourir",         "Sfoglia",              "Durchsuchen"},
    {"binaries.rename",       "Renombrar",        "Rename",              "重命名",   "Renommer",          "Rinomina",             "Umbenennen"},
    {"binaries.detectCaps",   "Detectar capacidades","Detect Capabilities","检测功能","Détecter les capacités","Rileva capacità","Fähigkeiten erkennen"},
    {"binaries.remove",       "Eliminar",         "Remove",              "移除",        "Supprimer",         "Rimuovi",              "Entfernen"},
    {"binaries.selectBinary",   "Seleccionar un binario","Select a binary","选择二进制","Sélectionner un binaire","Seleziona un binario","Binärdatei auswählen"},
    {"binaries.registered",     "registrados",      "registered",          "已注册",  "enregistrés",  "registrati",           "registriert"},
    {"binaries.downloadLatest", "↓ Descargar latest","↓ Download Latest",  "↓ 下载最新","↓ Télécharger latest","↓ Scarica latest","↓ Neueste herunterladen"},
    {"binaries.downloading",    "Descargando...",   "Downloading...",      "下载中...", "Téléchargement...","Download in corso...","Herunterladen..."},
    // Models page
    {"models.title",       "Modelos",           "Model Roots",           "模型目录","Racines des modèles","Radici modelli",  "Modell-Verzeichnisse"},
    {"models.addRoot",     "Agregar directorio","Add Model Root",        "添加目录","Ajouter une racine","Aggiungi radice",       "Verzeichnis hinzufügen"},
    {"models.addAction",   "+ Agregar directorio","+ Add Root",          "+ 添加目录","+ Ajouter racine","+ Aggiungi radice",    "+ Verzeichnis hinzufügen"},
    {"models.scanning",    "Escaneando...",     "Scanning…",        "扫描中...", "Analyse en cours…","Scansione in corso…","Scannen…"},
    {"models.scan",        "Escanear",         "Scan",                  "扫描",          "Analyser",          "Scansiona",            "Scannen"},
    {"models.scanAll",     "Escanear todo",    "Scan All",              "扫描全部","Tout analyser",   "Scansiona tutto",      "Alles scannen"},
    {"models.removeRoot",  "Eliminar directorio","Remove Root",         "删除目录","Supprimer la racine","Rimuovi radice",     "Verzeichnis entfernen"},
    {"models.filterFamily","Filtrar por familia...","Filter by family…","按族过滤...","Filtrer par famille…","Filtra per famiglia…","Nach Familie filtern…"},
    {"models.visionOnly",  "Solo visión", "Vision only",           "仅视觉",   "Vision uniquement", "Solo visione",         "Nur Vision"},
    {"models.selectRoot",  "Seleccionar un directorio","Select a root to view models","选择目录查看模型","Sélectionner une racine","Seleziona una radice","Verzeichnis auswählen"},
    {"models.noModels",    "Sin modelos. Hacer Scan.","No models found. Click Scan.","未找到模型，点击扫描","Aucun modèle. Cliquer Scan.","Nessun modello. Clicca Scan.","Keine Modelle. Scan klicken."},
    {"models.path",        "Ruta",             "Path",                  "路径",          "Chemin",            "Percorso",             "Pfad"},
    {"models.label",       "Etiqueta",         "Label",                 "标签",          "Étiquette",    "Etichetta",            "Bezeichnung"},
    {"models.scanMode",    "Modo de escaneo",  "Scan mode",             "扫描模式","Mode d'analyse",  "Modalità scansione","Scan-Modus"},
    {"models.roots",       "roots",            "roots",                 "目录",          "racines",           "radici",               "Verzeichnisse"},
    {"models.modelsCount", "modelos",          "models",                "模型",          "modèles",      "modelli",              "Modelle"},
    // Chat
    {"chat.serverStopped", "servidor detenido","server stopped",        "服务器已停止","serveur arrêté","server fermo","Server gestoppt"},
    {"chat.startMessage",  "Escribe un mensaje para empezar","Write a message to start","写一条消息开始","Escribez un message pour commencer","Scrivi un messaggio per iniziare","Schreib eine Nachricht zum Starten"},
    {"chat.startServer",   "Iniciá el servidor para chatear","Start the server to chat","启动服务器开始聊天","Démarrez le serveur pour discuter","Avvia il server per chattare","Starte den Server zum Chatten"},
    {"chat.clear",         "Limpiar",          "Clear",                 "清除",          "Effacer",           "Cancella",             "Leeren"},
    {"chat.generating",    "Generando...",     "Generating...",         "生成中...", "Génération...","È in corso...", "Generiere..."},
    {"chat.placeholder",   "Escribe un mensaje...","Write a message...","写一条消息...","Saisissez un message...","Scrivi un messaggio...","Nachricht schreiben..."},
    {"chat.stop",          "■  Parar",    "■  Stop",          "■  停止", "■  Arrêter","■  Ferma",        "■  Stopp"},
    {"chat.send",          "Enviar",           "Send",                  "发送",          "Envoyer",           "Invia",                "Senden"},
    // CommandPreview
    {"cmd.copy",           "Copiar",           "Copy",                  "复制",          "Copier",            "Copia",                "Kopieren"},
    {"cmd.copied",         "¡Copiado!",   "Copied!",               "已复制!",   "Copié !",      "Copiato!",             "Kopiert!"},
    {"cmd.noProfile",      "(sin perfil seleccionado)","(no profile selected)","(未选择配置)","(aucun profil sélectionné)","(nessun profilo selezionato)","(kein Profil ausgewählt)"},
    // Common
    {"common.ok",          "Aceptar",          "OK",                    "确定",          "OK",                "OK",                   "OK"},
    {"common.cancel",      "Cancelar",         "Cancel",                "取消",          "Annuler",           "Annulla",              "Abbrechen"},
    {"common.close",       "Cerrar",           "Close",                 "关闭",          "Fermer",            "Chiudi",               "Schließen"},
    {"common.browse",      "Explorar",         "Browse",                "浏览",          "Parcourir",         "Sfoglia",              "Durchsuchen"},
    {"common.deleteConfirm","¿Eliminar \"%1\"? Esta acción no se puede deshacer.","Delete \"%1\"? This action cannot be undone.","删除 \"%1\"？此操作无法撤销。","Supprimer \"%1\" ? Cette action est irréversible.","Eliminare \"%1\"? Questa azione non può essere annullata.","\"%1\" löschen? Diese Aktion kann nicht rückgängig gemacht werden."},
    {"common.selectPlaceholder","— seleccionar —","— select —",        "— 选择 —",      "— sélectionner —","— seleziona —",       "— auswählen —"},
    // Setup popup
    {"setup.title",        "Configuración inicial","Initial Setup","初始设置","Configuration initiale","Configurazione iniziale","Ersteinrichtung"},
    {"setup.description",  "No hay binarios ni modelos registrados. Necesitás instalar/localizar un llama-server y descargar al menos un modelo GGUF.",
                           "No binaries or models registered. You need to install/locate a llama-server and download at least one GGUF model.",
                           "没有注册的二进制文件或模型。需要安装/定位llama-server并下载至少一个GGUF模型。",
                           "Aucun binaire ni modèle enregistré. Vous devez installer/localiser un llama-server et télécharger au moins un modèle GGUF.",
                           "Nessun binario o modello registrato. È necessario installare/localizzare un llama-server e scaricare almeno un modello GGUF.",
                           "Keine Binärdateien oder Modelle registriert. Sie müssen einen llama-server installieren/finden und mindestens ein GGUF-Modell herunterladen."},
    {"setup.locateBinary", "Localizar binario","Locate binary",         "定位二进制","Localiser le binaire","Individua binario","Binärdatei suchen"},
    {"setup.installing",   "Instalando...",   "Installing...",          "安装中...", "Installation en cours...","Installazione in corso...","Installiere..."},
    {"setup.installBinary","Instalar binario","Install binary",         "安装二进制","Installer le binaire","Installa binario","Binärdatei installieren"},
    {"setup.cancel",       "Cancelar",        "Cancel",                 "取消",          "Annuler",           "Annulla",              "Abbrechen"},
    {"setup.locateModel", "Localizar carpeta","Locate folder",          "定位文件夹","Localiser le dossier","Individua cartella","Ordner suchen"},
    {"setup.downloadModel","Descargar modelo (GGUF)","Download model (GGUF)","下载模型(GGUF)","Télécharger modèle (GGUF)","Scarica modello (GGUF)","Modell herunterladen (GGUF)"},
    {"setup.goToModels",   "Ir a Modelos",    "Go to Model Roots",      "前往模型目录","Aller aux Racines modèles","Vai a Radici modelli","Zu Modell-Verzeichnissen"},
    {"setup.tip",          "El popup se cierra automáticamente cuando exista al menos 1 binario y 1 modelo.",
                           "Popup closes automatically when at least 1 binary and 1 model exist.",
                           "当至少存在1个二进制文件和1个模型时，弹窗自动关闭。",
                           "La fenêtre se ferme automatiquement quand au moins 1 binaire et 1 modèle existent.",
                           "Il popup si chiude automaticamente quando esistono almeno 1 binario e 1 modello.",
                           "Das Popup schließt sich automatisch, wenn mindestens 1 Binärdatei und 1 Modell vorhanden sind."},
    {"setup.installLog",   "Log de instalación","Installation Log", "安装日志","Journal d'installation","Log di installazione","Installationsprotokoll"},
    {"setup.copyLog",      "Copiar log",      "Copy log",               "复制日志","Copier le journal",  "Copia log",            "Log kopieren"},
    {"setup.viewLog",      "Ver log",         "View log",               "查看日志","Voir le journal",    "Vedi log",             "Log anzeigen"},
    // Agent page
    {"agent.title",        "Agente",          "Agent",             "代理",           "Agent",             "Agente",              "Agent"},
    {"agent.start",        "Iniciar agente",  "Start agent",       "启动代理",   "Démarrer l'agent","Avvia agente",       "Agent starten"},
    {"agent.stop",         "Detener agente",  "Stop agent",        "停止代理",   "Arrêter l'agent", "Ferma agente",       "Agent stoppen"},
    {"agent.running",      "Agente activo",   "Agent running",     "代理运行中", "Agent actif",      "Agente attivo",      "Agent läuft"},
    {"agent.stopped",      "Agente detenido", "Agent stopped",     "代理已停止", "Agent arrêté","Agente fermo",       "Agent gestoppt"},
    {"agent.log",          "Log del agente",  "Agent log",         "代理日志",   "Journal de l'agent","Log dell'agente","Agent-Log"},
    {"agent.input",        "Escribe un comando o prompt...", "Write a command or prompt...", "输入命令或提示...", "Saisir une commande...", "Scrivi un comando...", "Befehl eingeben..."},
    {"agent.send",         "Enviar",          "Send",              "发送",           "Envoyer",           "Invia",               "Senden"},
    {"agent.noHarness",    "Sin harness configurado para este perfil","No harness configured for this profile","此配置文件未配置执行框架","Aucun harness configuré pour ce profil","Nessun harness configurato per questo profilo","Kein Harness für dieses Profil konfiguriert"},
    {"agent.serverWarn",   "Servidor no activo — el agente puede no conectarse","Server not running — agent may not connect","服务器未运行 — 代理可能无法连接","Serveur inactif — l'agent peut ne pas se connecter","Server non attivo — l'agente potrebbe non connettersi","Server nicht aktiv — Agent verbindet sich möglicherweise nicht"},
    {"agent.profile",      "Perfil",          "Profile",           "配置文件",   "Profil",            "Profilo",             "Profil"},
    {"agent.clear",        "Limpiar",         "Clear",             "清除",           "Effacer",           "Cancella",            "Leeren"},
    {"agent.notInstalled", "Harness no instalado. Instalar en la sección Perfiles.", "Harness not installed. Install it in the Profiles section.", "执行框架未安装，请在配置文件页安装。","Harness non installé. Installer dans la section Profils.","Harness non installato. Installa nella sezione Profili.","Harness nicht installiert. In der Profile-Sektion installieren."},
    // Harness
    {"harness.title",        "Harness",         "Harness",            "执行框架",  "Harness",           "Harness",             "Harness"},
    {"harness.none",         "Ninguno",         "None",               "无",            "Aucun",             "Nessuno",             "Keiner"},
    {"harness.installed",    "Instalado",       "Installed",          "已安装",    "Installé",     "Installato",          "Installiert"},
    {"harness.notInstalled", "No instalado",    "Not installed",      "未安装",    "Non installé","Non installato",     "Nicht installiert"},
    {"harness.install",      "Instalar",        "Install",            "安装",          "Installer",         "Installa",            "Installieren"},
    {"harness.installing",   "Instalando...",   "Installing...",      "安装中...", "Installation...","Installazione...","Installiere..."},
    {"harness.cancelInstall","Cancelar",        "Cancel",             "取消",          "Annuler",           "Annulla",             "Abbrechen"},
    // Settings page
    {"settings.title",      "Configuración","Settings",           "设置",           "Paramètres",   "Impostazioni",         "Einstellungen"},
    {"settings.appearance", "Apariencia",      "Appearance",            "外观",           "Apparence",         "Aspetto",              "Erscheinungsbild"},
    {"settings.theme",      "Tema",            "Theme",                 "主题",           "Thème",        "Tema",                 "Design"},
    {"settings.dark",       "Oscuro",          "Dark",                  "深色",           "Sombre",            "Scuro",                "Dunkel"},
    {"settings.light",      "Claro",           "Light",                 "浅色",           "Clair",             "Chiaro",               "Hell"},
    {"settings.oled",       "OLED",            "OLED",                  "OLED",                   "OLED",              "OLED",                 "OLED"},
    {"settings.language",   "Idioma",          "Language",              "语言",           "Langue",            "Lingua",               "Sprache"},
};

const QHash<QString, QHash<QString, QString>> &AppController::translations()
{
    static QHash<QString, QHash<QString, QString>> t = []() {
        QHash<QString, QHash<QString, QString>> h;
        for (const auto &e : k_tr) {
            h[QStringLiteral("es")][QLatin1String(e.key)] = QString::fromUtf8(e.es);
            h[QStringLiteral("en")][QLatin1String(e.key)] = QString::fromUtf8(e.en);
            h[QStringLiteral("zh")][QLatin1String(e.key)] = QString::fromUtf8(e.zh);
            h[QStringLiteral("fr")][QLatin1String(e.key)] = QString::fromUtf8(e.fr);
            h[QStringLiteral("it")][QLatin1String(e.key)] = QString::fromUtf8(e.it);
            h[QStringLiteral("de")][QLatin1String(e.key)] = QString::fromUtf8(e.de);
        }
        return h;
    }();
    return t;
}

// ── Benchmark ─────────────────────────────────────────────────────────────────

struct BenchTaskDef {
    QString id;
    QString category;
    QString prompt;
    int maxTokens;
    bool isSpeed;
    std::function<bool(const QString &)> eval;
};

static bool benchEvalJson(const QString &r)
{
    const QString tr = r.trimmed();
    int s = tr.indexOf('{'), e = tr.lastIndexOf('}');
    if (s < 0 || e <= s) return false;
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(tr.mid(s, e - s + 1).toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;
    const auto o = doc.object();
    return o.contains("name") && o.contains("age") && o.contains("active");
}

static QVector<BenchTaskDef> buildBenchTasks(const QString &mode)
{
    const bool full = (mode == QStringLiteral("full"));
    QVector<BenchTaskDef> t;

    // ── Speed tasks (streaming, measure TTFT + TPS) ────────────────────────────
    t.append({"speed_short", "speed",
        "Write a Python function to check if a number is prime.",
        512, true, nullptr});

    t.append({"speed_medium", "speed",
        "You are a senior Python engineer. Review the following code and suggest "
        "specific improvements for performance, readability, and correctness.\n\n"
        "```python\ndef process_data(items):\n    result = []\n    for i in range(len(items)):\n"
        "        if items[i] > 0:\n            result.append(items[i] * 2)\n"
        "        elif items[i] == 0:\n            result.append(0)\n"
        "        else:\n            result.append(abs(items[i]))\n"
        "    seen = {}\n    final = []\n    for x in result:\n"
        "        if x not in seen:\n            seen[x] = True\n            final.append(x)\n"
        "    total = 0\n    for x in final:\n        total = total + x\n    return total, final\n```\n\n"
        "Provide: 1) A rewritten version using idiomatic Python, 2) Big-O analysis of original vs new, "
        "3) Any edge cases the original misses. Be concise and specific.",
        1024, true, nullptr});

    if (full) {
        t.append({"speed_long", "speed",
            "You are an expert software architect. Perform a comprehensive security review of this "
            "authentication system. List ALL vulnerabilities found, categorize by OWASP Top 10, "
            "and provide specific remediation code for each.\n\n"
            "=== FILE: src/api/auth.py ===\n"
            "import jwt\nimport hashlib\nimport time\nfrom functools import wraps\n"
            "from flask import request, jsonify\n\n"
            "SECRET_KEY = 'hardcoded-secret-do-not-use-in-prod'\nALGORITHM = 'HS256'\n\n"
            "def generate_token(user_id: int, role: str) -> str:\n"
            "    payload = {'user_id': user_id, 'role': role, 'exp': time.time() + 3600}\n"
            "    return jwt.encode(payload, SECRET_KEY, algorithm=ALGORITHM)\n\n"
            "def verify_token(token: str) -> dict:\n"
            "    try:\n        return jwt.decode(token, SECRET_KEY, algorithms=[ALGORITHM])\n"
            "    except: return None\n\n"
            "def require_auth(f):\n    @wraps(f)\n    def decorated(*args, **kwargs):\n"
            "        token = request.headers.get('Authorization','').replace('Bearer ','')\n"
            "        payload = verify_token(token)\n"
            "        if not payload: return jsonify({'error':'Unauthorized'}), 401\n"
            "        request.user = payload\n        return f(*args, **kwargs)\n"
            "    return decorated\n",
            2048, true, nullptr});

        t.append({"speed_prose", "speed",
            "Write an 800-word essay about paper money during the Tang Dynasty in China. "
            "Cover its origins, how it worked, economic effects, and eventual decline. "
            "Write in clear, flowing prose paragraphs. Do not use bullet points or headers.",
            2048, true, nullptr});

        t.append({"speed_json", "speed",
            "You are given this list of programming concepts:\n"
            "[\"recursion\", \"for loop\", \"while loop\", \"memoization\", \"dynamic programming\", "
            "\"merge sort\", \"quick sort\", \"bubble sort\", \"binary search\", \"linear search\", "
            "\"hash map\", \"linked list\", \"binary tree\", \"graph traversal\", \"stack\", \"queue\", "
            "\"BFS\", \"DFS\", \"greedy algorithm\", \"divide and conquer\"]\n\n"
            "Group these into categories. Return ONLY a valid JSON object where keys are category "
            "names and values are arrays of items. No explanation, no markdown, no code fences.",
            512, true, nullptr});
    }

    // ── Quality tasks (non-streaming, eval response) ───────────────────────────
    t.append({"python_prime", "coding",
        "Write a Python function is_prime(n: int) -> bool with proper type hints. "
        "Handle edge cases (n<=1, n=2). No explanation, just code.",
        300, false,
        [](const QString &r) {
            return r.contains("def is_prime") && r.contains("return") &&
                   (r.contains("n <= 1") || r.contains("n < 2") || r.contains("n == 1"));
        }});

    t.append({"math_arithmetic", "math",
        "Calculate: (17 * 23) + (456 / 8) - 12. Show each step. Give the final numeric answer.",
        512, false,
        [](const QString &r) { return r.contains("436") || r.contains("436.0"); }});

    t.append({"code_refactor", "coding",
        "Rewrite this as a one-liner using list comprehension:\n"
        "result = []\nfor x in range(10):\n    if x % 2 == 0:\n        result.append(x**2)\n"
        "Return only the one-liner, no explanation.",
        100, false,
        [](const QString &r) {
            return r.contains("[") && r.contains("for") && r.contains("if") &&
                   (r.contains("**2") || r.contains("x*x") || r.contains("pow("));
        }});

    t.append({"reasoning_logic", "reasoning",
        "All A are B. All B are C. Is all A are C? Answer YES or NO, then explain in one sentence.",
        150, false,
        [](const QString &r) {
            const QString u = r.toUpper().trimmed();
            return u.startsWith("YES") || u.left(20).contains("YES");
        }});

    t.append({"json_output", "instruction",
        "Return ONLY valid JSON: {\"name\":\"Alice\",\"age\":30,\"active\":true}. "
        "No markdown, no explanation, no code fences.",
        80, false, benchEvalJson});

    if (full) {
        t.append({"coding_sort", "coding",
            "Implement merge sort in Python. Function signature: def merge_sort(arr: list) -> list. "
            "Include the merge helper function. No explanation, just code.",
            500, false,
            [](const QString &r) {
                return r.contains("def merge_sort") && r.contains("def merge") && r.contains("return");
            }});

        t.append({"summary_strict", "instruction",
            "Summarize what a neural network is in EXACTLY 2 sentences. No more, no less.",
            200, false,
            [](const QString &r) { return r.trimmed().length() > 20; }});
    }

    return t;
}

// ── Public methods ─────────────────────────────────────────────────────────────

void AppController::clearBenchmarkResults()
{
    // Delete persisted index + per-result files
    const QString dir = benchmarkStorageDir();
    QFile fi(dir + "/index.json");
    if (fi.open(QIODevice::ReadOnly)) {
        const QJsonArray arr = QJsonDocument::fromJson(fi.readAll()).array();
        fi.close();
        for (const QJsonValue &v : arr)
            QFile::remove(dir + "/" + v.toObject().value("id").toString() + ".json");
    }
    QFile::remove(dir + "/index.json");

    m_benchmarkResults.clear();
    emit benchmarkResultsChanged();
}

void AppController::removeBenchmarkResult(int index)
{
    if (index < 0 || index >= m_benchmarkResults.size()) return;

    const QString id = m_benchmarkResults.at(index).toMap().value("id").toString();
    m_benchmarkResults.removeAt(index);
    emit benchmarkResultsChanged();

    if (id.isEmpty()) return;

    // Remove from persisted index + delete its file
    const QString dir = benchmarkStorageDir();
    const QString idxPath = dir + "/index.json";
    QFile fi(idxPath);
    if (fi.open(QIODevice::ReadOnly)) {
        const QJsonArray arr = QJsonDocument::fromJson(fi.readAll()).array();
        fi.close();
        QJsonArray kept;
        for (const QJsonValue &v : arr)
            if (v.toObject().value("id").toString() != id)
                kept.append(v);
        if (fi.open(QIODevice::WriteOnly | QIODevice::Truncate))
            fi.write(QJsonDocument(kept).toJson());
    }
    QFile::remove(dir + "/" + id + ".json");
}

void AppController::cancelBenchmark()
{
    m_benchmarkCanceled = true;
}

void AppController::startBenchmark(const QStringList &profileIds, const QString &mode)
{
    if (m_benchmarkRunning || profileIds.isEmpty()) return;

    m_benchmarkRunning  = true;
    m_benchmarkCanceled = false;
    m_benchmarkProgress = 0;
    m_benchmarkStatus   = "Iniciando...";
    emit benchmarkRunningChanged();
    emit benchmarkProgressChanged();
    emit benchmarkStatusChanged();

    loadBenchmarkResults();

    const QVector<BenchTaskDef> tasks = buildBenchTasks(mode);
    const int totalSteps = profileIds.size() * tasks.size();
    auto stepsDone = std::make_shared<int>(0);

    // Recursive per-profile sequence
    auto processNext = std::make_shared<std::function<void(int)>>();
    *processNext = [=](int idx) {
        if (m_benchmarkCanceled || idx >= profileIds.size()) {
            m_benchmarkRunning  = false;
            m_benchmarkProgress = 100;
            m_benchmarkStatus   = m_benchmarkCanceled ? "Cancelado." : "Completado.";
            emit benchmarkRunningChanged();
            emit benchmarkProgressChanged();
            emit benchmarkStatusChanged();
            return;
        }

        const QString profileId   = profileIds.at(idx);
        const QVariantMap profData = m_profiles.getLaunchProfile(profileId);
        const QString profNameRaw = profData.value("name").toString();
        const QString profName    = profNameRaw.isEmpty() ? profileId : profNameRaw;

        auto runProfile = [=]() {
            m_benchmarkStatus = QString("[%1/%2] %3 — iniciando servidor...")
                .arg(idx + 1).arg(profileIds.size()).arg(profName);
            emit benchmarkStatusChanged();

            startServer(profileId);
            const QString url = serverBaseUrl();

            // Large models (big ctx + mlock + no-mmap) can take minutes to load
            // on a cold cache; 150 × 2s = 5 min before giving up.
            const QString loadPrefix = QString("[%1/%2] %3").arg(idx+1).arg(profileIds.size()).arg(profName);
            benchmarkWaitServerReady(150, 150, url, loadPrefix, [=](bool ready) {
                if (!ready || m_benchmarkCanceled) {
                    (*processNext)(idx + 1);
                    return;
                }

                // Warm-up request (discard result)
                m_benchmarkStatus = QString("[%1/%2] %3 — calentando...").arg(idx+1).arg(profileIds.size()).arg(profName);
                emit benchmarkStatusChanged();

                benchmarkRequest(url, "Say hi.", 32, true, [=](QVariantMap) {
                    // Run all tasks sequentially
                    auto taskResults = std::make_shared<QVariantList>();
                    auto runTask = std::make_shared<std::function<void(int)>>();

                    *runTask = [=](int ti) {
                        if (m_benchmarkCanceled || ti >= tasks.size()) {
                            // Measure resources then store result
                            benchmarkMeasureResources([=](double ramMb, double vramMb) {
                                int passed = 0, qualTotal = 0;
                                double tpsSum = 0, ttftSum = 0; int speedCount = 0;
                                for (const QVariant &v : *taskResults) {
                                    const QVariantMap r = v.toMap();
                                    if (r.value("type") == "quality") {
                                        qualTotal++;
                                        if (r.value("passed").toBool()) passed++;
                                    } else {
                                        tpsSum  += r.value("tps").toDouble();
                                        ttftSum += r.value("ttft_ms").toDouble();
                                        speedCount++;
                                    }
                                }
                                const double avgTps  = speedCount > 0 ? tpsSum  / speedCount : 0;
                                const double avgTtft = speedCount > 0 ? ttftSum / speedCount : 0;

                                QVariantMap result;
                                result["profileId"]    = profileId;
                                result["profileName"]  = profName;
                                result["mode"]         = mode;
                                result["timestamp"]    = (double)QDateTime::currentMSecsSinceEpoch();
                                result["qualityScore"] = passed;
                                result["qualityTotal"] = qualTotal;
                                result["avgTps"]       = avgTps;
                                result["avgTtftMs"]    = avgTtft;
                                result["ramMb"]        = ramMb;
                                result["vramMb"]       = vramMb;
                                result["tasks"]        = *taskResults;
                                result["id"]           = QUuid::createUuid().toString(QUuid::WithoutBraces);

                                m_benchmarkResults.append(result);
                                emit benchmarkResultsChanged();
                                saveBenchmarkResult(result);

                                stopServer();
                                benchmarkWaitServerStopped(10000, [=]() {
                                    (*processNext)(idx + 1);
                                });
                            });
                            return;
                        }

                        const BenchTaskDef &task = tasks.at(ti);
                        m_benchmarkStatus = QString("[%1/%2] %3 — %4...")
                            .arg(idx+1).arg(profileIds.size()).arg(profName).arg(task.id);
                        emit benchmarkStatusChanged();

                        benchmarkRequest(url, task.prompt, task.maxTokens, task.isSpeed, [=](QVariantMap res) {
                            (*stepsDone)++;
                            m_benchmarkProgress = qMin(99, (*stepsDone * 100) / qMax(1, totalSteps));
                            emit benchmarkProgressChanged();

                            res["id"]       = task.id;
                            res["category"] = task.category;
                            if (!task.isSpeed && task.eval)
                                res["passed"] = task.eval(res.value("response").toString());
                            taskResults->append(res);
                            (*runTask)(ti + 1);
                        });
                    };
                    (*runTask)(0);
                });
            });
        };

        if (serverRunning()) {
            stopServer();
            benchmarkWaitServerStopped(8000, runProfile);
        } else {
            runProfile();
        }
    };
    (*processNext)(0);
}

// ── Helpers ───────────────────────────────────────────────────────────────────

void AppController::benchmarkWaitServerReady(int attemptsLeft, int totalAttempts, const QString &url,
                                              const QString &statusPrefix,
                                              std::function<void(bool)> onResult)
{
    if (attemptsLeft <= 0) { onResult(false); return; }

    // Surface load progress: model load can take minutes for big-ctx profiles.
    const int elapsedSec = (totalAttempts - attemptsLeft) * 2;
    m_benchmarkStatus = QString("%1 — cargando modelo (puede tardar)... %2s")
                            .arg(statusPrefix).arg(elapsedSec);
    emit benchmarkStatusChanged();

    if (!m_nam) m_nam = new QNetworkAccessManager(this);
    auto *reply = m_nam->get(QNetworkRequest(QUrl(url + "/health")));
    connect(reply, &QNetworkReply::finished, this, [=]() {
        const bool ok = reply->error() == QNetworkReply::NoError &&
                        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        reply->deleteLater();
        if (ok) { onResult(true); return; }
        QTimer::singleShot(2000, this, [=]() {
            benchmarkWaitServerReady(attemptsLeft - 1, totalAttempts, url, statusPrefix, onResult);
        });
    });
}

void AppController::benchmarkWaitServerStopped(int remainingMs, std::function<void()> onStopped)
{
    if (!serverRunning() || remainingMs <= 0) { onStopped(); return; }
    QTimer::singleShot(300, this, [=]() {
        benchmarkWaitServerStopped(remainingMs - 300, onStopped);
    });
}

void AppController::benchmarkRequest(const QString &url, const QString &prompt,
                                      int maxTokens, bool streaming,
                                      std::function<void(QVariantMap)> onDone)
{
    if (!m_nam) m_nam = new QNetworkAccessManager(this);
    QNetworkRequest req(QUrl(url + "/v1/chat/completions"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setTransferTimeout(120000);

    QJsonObject payload;
    payload["model"]      = "benchmark";
    payload["messages"]   = QJsonArray{QJsonObject{{"role","user"},{"content",prompt}}};
    payload["stream"]     = streaming;
    payload["max_tokens"] = maxTokens;
    payload["temperature"]= 0.0;
    payload["top_p"]      = 1.0;

    auto *reply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    const qint64 startMs = QDateTime::currentMSecsSinceEpoch();

    if (streaming) {
        struct SpeedState { QByteArray buf; qint64 ttftMs = -1; int chunks = 0; QString response; };
        auto state = std::make_shared<SpeedState>();

        connect(reply, &QNetworkReply::readyRead, this, [=]() {
            state->buf.append(reply->readAll());
            int nl;
            while ((nl = state->buf.indexOf('\n')) >= 0) {
                const QByteArray line = state->buf.left(nl).trimmed();
                state->buf.remove(0, nl + 1);
                if (!line.startsWith("data: ")) continue;
                const QByteArray json = line.mid(6);
                if (json == "[DONE]") continue;
                const QString delta = QJsonDocument::fromJson(json).object()
                    .value("choices").toArray().first().toObject()
                    .value("delta").toObject().value("content").toString();
                if (!delta.isEmpty()) {
                    if (state->ttftMs < 0) state->ttftMs = QDateTime::currentMSecsSinceEpoch() - startMs;
                    state->chunks++;
                    state->response += delta;
                }
            }
        });

        connect(reply, &QNetworkReply::finished, this, [=]() {
            const qint64 totalMs = QDateTime::currentMSecsSinceEpoch() - startMs;
            double tps = 0;
            if (state->ttftMs >= 0 && state->chunks > 0)
                tps = state->chunks / qMax(0.001, (totalMs - state->ttftMs) / 1000.0);
            QVariantMap r;
            r["type"]       = "speed";
            r["ttft_ms"]    = state->ttftMs;
            r["tps"]        = tps;
            r["chunks"]     = state->chunks;
            r["elapsed_ms"] = totalMs;
            r["response"]   = state->response;
            reply->deleteLater();
            onDone(r);
        });
    } else {
        connect(reply, &QNetworkReply::finished, this, [=]() {
            const qint64 totalMs = QDateTime::currentMSecsSinceEpoch() - startMs;
            QString response;
            if (reply->error() == QNetworkReply::NoError) {
                response = QJsonDocument::fromJson(reply->readAll()).object()
                    .value("choices").toArray().first().toObject()
                    .value("message").toObject().value("content").toString();
            }
            QVariantMap r;
            r["type"]       = "quality";
            r["elapsed_ms"] = totalMs;
            r["response"]   = response;
            reply->deleteLater();
            onDone(r);
        });
    }
}

void AppController::benchmarkMeasureResources(std::function<void(double, double)> onDone)
{
    // VRAM via nvidia-smi
    auto *nsmi = new QProcess(this);
    nsmi->start("nvidia-smi",
                {"--query-compute-apps=used_memory", "--format=csv,noheader,nounits"});
    connect(nsmi, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
            this, [=](int, QProcess::ExitStatus) {
        double vramMb = 0;
        for (const QString &line :
             QString::fromUtf8(nsmi->readAllStandardOutput()).split('\n', Qt::SkipEmptyParts))
            vramMb += line.trimmed().toDouble();
        nsmi->deleteLater();

        // RAM via tasklist
        auto *tl = new QProcess(this);
        tl->start("tasklist", {"/FI","IMAGENAME eq llama-server.exe","/FO","CSV","/NH"});
        connect(tl, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
                this, [=](int, QProcess::ExitStatus) {
            double ramMb = 0;
            for (const QString &line :
                 QString::fromUtf8(tl->readAllStandardOutput()).split('\n', Qt::SkipEmptyParts)) {
                const QStringList parts = line.split(',');
                if (parts.size() >= 5) {
                    const QString mem = parts[4].trimmed().remove('"').remove(" K").remove(',');
                    ramMb += mem.toDouble() / 1024.0;
                }
            }
            tl->deleteLater();
            onDone(ramMb, vramMb);
        });
    });
}

// ── Persistence ───────────────────────────────────────────────────────────────

QString AppController::benchmarkStorageDir() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + "/benchmarks";
    QDir().mkpath(dir);
    return dir;
}

void AppController::saveBenchmarkResult(const QVariantMap &result)
{
    const QString dir = benchmarkStorageDir();
    const QString existingId = result.value("id").toString();
    const QString id  = existingId.isEmpty()
                            ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                            : existingId;

    // Update index
    const QString idxPath = dir + "/index.json";
    QJsonArray idx;
    QFile fi(idxPath);
    if (fi.open(QIODevice::ReadOnly)) { idx = QJsonDocument::fromJson(fi.readAll()).array(); fi.close(); }

    QJsonObject summary;
    summary["id"]           = id;
    summary["profileId"]    = result.value("profileId").toString();
    summary["profileName"]  = result.value("profileName").toString();
    summary["mode"]         = result.value("mode").toString();
    summary["timestamp"]    = result.value("timestamp").toDouble();
    summary["qualityScore"] = result.value("qualityScore").toInt();
    summary["qualityTotal"] = result.value("qualityTotal").toInt();
    summary["avgTps"]       = result.value("avgTps").toDouble();
    summary["avgTtftMs"]    = result.value("avgTtftMs").toDouble();
    summary["ramMb"]        = result.value("ramMb").toDouble();
    summary["vramMb"]       = result.value("vramMb").toDouble();
    idx.append(summary);

    if (fi.open(QIODevice::WriteOnly | QIODevice::Truncate))
        fi.write(QJsonDocument(idx).toJson());

    // Full result file
    QFile rf(dir + "/" + id + ".json");
    if (rf.open(QIODevice::WriteOnly | QIODevice::Truncate))
        rf.write(QJsonDocument(QJsonObject::fromVariantMap(result)).toJson());
}

void AppController::loadBenchmarkResults()
{
    const QString idxPath = benchmarkStorageDir() + "/index.json";
    QFile f(idxPath);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    // Merge: add only entries not already in m_benchmarkResults
    QSet<QString> existing;
    for (const QVariant &v : std::as_const(m_benchmarkResults))
        existing.insert(v.toMap().value("id").toString());
    for (const QJsonValue &v : arr) {
        const QVariantMap m = v.toObject().toVariantMap();
        if (!existing.contains(m.value("id").toString()))
            m_benchmarkResults.append(m);
    }
    emit benchmarkResultsChanged();
}
