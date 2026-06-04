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
#include <QTcpServer>
#include <QHostAddress>
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
#include "core/agent/McpClient.h"
#include <QtConcurrent>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QDesktopServices>
#include <QFileDialog>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QCoreApplication>
#include <QUuid>
#include <QDirIterator>
#include <functional>
#include <QSet>
#include <memory>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>
#include <algorithm>
#include <cmath>

struct ResearchHit {
    QString title;
    QString url;
    QString snippet;
};

// Agrupa sesiones por proyecto (secciones contiguas), proyecto más reciente
// arriba, y dentro de cada proyecto la sesión más nueva primero. Necesario
// porque ListView.section sólo agrupa filas contiguas: si la lista no viene
// ordenada por projectName, el mismo proyecto aparece partido en secciones.
static QVariantList groupSessionsByProject(const QVariantList &in)
{
    QVector<QVariantMap> rows;
    rows.reserve(in.size());
    for (const QVariant &v : in) rows.append(v.toMap());

    auto stamp = [](const QVariantMap &s) {
        const double u = s.value(QStringLiteral("updated")).toDouble();
        return u > 0.0 ? u : s.value(QStringLiteral("created")).toDouble();
    };

    QHash<QString, double> projLatest;
    for (const auto &s : std::as_const(rows)) {
        const QString p = s.value(QStringLiteral("projectName")).toString();
        const double u = stamp(s);
        if (u > projLatest.value(p, 0.0)) projLatest[p] = u;
    }
    std::stable_sort(rows.begin(), rows.end(), [&](const QVariantMap &a, const QVariantMap &b) {
        const QString pa = a.value(QStringLiteral("projectName")).toString();
        const QString pb = b.value(QStringLiteral("projectName")).toString();
        if (pa != pb) {
            const double la = projLatest.value(pa);
            const double lb = projLatest.value(pb);
            // Desempate por nombre: si los timestamps de proyecto empatan, igual
            // hay que forzar contigüidad o las secciones quedan intercaladas.
            if (la != lb) return la > lb;
            return pa < pb;
        }
        return stamp(a) > stamp(b);
    });

    QVariantList out;
    out.reserve(rows.size());
    for (const auto &s : std::as_const(rows)) out.append(s);
    return out;
}

static bool isSupportedImageAttachment(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == QLatin1String("png") || ext == QLatin1String("jpg")
           || ext == QLatin1String("jpeg") || ext == QLatin1String("webp")
           || ext == QLatin1String("gif") || ext == QLatin1String("bmp");
}

static QString researchCleanHtmlToText(QString text)
{
    text.remove(QRegularExpression(QStringLiteral("(?is)<(script|style)[^>]*>.*?</\\1>")));
    text.remove(QRegularExpression(QStringLiteral("(?s)<[^>]+>")));
    text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    text.replace(QStringLiteral("&#x27;"), QStringLiteral("'"));
    text.replace(QRegularExpression(QStringLiteral("[ \t]+")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("\n[ \t]*(?:\n[ \t]*)+")), QStringLiteral("\n\n"));
    return text.trimmed();
}

static QString researchResolveDdgRedirect(QString raw)
{
    if (raw.startsWith(QLatin1String("//"))) raw = QStringLiteral("https:") + raw;
    QUrl ru(raw);
    if (ru.path().endsWith(QLatin1String("/l/")) || ru.path() == QLatin1String("/l")) {
        const QString uddg = QUrlQuery(ru).queryItemValue(QStringLiteral("uddg"), QUrl::FullyDecoded);
        if (!uddg.isEmpty()) return uddg;
    }
    return raw;
}

static QVector<ResearchHit> researchParseDdg(const QString &html, int count)
{
    QVector<ResearchHit> hits;
    QRegularExpression reTitle(
        QStringLiteral("(?is)<a[^>]+class=\"[^\"]*result__a[^\"]*\"[^>]+href=\"([^\"]+)\"[^>]*>(.*?)</a>"));
    QRegularExpression reSnip(
        QStringLiteral("(?is)class=\"[^\"]*result__snippet[^\"]*\"[^>]*>(.*?)</a>"));
    auto snipIt = reSnip.globalMatch(html);
    auto titleIt = reTitle.globalMatch(html);
    while (titleIt.hasNext() && hits.size() < count) {
        const auto tm = titleIt.next();
        ResearchHit h;
        h.url = researchResolveDdgRedirect(tm.captured(1));
        h.title = researchCleanHtmlToText(tm.captured(2));
        if (snipIt.hasNext()) h.snippet = researchCleanHtmlToText(snipIt.next().captured(1));
        if (!h.url.isEmpty()) hits.append(h);
    }
    return hits;
}

static QStringList researchQueriesFor(const QString &topic, const QString &mode)
{
    QStringList queries;
    queries << topic;
    if (mode == QLatin1String("compare")) {
        queries << topic + QStringLiteral(" comparison benchmarks alternatives");
        queries << topic + QStringLiteral(" pros cons limitations");
    } else if (mode == QLatin1String("product")) {
        queries << topic + QStringLiteral(" official pricing features docs");
        queries << topic + QStringLiteral(" review limitations competitors");
    } else if (mode == QLatin1String("howto")) {
        queries << topic + QStringLiteral(" official docs tutorial setup");
        queries << topic + QStringLiteral(" common errors troubleshooting");
    } else if (mode == QLatin1String("factcheck")) {
        queries << topic + QStringLiteral(" source evidence fact check");
        queries << topic + QStringLiteral(" controversy correction");
    } else {
        queries << topic + QStringLiteral(" official docs latest");
        queries << topic + QStringLiteral(" analysis risks limitations");
    }
    queries.removeDuplicates();
    return queries.mid(0, 4);
}

static QString researchModeTitle(const QString &mode)
{
    if (mode == QLatin1String("product")) return QStringLiteral("Product");
    if (mode == QLatin1String("compare")) return QStringLiteral("Compare");
    if (mode == QLatin1String("howto")) return QStringLiteral("How-to");
    if (mode == QLatin1String("factcheck")) return QStringLiteral("Fact-check");
    return QStringLiteral("Auto");
}

AppController::AppController(QObject *parent) : QObject(parent)
{
    QSettings s;
    m_language = s.value(QStringLiteral("language"), QStringLiteral("es")).toString();
    m_agentApprovalMode = s.value(QStringLiteral("agent/approvalMode"), QStringLiteral("ask")).toString();
    m_agentThinkingEnabled = s.value(QStringLiteral("agent/thinkingEnabled"), false).toBool();
    m_agentSystemPrompt = s.value(QStringLiteral("agent/systemPrompt")).toString();
    m_agentPermRules    = s.value(QStringLiteral("agent/permRules")).toString();
    m_agentTemperature  = s.value(QStringLiteral("agent/temperature"), -1.0).toDouble();
    m_agentDisabledTools = s.value(QStringLiteral("agent/disabledTools")).toStringList();
    m_agentTeacherUrl   = s.value(QStringLiteral("agent/teacherUrl")).toString();
    m_agentTeacherModel = s.value(QStringLiteral("agent/teacherModel")).toString();
    m_agentTeacherKey   = s.value(QStringLiteral("agent/teacherKey")).toString();
    m_gitAvailable      = !QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty();

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
    rescanHardware();

    // If folders are registered but no models were found yet (e.g. a root added
    // in "manual" mode), scan them once on startup so the catalog is populated.
    if (m_roots.count() > 0 && m_catalog.count() == 0)
        m_roots.scanAll();
    refreshResearchReports();
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

    // Diagnóstico temprano del binario: existe, es archivo y es ejecutable.
    {
        QFileInfo bi(binaryPath);
        if (binaryPath.isEmpty() || !bi.exists() || !bi.isFile()) {
            emit serverError(QStringLiteral(
                "El binario del perfil no existe: %1. Revisá la ruta en la página Binarios.")
                                 .arg(binaryPath.isEmpty() ? QStringLiteral("(vacío)") : binaryPath));
            return;
        }
        if (!bi.isExecutable()) {
            emit serverError(QStringLiteral(
                "El binario no es ejecutable: %1.").arg(binaryPath));
            return;
        }
    }

    // Pre-check de colisión de puerto: si está ocupado, llama-server falla al bindear
    // y sale con error genérico. Avisamos antes con mensaje claro.
    {
        const int portIdx = args.indexOf(QStringLiteral("--port"));
        const quint16 port = (portIdx >= 0 && portIdx + 1 < args.size())
                                 ? static_cast<quint16>(args[portIdx + 1].toInt()) : 8080;
        const int hostIdx = args.indexOf(QStringLiteral("--host"));
        const QString host = (hostIdx >= 0 && hostIdx + 1 < args.size())
                                 ? args[hostIdx + 1] : QStringLiteral("127.0.0.1");
        QTcpServer probe;
        QHostAddress addr(host);
        if (host == QStringLiteral("0.0.0.0") || host.isEmpty())
            addr = QHostAddress(QHostAddress::Any);
        else if (host == QStringLiteral("localhost"))
            addr = QHostAddress(QHostAddress::LocalHost);
        if (!probe.listen(addr, port)) {
            emit serverError(QStringLiteral(
                "El puerto %1 ya está en uso. Cerrá el proceso que lo ocupa o cambiá el puerto del perfil.")
                                 .arg(port));
            return;
        }
        probe.close();
    }

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
        if (!m_pendingAutoAgentLaunchId.isEmpty()) {
            m_pendingAutoAgentLaunchId.clear();
            m_agentStarting = false;
            emit agentStartingChanged();
        }
        if (m_stopKillTimer) { m_stopKillTimer->stop(); m_stopKillTimer->deleteLater(); m_stopKillTimer = nullptr; }
        stopHealthPolling();
        m_serverStopping = false;
        m_serverReady    = false;
        m_proc->deleteLater();
        m_proc = nullptr;
        emit serverRunningChanged();
        emit serverReadyChanged();
    });

    // Capacidad de visión del modelo activo: el server cargó un mmproj.
    const bool vision = args.contains(QStringLiteral("--mmproj"));
    if (vision != m_serverHasVision) { m_serverHasVision = vision; emit serverHasVisionChanged(); }

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
            const QString errStr = reply->errorString();
            reply->deleteLater();
            if (!ok) {
                appendServerEvent(QStringLiteral("health"),
                                  QStringLiteral("health fail status=%1 error=%2")
                                      .arg(status).arg(errStr));
            }
            if (ok && !m_serverReady) {
                m_serverReady = true;
                appendServerEvent(QStringLiteral("health"), QStringLiteral("Server ready - model loaded"));
                emit serverReadyChanged();
                stopHealthPolling();
                fetchChatThinkingSupport();
                if (!m_pendingAutoAgentLaunchId.isEmpty() && !agentRunning()) {
                    const QString launchId = m_pendingAutoAgentLaunchId;
                    m_pendingAutoAgentLaunchId.clear();
                    emit agentStartingChanged();
                    appendAgentEvent(QStringLiteral("lifecycle"),
                                     QStringLiteral("Servidor listo; iniciando agente del perfil."));
                    startAgent(launchId);
                }
            }
        });
    });
    m_healthPollTimer->start();
}

void AppController::startServerAndAgent(const QString &launchProfileId)
{
    const auto ctx = buildContext(launchProfileId);
    const QString adapter = ctx.harness.adapter.trimmed();
    const bool hasAgent = !adapter.isEmpty()
                          && adapter != QLatin1String("none")
                          && adapter != QLatin1String("raw");

    if (serverRunning()) {
        if (hasAgent && !agentRunning())
            startAgent(launchProfileId);
        return;
    }

    m_pendingAutoAgentLaunchId = hasAgent ? launchProfileId : QString();
    if (hasAgent) {
        m_agentStarting = true;
        emit agentStartingChanged();
        appendAgentEvent(QStringLiteral("lifecycle"),
                         QStringLiteral("Agente programado para iniciar cuando el servidor este listo."));
    }

    startServer(launchProfileId);

    if (!serverRunning()) {
        m_pendingAutoAgentLaunchId.clear();
        if (m_agentStarting) {
            m_agentStarting = false;
            emit agentStartingChanged();
        }
        return;
    }

    if (m_serverReady && !m_pendingAutoAgentLaunchId.isEmpty() && !agentRunning()) {
        const QString launchId = m_pendingAutoAgentLaunchId;
        m_pendingAutoAgentLaunchId.clear();
        emit agentStartingChanged();
        startAgent(launchId);
    }
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
    if (m_agentStarting || !m_pendingAutoAgentLaunchId.isEmpty()) {
        m_agentStarting = false;
        m_pendingAutoAgentLaunchId.clear();
        emit agentStartingChanged();
    }
    stopHealthPolling();
    m_serverReady    = false;
    m_serverStopping = true;
    if (m_serverHasVision) { m_serverHasVision = false; emit serverHasVisionChanged(); }
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
    if (source == QLatin1String("stdout") || source == QLatin1String("stderr"))
        detectServerLogPatterns(text);
    emit serverLogChanged();
}

void AppController::detectServerLogPatterns(const QString &text)
{
    // Patrones (regex, case-insensitive). level → señal serverDiagnostic.
    struct Pat { const char *rx; const char *level; const char *msg; };
    static const Pat pats[] = {
        {"out of memory|cudaMalloc failed|failed to allocate|ggml_backend.*alloc.*fail",
         "error", "Memoria insuficiente (OOM) al cargar el modelo. Bajá ctx/gpu-layers o usá un quant menor."},
        {"bind.*address already in use|error.*bind|failed to listen|EADDRINUSE",
         "error", "El puerto está ocupado: el server no pudo bindear."},
        {"unknown argument|invalid argument|error while parsing|unrecognized.*option",
         "error", "Argumento inválido en la línea de comando del server."},
        {"failed to load model|error loading model|unable to load model|llama_load_model_from_file.*fail",
         "error", "No se pudo cargar el modelo (archivo corrupto, incompatible o ruta inválida)."},
        {"all slots are idle|HTTP server is listening|model loaded|server is listening",
         "info", "Server escuchando / modelo cargado."},
        {"slot.*context shift|context shift|n_past.*truncat",
         "warn", "Context shift: el prompt superó el ctx y se truncó."},
    };
    static QHash<QString, qint64> lastEmit;  // throttle por patrón (no spamear)
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const auto &p : pats) {
        static QHash<const char*, QRegularExpression> cache;
        if (!cache.contains(p.rx))
            cache.insert(p.rx, QRegularExpression(QString::fromLatin1(p.rx),
                                                  QRegularExpression::CaseInsensitiveOption));
        if (cache[p.rx].match(text).hasMatch()) {
            const QString key = QString::fromLatin1(p.rx);
            if (now - lastEmit.value(key, 0) < 3000) continue;  // máx 1 cada 3s por patrón
            lastEmit[key] = now;
            appendServerEvent(QStringLiteral("diag"),
                              QStringLiteral("[%1] %2").arg(QString::fromLatin1(p.level),
                                                            QString::fromLatin1(p.msg)));
            emit serverDiagnostic(QString::fromLatin1(p.level), QString::fromLatin1(p.msg));
        }
    }
}

QString AppController::serverLogByLevel(const QString &level) const
{
    if (level.isEmpty() || level == QLatin1String("all")) return m_log;
    const QStringList lines = m_log.split(QLatin1Char('\n'));
    QStringList out;
    for (const QString &ln : lines) {
        if (level == QLatin1String("error")) {
            if (ln.contains(QLatin1String("[diag] [error]")) ||
                ln.contains(QLatin1String("/stderr]")) ||
                ln.contains(QLatin1String("[error]"), Qt::CaseInsensitive))
                out << ln;
        } else if (level == QLatin1String("warn")) {
            if (ln.contains(QLatin1String("[warn]"), Qt::CaseInsensitive)) out << ln;
        } else {
            // fuente exacta: stdout/stderr/lifecycle/health/diag
            if (ln.contains(QStringLiteral("/%1]").arg(level)) ||
                ln.contains(QStringLiteral("server/%1]").arg(level)))
                out << ln;
        }
    }
    return out.join(QLatin1Char('\n'));
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
        // Llega la lista autoritativa → terminar cualquier override de streaming
        // (el contenido final ya está en m_agentMessages).
        if (m_agentStreamingIndex != -1) {
            m_agentStreamingIndex = -1;
            m_agentStreamingText.clear();
            emit agentStreamingChanged();
        }
        emit agentMessagesChanged();
    });
    // Streaming incremental: refresca SÓLO la burbuja activa, sin re-bindear toda
    // la lista (evita re-instanciar todos los delegates por token).
    connect(b, &IAgentBackend::streamingText, this, [this](int idx, const QString &content) {
        m_agentStreamingIndex = idx;
        m_agentStreamingText  = content;
        emit agentStreamingChanged();
    });
    connect(b, &IAgentBackend::queueChanged, this, [this, b]() {
        m_agentQueuedCount = b->queuedCount();
        emit agentQueueChanged();
    });
    connect(b, &IAgentBackend::gitRequired, this, [this]() {
        recheckGit();
        emit gitRequiredForSubagents();   // la UI ofrece instalar git
    });
    connect(b, &IAgentBackend::sessionsChanged, this, [this, b]() {
        m_agentSessions = groupSessionsByProject(b->sessions());
        m_opencodeSessionId = b->currentSessionId();
        m_opencodeSessionTitle = b->currentSessionTitle();
        emit agentSessionsChanged();
    });
    connect(b, &IAgentBackend::logAppended, this, [this](const QString &chunk) {
        appendAgentEvent(QStringLiteral("backend"), chunk);
    });
    connect(b, &IAgentBackend::runningChanged, this, [this, b]() {
        if (m_agentStarting) {
            m_agentStarting = false;
            emit agentStartingChanged();
        }
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
        if (m_agentStarting) {
            m_agentStarting = false;
            emit agentStartingChanged();
        }
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
    b->setPermissionRules(m_agentPermRules);
    b->setAgentTuning(m_agentSystemPrompt, m_agentTemperature >= 0.0 ? m_agentTemperature : m_resolvedProfileTemperature);
    if (auto *cb = qobject_cast<LlamaAgentBackend *>(b)) {
        cb->setDisabledTools(m_agentDisabledTools);
        cb->setTeacherConfig(m_agentTeacherUrl, m_agentTeacherModel, m_agentTeacherKey);
    }
    m_agentBackend = b;
    return b;
}

void AppController::setAgentPermRules(const QString &rules)
{
    if (rules == m_agentPermRules) return;
    m_agentPermRules = rules;
    writeSetting(QStringLiteral("agent/permRules"), rules);
    if (m_agentBackend) m_agentBackend->setPermissionRules(rules);
    emit agentTuningChanged();
}

void AppController::setAgentSystemPrompt(const QString &p)
{
    if (p == m_agentSystemPrompt) return;
    m_agentSystemPrompt = p;
    writeSetting(QStringLiteral("agent/systemPrompt"), p);
    if (m_agentBackend) m_agentBackend->setAgentTuning(m_agentSystemPrompt, m_agentTemperature >= 0.0 ? m_agentTemperature : m_resolvedProfileTemperature);
    emit agentTuningChanged();
}

void AppController::setAgentTemperature(double t)
{
    if (qFuzzyCompare(t, m_agentTemperature)) return;
    m_agentTemperature = t;
    writeSetting(QStringLiteral("agent/temperature"), t);
    if (m_agentBackend) m_agentBackend->setAgentTuning(m_agentSystemPrompt, m_agentTemperature >= 0.0 ? m_agentTemperature : m_resolvedProfileTemperature);
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

void AppController::setAgentTeacherUrl(const QString &url)
{
    if (url == m_agentTeacherUrl) return;
    m_agentTeacherUrl = url;
    writeSetting(QStringLiteral("agent/teacherUrl"), url);
    if (auto *cb = qobject_cast<LlamaAgentBackend *>(m_agentBackend))
        cb->setTeacherConfig(m_agentTeacherUrl, m_agentTeacherModel, m_agentTeacherKey);
    emit agentTeacherChanged();
}

void AppController::setAgentTeacherModel(const QString &model)
{
    if (model == m_agentTeacherModel) return;
    m_agentTeacherModel = model;
    writeSetting(QStringLiteral("agent/teacherModel"), model);
    if (auto *cb = qobject_cast<LlamaAgentBackend *>(m_agentBackend))
        cb->setTeacherConfig(m_agentTeacherUrl, m_agentTeacherModel, m_agentTeacherKey);
    emit agentTeacherChanged();
}

void AppController::setAgentTeacherKey(const QString &key)
{
    if (key == m_agentTeacherKey) return;
    m_agentTeacherKey = key;
    writeSetting(QStringLiteral("agent/teacherKey"), key);
    if (auto *cb = qobject_cast<LlamaAgentBackend *>(m_agentBackend))
        cb->setTeacherConfig(m_agentTeacherUrl, m_agentTeacherModel, m_agentTeacherKey);
    emit agentTeacherChanged();
}

QVariantList AppController::agentToolCatalog() const
{
    QVariantList out = LlamaAgentBackend::toolCatalog();
    for (QVariant &v : out) {
        QVariantMap m = v.toMap();
        m.insert(QStringLiteral("enabled"),
                 !m_agentDisabledTools.contains(m.value(QStringLiteral("name")).toString()));
        v = m;
    }
    return out;
}

void AppController::setAgentToolEnabled(const QString &name, bool enabled)
{
    const bool isDisabled = m_agentDisabledTools.contains(name);
    if (enabled == !isDisabled) return;               // sin cambio
    if (enabled) m_agentDisabledTools.removeAll(name);
    else if (!isDisabled) m_agentDisabledTools.append(name);
    writeSetting(QStringLiteral("agent/disabledTools"), m_agentDisabledTools);
    if (auto *cb = qobject_cast<LlamaAgentBackend *>(m_agentBackend))
        cb->setDisabledTools(m_agentDisabledTools);   // efectivo en el próximo turno
    emit agentToolsChanged();
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
    connect(b, &IAgentBackend::queueChanged, this, [this, b]() {
        m_chatQueuedCount = b->queuedCount();
        emit chatQueueChanged();
    });
    connect(b, &IAgentBackend::sessionsChanged, this, [this, b]() {
        m_chatSessions = groupSessionsByProject(b->sessions());
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
        if (m_agentStarting) { m_agentStarting = false; emit agentStartingChanged(); }
        appendAgentEvent(QStringLiteral("lifecycle"), QStringLiteral("Error: no harness configured for this profile"));
        return;
    }
    if (adapter == QLatin1String("raw")) {
        if (m_agentStarting) { m_agentStarting = false; emit agentStartingChanged(); }
        appendAgentEvent(QStringLiteral("lifecycle"),
                         QStringLiteral("Error: 'raw' no soporta tool-calling. Usá 'opencode' para modo Agente."));
        emit serverError(QStringLiteral("El harness 'raw' no soporta tool-calling en modo Agente."));
        return;
    }

    if (!m_agentStarting) {
        m_agentStarting = true;
        emit agentStartingChanged();
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
            m_agentStarting = false;
            emit agentStartingChanged();
            return;
        }

        // Parse profile temperature if agent temperature is default (< 0)
        m_resolvedProfileTemperature = -1.0;
        const QStringList &args = ctx.launch.extraArgs;
        for (int i = 0; i < args.size() - 1; ++i) {
            if (args[i] == QStringLiteral("--temp") || args[i] == QStringLiteral("-t")) {
                bool ok = false;
                double val = args[i+1].toDouble(&ok);
                if (ok) {
                    m_resolvedProfileTemperature = val;
                    break;
                }
            }
        }

        IAgentBackend *b = ensureAgentBackend(adapter);
        if (!b) {
            appendAgentEvent(QStringLiteral("lifecycle"), QStringLiteral("Error: backend LlamaAgent no disponible"));
            m_agentStarting = false;
            emit agentStartingChanged();
            return;
        }
        b->setAgentTuning(m_agentSystemPrompt, m_agentTemperature >= 0.0 ? m_agentTemperature : m_resolvedProfileTemperature);
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
        m_agentStarting = false;
        emit agentStartingChanged();
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
        m_agentStarting       = false;
        m_activeAgentAdapter  = adapter;
        m_agentInTerminal     = false;
        m_agentMessages.clear();
        m_currentAssistantIdx = -1;
        appendAgentEvent(QStringLiteral("pi/lifecycle"), QStringLiteral("pi listo - modo print por mensaje"));
        emit agentMessagesChanged();
        emit agentStartingChanged();
        emit agentRunningChanged();
        return;
    }

    // Backends estructurados (proceso propio o sin proceso): delegados a IAgentBackend.
    if (adapter == QLatin1String("opencode")) {
        IAgentBackend *b = ensureAgentBackend(adapter);
        if (!b) {
            appendAgentEvent(QStringLiteral("lifecycle"), QStringLiteral("Error: backend %1 no disponible").arg(adapter));
            m_agentStarting = false;
            emit agentStartingChanged();
            return;
        }
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
        if (m_agentStarting) {
            m_agentStarting = false;
            emit agentStartingChanged();
        }
        m_agentProc->deleteLater();
        m_agentProc = nullptr;
        m_activeAgentAdapter.clear();
        return;
    }
    if (m_agentStarting) {
        m_agentStarting = false;
        emit agentStartingChanged();
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
    if (m_agentStarting || !m_pendingAutoAgentLaunchId.isEmpty()) {
        m_agentStarting = false;
        m_pendingAutoAgentLaunchId.clear();
        emit agentStartingChanged();
    }
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

void AppController::sendToAgentWithAttachments(const QString &text, const QStringList &paths)
{
    QStringList filtered;
    for (const QString &p : paths) {
        if (!m_serverHasVision && isSupportedImageAttachment(p)) {
            appendAgentEvent(QStringLiteral("attachments"),
                             QStringLiteral("Imagen omitida sin vision: %1").arg(QFileInfo(p).fileName()));
            continue;
        }
        filtered.append(p);
    }

    if (text.trimmed().isEmpty() && filtered.isEmpty()) return;
    if (auto *la = qobject_cast<LlamaAgentBackend *>(m_agentBackend)) {
        if (m_agentBackend->running()) {
            la->setPendingAttachments(filtered);
            la->sendMessage(text);
            return;
        }
    }
    sendToAgent(text);   // backend sin soporte de adjuntos → texto solo
}

QStringList AppController::pickAgentAttachments()
{
    QWidget *parent = QApplication::activeWindow();
    // Sin visión: ocultar imágenes del filtro (no las puede usar).
    const QString filter = m_serverHasVision
        ? QStringLiteral("Soportados (*.png *.jpg *.jpeg *.webp *.gif *.bmp *.txt *.md *.json *.csv *.log *.py *.js *.ts *.cpp *.h *.qml);;Todos (*.*)")
        : QStringLiteral("Texto (*.txt *.md *.json *.csv *.log *.py *.js *.ts *.cpp *.h *.qml);;Todos (*.*)");
    return QFileDialog::getOpenFileNames(
        parent, QStringLiteral("Adjuntar archivos"), QDir::homePath(), filter);
}

void AppController::steerAgent(const QString &text)
{
    if (text.trimmed().isEmpty()) return;
    if (m_agentBackend && m_agentBackend->running()) { m_agentBackend->steerMessage(text); return; }
    sendToAgent(text);   // sin turno activo → envío normal
}

void AppController::queueAgent(const QString &text)
{
    if (text.trimmed().isEmpty()) return;
    if (m_agentBackend && m_agentBackend->running()) { m_agentBackend->queueMessage(text); return; }
    sendToAgent(text);   // sin turno activo → envío normal
}

void AppController::clearAgentQueue()
{
    if (m_agentBackend) m_agentBackend->clearQueue();
}

void AppController::recheckGit()
{
    const bool avail = !QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty();
    if (avail != m_gitAvailable) { m_gitAvailable = avail; emit gitAvailableChanged(); }
}

void AppController::installGit()
{
    if (m_gitInstallProc) return;   // ya en curso
#ifdef Q_OS_WIN
    m_gitInstallProc = new QProcess(this);
    connect(m_gitInstallProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
        appendAgentEvent(QStringLiteral("git"),
                         QStringLiteral("Instalación de git terminó (code %1)").arg(code));
        m_gitInstallProc->deleteLater();
        m_gitInstallProc = nullptr;
        recheckGit();                 // re-detectar (puede requerir reabrir la terminal/PATH)
        emit gitAvailableChanged();   // refrescar installingGit
    });
    appendAgentEvent(QStringLiteral("git"), QStringLiteral("Instalando Git vía winget…"));
    emit gitAvailableChanged();       // installingGit = true
    m_gitInstallProc->start(QStringLiteral("winget"),
        {QStringLiteral("install"), QStringLiteral("--id"), QStringLiteral("Git.Git"),
         QStringLiteral("--exact"), QStringLiteral("--source"), QStringLiteral("winget"),
         QStringLiteral("--accept-source-agreements"), QStringLiteral("--accept-package-agreements")});
    if (!m_gitInstallProc->waitForStarted(5000)) {
        emit serverError(QStringLiteral("No se pudo iniciar winget para instalar Git."));
        m_gitInstallProc->deleteLater(); m_gitInstallProc = nullptr;
        emit gitAvailableChanged();
    }
#else
    emit serverError(QStringLiteral("Instalá git con el gestor de paquetes de tu sistema."));
#endif
}

void AppController::rollbackAgentToMessage(int msgIndex)
{
    if (auto *la = qobject_cast<LlamaAgentBackend *>(m_agentBackend))
        la->rollbackToMessage(msgIndex);
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

QStringList AppController::agentProjectFiles(const QString &query) const
{
    const QString root = currentAgentProjectDir();
    if (root.isEmpty() || !QFileInfo(root).isDir()) return {};
    static const QSet<QString> ignored{
        QStringLiteral("node_modules"), QStringLiteral(".git"), QStringLiteral("build"),
        QStringLiteral("build2"), QStringLiteral("dist"), QStringLiteral(".venv"),
        QStringLiteral("venv"), QStringLiteral("__pycache__"), QStringLiteral(".next"),
        QStringLiteral(".turbo"), QStringLiteral("coverage"), QStringLiteral("target"),
        QStringLiteral(".cache"), QStringLiteral(".idea"), QStringLiteral(".vs")};
    const QString q = query.trimmed().toLower();
    const QDir base(root);
    QStringList out;
    QStringList stack{root};
    int scanned = 0;
    while (!stack.isEmpty() && out.size() < 50 && scanned < 20000) {
        QDir d(stack.takeLast());
        const auto entries = d.entryInfoList(
            QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs, QDir::Name);
        for (const QFileInfo &fi : entries) {
            ++scanned;
            if (fi.isDir()) {
                if (!ignored.contains(fi.fileName())) stack << fi.absoluteFilePath();
                continue;
            }
            const QString rel = base.relativeFilePath(fi.absoluteFilePath());
            if (q.isEmpty() || rel.toLower().contains(q)) {
                out << rel;
                if (out.size() >= 50) break;
            }
        }
    }
    out.sort();
    return out;
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

// ───────────────────────── Integrations ──────────────────────────────
QString AppController::integrationsFilePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/integrations.json");
}

QJsonArray AppController::readApiServices() const
{
    QFile f(integrationsFilePath());
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).array();
}

bool AppController::writeApiServices(const QJsonArray &arr)
{
    QFile f(integrationsFilePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(arr).toJson());
    f.close();
    emit integrationsChanged();
    return true;
}

QVariantList AppController::integrations() const
{
    QVariantList out;
    // MCP servers desde el config opencode global (single source: lo consume el agente).
    for (const QVariant &mv : listMcpServers(QStringLiteral("global"), QString())) {
        const QVariantMap m = mv.toMap();
        const QString type = m.value(QStringLiteral("type")).toString();
        QVariantMap it;
        it[QStringLiteral("id")]      = QStringLiteral("mcp:") + m.value(QStringLiteral("name")).toString();
        it[QStringLiteral("type")]    = QStringLiteral("mcp");
        it[QStringLiteral("name")]    = m.value(QStringLiteral("name"));
        it[QStringLiteral("enabled")] = m.value(QStringLiteral("enabled"), true);
        it[QStringLiteral("summary")] = type == QLatin1String("remote")
            ? m.value(QStringLiteral("url")).toString()
            : m.value(QStringLiteral("command")).toString();
        it[QStringLiteral("config")]  = m;
        out.append(it);
    }
    // API services desde integrations.json.
    for (const QJsonValue &v : readApiServices()) {
        const QJsonObject o = v.toObject();
        QVariantMap it;
        it[QStringLiteral("id")]      = QStringLiteral("api:") + o.value(QStringLiteral("id")).toString();
        it[QStringLiteral("type")]    = QStringLiteral("api_service");
        it[QStringLiteral("name")]    = o.value(QStringLiteral("name")).toString();
        it[QStringLiteral("enabled")] = o.value(QStringLiteral("enabled")).toBool(true);
        it[QStringLiteral("summary")] = o.value(QStringLiteral("baseUrl")).toString();
        QVariantMap cfg;
        cfg[QStringLiteral("baseUrl")] = o.value(QStringLiteral("baseUrl")).toString();
        cfg[QStringLiteral("hasKey")]  = !o.value(QStringLiteral("apiKey")).toString().isEmpty();
        it[QStringLiteral("config")]   = cfg;
        out.append(it);
    }
    return out;
}

bool AppController::saveMcpIntegration(const QString &name, const QString &type,
                                       const QString &commandOrUrl)
{
    if (name.trimmed().isEmpty()) return false;
    QVariantMap def;
    def[QStringLiteral("type")]    = type.isEmpty() ? QStringLiteral("local") : type;
    def[QStringLiteral("enabled")] = true;
    if (type == QLatin1String("remote")) def[QStringLiteral("url")]     = commandOrUrl;
    else                                 def[QStringLiteral("command")] = commandOrUrl;
    const bool ok = setMcpServer(QStringLiteral("global"), QString(), name.trimmed(), def);
    if (ok) emit integrationsChanged();
    return ok;
}

bool AppController::saveApiService(const QString &id, const QString &name,
                                   const QString &baseUrl, const QString &apiKey, bool enabled)
{
    if (name.trimmed().isEmpty()) return false;
    QJsonArray arr = readApiServices();
    if (id.isEmpty()) {
        arr.append(QJsonObject{
            {QStringLiteral("id"),      QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {QStringLiteral("name"),    name.trimmed()},
            {QStringLiteral("baseUrl"), baseUrl.trimmed()},
            {QStringLiteral("apiKey"),  apiKey},
            {QStringLiteral("enabled"), enabled}});
        return writeApiServices(arr);
    }
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject o = arr[i].toObject();
        if (o.value(QStringLiteral("id")).toString() == id) {
            o[QStringLiteral("name")]    = name.trimmed();
            o[QStringLiteral("baseUrl")] = baseUrl.trimmed();
            // key vacío en edición = conservar la existente.
            if (!apiKey.isEmpty()) o[QStringLiteral("apiKey")] = apiKey;
            o[QStringLiteral("enabled")] = enabled;
            arr[i] = o;
            return writeApiServices(arr);
        }
    }
    return false;
}

bool AppController::removeIntegration(const QString &id)
{
    if (id.startsWith(QStringLiteral("mcp:"))) {
        const bool ok = removeMcpServer(QStringLiteral("global"), QString(), id.mid(4));
        if (ok) emit integrationsChanged();
        return ok;
    }
    if (id.startsWith(QStringLiteral("api:"))) {
        const QString aid = id.mid(4);
        QJsonArray arr = readApiServices();
        for (int i = 0; i < arr.size(); ++i)
            if (arr[i].toObject().value(QStringLiteral("id")).toString() == aid) {
                arr.removeAt(i);
                return writeApiServices(arr);
            }
    }
    return false;
}

bool AppController::setIntegrationEnabled(const QString &id, bool enabled)
{
    if (id.startsWith(QStringLiteral("mcp:"))) {
        const bool ok = toggleMcpServer(QStringLiteral("global"), QString(), id.mid(4), enabled);
        if (ok) emit integrationsChanged();
        return ok;
    }
    if (id.startsWith(QStringLiteral("api:"))) {
        const QString aid = id.mid(4);
        QJsonArray arr = readApiServices();
        for (int i = 0; i < arr.size(); ++i) {
            QJsonObject o = arr[i].toObject();
            if (o.value(QStringLiteral("id")).toString() == aid) {
                o[QStringLiteral("enabled")] = enabled; arr[i] = o;
                return writeApiServices(arr);
            }
        }
    }
    return false;
}

void AppController::testIntegration(const QString &id)
{
    if (id.startsWith(QStringLiteral("api:"))) {
        const QString aid = id.mid(4);
        QString baseUrl, apiKey;
        for (const QJsonValue &v : readApiServices()) {
            const QJsonObject o = v.toObject();
            if (o.value(QStringLiteral("id")).toString() == aid) {
                baseUrl = o.value(QStringLiteral("baseUrl")).toString();
                apiKey  = o.value(QStringLiteral("apiKey")).toString();
                break;
            }
        }
        if (baseUrl.isEmpty()) { emit integrationTestResult(id, false, QStringLiteral("URL vacía")); return; }
        if (!m_nam) m_nam = new QNetworkAccessManager(this);
        QNetworkRequest req((QUrl(baseUrl)));
        if (!apiKey.isEmpty())
            req.setRawHeader(QByteArrayLiteral("Authorization"),
                             QByteArrayLiteral("Bearer ") + apiKey.toUtf8());
        QNetworkReply *reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, id]() {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QString err = reply->errorString();
            const bool neterr = reply->error() != QNetworkReply::NoError && status == 0;
            reply->deleteLater();
            if (neterr) emit integrationTestResult(id, false, err);
            else        emit integrationTestResult(id, status > 0 && status < 500,
                                                   QStringLiteral("HTTP %1").arg(status));
        });
        return;
    }
    if (id.startsWith(QStringLiteral("mcp:"))) {
        const QString name = id.mid(4);
        // Buscar command/url del MCP en el config global.
        QString command, type;
        for (const QVariant &mv : listMcpServers(QStringLiteral("global"), QString())) {
            const QVariantMap m = mv.toMap();
            if (m.value(QStringLiteral("name")).toString() == name) {
                type = m.value(QStringLiteral("type")).toString();
                command = (type == QLatin1String("remote"))
                    ? m.value(QStringLiteral("url")).toString()
                    : m.value(QStringLiteral("command")).toString();
                break;
            }
        }
        if (type == QLatin1String("remote")) {
            emit integrationTestResult(id, false,
                QStringLiteral("Test de MCP remoto no soportado todavía (solo stdio local)."));
            return;
        }
        if (command.isEmpty()) { emit integrationTestResult(id, false, QStringLiteral("Sin comando")); return; }
        const QString cwd = currentAgentProjectDir();
        // Handshake en un hilo aparte (McpClient hace I/O bloqueante con QProcess).
        QPointer<AppController> self(this);
        (void)QtConcurrent::run([self, id, command, cwd]() {
            McpClient client(QStringLiteral("test"));
            const bool ready = client.start(command, cwd);
            int n = ready ? client.tools().size() : 0;
            QStringList names;
            for (const auto &t : client.tools()) { names << t.name; if (names.size() >= 8) break; }
            client.shutdown();
            const QString msg = ready
                ? QStringLiteral("OK · %1 tools%2").arg(n)
                      .arg(names.isEmpty() ? QString() : QStringLiteral(": ") + names.join(QStringLiteral(", ")))
                : QStringLiteral("Falló el handshake (revisá el comando).");
            if (!self) return;
            QMetaObject::invokeMethod(self, [self, id, ready, msg]() {
                if (self) emit self->integrationTestResult(id, ready, msg);
            }, Qt::QueuedConnection);
        });
        return;
    }
    emit integrationTestResult(id, false, QStringLiteral("Tipo desconocido"));
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

QJsonObject AppController::exportFileSet(const QString &root, const QStringList &relativePaths) const
{
    QJsonArray files;
    const QDir base(root);
    const QString basePath = QFileInfo(root).absoluteFilePath();

    auto addFile = [&](const QString &absolutePath) {
        QFileInfo fi(absolutePath);
        if (!fi.exists() || !fi.isFile()) return;
        const QString rel = base.relativeFilePath(fi.absoluteFilePath());
        if (rel.startsWith(QStringLiteral(".."))) return;
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) return;
        files.append(QJsonObject{
            {QStringLiteral("path"), rel},
            {QStringLiteral("base64"), QString::fromLatin1(f.readAll().toBase64())}
        });
    };

    for (const QString &rel : relativePaths) {
        const QString cleanRel = QDir::cleanPath(rel);
        const QFileInfo fi(base.filePath(cleanRel));
        if (!fi.exists()) continue;
        if (fi.isDir()) {
            QDirIterator it(fi.absoluteFilePath(), QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) addFile(it.next());
        } else {
            addFile(fi.absoluteFilePath());
        }
    }

    return QJsonObject{
        {QStringLiteral("rootName"), QFileInfo(basePath).fileName()},
        {QStringLiteral("files"), files}
    };
}

bool AppController::importFileSet(const QString &root, const QJsonObject &set, QStringList *written)
{
    const QDir base(root);
    const QString basePath = QFileInfo(root).absoluteFilePath();
    QDir().mkpath(basePath);

    for (const QJsonValue &v : set.value(QStringLiteral("files")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString rel = QDir::cleanPath(o.value(QStringLiteral("path")).toString());
        if (rel.isEmpty() || rel.startsWith(QStringLiteral("..")) || QDir::isAbsolutePath(rel))
            continue;

        const QString path = base.filePath(rel);
        const QString abs = QFileInfo(path).absoluteFilePath();
        if (!abs.startsWith(basePath, Qt::CaseInsensitive))
            continue;

        QByteArray bytes = QByteArray::fromBase64(o.value(QStringLiteral("base64")).toString().toLatin1());
        QDir().mkpath(QFileInfo(abs).absolutePath());

        if (QFileInfo(abs).exists()) {
            QJsonParseError newErr;
            const QJsonDocument incoming = QJsonDocument::fromJson(bytes, &newErr);
            QFile existing(abs);
            if (newErr.error == QJsonParseError::NoError && existing.open(QIODevice::ReadOnly)) {
                QJsonParseError oldErr;
                const QJsonDocument current = QJsonDocument::fromJson(existing.readAll(), &oldErr);
                existing.close();

                if (oldErr.error == QJsonParseError::NoError && incoming.isArray() && current.isArray()) {
                    QJsonArray merged = current.array();
                    QSet<QString> ids;
                    for (const QJsonValue &mv : std::as_const(merged)) {
                        const QString id = mv.toObject().value(QStringLiteral("id")).toString();
                        if (!id.isEmpty()) ids.insert(id);
                    }
                    for (const QJsonValue &mv : incoming.array()) {
                        const QJsonObject obj = mv.toObject();
                        const QString id = obj.value(QStringLiteral("id")).toString();
                        if (!id.isEmpty() && ids.contains(id)) continue;
                        merged.append(mv);
                        if (!id.isEmpty()) ids.insert(id);
                    }
                    bytes = QJsonDocument(merged).toJson(QJsonDocument::Indented);
                } else if (oldErr.error == QJsonParseError::NoError && incoming.isObject() && current.isObject()
                           && QFileInfo(rel).fileName() == QLatin1String("opencode.json")) {
                    QJsonObject merged = current.object();
                    const QJsonObject inc = incoming.object();
                    for (auto it = inc.begin(); it != inc.end(); ++it) {
                        if (it.key() == QLatin1String("mcp") && it.value().isObject()) {
                            QJsonObject mcp = merged.value(QStringLiteral("mcp")).toObject();
                            const QJsonObject incMcp = it.value().toObject();
                            for (auto mi = incMcp.begin(); mi != incMcp.end(); ++mi)
                                if (!mcp.contains(mi.key())) mcp.insert(mi.key(), mi.value());
                            merged[QStringLiteral("mcp")] = mcp;
                        } else if (!merged.contains(it.key())) {
                            merged.insert(it.key(), it.value());
                        }
                    }
                    bytes = QJsonDocument(merged).toJson(QJsonDocument::Indented);
                }
            }
        }

        QFile f(abs);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        f.write(bytes);
        f.close();
        if (written) written->append(abs);
    }
    return true;
}

QString AppController::exportUserData()
{
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString appLocal = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);

    QVariantMap settings;
    QSettings s;
    for (const QString &key : s.allKeys())
        settings.insert(key, s.value(key));

    QJsonObject root;
    root[QStringLiteral("app")] = QStringLiteral("LlamaCode");
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("exportedAt")] = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    root[QStringLiteral("settings")] = QJsonObject::fromVariantMap(settings);
    root[QStringLiteral("appData")] = exportFileSet(appData, {
        QStringLiteral("binary_registry.json"),
        QStringLiteral("model_roots.json"),
        QStringLiteral("profiles")
    });
    root[QStringLiteral("appLocalData")] = exportFileSet(appLocal, {
        QStringLiteral("integrations.json"),
        QStringLiteral("chat"),
        QStringLiteral("chat_raw"),
        QStringLiteral("agent_llamaagent"),
        QStringLiteral("benchmarks")
    });
    root[QStringLiteral("opencode")] = exportFileSet(ocGlobalConfigDir(), {
        QStringLiteral("opencode.json"),
        QStringLiteral("command")
    });

    const QString defName = QStringLiteral("llamacode_backup_%1.json")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    const QString path = QFileDialog::getSaveFileName(
        nullptr, QStringLiteral("Exportar datos de LlamaCode"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(defName),
        QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) return QString();

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit serverError(QStringLiteral("No se pudo escribir el backup: %1").arg(path));
        return QString();
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    emit serverError(QStringLiteral("Backup exportado: %1").arg(QDir::toNativeSeparators(path)));
    return path;
}

QString AppController::importUserData()
{
    const QString path = QFileDialog::getOpenFileName(
        nullptr, QStringLiteral("Importar datos de LlamaCode"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) return QString();

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        emit serverError(QStringLiteral("No se pudo leer el backup."));
        return QString();
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        emit serverError(QStringLiteral("Backup JSON inválido: %1").arg(err.errorString()));
        return QString();
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("app")).toString() != QLatin1String("LlamaCode")) {
        emit serverError(QStringLiteral("El archivo no parece ser un backup de LlamaCode."));
        return QString();
    }

    QStringList written;
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString appLocal = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!importFileSet(appData, root.value(QStringLiteral("appData")).toObject(), &written) ||
        !importFileSet(appLocal, root.value(QStringLiteral("appLocalData")).toObject(), &written) ||
        !importFileSet(ocGlobalConfigDir(), root.value(QStringLiteral("opencode")).toObject(), &written)) {
        emit serverError(QStringLiteral("No se pudo importar todo el backup."));
        return QString();
    }

    QSettings s;
    const QJsonObject settings = root.value(QStringLiteral("settings")).toObject();
    for (auto it = settings.begin(); it != settings.end(); ++it)
        s.setValue(it.key(), it.value().toVariant());

    reloadPersistentStateAfterImportOrWipe();
    emit serverError(QStringLiteral("Backup importado: %1 archivo(s).").arg(written.size()));
    return path;
}

bool AppController::removePathForWipe(const QString &path)
{
    if (path.trimmed().isEmpty()) return true;
    QFileInfo fi(path);
    if (!fi.exists()) return true;
    if (fi.isDir()) {
        QDir d(fi.absoluteFilePath());
        return d.removeRecursively();
    }
    return QFile::remove(fi.absoluteFilePath());
}

QVariantList AppController::wipeCategories() const
{
    return {
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("chats")},
                    {QStringLiteral("title"), QStringLiteral("Wipe all chats")},
                    {QStringLiteral("description"), QStringLiteral("Every local chat and agent session stored by LlamaCode.")}},
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("memory")},
                    {QStringLiteral("title"), QStringLiteral("Wipe project memory")},
                    {QStringLiteral("description"), QStringLiteral("Deletes .llamacode/memory.md for the current agent project.")}},
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("skills")},
                    {QStringLiteral("title"), QStringLiteral("Wipe all skills")},
                    {QStringLiteral("description"), QStringLiteral("Deletes global opencode command skills from the LlamaCode-managed command folder.")}},
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("integrations")},
                    {QStringLiteral("title"), QStringLiteral("Wipe integrations")},
                    {QStringLiteral("description"), QStringLiteral("Deletes API service entries and global MCP server config.")}},
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("profiles")},
                    {QStringLiteral("title"), QStringLiteral("Wipe profiles")},
                    {QStringLiteral("description"), QStringLiteral("Deletes backend, model, runtime, harness, workspace, and launch profiles.")}},
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("benchmarks")},
                    {QStringLiteral("title"), QStringLiteral("Wipe benchmarks")},
                    {QStringLiteral("description"), QStringLiteral("Deletes saved benchmark results and run summaries.")}},
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("logs")},
                    {QStringLiteral("title"), QStringLiteral("Wipe logs")},
                    {QStringLiteral("description"), QStringLiteral("Deletes runtime server and agent logs.")}},
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("settings")},
                    {QStringLiteral("title"), QStringLiteral("Wipe settings")},
                    {QStringLiteral("description"), QStringLiteral("Clears QSettings preferences such as language, last launch, and agent tuning.")}}
    };
}

bool AppController::wipeUserData(const QString &kind, const QString &confirmation)
{
    if (confirmation.trimmed() != QLatin1String("WIPE")) {
        emit serverError(QStringLiteral("Escribí WIPE para confirmar el borrado."));
        return false;
    }

    const QString k = kind.trimmed().toLower();
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString appLocal = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    bool ok = true;

    if (k == QLatin1String("chats")) {
        ok = removePathForWipe(appLocal + QStringLiteral("/chat"))
          && removePathForWipe(appLocal + QStringLiteral("/chat_raw"))
          && removePathForWipe(appLocal + QStringLiteral("/agent_llamaagent"))
          && removePathForWipe(appData + QStringLiteral("/pi-sessions"));
        m_chatSessions.clear();
        m_chatMessages.clear();
        m_chatSessionId.clear();
        m_chatSessionTitle.clear();
        m_agentSessions.clear();
        m_agentMessages.clear();
        emit chatSessionsChanged();
        emit chatMessagesChanged();
        emit agentSessionsChanged();
        emit agentMessagesChanged();
    } else if (k == QLatin1String("memory")) {
        const QString dir = currentAgentProjectDir();
        ok = !dir.isEmpty() && removePathForWipe(LlamaAgentBackend::memoryFilePath(dir));
        if (dir.isEmpty())
            emit serverError(QStringLiteral("No hay proyecto activo para borrar memoria."));
    } else if (k == QLatin1String("skills")) {
        ok = removePathForWipe(ocGlobalConfigDir() + QStringLiteral("/command"));
    } else if (k == QLatin1String("integrations")) {
        ok = removePathForWipe(integrationsFilePath());
        QJsonObject cfg = ocReadConfigObj(QStringLiteral("global"), QString());
        cfg.remove(QStringLiteral("mcp"));
        ok = ok && ocWriteConfigObj(QStringLiteral("global"), QString(), cfg);
        emit integrationsChanged();
    } else if (k == QLatin1String("profiles")) {
        ok = removePathForWipe(appData + QStringLiteral("/profiles"));
        m_profiles.reloadFromDisk();
        emit effectiveProfileChanged();
    } else if (k == QLatin1String("benchmarks")) {
        ok = removePathForWipe(appLocal + QStringLiteral("/benchmarks"));
        m_benchmarkResults.clear();
        emit benchmarkResultsChanged();
    } else if (k == QLatin1String("logs")) {
        ok = removePathForWipe(runtimeLogDir());
        m_log.clear();
        m_agentLog.clear();
        emit serverLogChanged();
        emit agentLogChanged();
    } else if (k == QLatin1String("settings")) {
        QSettings s;
        s.clear();
        m_language = QStringLiteral("es");
        m_agentApprovalMode = QStringLiteral("ask");
        m_agentThinkingEnabled = false;
        m_agentSystemPrompt.clear();
        m_agentPermRules.clear();
        m_agentTemperature = -1.0;
        emit languageChanged();
        emit agentApprovalModeChanged();
        emit agentThinkingChanged();
        emit agentTuningChanged();
    } else {
        emit serverError(QStringLiteral("Categoría desconocida: %1").arg(kind));
        return false;
    }

    if (!ok) {
        emit serverError(QStringLiteral("No se pudo completar el borrado de %1.").arg(k));
        return false;
    }

    reloadPersistentStateAfterImportOrWipe();
    emit serverError(QStringLiteral("Borrado completado: %1.").arg(k));
    return true;
}

void AppController::reloadPersistentStateAfterImportOrWipe()
{
    m_binaries.refresh();
    m_roots.refresh();
    loadChatSessions();
    m_profiles.reloadFromDisk();
    loadBenchmarkResults();
    loadCustomBenchmarks();
    emit integrationsChanged();
    emit setupStateChanged();
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
    // Draft (sesión activa sin persistir) ANTES de agrupar, así entra en el grupo
    // de su proyecto en vez de aparecer como sección duplicada al tope.
    injectDraftSession(sorted);
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
    emit chatSessionsChanged();
}

// Active session may not be persisted yet (no messages sent). Show it in the
// list anyway so the chat / new project is visible immediately. Se agrega al
// vector ANTES del agrupado para que caiga en el grupo de su proyecto.
void AppController::injectDraftSession(QVector<QVariantMap> &sessions)
{
    if (m_chatSessionId.isEmpty()) return;
    for (const auto &s : std::as_const(sessions))
        if (s.value(QStringLiteral("id")).toString() == m_chatSessionId)
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
    sessions.append(s);
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

QString AppController::exportChatSession(const QString &id, const QString &format)
{
    const QString sid = id.isEmpty() ? m_chatSessionId : id;
    if (sid.isEmpty()) return QString();

    QFile f(chatStorageDir() + QStringLiteral("/") + sid + QStringLiteral(".json"));
    if (!f.open(QIODevice::ReadOnly)) { emit serverError(QStringLiteral("La sesión no tiene archivo guardado.")); return QString(); }
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    f.close();

    const QString title = obj.value(QStringLiteral("title")).toString(QStringLiteral("chat"));
    const bool asJson = (format.compare(QLatin1String("json"), Qt::CaseInsensitive) == 0);

    // Nombre de archivo sugerido: título saneado.
    QString safe = title;
    safe.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    if (safe.trimmed().isEmpty()) safe = QStringLiteral("chat");
    const QString ext = asJson ? QStringLiteral("json") : QStringLiteral("md");
    const QString defName = safe + QStringLiteral(".") + ext;

    const QString path = QFileDialog::getSaveFileName(
        nullptr, QStringLiteral("Exportar conversación"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(defName),
        asJson ? QStringLiteral("JSON (*.json)") : QStringLiteral("Markdown (*.md)"));
    if (path.isEmpty()) return QString();

    QByteArray out;
    if (asJson) {
        out = QJsonDocument(obj).toJson();
    } else {
        QString md = QStringLiteral("# %1\n\n").arg(title);
        for (const QJsonValue &v : obj.value(QStringLiteral("messages")).toArray()) {
            const QJsonObject m = v.toObject();
            const QString role = m.value(QStringLiteral("role")).toString();
            const QString who = role == QLatin1String("user") ? QStringLiteral("🧑 Usuario")
                              : role == QLatin1String("assistant") ? QStringLiteral("🤖 Asistente")
                              : role;
            md += QStringLiteral("## %1\n\n%2\n\n").arg(who, m.value(QStringLiteral("content")).toString());
        }
        out = md.toUtf8();
    }

    QFile of(path);
    if (!of.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit serverError(QStringLiteral("No se pudo escribir el archivo de exportación."));
        return QString();
    }
    of.write(out);
    of.close();
    return path;
}

QVariantList AppController::searchChatHistory(const QString &query) const
{
    QVariantList results;
    const QString q = query.trimmed();
    if (q.isEmpty()) return results;

    for (const QVariant &sv : m_chatSessions) {
        const QVariantMap s = sv.toMap();
        const QString id = s.value(QStringLiteral("id")).toString();
        const QString title = s.value(QStringLiteral("title")).toString();
        bool match = title.contains(q, Qt::CaseInsensitive);
        QString snippet;

        QFile f(chatStorageDir() + QStringLiteral("/") + id + QStringLiteral(".json"));
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
            for (const QJsonValue &v : obj.value(QStringLiteral("messages")).toArray()) {
                const QString content = v.toObject().value(QStringLiteral("content")).toString();
                const int idx = content.indexOf(q, 0, Qt::CaseInsensitive);
                if (idx >= 0) {
                    match = true;
                    const int from = qMax(0, idx - 40);
                    snippet = content.mid(from, 120).simplified();
                    break;
                }
            }
        }
        if (match) {
            QVariantMap r;
            r[QStringLiteral("id")]          = id;
            r[QStringLiteral("title")]       = title;
            r[QStringLiteral("projectName")] = s.value(QStringLiteral("projectName"));
            r[QStringLiteral("snippet")]     = snippet;
            results.append(r);
        }
    }
    return results;
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

void AppController::steerChat(const QString &text)
{
    if (text.trimmed().isEmpty()) return;
    if (IAgentBackend *b = ensureChatBackend()) b->steerMessage(text);
}

void AppController::queueChat(const QString &text)
{
    if (text.trimmed().isEmpty()) return;
    if (IAgentBackend *b = ensureChatBackend()) b->queueMessage(text);
}

void AppController::clearChatQueue()
{
    if (m_chatBackend) m_chatBackend->clearQueue();
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
    const QString filter = m_serverHasVision
        ? QStringLiteral("Soportados (*.png *.jpg *.jpeg *.webp *.gif *.bmp *.txt *.md *.json *.csv *.log *.py *.js *.ts *.cpp *.h *.qml);;Todos (*.*)")
        : QStringLiteral("Texto (*.txt *.md *.json *.csv *.log *.py *.js *.ts *.cpp *.h *.qml);;Todos (*.*)");
    return QFileDialog::getOpenFileNames(
        parent, QStringLiteral("Adjuntar archivos"), QDir::homePath(), filter);
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
    {"nav.research",  "Research",       "Research",     "研究",       "Recherche",      "Ricerca",        "Recherche"},
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

struct RecommendedModelDef {
    QString name;
    QString repo;
    QString fileName;
    QString family;
    QString capabilities;
    QString params;
    QString quant;
    double sizeGb;
    double minRamGb;
    double recommendedRamGb;
    double minVramGb;
    int ctxK;
    QString notes;
};

static double catalogParamsB(const QJsonObject &model)
{
    const double raw = model.value(QStringLiteral("parameters_raw")).toDouble();
    if (raw > 0)
        return raw / 1000000000.0;

    QString pc = model.value(QStringLiteral("parameter_count")).toString().trimmed().toUpper();
    QRegularExpression re(QStringLiteral("^([\\d.]+)\\s*([BKMGT]?)$"));
    const QRegularExpressionMatch m = re.match(pc);
    if (!m.hasMatch())
        return 0;
    bool ok = false;
    const double val = m.captured(1).toDouble(&ok);
    if (!ok)
        return 0;
    const QString suffix = m.captured(2);
    if (suffix == QLatin1String("B")) return val;
    if (suffix == QLatin1String("M")) return val / 1000.0;
    if (suffix == QLatin1String("K")) return val / 1000000.0;
    if (suffix == QLatin1String("T")) return val * 1000.0;
    if (val >= 1000000.0) return val / 1000000000.0;
    return val / 1000.0;
}

static QString catalogFamily(const QString &name, const QString &arch)
{
    const QString s = (name + QLatin1Char(' ') + arch).toLower();
    if (s.contains(QStringLiteral("qwen"))) return QStringLiteral("Qwen");
    if (s.contains(QStringLiteral("mistral")) || s.contains(QStringLiteral("mixtral"))) return QStringLiteral("Mistral");
    if (s.contains(QStringLiteral("deepseek"))) return QStringLiteral("DeepSeek");
    if (s.contains(QStringLiteral("llama"))) return QStringLiteral("Llama");
    if (s.contains(QStringLiteral("gemma"))) return QStringLiteral("Gemma");
    if (s.contains(QStringLiteral("phi"))) return QStringLiteral("Phi");
    if (s.contains(QStringLiteral("gpt-oss")) || s.contains(QStringLiteral("openai"))) return QStringLiteral("GPT OSS");
    if (s.contains(QStringLiteral("olmo"))) return QStringLiteral("Olmo");
    return name.section(QLatin1Char('/'), 0, 0);
}

static QString catalogCapabilities(const QJsonObject &model)
{
    QStringList caps;
    const QString combined = (model.value(QStringLiteral("name")).toString() + QLatin1Char(' ') +
                              model.value(QStringLiteral("use_case")).toString() + QLatin1Char(' ') +
                              model.value(QStringLiteral("pipeline_tag")).toString()).toLower();
    if (combined.contains(QStringLiteral("code"))) caps << QStringLiteral("code");
    if (combined.contains(QStringLiteral("reason")) || combined.contains(QStringLiteral("thinking"))) caps << QStringLiteral("reasoning");
    if (combined.contains(QStringLiteral("vision")) || combined.contains(QStringLiteral("multimodal")) || combined.contains(QStringLiteral("vl"))) caps << QStringLiteral("vision");
    if (combined.contains(QStringLiteral("embed"))) caps << QStringLiteral("embedding");
    if (combined.contains(QStringLiteral("chat")) || combined.contains(QStringLiteral("instruct")) || combined.contains(QStringLiteral("instruction"))) caps << QStringLiteral("chat");
    if (model.value(QStringLiteral("is_moe")).toBool() || model.value(QStringLiteral("active_parameters")).toDouble() > 0) caps << QStringLiteral("moe");
    if (caps.isEmpty()) caps << QStringLiteral("general");
    caps.removeDuplicates();
    return caps.join(QLatin1Char(','));
}

static QString catalogUseCase(const QString &caps)
{
    if (caps.contains(QStringLiteral("code"))) return QStringLiteral("coding");
    if (caps.contains(QStringLiteral("reasoning"))) return QStringLiteral("reasoning");
    if (caps.contains(QStringLiteral("vision"))) return QStringLiteral("multimodal");
    if (caps.contains(QStringLiteral("embedding"))) return QStringLiteral("embedding");
    if (caps.contains(QStringLiteral("chat"))) return QStringLiteral("chat");
    return QStringLiteral("general");
}

static QString guessGgufFileName(const QString &repo, const QString &modelName, const QString &quant)
{
    QString base = repo.section(QLatin1Char('/'), -1);
    base.remove(QRegularExpression(QStringLiteral("[-_]?GGUF$"), QRegularExpression::CaseInsensitiveOption));
    if (base.isEmpty())
        base = modelName.section(QLatin1Char('/'), -1);
    return QStringLiteral("%1-%2.gguf").arg(base, quant.isEmpty() ? QStringLiteral("Q4_K_M") : quant);
}

static double quantBpp(const QString &quant)
{
    const QString q = quant.toUpper();
    if (q.contains(QStringLiteral("Q8")) || q.contains(QStringLiteral("FP8")) || q.contains(QStringLiteral("INT8"))) return 1.05;
    if (q.contains(QStringLiteral("Q6"))) return 0.80;
    if (q.contains(QStringLiteral("Q5"))) return 0.68;
    if (q.contains(QStringLiteral("Q3"))) return 0.48;
    if (q.contains(QStringLiteral("Q2"))) return 0.37;
    if (q.contains(QStringLiteral("BF16")) || q.contains(QStringLiteral("F16"))) return 2.0;
    if (q.contains(QStringLiteral("AWQ")) || q.contains(QStringLiteral("GPTQ")) || q.contains(QStringLiteral("Q4")) || q.contains(QStringLiteral("FP4")) || q.contains(QStringLiteral("NVFP4")) || q.contains(QStringLiteral("MXFP4"))) return 0.58;
    return 0.58;
}

static double estimateCatalogMemoryGb(const QJsonObject &model, double paramsB, const QString &quant, int ctx)
{
    double activeB = paramsB;
    const double activeRaw = model.value(QStringLiteral("active_parameters")).toDouble();
    if (model.value(QStringLiteral("is_moe")).toBool() && activeRaw > 0)
        activeB = activeRaw / 1000000000.0;
    return paramsB * quantBpp(quant) + 0.000008 * activeB * qMax(1024, ctx) + 0.5;
}

static double catalogSpeedTps(const QString &gpuName, double activeParamsB, double requiredGb, const QString &runMode)
{
    double bw = 220.0;
    const QString g = gpuName.toLower();
    if (g.contains(QStringLiteral("3090"))) bw = 936.0;
    else if (g.contains(QStringLiteral("4090"))) bw = 1008.0;
    else if (g.contains(QStringLiteral("4080"))) bw = 717.0;
    else if (g.contains(QStringLiteral("3080"))) bw = 760.0;
    else if (g.contains(QStringLiteral("3060"))) bw = 360.0;
    else if (g.contains(QStringLiteral("a6000"))) bw = 768.0;
    if (runMode == QLatin1String("cpu_only")) bw = 70.0;
    if (runMode == QLatin1String("cpu_offload")) bw = qMin(bw, 110.0);
    const double denom = qMax(0.2, activeParamsB * qMax(0.35, requiredGb / qMax(0.2, activeParamsB)));
    return qMax(0.0, (bw / denom) * 0.55);
}

static int catalogScore(const QJsonObject &model, double paramsB, double requiredGb, double ramGb, double vramGb,
                        const QString &quant, const QString &caps, int ctx, const QString &fit, double tps)
{
    Q_UNUSED(requiredGb)
    int quality = 30;
    if (paramsB < 3) quality = 45;
    else if (paramsB < 7) quality = 60;
    else if (paramsB < 10) quality = 75;
    else if (paramsB < 20) quality = 82;
    else if (paramsB < 40) quality = 89;
    else quality = 95;

    const QString name = model.value(QStringLiteral("name")).toString().toLower();
    if (name.contains(QStringLiteral("qwen3"))) quality += 4;
    else if (name.contains(QStringLiteral("qwen2.5"))) quality += 2;
    if (name.contains(QStringLiteral("deepseek"))) quality += 3;
    if (name.contains(QStringLiteral("llama"))) quality += 2;
    if (quant.contains(QStringLiteral("Q4"))) quality -= 5;
    if (quant.contains(QStringLiteral("Q5"))) quality -= 2;
    if (quant.contains(QStringLiteral("Q6"))) quality -= 1;
    if (caps.contains(QStringLiteral("code"))) quality += 4;
    if (caps.contains(QStringLiteral("moe"))) quality += 2;

    int fitScore = 0;
    if (fit == QLatin1String("Perfecto")) fitScore = 100;
    else if (fit == QLatin1String("Bueno")) fitScore = 82;
    else if (fit == QLatin1String("Marginal")) fitScore = 55;
    else fitScore = 10;

    const int speedScore = qBound(0, int((tps / 40.0) * 100.0), 100);
    const int ctxScore = ctx >= 32768 ? 100 : (ctx >= 8192 ? 80 : 55);
    const int popularity = qBound(0, int(std::log10(qMax(1.0, model.value(QStringLiteral("hf_downloads")).toDouble())) * 12.0), 60);
    int score = qRound(quality * 0.45 + speedScore * 0.20 + fitScore * 0.20 + ctxScore * 0.08 + popularity * 0.07);

    if (ramGb >= 64 && vramGb >= 16 && paramsB < 7)
        score -= 20;
    return qBound(0, score, 100);
}

// ── Public methods ─────────────────────────────────────────────────────────────

void AppController::rescanHardware()
{
    QVariantMap hw;
    hw[QStringLiteral("cpuThreads")] = QThread::idealThreadCount();

    double ramGb = 0;
#ifdef Q_OS_WIN
    {
        ULONGLONG kb = 0;
        if (GetPhysicallyInstalledSystemMemory(&kb))
            ramGb = kb / 1024.0 / 1024.0;
        if (ramGb <= 0) {
            MEMORYSTATUSEX st;
            st.dwLength = sizeof(st);
            if (GlobalMemoryStatusEx(&st))
                ramGb = st.ullTotalPhys / 1024.0 / 1024.0 / 1024.0;
        }
    }
#endif
    hw[QStringLiteral("ramGb")] = ramGb;

    double vramGb = 0;
    QString gpuName;
    const QString nvidiaSmi = QStandardPaths::findExecutable(QStringLiteral("nvidia-smi"));
    if (!nvidiaSmi.isEmpty()) {
        QProcess p;
        p.start(nvidiaSmi, {QStringLiteral("--query-gpu=name,memory.total"),
                            QStringLiteral("--format=csv,noheader,nounits")});
        if (p.waitForFinished(1800)) {
            const QString firstLine = QString::fromUtf8(p.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts).value(0).trimmed();
            const int comma = firstLine.lastIndexOf(',');
            if (comma > 0) {
                gpuName = firstLine.left(comma).trimmed();
                vramGb = firstLine.mid(comma + 1).trimmed().toDouble() / 1024.0;
            }
        }
    }
    hw[QStringLiteral("gpuName")] = gpuName.isEmpty() ? QStringLiteral("GPU no detectada") : gpuName;
    hw[QStringLiteral("vramGb")] = vramGb;
    hw[QStringLiteral("backendHint")] = vramGb >= 6 ? QStringLiteral("GPU") : QStringLiteral("CPU");
    hw[QStringLiteral("summary")] = QStringLiteral("%1 hilos · %2 GB RAM · %3")
        .arg(QThread::idealThreadCount())
        .arg(ramGb > 0 ? QString::number(ramGb, 'f', 1) : QStringLiteral("?"))
        .arg(vramGb > 0
                 ? QStringLiteral("%1 (%2 GB VRAM)").arg(gpuName).arg(QString::number(vramGb, 'f', 1))
                 : QStringLiteral("sin VRAM NVIDIA detectada"));

    m_hardwareSummary = hw;
    emit hardwareSummaryChanged();
    rebuildModelRecommendations();
}

void AppController::rebuildModelRecommendations()
{
    const double ramGb = m_hardwareSummary.value(QStringLiteral("ramGb")).toDouble();
    const double vramGb = m_hardwareSummary.value(QStringLiteral("vramGb")).toDouble();
    const QString gpuName = m_hardwareSummary.value(QStringLiteral("gpuName")).toString();

    QVariantList rows;
    QFile catalog(QStringLiteral(":/assets/hwfit/hf_models.json"));
    if (catalog.open(QIODevice::ReadOnly)) {
        const QJsonArray arr = QJsonDocument::fromJson(catalog.readAll()).array();
        rows.reserve(arr.size());
        for (const QJsonValue &value : arr) {
            const QJsonObject m = value.toObject();
            const QString name = m.value(QStringLiteral("name")).toString();
            const double paramsB = catalogParamsB(m);
            if (name.isEmpty() || paramsB <= 0)
                continue;

            const QString quant = m.value(QStringLiteral("quantization")).toString(QStringLiteral("Q4_K_M"));
            const int ctx = qMax(1024, m.value(QStringLiteral("context_length")).toInt(4096));
            const double requiredGb = estimateCatalogMemoryGb(m, paramsB, quant, ctx);
            const double activeRaw = m.value(QStringLiteral("active_parameters")).toDouble();
            const double activeB = (m.value(QStringLiteral("is_moe")).toBool() && activeRaw > 0)
                ? activeRaw / 1000000000.0
                : paramsB;

            QString runMode;
            QString fit;
            if (vramGb > 0 && requiredGb <= vramGb) {
                runMode = QStringLiteral("gpu");
                fit = (m.value(QStringLiteral("recommended_ram_gb")).toDouble(requiredGb) <= vramGb)
                    ? QStringLiteral("Perfecto")
                    : QStringLiteral("Bueno");
            } else if (requiredGb <= ramGb) {
                runMode = vramGb > 0 ? QStringLiteral("cpu_offload") : QStringLiteral("cpu_only");
                fit = ramGb >= requiredGb * 1.2 ? QStringLiteral("Bueno") : QStringLiteral("Marginal");
            } else {
                runMode = QStringLiteral("no_fit");
                fit = QStringLiteral("No entra");
            }

            const QString caps = catalogCapabilities(m);
            const double tps = runMode == QLatin1String("no_fit")
                ? 0.0
                : catalogSpeedTps(gpuName, activeB, requiredGb, runMode);
            const int score = catalogScore(m, paramsB, requiredGb, ramGb, vramGb, quant, caps, ctx, fit, tps);

            QString repo;
            QString fileName;
            const QJsonArray ggufSources = m.value(QStringLiteral("gguf_sources")).toArray();
            if (!ggufSources.isEmpty()) {
                const QJsonObject src = ggufSources.first().toObject();
                repo = src.value(QStringLiteral("repo")).toString();
                fileName = src.value(QStringLiteral("file")).toString();
            }
            if (repo.isEmpty() && (m.value(QStringLiteral("is_gguf")).toBool() || name.toLower().contains(QStringLiteral("gguf"))))
                repo = name;
            if (!repo.isEmpty() && fileName.isEmpty())
                fileName = guessGgufFileName(repo, name, quant);

            QVariantMap row;
            row[QStringLiteral("name")] = name;
            row[QStringLiteral("repo")] = repo.isEmpty() ? name : repo;
            row[QStringLiteral("fileName")] = fileName;
            row[QStringLiteral("family")] = catalogFamily(name, m.value(QStringLiteral("architecture")).toString());
            row[QStringLiteral("capabilities")] = caps;
            row[QStringLiteral("useCase")] = catalogUseCase(caps);
            row[QStringLiteral("params")] = m.value(QStringLiteral("parameter_count")).toString(QStringLiteral("%1B").arg(QString::number(paramsB, 'f', 1)));
            row[QStringLiteral("quant")] = quant;
            row[QStringLiteral("sizeGb")] = requiredGb;
            row[QStringLiteral("requiredGb")] = requiredGb;
            row[QStringLiteral("minRamGb")] = m.value(QStringLiteral("min_ram_gb")).toDouble();
            row[QStringLiteral("recommendedRamGb")] = m.value(QStringLiteral("recommended_ram_gb")).toDouble();
            row[QStringLiteral("minVramGb")] = m.value(QStringLiteral("min_vram_gb")).toDouble();
            row[QStringLiteral("ctxK")] = qRound(ctx / 1000.0);
            row[QStringLiteral("context")] = ctx;
            row[QStringLiteral("notes")] = QStringLiteral("%1 · %2 t/s est. · %3 downloads")
                .arg(runMode)
                .arg(QString::number(tps, 'f', 1))
                .arg(qRound(m.value(QStringLiteral("hf_downloads")).toDouble()));
            row[QStringLiteral("fit")] = fit;
            row[QStringLiteral("score")] = score;
            row[QStringLiteral("downloadable")] = !repo.isEmpty() && !fileName.isEmpty();
            row[QStringLiteral("downloadUrl")] = (!repo.isEmpty() && !fileName.isEmpty())
                ? QStringLiteral("https://huggingface.co/%1/resolve/main/%2?download=true")
                    .arg(repo, QString::fromLatin1(QUrl::toPercentEncoding(fileName)))
                : QString();
            rows.append(row);
        }
    }

    if (rows.isEmpty()) {
        const QVector<RecommendedModelDef> fallback = {
            {QStringLiteral("Qwen3 Coder 30B A3B Instruct"), QStringLiteral("unsloth/Qwen3-Coder-30B-A3B-Instruct-GGUF"),
             QStringLiteral("Qwen3-Coder-30B-A3B-Instruct-Q4_K_M.gguf"), QStringLiteral("Qwen"), QStringLiteral("code,moe"),
             QStringLiteral("30B MoE"), QStringLiteral("Q4_K_M"), 18.5, 48, 64, 18, 256, QStringLiteral("fallback local")},
            {QStringLiteral("Qwen2.5 Coder 32B Instruct"), QStringLiteral("bartowski/Qwen2.5-Coder-32B-Instruct-GGUF"),
             QStringLiteral("Qwen2.5-Coder-32B-Instruct-Q4_K_M.gguf"), QStringLiteral("Qwen"), QStringLiteral("code,chat"),
             QStringLiteral("32B"), QStringLiteral("Q4_K_M"), 20.0, 48, 64, 24, 32, QStringLiteral("fallback local")}
        };
        for (const RecommendedModelDef &m : fallback) {
            const QString fit = (ramGb >= m.recommendedRamGb && (vramGb <= 0 || vramGb >= m.minVramGb))
                ? QStringLiteral("Perfecto")
                : (ramGb >= m.minRamGb ? QStringLiteral("Bueno") : QStringLiteral("Marginal"));
            const int score = qBound(0, int(70 + m.sizeGb), 100);
            QVariantMap row;
            row[QStringLiteral("name")] = m.name;
            row[QStringLiteral("repo")] = m.repo;
            row[QStringLiteral("fileName")] = m.fileName;
            row[QStringLiteral("family")] = m.family;
            row[QStringLiteral("capabilities")] = m.capabilities;
            row[QStringLiteral("params")] = m.params;
            row[QStringLiteral("quant")] = m.quant;
            row[QStringLiteral("sizeGb")] = m.sizeGb;
            row[QStringLiteral("ctxK")] = m.ctxK;
            row[QStringLiteral("notes")] = m.notes;
            row[QStringLiteral("fit")] = fit;
            row[QStringLiteral("score")] = score;
            row[QStringLiteral("downloadable")] = true;
            rows.append(row);
        }
    }

    std::sort(rows.begin(), rows.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("score")).toInt() >
               b.toMap().value(QStringLiteral("score")).toInt();
    });

    m_modelRecommendations = rows;
    emit modelRecommendationsChanged();
}

QString AppController::modelDownloadDir() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                  + QStringLiteral("/models");
    QDir().mkpath(dir);
    return dir;
}

void AppController::openModelRecommendation(const QString &repo)
{
    if (repo.trimmed().isEmpty()) return;
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://huggingface.co/%1").arg(repo)));
}

void AppController::downloadRecommendedModel(const QString &repo, const QString &fileName)
{
    if (repo.trimmed().isEmpty() || fileName.trimmed().isEmpty() || m_modelDownloadReply)
        return;

    const QString dir = modelDownloadDir();
    const QString outPath = dir + QLatin1Char('/') + QFileInfo(fileName).fileName();
    if (QFileInfo::exists(outPath)) {
        m_modelDownloadProgress = 100;
        m_modelDownloadStatus = QStringLiteral("Ya existe: %1").arg(outPath);
        emit modelDownloadChanged();
        QString existingRootId;
        for (int i = 0; i < m_roots.rowCount(); ++i) {
            const QModelIndex mi = m_roots.index(i, 0);
            if (QDir::cleanPath(m_roots.data(mi, ModelRootRegistry::PathRole).toString()) == QDir::cleanPath(dir)) {
                existingRootId = m_roots.data(mi, ModelRootRegistry::IdRole).toString();
                break;
            }
        }
        if (existingRootId.isEmpty())
            m_roots.add(dir, QStringLiteral("Modelos descargados"), QStringLiteral("startup"), {});
        else
            m_roots.scan(existingRootId);
        return;
    }

    auto *file = new QFile(outPath, this);
    if (!file->open(QIODevice::WriteOnly)) {
        file->deleteLater();
        emit serverError(QStringLiteral("No se pudo escribir el modelo en %1").arg(outPath));
        return;
    }

    if (!m_nam) m_nam = new QNetworkAccessManager(this);
    QUrl url(QStringLiteral("https://huggingface.co/%1/resolve/main/%2")
                 .arg(repo, QString::fromLatin1(QUrl::toPercentEncoding(fileName))));
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(0);

    m_modelDownloadFile = file;
    m_modelDownloadPath = outPath;
    m_modelDownloadProgress = 0;
    m_modelDownloadStatus = QStringLiteral("Descargando %1...").arg(fileName);
    emit modelDownloadChanged();

    m_modelDownloadReply = m_nam->get(req);
    connect(m_modelDownloadReply, &QNetworkReply::readyRead, this, [this]() {
        if (m_modelDownloadFile && m_modelDownloadReply)
            m_modelDownloadFile->write(m_modelDownloadReply->readAll());
    });
    connect(m_modelDownloadReply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (total > 0) {
            m_modelDownloadProgress = qBound(0, int((received * 100) / total), 100);
            m_modelDownloadStatus = QStringLiteral("Descargando modelo... %1/%2 MB")
                .arg(received / 1024 / 1024).arg(total / 1024 / 1024);
            emit modelDownloadChanged();
        }
    });
    connect(m_modelDownloadReply, &QNetworkReply::finished, this, [this, dir]() {
        QNetworkReply *reply = m_modelDownloadReply;
        QFile *file = m_modelDownloadFile;
        m_modelDownloadReply = nullptr;
        m_modelDownloadFile = nullptr;

        const bool ok = reply && reply->error() == QNetworkReply::NoError;
        const QString err = reply ? reply->errorString() : QStringLiteral("respuesta inválida");
        if (file) {
            file->write(reply ? reply->readAll() : QByteArray());
            file->close();
            file->deleteLater();
        }
        if (reply)
            reply->deleteLater();

        if (!ok) {
            QFile::remove(m_modelDownloadPath);
            m_modelDownloadProgress = 0;
            m_modelDownloadStatus = QStringLiteral("Error descargando modelo: %1").arg(err);
            emit modelDownloadChanged();
            emit serverError(m_modelDownloadStatus);
            return;
        }

        m_modelDownloadProgress = 100;
        m_modelDownloadStatus = QStringLiteral("Modelo descargado: %1").arg(m_modelDownloadPath);
        emit modelDownloadChanged();

        QString existingRootId;
        for (int i = 0; i < m_roots.rowCount(); ++i) {
            const QModelIndex mi = m_roots.index(i, 0);
            if (QDir::cleanPath(m_roots.data(mi, ModelRootRegistry::PathRole).toString()) == QDir::cleanPath(dir)) {
                existingRootId = m_roots.data(mi, ModelRootRegistry::IdRole).toString();
                break;
            }
        }
        if (existingRootId.isEmpty())
            m_roots.add(dir, QStringLiteral("Modelos descargados"), QStringLiteral("startup"), {});
        else
            m_roots.scan(existingRootId);
        emit setupStateChanged();
    });
}

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

// ── Heurísticas para benchmarks personalizados ───────────────────────────────

// "Solo código": la respuesta es esencialmente código, sin prosa explicativa.
// Heurística: si hay fences ```…```, mide cuánto texto queda fuera de ellos;
// si no hay fences, mira si las líneas parecen código (indentación / símbolos).
static bool benchCodeOnly(const QString &resp)
{
    const QString r = resp.trimmed();
    if (r.isEmpty()) return false;

    static const QRegularExpression fence(QStringLiteral("```[^\\n]*\\n.*?```"),
                                          QRegularExpression::DotMatchesEverythingOption);
    QString outside = r;
    bool hadFence = false;
    auto it = fence.globalMatch(r);
    while (it.hasNext()) { hadFence = true; outside.remove(it.next().captured(0)); }

    if (hadFence) {
        // Prosa fuera de los bloques de código → no es "solo código".
        const QString stray = outside.trimmed();
        return stray.length() <= 40;   // tolera algún "Here:" mínimo
    }

    // Sin fences: heurística de líneas-código.
    const QStringList lines = r.split('\n', Qt::SkipEmptyParts);
    int codey = 0;
    static const QRegularExpression codeHint(
        QStringLiteral("(^\\s+|[;{}=()\\[\\]]|\\b(def|class|import|from|return|for|while|if|var|let|const|function)\\b)"));
    for (const QString &l : lines)
        if (codeHint.match(l).hasMatch()) codey++;
    return lines.size() > 0 && (double)codey / lines.size() >= 0.7;
}

// Dependencias Python: extrae imports top-level y marca las que no son stdlib.
// No verifica existencia real en PyPI (offline) → "invented" = no-stdlib.
static QVariantMap benchPythonDeps(const QString &resp)
{
    static const QSet<QString> stdlib = {
        "abc","argparse","array","asyncio","base64","bisect","calendar","collections",
        "contextlib","copy","csv","datetime","decimal","enum","functools","glob","gzip",
        "hashlib","heapq","hmac","html","http","io","itertools","json","logging","math",
        "multiprocessing","os","pathlib","pickle","queue","random","re","shutil","signal",
        "socket","sqlite3","statistics","string","struct","subprocess","sys","tempfile",
        "textwrap","threading","time","timeit","traceback","types","typing","unittest",
        "urllib","uuid","warnings","weakref","xml","zipfile","zlib","dataclasses","secrets",
        "inspect","operator","platform","getpass","shlex","fnmatch","difflib","pprint"
    };
    static const QRegularExpression imp(
        QStringLiteral("(?m)^\\s*(?:import\\s+([A-Za-z_][\\w]*)|from\\s+([A-Za-z_][\\w]*))"));

    QSet<QString> nonStd;
    QSet<QString> all;
    auto it = imp.globalMatch(resp);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString mod = m.captured(1).isEmpty() ? m.captured(2) : m.captured(1);
        if (mod.isEmpty()) continue;
        all.insert(mod);
        if (!stdlib.contains(mod)) nonStd.insert(mod);
    }
    QStringList list(nonStd.begin(), nonStd.end());
    list.sort();
    return QVariantMap{
        {"hasCode", !all.isEmpty()},
        {"invented", !nonStd.isEmpty()},
        {"count", (int)nonStd.size()},
        {"list", list}
    };
}

// Convert user-defined custom tasks (QVariantList of maps) into BenchTaskDef.
// Custom tasks have no auto-eval; quality tasks just record the response.
static QVector<BenchTaskDef> buildCustomBenchTasks(const QVariantList &custom)
{
    QVector<BenchTaskDef> t;
    int n = 0;
    for (const QVariant &v : custom) {
        const QVariantMap m = v.toMap();
        const QString prompt = m.value("prompt").toString();
        if (prompt.trimmed().isEmpty()) continue;
        BenchTaskDef d;
        d.id        = m.value("id").toString();
        if (d.id.isEmpty()) d.id = QStringLiteral("task_%1").arg(++n);
        d.isSpeed   = m.value("isSpeed", true).toBool();
        d.category  = m.value("category", d.isSpeed ? "speed" : "custom").toString();
        d.prompt    = prompt;
        d.maxTokens = m.value("maxTokens", 8192).toInt();
        d.eval      = nullptr;
        t.append(d);
    }
    return t;
}

void AppController::startBenchmark(const QStringList &profileIds, const QString &mode, int passes)
{
    runBenchmarkInternal(profileIds, mode, {}, QStringLiteral("standard"), qMax(1, passes));
}

void AppController::startCustomBenchmark(const QStringList &profileIds, const QString &customId, int passes)
{
    QVariantMap def;
    for (const QVariant &v : std::as_const(m_customBenchmarks)) {
        const QVariantMap m = v.toMap();
        if (m.value("id").toString() == customId) { def = m; break; }
    }
    if (def.isEmpty()) return;
    const QString label = def.value("name").toString().isEmpty()
                              ? QStringLiteral("custom") : def.value("name").toString();
    runBenchmarkInternal(profileIds, QStringLiteral("custom"),
                         def.value("prompts").toList(), label, qMax(1, passes));
}

void AppController::openBenchmarkFolder(const QString &path)
{
    if (path.trimmed().isEmpty()) return;
    const QFileInfo fi(path);
    if (!fi.exists()) {
        emit serverError(QStringLiteral("La carpeta del benchmark ya no existe:\n%1").arg(path));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absoluteFilePath()));
}

void AppController::runBenchmarkInternal(const QStringList &profileIds, const QString &mode,
                                         const QVariantList &customTasks, const QString &runLabel,
                                         int passes)
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

    const bool isCustom = !customTasks.isEmpty();
    const QVector<BenchTaskDef> tasks = isCustom
                                            ? buildCustomBenchTasks(customTasks)
                                            : buildBenchTasks(mode);
    passes = qMax(1, passes);
    const int totalSteps = profileIds.size() * tasks.size() * passes;

    // ── Isolated run folder: benchmark/<label>_<timestamp>/ ───────────────────
    auto sanitize = [](QString s) {
        s.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")), QStringLiteral("_"));
        return s.left(40);
    };
    const QString stamp   = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const QString runDir  = benchmarkRunsDir() + "/" + sanitize(runLabel) + "_" + stamp;
    QDir().mkpath(runDir);

    // Write run metadata up front
    {
        QJsonObject meta;
        meta["label"]      = runLabel;
        meta["mode"]       = mode;
        meta["startedAt"]  = QDateTime::currentDateTime().toString(Qt::ISODate);
        meta["timestamp"]  = (double)QDateTime::currentMSecsSinceEpoch();
        QJsonArray profArr;
        for (const QString &pid : profileIds) {
            const QVariantMap pd = m_profiles.getLaunchProfile(pid);
            QJsonObject po;
            po["profileId"]   = pid;
            po["profileName"] = pd.value("name").toString();
            profArr.append(po);
        }
        meta["profiles"] = profArr;
        QJsonArray taskArr;
        for (const BenchTaskDef &t : tasks) {
            QJsonObject to;
            to["id"]        = t.id;
            to["category"]  = t.category;
            to["isSpeed"]   = t.isSpeed;
            to["maxTokens"] = t.maxTokens;
            to["prompt"]    = t.prompt;
            taskArr.append(to);
        }
        meta["tasks"] = taskArr;
        meta["passes"] = passes;
        QFile mf(runDir + "/metadata.json");
        if (mf.open(QIODevice::WriteOnly | QIODevice::Truncate))
            mf.write(QJsonDocument(meta).toJson());
    }
    const auto runDirShared = std::make_shared<QString>(runDir);
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
                    // Run all tasks sequentially, repeated `passes` times per profile.
                    auto taskResults = std::make_shared<QVariantList>();
                    auto passNo = std::make_shared<int>(1);
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

                                const QString rowName = passes > 1
                                    ? QString("%1 · pasada %2/%3").arg(profName).arg(*passNo).arg(passes)
                                    : profName;

                                QVariantMap result;
                                result["profileId"]    = profileId;
                                result["profileName"]  = rowName;
                                result["pass"]         = *passNo;
                                result["passesTotal"]  = passes;
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

                                result["runLabel"] = runLabel;
                                result["runDir"]   = *runDirShared;

                                m_benchmarkResults.append(result);
                                emit benchmarkResultsChanged();
                                saveBenchmarkResult(result);

                                // Also drop a per-profile file into the isolated run folder
                                {
                                    QString fname = profName;
                                    fname.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")),
                                                  QStringLiteral("_"));
                                    QFile pf(*runDirShared + "/" + fname.left(60) + "_"
                                             + result.value("id").toString() + ".json");
                                    if (pf.open(QIODevice::WriteOnly | QIODevice::Truncate))
                                        pf.write(QJsonDocument(
                                            QJsonObject::fromVariantMap(result)).toJson());
                                }

                                // Más pasadas para este perfil → reusar servidor cargado.
                                if (!m_benchmarkCanceled && *passNo < passes) {
                                    (*passNo)++;
                                    taskResults->clear();
                                    m_benchmarkStatus = QString("[%1/%2] %3 — pasada %4/%5...")
                                        .arg(idx+1).arg(profileIds.size()).arg(profName)
                                        .arg(*passNo).arg(passes);
                                    emit benchmarkStatusChanged();
                                    (*runTask)(0);
                                    return;
                                }

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

                        // Custom tasks always stream so we capture TTFT/tps/tokens for every prompt.
                        const bool stream = isCustom ? true : task.isSpeed;
                        benchmarkRequest(url, task.prompt, task.maxTokens, stream, [=](QVariantMap res) {
                            (*stepsDone)++;
                            m_benchmarkProgress = qMin(99, (*stepsDone * 100) / qMax(1, totalSteps));
                            emit benchmarkProgressChanged();

                            res["id"]       = task.id;
                            res["category"] = task.category;
                            if (!task.isSpeed && task.eval)
                                res["passed"] = task.eval(res.value("response").toString());

                            if (isCustom) {
                                const QString resp = res.value("response").toString();
                                res["isSpeedTask"] = task.isSpeed;
                                res["codeOnly"]    = benchCodeOnly(resp);
                                const QVariantMap deps = benchPythonDeps(resp);
                                res["hasCode"]       = deps.value("hasCode");
                                res["inventedDeps"]  = deps.value("invented");
                                res["depCount"]      = deps.value("count");
                                res["depList"]       = deps.value("list");
                            }

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
    if (streaming)
        payload["stream_options"] = QJsonObject{{"include_usage", true}};

    auto *reply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    const qint64 startMs = QDateTime::currentMSecsSinceEpoch();

    if (streaming) {
        struct SpeedState { QByteArray buf; qint64 ttftMs = -1; int chunks = 0;
                            int tokens = 0; QString response; };
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
                const QJsonObject obj = QJsonDocument::fromJson(json).object();
                // Final usage chunk (choices empty, usage populated)
                const QJsonObject usage = obj.value("usage").toObject();
                if (!usage.isEmpty())
                    state->tokens = usage.value("completion_tokens").toInt(state->tokens);
                const QString delta = obj.value("choices").toArray().first().toObject()
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
            const int tokens = state->tokens > 0 ? state->tokens : state->chunks;
            double tps = 0;
            if (state->ttftMs >= 0 && tokens > 0)
                tps = tokens / qMax(0.001, (totalMs - state->ttftMs) / 1000.0);
            QVariantMap r;
            r["type"]       = "speed";
            r["ttft_ms"]    = state->ttftMs;
            r["tps"]        = tps;
            r["chunks"]     = state->chunks;
            r["tokens"]     = tokens;
            r["elapsed_ms"] = totalMs;
            r["response"]   = state->response;
            reply->deleteLater();
            onDone(r);
        });
    } else {
        connect(reply, &QNetworkReply::finished, this, [=]() {
            const qint64 totalMs = QDateTime::currentMSecsSinceEpoch() - startMs;
            QString response;
            int tokens = 0;
            if (reply->error() == QNetworkReply::NoError) {
                const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
                response = obj.value("choices").toArray().first().toObject()
                    .value("message").toObject().value("content").toString();
                tokens = obj.value("usage").toObject().value("completion_tokens").toInt();
            }
            QVariantMap r;
            r["type"]       = "quality";
            r["elapsed_ms"] = totalMs;
            r["tokens"]     = tokens;
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

// ── Custom benchmarks ───────────────────────────────────────────────────────

QString AppController::benchmarkRunsDir() const
{
    // Isolated, timestamped run folders live under <appDir>/benchmark
    const QString dir = QCoreApplication::applicationDirPath() + "/benchmark";
    QDir().mkpath(dir);
    return dir;
}

QString AppController::customBenchmarkDir() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + "/benchmarks/custom";
    QDir().mkpath(dir);
    return dir;
}

void AppController::loadCustomBenchmarks()
{
    m_customBenchmarks.clear();
    const QDir dir(customBenchmarkDir());
    const auto files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString &f : files) {
        QFile jf(dir.filePath(f));
        if (!jf.open(QIODevice::ReadOnly)) continue;
        const QJsonObject o = QJsonDocument::fromJson(jf.readAll()).object();
        if (!o.isEmpty()) m_customBenchmarks.append(o.toVariantMap());
    }
    emit customBenchmarksChanged();
}

QString AppController::saveCustomBenchmark(const QVariantMap &def)
{
    QVariantMap m = def;
    QString id = m.value("id").toString();
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m["id"] = id;
    }
    QFile f(customBenchmarkDir() + "/" + id + ".json");
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(QJsonObject::fromVariantMap(m)).toJson());
        f.close();   // flush before reload, else loadCustomBenchmarks reads empty file
    }
    loadCustomBenchmarks();
    return id;
}

void AppController::deleteCustomBenchmark(const QString &id)
{
    if (id.isEmpty()) return;
    QFile::remove(customBenchmarkDir() + "/" + id + ".json");
    loadCustomBenchmarks();
}

// ── Deep Research ─────────────────────────────────────────────────────────────

QString AppController::researchStorageDir() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/research");
    QDir().mkpath(dir);
    return dir;
}

void AppController::setResearchState(bool running, int progress, const QString &status)
{
    m_researchRunning = running;
    m_researchProgress = qBound(0, progress, 100);
    m_researchStatus = status;
    emit researchChanged();
}

void AppController::refreshResearchReports()
{
    const QString idxPath = researchStorageDir() + QStringLiteral("/index.json");
    QFile f(idxPath);
    QVariantList out;
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
        QList<QJsonObject> objs;
        for (const QJsonValue &v : arr) objs.append(v.toObject());
        std::sort(objs.begin(), objs.end(), [](const QJsonObject &a, const QJsonObject &b) {
            return a.value(QStringLiteral("timestamp")).toDouble()
                   > b.value(QStringLiteral("timestamp")).toDouble();
        });
        for (const QJsonObject &o : objs)
            out.append(o.toVariantMap());
    }
    m_researchReports = out;
    emit researchReportsChanged();
}

void AppController::saveResearchReport(const QVariantMap &summary, const QString &markdown,
                                       const QJsonObject &full)
{
    const QString dir = researchStorageDir();
    const QString id = summary.value(QStringLiteral("id")).toString();
    if (id.isEmpty()) return;

    QFile md(dir + QLatin1Char('/') + id + QStringLiteral(".md"));
    if (md.open(QIODevice::WriteOnly | QIODevice::Truncate))
        md.write(markdown.toUtf8());

    QFile json(dir + QLatin1Char('/') + id + QStringLiteral(".json"));
    if (json.open(QIODevice::WriteOnly | QIODevice::Truncate))
        json.write(QJsonDocument(full).toJson(QJsonDocument::Indented));

    const QString idxPath = dir + QStringLiteral("/index.json");
    QJsonArray idx;
    QFile idxFile(idxPath);
    if (idxFile.open(QIODevice::ReadOnly)) {
        idx = QJsonDocument::fromJson(idxFile.readAll()).array();
        idxFile.close();
    }
    QJsonArray kept;
    for (const QJsonValue &v : idx)
        if (v.toObject().value(QStringLiteral("id")).toString() != id)
            kept.append(v);
    kept.prepend(QJsonObject::fromVariantMap(summary));
    if (idxFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        idxFile.write(QJsonDocument(kept).toJson(QJsonDocument::Indented));

    refreshResearchReports();
}

void AppController::startResearch(const QString &topic, const QString &mode, int maxPages)
{
    const QString cleanTopic = topic.trimmed();
    if (cleanTopic.isEmpty()) return;
    if (m_researchRunning) {
        emit serverError(QStringLiteral("Ya hay una investigación en curso."));
        return;
    }
    if (!serverRunning() || !serverReady()) {
        emit serverError(QStringLiteral("Deep Research necesita el servidor listo para sintetizar el reporte."));
        return;
    }

    if (!m_nam) m_nam = new QNetworkAccessManager(this);
    maxPages = qBound(2, maxPages <= 0 ? 6 : maxPages, 10);

    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString normalizedMode = mode.trimmed().isEmpty() ? QStringLiteral("auto") : mode.trimmed();
    const QStringList queries = researchQueriesFor(cleanTopic, normalizedMode);
    auto hits = std::make_shared<QVector<ResearchHit>>();
    auto sourceTexts = std::make_shared<QStringList>();
    auto sources = std::make_shared<QJsonArray>();
    auto searchLogs = std::make_shared<QStringList>();
    auto addHit = [hits](const ResearchHit &h) {
        if (h.url.isEmpty()) return;
        for (const ResearchHit &existing : std::as_const(*hits))
            if (existing.url == h.url) return;
        hits->append(h);
    };

    setResearchState(true, 1, QStringLiteral("Preparando búsqueda..."));

    auto fail = [this](const QString &message) {
        if (m_researchReply) {
            m_researchReply->deleteLater();
            m_researchReply = nullptr;
        }
        setResearchState(false, 0, message);
        emit serverError(message);
    };

    auto synthesize = std::make_shared<std::function<void()>>();
    auto fetchNext = std::make_shared<std::function<void(int)>>();
    auto searchNext = std::make_shared<std::function<void(int)>>();

    *synthesize = [=]() {
        setResearchState(true, 82, QStringLiteral("Sintetizando reporte..."));

        QStringList sourceLines;
        for (int i = 0; i < sources->size(); ++i) {
            const QJsonObject s = sources->at(i).toObject();
            sourceLines << QStringLiteral("[%1] %2 — %3")
                               .arg(i + 1)
                               .arg(s.value(QStringLiteral("title")).toString(),
                                    s.value(QStringLiteral("url")).toString());
        }

        QString dossier;
        dossier += QStringLiteral("# Dossier: %1\n\n").arg(cleanTopic);
        dossier += QStringLiteral("Mode: %1\n\n").arg(researchModeTitle(normalizedMode));
        dossier += QStringLiteral("## Search log\n%1\n\n").arg(searchLogs->join(QLatin1Char('\n')));
        dossier += QStringLiteral("## Sources\n%1\n\n").arg(sourceLines.join(QLatin1Char('\n')));
        dossier += QStringLiteral("## Extracts\n%1\n").arg(sourceTexts->join(QStringLiteral("\n\n")));

        const QString sys = QStringLiteral(
            "Sos un investigador técnico. Sintetizá fuentes web en español. "
            "Separá hechos confirmados de inferencias, citá fuentes como [1], [2], "
            "marcá contradicciones y no inventes evidencia.");
        const QString user = QStringLiteral(
            "Tema: %1\nModo: %2\n\n"
            "Usá el dossier de fuentes de abajo y devolvé un reporte Markdown con: "
            "Resumen ejecutivo, Hallazgos clave, Evidencia, Riesgos/limitaciones, "
            "Próximos pasos. Para Product/Compare agregá matriz de recomendación; "
            "para How-to pasos; para Fact-check veredictos.\n\n%3")
            .arg(cleanTopic, researchModeTitle(normalizedMode), dossier.left(30000));

        QJsonObject payload{
            {QStringLiteral("model"), QStringLiteral("research")},
            {QStringLiteral("messages"), QJsonArray{
                QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                            {QStringLiteral("content"), sys}},
                QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                            {QStringLiteral("content"), user}}}},
            {QStringLiteral("stream"), false},
            {QStringLiteral("temperature"), 0.2},
            {QStringLiteral("max_tokens"), 2200},
            {QStringLiteral("cache_prompt"), true}
        };

        QNetworkRequest req(QUrl(serverBaseUrl() + QStringLiteral("/v1/chat/completions")));
        req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
        req.setTransferTimeout(180000);
        m_researchReply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
        connect(m_researchReply, &QNetworkReply::finished, this, [=]() {
            QNetworkReply *reply = m_researchReply;
            m_researchReply = nullptr;
            if (!reply) return;
            const QByteArray raw = reply->readAll();
            const bool ok = reply->error() == QNetworkReply::NoError;
            const QString err = reply->errorString();
            reply->deleteLater();
            if (!m_researchRunning) return;

            QString report;
            if (ok) {
                const QJsonObject root = QJsonDocument::fromJson(raw).object();
                const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
                if (!choices.isEmpty())
                    report = choices.first().toObject()
                                 .value(QStringLiteral("message")).toObject()
                                 .value(QStringLiteral("content")).toString().trimmed();
            }
            if (report.isEmpty()) {
                report = QStringLiteral("# Deep Research: %1\n\n"
                                        "> No se pudo sintetizar con el modelo (%2). "
                                        "Se guarda el dossier crudo.\n\n%3")
                             .arg(cleanTopic, ok ? QStringLiteral("respuesta vacía") : err, dossier);
            }

            const double ts = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
            const QString title = cleanTopic.left(96);
            QVariantMap summary{
                {QStringLiteral("id"), id},
                {QStringLiteral("title"), title},
                {QStringLiteral("topic"), cleanTopic},
                {QStringLiteral("mode"), normalizedMode},
                {QStringLiteral("modeLabel"), researchModeTitle(normalizedMode)},
                {QStringLiteral("timestamp"), ts},
                {QStringLiteral("sourceCount"), sources->size()},
                {QStringLiteral("path"), researchStorageDir() + QLatin1Char('/') + id + QStringLiteral(".md")}
            };
            QJsonObject full{
                {QStringLiteral("id"), id},
                {QStringLiteral("topic"), cleanTopic},
                {QStringLiteral("mode"), normalizedMode},
                {QStringLiteral("timestamp"), ts},
                {QStringLiteral("sources"), *sources},
                {QStringLiteral("dossier"), dossier},
                {QStringLiteral("report"), report}
            };
            saveResearchReport(summary, report, full);
            setResearchState(false, 100, QStringLiteral("Reporte guardado."));
        });
    };

    *fetchNext = [=](int index) {
        const int wanted = qMin(maxPages, hits->size());
        if (index >= wanted) {
            if (sources->isEmpty()) {
                fail(QStringLiteral("No se pudieron descargar fuentes útiles."));
                return;
            }
            (*synthesize)();
            return;
        }

        const ResearchHit h = hits->at(index);
        setResearchState(true, 35 + (index * 45 / qMax(1, wanted)),
                         QStringLiteral("Leyendo fuente %1/%2...").arg(index + 1).arg(wanted));
        QNetworkRequest req(QUrl(h.url));
        req.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("Mozilla/5.0 LlamaCode/0.1"));
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setTransferTimeout(25000);
        m_researchReply = m_nam->get(req);
        connect(m_researchReply, &QNetworkReply::finished, this, [=]() {
            QNetworkReply *reply = m_researchReply;
            m_researchReply = nullptr;
            if (!reply) return;
            const QByteArray raw = reply->readAll();
            const bool ok = reply->error() == QNetworkReply::NoError;
            reply->deleteLater();
            if (!m_researchRunning) return;

            QString text;
            if (ok && !raw.isEmpty())
                text = researchCleanHtmlToText(QString::fromUtf8(raw)).left(4200);
            if (!text.trimmed().isEmpty()) {
                sources->append(QJsonObject{
                    {QStringLiteral("title"), h.title.isEmpty() ? h.url : h.title},
                    {QStringLiteral("url"), h.url},
                    {QStringLiteral("snippet"), h.snippet}
                });
                sourceTexts->append(QStringLiteral("### [%1] %2\n%3")
                                        .arg(sources->size())
                                        .arg(h.title.isEmpty() ? h.url : h.title,
                                             text));
            }
            (*fetchNext)(index + 1);
        });
    };

    *searchNext = [=](int index) {
        if (index >= queries.size() || hits->size() >= maxPages) {
            if (hits->isEmpty()) {
                fail(QStringLiteral("No se encontraron resultados para la investigación."));
                return;
            }
            (*fetchNext)(0);
            return;
        }

        const QString query = queries.at(index);
        setResearchState(true, 5 + (index * 25 / qMax(1, queries.size())),
                         QStringLiteral("Buscando: %1").arg(query.left(80)));

        const QString searxng = qEnvironmentVariable("LLAMACODE_SEARXNG_URL").trimmed();
        QUrl url;
        if (!searxng.isEmpty()) {
            url = QUrl(searxng.endsWith(QLatin1Char('/'))
                           ? searxng + QStringLiteral("search")
                           : searxng + QStringLiteral("/search"));
            QUrlQuery q;
            q.addQueryItem(QStringLiteral("q"), query);
            q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
            url.setQuery(q);
        } else {
            url = QUrl(QStringLiteral("https://html.duckduckgo.com/html/"));
            QUrlQuery q;
            q.addQueryItem(QStringLiteral("q"), query);
            url.setQuery(q);
        }

        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("Mozilla/5.0 LlamaCode/0.1"));
        req.setTransferTimeout(25000);
        m_researchReply = m_nam->get(req);
        connect(m_researchReply, &QNetworkReply::finished, this, [=]() {
            QNetworkReply *reply = m_researchReply;
            m_researchReply = nullptr;
            if (!reply) return;
            const QByteArray raw = reply->readAll();
            const bool ok = reply->error() == QNetworkReply::NoError;
            reply->deleteLater();
            if (!m_researchRunning) return;

            int addedBefore = hits->size();
            if (ok && !raw.isEmpty()) {
                if (!searxng.isEmpty()) {
                    const QJsonArray arr = QJsonDocument::fromJson(raw).object()
                                               .value(QStringLiteral("results")).toArray();
                    for (const QJsonValue &v : arr) {
                        const QJsonObject o = v.toObject();
                        addHit({o.value(QStringLiteral("title")).toString(),
                                o.value(QStringLiteral("url")).toString(),
                                o.value(QStringLiteral("content")).toString()});
                        if (hits->size() >= maxPages) break;
                    }
                } else {
                    const QVector<ResearchHit> parsed = researchParseDdg(QString::fromUtf8(raw), 6);
                    for (const ResearchHit &h : parsed) {
                        addHit(h);
                        if (hits->size() >= maxPages) break;
                    }
                }
            }
            searchLogs->append(QStringLiteral("- \"%1\" -> %2 new result(s)")
                                   .arg(query).arg(hits->size() - addedBefore));
            (*searchNext)(index + 1);
        });
    };

    (*searchNext)(0);
}

void AppController::cancelResearch()
{
    if (m_researchReply) {
        QNetworkReply *reply = m_researchReply;
        m_researchReply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    if (m_researchRunning)
        setResearchState(false, 0, QStringLiteral("Investigación cancelada."));
}

QString AppController::readResearchReport(const QString &id) const
{
    if (id.trimmed().isEmpty()) return QString();
    QFile f(researchStorageDir() + QLatin1Char('/') + id + QStringLiteral(".md"));
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(f.readAll());
}

void AppController::openResearchReport(const QString &id)
{
    if (id.trimmed().isEmpty()) return;
    const QString path = researchStorageDir() + QLatin1Char('/') + id + QStringLiteral(".md");
    if (!QFileInfo::exists(path)) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void AppController::deleteResearchReport(const QString &id)
{
    if (id.trimmed().isEmpty()) return;
    const QString dir = researchStorageDir();
    QFile::remove(dir + QLatin1Char('/') + id + QStringLiteral(".md"));
    QFile::remove(dir + QLatin1Char('/') + id + QStringLiteral(".json"));

    const QString idxPath = dir + QStringLiteral("/index.json");
    QFile f(idxPath);
    QJsonArray kept;
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
        f.close();
        for (const QJsonValue &v : arr)
            if (v.toObject().value(QStringLiteral("id")).toString() != id)
                kept.append(v);
    }
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(kept).toJson(QJsonDocument::Indented));
    refreshResearchReports();
}
