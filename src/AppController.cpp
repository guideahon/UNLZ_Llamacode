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
#include <QFile>
#include <QStandardPaths>

AppController::AppController(QObject *parent) : QObject(parent)
{
    QSettings s;
    m_language = s.value(QStringLiteral("language"), QStringLiteral("es")).toString();

    killManagedOrphans();
    createJobObject();
    loadChatSessions();

    m_binaries.refresh();
    m_roots.refresh();
    connect(&m_binaries, &BinaryRegistry::countChanged, this, &AppController::setupStateChanged);
    connect(&m_catalog, &ModelCatalog::countChanged, this, &AppController::setupStateChanged);
}

void AppController::startServer(const QString &launchProfileId)
{
    if (serverRunning()) {
        emit serverError("Server already running. Stop it first.");
        return;
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
        appendLog(QString::fromUtf8(m_proc->readAllStandardOutput()));
    });
    connect(m_proc, &QProcess::readyReadStandardError, this, [this]() {
        appendLog(QString::fromUtf8(m_proc->readAllStandardError()));
    });
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
        appendLog(QStringLiteral("\n[Server exited with code %1]\n").arg(code));
        clearServiceState(QStringLiteral("server"));
        if (m_stopKillTimer) { m_stopKillTimer->stop(); m_stopKillTimer->deleteLater(); m_stopKillTimer = nullptr; }
        m_serverStopping = false;
        m_proc->deleteLater();
        m_proc = nullptr;
        emit serverRunningChanged();
    });

    m_activeLaunchId = launchProfileId;
    appendLog(QStringLiteral("[%1] Starting: %2 %3\n")
              .arg(QDateTime::currentDateTime().toString("hh:mm:ss"),
                   binaryPath, args.join(' ')));

    m_proc->start(binaryPath, args);
    if (m_proc->waitForStarted(5000)) {
        assignToJobObject(m_proc->processId());
        const int portIdx = args.indexOf(QStringLiteral("--port"));
        QVariantMap extra;
        extra[QStringLiteral("launch_id")] = launchProfileId;
        extra[QStringLiteral("port")] = (portIdx >= 0 && portIdx + 1 < args.size())
                                            ? args[portIdx + 1].toInt() : 8080;
        writeServiceState(QStringLiteral("server"), m_proc->processId(), extra);
    }
    emit serverRunningChanged();
    emit activeLaunchIdChanged();
}

void AppController::stopServer()
{
    if (!m_proc || m_serverStopping) return;
    m_serverStopping = true;
    emit serverRunningChanged();
    appendLog("\n[Stopping server...]\n");
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

void AppController::clearLog()
{
    m_log.clear();
    emit serverLogChanged();
}

void AppController::copyToClipboard(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
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
    $pick = $assets | Where-Object { $_.name -match 'bin-win-cuda.*x64.*\.zip$' } | Select-Object -First 1
    if (-not $pick) { $pick = $assets | Where-Object { $_.name -match 'bin-win-(avx2|cpu|openblas).*x64.*\.zip$' } | Select-Object -First 1 }
    if (-not $pick) { $pick = $assets | Where-Object { $_.name -match 'win.*x64.*\.zip$' } | Select-Object -First 1 }
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
        const QString stdOut = QString::fromUtf8(m_installerProc->readAllStandardOutput()).trimmed();
        const QString stdErr = QString::fromUtf8(m_installerProc->readAllStandardError()).trimmed();

        bool ok = false;
        QString installedPath;
        QString message;
        if (exitCode == 0) {
            const QStringList lines = stdOut.split('\n', Qt::SkipEmptyParts);
            if (!lines.isEmpty()) installedPath = lines.last().trimmed();
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
    emit serverLogChanged();
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

void AppController::startAgent(const QString &launchProfileId)
{
    if (agentRunning()) return;

    const auto ctx = buildContext(launchProfileId);
    const QString adapter = ctx.harness.adapter;
    if (adapter.isEmpty() || adapter == QLatin1String("none")) {
        m_agentLog += QStringLiteral("[Error: no harness configured for this profile]\n");
        emit agentLogChanged();
        return;
    }
    const QString exe = QStandardPaths::findExecutable(adapter);
    if (exe.isEmpty()) {
        m_agentLog += QStringLiteral("[Error: '%1' not found in PATH — install it first]\n").arg(adapter);
        emit agentLogChanged();
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
    for (auto it = ctx.harness.env.cbegin(); it != ctx.harness.env.cend(); ++it)
        env.insert(it.key(), it.value());
    env.insert(QStringLiteral("LLAMACODE_MANAGED"), QStringLiteral("1"));
    env.insert(QStringLiteral("LLAMACODE_ROLE"),    QStringLiteral("harness-") + adapter);
    env.insert(QStringLiteral("LLAMACODE_APP_PID"), QString::number(QCoreApplication::applicationPid()));

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
        const QString chunk = QString::fromUtf8(m_agentProc->readAllStandardOutput());
        m_agentLog += chunk;
        emit agentLogChanged();
        if (m_activeAgentAdapter == QLatin1String("opencode") && m_opencodeSessionId.isEmpty()
                && chunk.contains(QLatin1String("server listening")))
            initOpencodeSession();
    });
    connect(m_agentProc, &QProcess::readyReadStandardError, this, [this]() {
        if (!m_agentProc) return;
        const QString chunk = QString::fromUtf8(m_agentProc->readAllStandardError());
        m_agentLog += chunk;
        emit agentLogChanged();
        if (m_activeAgentAdapter == QLatin1String("opencode") && m_opencodeSessionId.isEmpty()
                && chunk.contains(QLatin1String("server listening")))
            initOpencodeSession();
    });
    connect(m_agentProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
        m_agentLog += QStringLiteral("\n[agent exited with code %1]\n").arg(code);
        if (m_agentProc) {
            m_agentProc->deleteLater();
            m_agentProc = nullptr;
        }
        if (m_opencodeEventReply) {
            m_opencodeEventReply->abort();
            m_opencodeEventReply->deleteLater();
            m_opencodeEventReply = nullptr;
        }
        m_opencodeSessionId.clear();
        m_agentMessages.clear();
        m_currentAssistantIdx = -1;
        emit agentMessagesChanged();
        clearServiceState(QStringLiteral("harness"));
        m_activeAgentAdapter.clear();
        m_agentPid = 0;
        m_agentInTerminal = false;
        emit agentLogChanged();
        emit agentRunningChanged();
    });

    QString program = exe;
    QStringList programArgs = ctx.harness.args;
    if (adapter == QLatin1String("opencode")) {
        // Kill any stale process holding port 4096 before binding
#ifdef Q_OS_WIN
        QProcess::execute(QStringLiteral("cmd"),
            {QStringLiteral("/c"),
             QStringLiteral("for /f \"tokens=5\" %a in ('netstat -ano ^| findstr :4096 ^| findstr LISTENING') do taskkill /PID %a /F")});
#else
        QProcess::execute(QStringLiteral("sh"),
            {QStringLiteral("-c"), QStringLiteral("fuser -k 4096/tcp 2>/dev/null || true")});
#endif
        programArgs = QStringList{
            QStringLiteral("serve"),
            QStringLiteral("--hostname"), QStringLiteral("127.0.0.1"),
            QStringLiteral("--port"), QStringLiteral("4096")
        };
        m_opencodeAttachUrl = QStringLiteral("http://127.0.0.1:4096");
        m_agentLog += QStringLiteral("[opencode headless server mode]\n");
    }

    m_agentLog += QStringLiteral("[starting %1]\n").arg(adapter);
    if (!cwd.isEmpty())
        m_agentLog += QStringLiteral("[cwd: %1]\n").arg(QDir::toNativeSeparators(cwd));
    m_agentProc->start(program, programArgs);
    if (!m_agentProc->waitForStarted(5000)) {
        m_agentLog += QStringLiteral("[Error: failed to start agent process]\n");
        m_agentProc->deleteLater();
        m_agentProc = nullptr;
        m_activeAgentAdapter.clear();
        emit agentLogChanged();
        return;
    }
    m_agentPid = m_agentProc->processId();
    assignToJobObject(m_agentPid);
    QVariantMap agentExtra;
    agentExtra[QStringLiteral("adapter")] = adapter;
    if (adapter == QLatin1String("opencode"))
        agentExtra[QStringLiteral("port")] = 4096;
    writeServiceState(QStringLiteral("harness"), m_agentPid, agentExtra);
    m_agentLog += QStringLiteral("[%1 running (PID %2)]\n").arg(adapter).arg(m_agentPid);
    emit agentLogChanged();
    emit agentRunningChanged();
}

void AppController::stopAgent()
{
    if (m_opencodeEventReply) {
        m_opencodeEventReply->abort();
        m_opencodeEventReply->deleteLater();
        m_opencodeEventReply = nullptr;
    }
    m_opencodeSessionId.clear();

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
    m_agentProc->terminate();
    if (!m_agentProc->waitForFinished(2000))
        m_agentProc->kill();
}

void AppController::sendToAgent(const QString &text)
{
    if (text.trimmed().isEmpty()) return;
    if (!m_agentProc || m_agentProc->state() != QProcess::Running) return;

    m_agentLog += QStringLiteral("> %1\n").arg(text);
    emit agentLogChanged();

    if (m_activeAgentAdapter == QLatin1String("opencode")) {
        // Append user + empty assistant message to structured model
        QVariantMap userMsg;
        userMsg[QStringLiteral("role")]    = QStringLiteral("user");
        userMsg[QStringLiteral("content")] = text;
        userMsg[QStringLiteral("typing")]  = false;
        m_agentMessages.append(userMsg);
        QVariantMap asstMsg;
        asstMsg[QStringLiteral("role")]    = QStringLiteral("assistant");
        asstMsg[QStringLiteral("content")] = QString();
        asstMsg[QStringLiteral("typing")]  = true;
        m_agentMessages.append(asstMsg);
        m_currentAssistantIdx = m_agentMessages.size() - 1;
        emit agentMessagesChanged();

        if (m_opencodeSessionId.isEmpty()) {
            m_agentLog += QStringLiteral("[waiting: opencode session not ready yet]\n");
            emit agentLogChanged();
            return;
        }
        if (!m_nam) m_nam = new QNetworkAccessManager(this);
        QNetworkRequest req(QUrl(m_opencodeAttachUrl + QStringLiteral("/session/")
                                 + m_opencodeSessionId + QStringLiteral("/prompt_async")));
        req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
        const QJsonObject partObj{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), text}};
        const QJsonObject payload{{QStringLiteral("parts"), QJsonArray{partObj}}};
        auto *reply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                m_agentLog += QStringLiteral("[error sending message: %1]\n").arg(reply->errorString());
                emit agentLogChanged();
            }
        });
        return;
    }

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

void AppController::initOpencodeSession()
{
    if (!m_nam) m_nam = new QNetworkAccessManager(this);
    loadOpencodeSessionList([this]() { resumeOrCreateOpencodeSession(); });
}

void AppController::loadOpencodeSessionList(std::function<void()> then)
{
    if (!m_nam) m_nam = new QNetworkAccessManager(this);
    auto *reply = m_nam->get(QNetworkRequest(QUrl(m_opencodeAttachUrl + QStringLiteral("/session"))));
    connect(reply, &QNetworkReply::finished, this, [this, reply, then]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            const QJsonArray arr = QJsonDocument::fromJson(reply->readAll()).array();
            // Sort newest-first client-side
            QVector<QJsonObject> sorted;
            sorted.reserve(arr.size());
            for (const QJsonValue &v : arr) sorted.append(v.toObject());
            std::sort(sorted.begin(), sorted.end(), [](const QJsonObject &a, const QJsonObject &b) {
                return a.value(QStringLiteral("time")).toObject().value(QStringLiteral("created")).toDouble()
                     > b.value(QStringLiteral("time")).toObject().value(QStringLiteral("created")).toDouble();
            });
            m_agentSessions.clear();
            for (const QJsonObject &s : sorted) {
                const QString dir  = s.value(QStringLiteral("directory")).toString();
                const QString path = s.value(QStringLiteral("path")).toString();
                // Project display name = last two path segments of directory
                const QStringList parts = QDir::toNativeSeparators(dir).split(QDir::separator(), Qt::SkipEmptyParts);
                const QString projectName = parts.size() >= 2
                    ? parts[parts.size()-2] + QStringLiteral("/") + parts.last()
                    : (parts.isEmpty() ? QStringLiteral("(sin proyecto)") : parts.last());
                QVariantMap entry;
                entry[QStringLiteral("id")]          = s.value(QStringLiteral("id")).toString();
                entry[QStringLiteral("title")]        = s.value(QStringLiteral("title")).toString();
                entry[QStringLiteral("created")]      = s.value(QStringLiteral("time")).toObject()
                                                          .value(QStringLiteral("created")).toDouble();
                entry[QStringLiteral("projectId")]    = s.value(QStringLiteral("projectID")).toString();
                entry[QStringLiteral("projectName")]  = projectName;
                entry[QStringLiteral("projectDir")]   = dir;
                m_agentSessions.append(entry);
            }
            emit agentSessionsChanged();
        }
        if (then) then();
    });
}

void AppController::resumeOrCreateOpencodeSession()
{
    QSettings s;
    const QString savedId = s.value(QStringLiteral("opencode/lastSessionId")).toString();
    if (!savedId.isEmpty()) {
        for (const QVariant &v : std::as_const(m_agentSessions)) {
            if (v.toMap().value(QStringLiteral("id")).toString() == savedId) {
                m_opencodeSessionId = savedId;
                m_opencodeSessionTitle = v.toMap().value(QStringLiteral("title")).toString();
                emit agentSessionsChanged();
                m_agentLog += QStringLiteral("[opencode session resumed]\n");
                emit agentLogChanged();
                loadOpencodeSessionMessages(savedId);
                subscribeOpencodeEvents();
                return;
            }
        }
    }
    doCreateOpencodeSession();
}

void AppController::doCreateOpencodeSession()
{
    QNetworkRequest req(QUrl(m_opencodeAttachUrl + QStringLiteral("/session")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    auto *reply = m_nam->post(req, QByteArrayLiteral("{}"));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_agentLog += QStringLiteral("[error: failed to create opencode session: %1]\n")
                              .arg(reply->errorString());
            emit agentLogChanged();
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        m_opencodeSessionId    = obj.value(QStringLiteral("id")).toString();
        m_opencodeSessionTitle = obj.value(QStringLiteral("title")).toString();
        QSettings().setValue(QStringLiteral("opencode/lastSessionId"), m_opencodeSessionId);
        // Prepend to session list (reload full list to get project info from server)
        const QString cwd = m_agentProc ? m_agentProc->workingDirectory() : QString();
        const QStringList cwdParts = QDir::toNativeSeparators(cwd).split(QDir::separator(), Qt::SkipEmptyParts);
        const QString projectName = cwdParts.size() >= 2
            ? cwdParts[cwdParts.size()-2] + QStringLiteral("/") + cwdParts.last()
            : (cwdParts.isEmpty() ? QStringLiteral("(sin proyecto)") : cwdParts.last());
        QVariantMap entry;
        entry[QStringLiteral("id")]         = m_opencodeSessionId;
        entry[QStringLiteral("title")]      = m_opencodeSessionTitle;
        entry[QStringLiteral("created")]    = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
        entry[QStringLiteral("projectId")]  = obj.value(QStringLiteral("projectID")).toString();
        entry[QStringLiteral("projectName")]= projectName;
        entry[QStringLiteral("projectDir")] = cwd;
        m_agentSessions.prepend(entry);
        emit agentSessionsChanged();
        m_agentLog += QStringLiteral("[opencode session ready]\n");
        emit agentLogChanged();
        subscribeOpencodeEvents();
    });
}

void AppController::loadOpencodeSessionMessages(const QString &sessionId)
{
    if (!m_nam) return;
    const QUrl url(m_opencodeAttachUrl + QStringLiteral("/session/") + sessionId + QStringLiteral("/message"));
    auto *reply = m_nam->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        const QJsonArray msgs = QJsonDocument::fromJson(reply->readAll()).array();
        m_agentMessages.clear();
        m_currentAssistantIdx = -1;
        for (const QJsonValue &mv : msgs) {
            const QJsonObject msg = mv.toObject();
            const QString role = msg.value(QStringLiteral("info")).toObject()
                                     .value(QStringLiteral("role")).toString();
            QString text;
            const QJsonArray parts = msg.value(QStringLiteral("parts")).toArray();
            for (const QJsonValue &pv : parts) {
                const QJsonObject part = pv.toObject();
                if (part.value(QStringLiteral("type")).toString() == QLatin1String("text"))
                    text += part.value(QStringLiteral("text")).toString();
            }
            if (role.isEmpty() || text.isEmpty()) continue;
            QVariantMap entry;
            entry[QStringLiteral("role")]    = role;
            entry[QStringLiteral("content")] = text;
            entry[QStringLiteral("typing")]  = false;
            m_agentMessages.append(entry);
        }
        emit agentMessagesChanged();
    });
}

void AppController::newOpencodeSession()
{
    if (!m_nam || m_opencodeAttachUrl.isEmpty()) return;
    m_agentMessages.clear();
    m_currentAssistantIdx = -1;
    m_opencodeSessionId.clear();
    m_opencodeSessionTitle.clear();
    emit agentMessagesChanged();
    emit agentSessionsChanged();
    doCreateOpencodeSession();
}

void AppController::switchOpencodeSession(const QString &sessionId)
{
    if (sessionId == m_opencodeSessionId) return;
    m_opencodeSessionId = sessionId;
    m_agentMessages.clear();
    m_currentAssistantIdx = -1;
    for (const QVariant &v : std::as_const(m_agentSessions)) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("id")).toString() == sessionId) {
            m_opencodeSessionTitle = m.value(QStringLiteral("title")).toString();
            break;
        }
    }
    QSettings().setValue(QStringLiteral("opencode/lastSessionId"), sessionId);
    emit agentSessionsChanged();
    emit agentMessagesChanged();
    loadOpencodeSessionMessages(sessionId);
}

void AppController::refreshOpencodeSessionList()
{
    loadOpencodeSessionList(nullptr);
}

QString AppController::pickDirectory(const QString &title)
{
    const QString dir = QFileDialog::getExistingDirectory(
        nullptr,
        title.isEmpty() ? QStringLiteral("Seleccionar carpeta del proyecto") : title,
        QDir::homePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    return dir;
}

void AppController::changeAgentProject(const QString &directory)
{
    if (directory.isEmpty()) return;
    m_agentCwdOverride = directory;

    if (!agentRunning()) return;

    // Restart agent with new CWD — remember launch ID
    m_pendingAgentLaunchId = m_activeAgentAdapter.isEmpty()
        ? m_activeLaunchId : m_pendingAgentLaunchId;

    // When current agent stops, restart with override
    connect(this, &AppController::agentRunningChanged, this, [this]() {
        if (agentRunning()) return;  // wait until stopped
        disconnect(this, &AppController::agentRunningChanged, this, nullptr);
        if (!m_agentCwdOverride.isEmpty() && !m_activeLaunchId.isEmpty()) {
            const QString launchId = m_activeLaunchId.isEmpty()
                ? m_pendingAgentLaunchId : m_activeLaunchId;
            QTimer::singleShot(300, this, [this, launchId]() {
                startAgent(launchId);
            });
        }
    }, Qt::SingleShotConnection);

    stopAgent();
}

void AppController::subscribeOpencodeEvents()
{
    if (!m_nam) return;
    QNetworkRequest req(QUrl(m_opencodeAttachUrl + QStringLiteral("/event")));
    req.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("text/event-stream"));
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    m_opencodeEventReply = m_nam->get(req);

    connect(m_opencodeEventReply, &QNetworkReply::readyRead, this, [this]() {
        if (!m_opencodeEventReply) return;
        const QByteArray data = m_opencodeEventReply->readAll();
        bool changed = false;
        for (const QByteArray &raw : data.split('\n')) {
            const QByteArray line = raw.trimmed();
            if (!line.startsWith("data: ")) continue;
            const QJsonDocument doc = QJsonDocument::fromJson(line.mid(6));
            if (doc.isNull()) continue;
            const QJsonObject obj = doc.object();
            const QString type = obj.value(QStringLiteral("type")).toString();
            const QJsonObject props = obj.value(QStringLiteral("properties")).toObject();
            if (type == QLatin1String("message.part.delta")) {
                if (props.value(QStringLiteral("field")).toString() == QLatin1String("text")) {
                    const QString delta = props.value(QStringLiteral("delta")).toString();
                    if (!delta.isEmpty()) {
                        m_agentLog += delta;
                        if (m_currentAssistantIdx >= 0 && m_currentAssistantIdx < m_agentMessages.size()) {
                            auto msg = m_agentMessages[m_currentAssistantIdx].toMap();
                            msg[QStringLiteral("content")] = msg[QStringLiteral("content")].toString() + delta;
                            m_agentMessages[m_currentAssistantIdx] = msg;
                            emit agentMessagesChanged();
                        }
                        changed = true;
                    }
                }
            } else if (type == QLatin1String("session.updated")) {
                const QJsonObject info = props.value(QStringLiteral("info")).toObject();
                const QString title = info.value(QStringLiteral("title")).toString();
                const QString sid   = info.value(QStringLiteral("id")).toString();
                if (!title.isEmpty() && sid == m_opencodeSessionId) {
                    m_opencodeSessionTitle = title;
                    for (int i = 0; i < m_agentSessions.size(); ++i) {
                        auto sm = m_agentSessions[i].toMap();
                        if (sm.value(QStringLiteral("id")).toString() == sid) {
                            sm[QStringLiteral("title")] = title;
                            m_agentSessions[i] = sm;
                            break;
                        }
                    }
                    emit agentSessionsChanged();
                }
            } else if (type == QLatin1String("session.status")) {
                const QString status = props.value(QStringLiteral("status"))
                                            .toObject().value(QStringLiteral("type")).toString();
                if (status == QLatin1String("idle")) {
                    m_agentLog += QLatin1Char('\n');
                    if (m_currentAssistantIdx >= 0 && m_currentAssistantIdx < m_agentMessages.size()) {
                        auto msg = m_agentMessages[m_currentAssistantIdx].toMap();
                        msg[QStringLiteral("typing")] = false;
                        m_agentMessages[m_currentAssistantIdx] = msg;
                        emit agentMessagesChanged();
                    }
                    m_currentAssistantIdx = -1;
                    changed = true;
                }
            } else if (type.contains(QLatin1String("error"))) {
                const QString errMsg = props.value(QStringLiteral("message")).toString();
                if (!errMsg.isEmpty()) {
                    m_agentLog += QStringLiteral("[error: %1]\n").arg(errMsg);
                    if (m_currentAssistantIdx >= 0 && m_currentAssistantIdx < m_agentMessages.size()) {
                        auto msg = m_agentMessages[m_currentAssistantIdx].toMap();
                        msg[QStringLiteral("content")] = QStringLiteral("[error: %1]").arg(errMsg);
                        msg[QStringLiteral("typing")] = false;
                        m_agentMessages[m_currentAssistantIdx] = msg;
                        emit agentMessagesChanged();
                    }
                    m_currentAssistantIdx = -1;
                    changed = true;
                }
            }
        }
        if (changed) emit agentLogChanged();
    });

    connect(m_opencodeEventReply, &QNetworkReply::errorOccurred, this,
            [this](QNetworkReply::NetworkError) {
        if (!m_opencodeEventReply) return;
        m_agentLog += QStringLiteral("[opencode event stream disconnected]\n");
        emit agentLogChanged();
    });
}

QString AppController::agentNativeLogDir(const QString &adapter) const
{
    // opencode stores logs in XDG_DATA_HOME/<adapter>/log/ on all platforms
    const QString xdgData = qEnvironmentVariable("XDG_DATA_HOME",
        QDir::homePath() + QStringLiteral("/.local/share"));
    return QDir::toNativeSeparators(xdgData + QStringLiteral("/") + adapter + QStringLiteral("/log"));
}

void AppController::openAgentLogDir(const QString &adapter) const
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(agentNativeLogDir(adapter)));
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
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
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
    std::sort(sorted.begin(), sorted.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value(QStringLiteral("updated")).toDouble() > b.value(QStringLiteral("updated")).toDouble();
    });
    for (const auto &s : sorted) m_chatSessions.append(s);
    emit chatSessionsChanged();
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
        const QString projId   = m_activeLaunchId;
        const QString projName = [&]() -> QString {
            const auto profiles = m_profiles.launchProfiles();
            for (int i = 0; i < profiles->rowCount(); ++i) {
                const auto idx2 = profiles->index(i, 0);
                if (profiles->data(idx2, 257).toString() == projId)
                    return profiles->data(idx2, Qt::DisplayRole).toString();
            }
            return projId;
        }();
        idx.append(QJsonObject{
            {QStringLiteral("id"),          m_chatSessionId},
            {QStringLiteral("title"),       m_chatSessionTitle},
            {QStringLiteral("projectId"),   projId},
            {QStringLiteral("projectName"), projName},
            {QStringLiteral("created"),     now},
            {QStringLiteral("updated"),     now}
        });
    }
    if (idxFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        idxFile.write(QJsonDocument(idx).toJson());

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
    if (m_chatGenerating) stopChatGeneration();
    m_chatSessionId    = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_chatSessionTitle = QString();
    m_chatMessages.clear();
    m_chatAssistantIdx = -1;
    emit chatSessionsChanged();
    emit chatMessagesChanged();
}

void AppController::switchChatSession(const QString &id)
{
    if (id == m_chatSessionId) return;
    if (m_chatGenerating) stopChatGeneration();
    m_chatSessionId    = id;
    m_chatMessages.clear();
    m_chatAssistantIdx = -1;
    for (const QVariant &v : std::as_const(m_chatSessions)) {
        const QVariantMap s = v.toMap();
        if (s.value(QStringLiteral("id")).toString() == id) {
            m_chatSessionTitle = s.value(QStringLiteral("title")).toString();
            break;
        }
    }
    emit chatSessionsChanged();
    loadChatSessionMessages(id);
}

void AppController::sendChatMessage(const QString &text)
{
    if (text.trimmed().isEmpty() || m_chatGenerating) return;
    if (m_chatSessionId.isEmpty()) newChatSession();
    if (!m_nam) m_nam = new QNetworkAccessManager(this);

    QVariantMap userMsg;
    userMsg[QStringLiteral("role")]    = QStringLiteral("user");
    userMsg[QStringLiteral("content")] = text;
    userMsg[QStringLiteral("typing")]  = false;
    m_chatMessages.append(userMsg);

    QVariantMap asstMsg;
    asstMsg[QStringLiteral("role")]    = QStringLiteral("assistant");
    asstMsg[QStringLiteral("content")] = QString();
    asstMsg[QStringLiteral("typing")]  = true;
    m_chatMessages.append(asstMsg);
    m_chatAssistantIdx = m_chatMessages.size() - 1;
    m_chatGenerating   = true;
    emit chatMessagesChanged();
    emit chatGeneratingChanged();

    QJsonArray msgs;
    for (int i = 0; i < m_chatMessages.size() - 1; ++i) {
        const QVariantMap &m = m_chatMessages[i].toMap();
        msgs.append(QJsonObject{{QStringLiteral("role"),    m.value(QStringLiteral("role")).toString()},
                                {QStringLiteral("content"), m.value(QStringLiteral("content")).toString()}});
    }
    QNetworkRequest req(QUrl(serverBaseUrl() + QStringLiteral("/v1/chat/completions")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    const QJsonObject payload{{QStringLiteral("model"),    QStringLiteral("default")},
                              {QStringLiteral("messages"), msgs},
                              {QStringLiteral("stream"),   true}};
    m_chatReply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));

    auto *buf = new QByteArray();
    connect(m_chatReply, &QNetworkReply::readyRead, this, [this, buf]() {
        if (!m_chatReply) return;
        buf->append(m_chatReply->readAll());
        int nl;
        while ((nl = buf->indexOf('\n')) >= 0) {
            const QByteArray line = buf->left(nl).trimmed();
            buf->remove(0, nl + 1);
            if (!line.startsWith("data: ")) continue;
            const QByteArray json = line.mid(6);
            if (json == "[DONE]") continue;
            const QString delta = QJsonDocument::fromJson(json).object()
                .value(QStringLiteral("choices")).toArray().first().toObject()
                .value(QStringLiteral("delta")).toObject()
                .value(QStringLiteral("content")).toString();
            if (!delta.isEmpty() && m_chatAssistantIdx >= 0 && m_chatAssistantIdx < m_chatMessages.size()) {
                auto msg = m_chatMessages[m_chatAssistantIdx].toMap();
                msg[QStringLiteral("content")] = msg[QStringLiteral("content")].toString() + delta;
                m_chatMessages[m_chatAssistantIdx] = msg;
                emit chatMessagesChanged();
            }
        }
    });

    connect(m_chatReply, &QNetworkReply::finished, this, [this, buf, text]() {
        delete buf;
        if (m_chatAssistantIdx >= 0 && m_chatAssistantIdx < m_chatMessages.size()) {
            auto msg = m_chatMessages[m_chatAssistantIdx].toMap();
            if (m_chatReply && m_chatReply->error() != QNetworkReply::NoError
                    && msg[QStringLiteral("content")].toString().isEmpty())
                msg[QStringLiteral("content")] = QStringLiteral("[Error: %1]").arg(m_chatReply->errorString());
            msg[QStringLiteral("typing")] = false;
            m_chatMessages[m_chatAssistantIdx] = msg;
        }
        if (m_chatReply) { m_chatReply->deleteLater(); m_chatReply = nullptr; }
        m_chatAssistantIdx = -1;
        m_chatGenerating   = false;
        emit chatMessagesChanged();
        emit chatGeneratingChanged();
        // Auto-title from first user message
        if (m_chatSessionTitle.isEmpty())
            m_chatSessionTitle = text.left(50);
        saveChatSession();
    });
}

void AppController::stopChatGeneration()
{
    if (m_chatReply) { m_chatReply->abort(); m_chatReply->deleteLater(); m_chatReply = nullptr; }
    if (m_chatAssistantIdx >= 0 && m_chatAssistantIdx < m_chatMessages.size()) {
        auto msg = m_chatMessages[m_chatAssistantIdx].toMap();
        msg[QStringLiteral("typing")] = false;
        m_chatMessages[m_chatAssistantIdx] = msg;
    }
    m_chatAssistantIdx = -1;
    m_chatGenerating   = false;
    emit chatMessagesChanged();
    emit chatGeneratingChanged();
    saveChatSession();
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
    {"nav.chat",     "Chat",           "Chat",         "聊天",       "Discussion",     "Chat",           "Chat"},
    {"nav.settings", "Configuración", "Settings", "设置",       "Paramètres","Impostazioni",   "Einstellungen"},
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
    {"setup.downloadModel","Descargar modelo (GGUF)","Download model (GGUF)","下载模型(GGUF)","Télécharger modèle (GGUF)","Scarica modello (GGUF)","Modell herunterladen (GGUF)"},
    {"setup.goToModels",   "Ir a Modelos",    "Go to Model Roots",      "前往模型目录","Aller aux Racines modèles","Vai a Radici modelli","Zu Modell-Verzeichnissen"},
    {"setup.tip",          "El popup se cierra automáticamente cuando exista al menos 1 binario o 1 modelo.",
                           "Popup closes automatically when at least 1 binary or 1 model exists.",
                           "当至少存在1个二进制文件或模型时，弹窗自动关闭。",
                           "La fenêtre se ferme automatiquement quand au moins 1 binaire ou 1 modèle existe.",
                           "Il popup si chiude automaticamente quando esiste almeno 1 binario o 1 modello.",
                           "Das Popup schließt sich automatisch, wenn mindestens 1 Binärdatei oder 1 Modell vorhanden ist."},
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
