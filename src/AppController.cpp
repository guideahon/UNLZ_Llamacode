#include "AppController.h"
#include <QProcess>
#include <QDateTime>
#include <QClipboard>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>

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
$headers = @{ 'User-Agent' = 'LlamaCode' }
$api = 'https://api.github.com/repos/ggml-org/llama.cpp/releases/latest'
$rel = Invoke-RestMethod -Uri $api -Headers $headers
$assets = @($rel.assets)
$pick = $assets | Where-Object { $_.name -match 'bin-win-cuda.*x64.*\.zip$' } | Select-Object -First 1
if (-not $pick) { $pick = $assets | Where-Object { $_.name -match 'bin-win-(avx2|cpu|openblas).*x64.*\.zip$' } | Select-Object -First 1 }
if (-not $pick) { $pick = $assets | Where-Object { $_.name -match 'win.*x64.*\.zip$' } | Select-Object -First 1 }
if (-not $pick) { throw 'No suitable Windows x64 binary asset found in latest release.' }

$dest = '%1'
New-Item -ItemType Directory -Force -Path $dest | Out-Null
$zip = Join-Path $dest $pick.name
Invoke-WebRequest -Uri $pick.browser_download_url -Headers $headers -OutFile $zip
$extract = Join-Path $dest 'llama.cpp-latest'
if (Test-Path $extract) { Remove-Item -LiteralPath $extract -Recurse -Force }
Expand-Archive -LiteralPath $zip -DestinationPath $extract -Force
$exe = Get-ChildItem -Path $extract -Recurse -Filter 'llama-server.exe' | Select-Object -First 1
if (-not $exe) { throw 'llama-server.exe not found after extraction.' }
Write-Output $exe.FullName
)PS").arg(QDir::toNativeSeparators(toolsDir).replace("'", "''"));

    m_installerProc = new QProcess(this);
    m_installingOfficialBinary = true;
    emit installingOfficialBinaryChanged();

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
            message = "Automatic install failed.";
            if (!stdErr.isEmpty()) message += " " + stdErr;
        }

        m_installerProc->deleteLater();
        m_installerProc = nullptr;
        m_installingOfficialBinary = false;
        emit installingOfficialBinaryChanged();
        emit setupStateChanged();
        emit officialBinaryInstallFinished(ok, message, installedPath);
    });

    m_installerProc->start("powershell", {"-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script});
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
