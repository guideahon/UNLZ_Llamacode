#include "AppController.h"
#include <QProcess>
#include <QDateTime>
#include <QClipboard>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

AppController::AppController(QObject *parent) : QObject(parent)
{
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
        m_proc->deleteLater();
        m_proc = nullptr;
        emit serverRunningChanged();
    });

    m_activeLaunchId = launchProfileId;
    appendLog(QStringLiteral("[%1] Starting: %2 %3\n")
              .arg(QDateTime::currentDateTime().toString("hh:mm:ss"),
                   binaryPath, args.join(' ')));

    m_proc->start(binaryPath, args);
    emit serverRunningChanged();
    emit activeLaunchIdChanged();
}

void AppController::stopServer()
{
    if (!m_proc) return;
    appendLog("\n[Stopping server...]\n");
    m_proc->terminate();
    if (!m_proc->waitForFinished(5000))
        m_proc->kill();
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
