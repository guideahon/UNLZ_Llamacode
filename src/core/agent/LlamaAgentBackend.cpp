#include "LlamaAgentBackend.h"
#include "AgentToolRunner.h"

#include <QDateTime>
#include <QThread>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QPointer>
#include <QRegularExpression>

static int estimateTokens(const QString &text)
{
    const int n = text.trimmed().size();
    if (n <= 0) return 0;
    return (n + 3) / 4;
}

// Quita bloques <think>...</think> antes de mandar el historial al modelo: el
// razonamiento es solo para mostrar; reenviarlo confunde el tool-calling.
static QString stripThinkForContext(const QString &s)
{
    QString out = s;
    out.remove(QRegularExpression(QStringLiteral("<think>[\\s\\S]*?</think>"),
                                  QRegularExpression::CaseInsensitiveOption));
    out.remove(QRegularExpression(QStringLiteral("</?think>"),
                                  QRegularExpression::CaseInsensitiveOption));
    return out.trimmed();
}

static const QString kMcpPrefix = QStringLiteral("mcp__");

static QString toolArgumentsToString(const QJsonValue &v)
{
    if (v.isString()) return v.toString();
    if (v.isObject()) return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    if (v.isArray())  return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    return {};
}

LlamaAgentBackend::LlamaAgentBackend(QObject *parent) : IAgentBackend(parent)
{
    m_nam = new QNetworkAccessManager(this);
}

LlamaAgentBackend::~LlamaAgentBackend() { stop(); teardownWorker(); }

// ───────────────────────────── Ciclo de vida ─────────────────────────────
void LlamaAgentBackend::start(const AgentContext &ctx)
{
    m_ctx = ctx;
    m_cwd = (!ctx.cwd.isEmpty() && QFileInfo(ctx.cwd).isDir())
                ? ctx.cwd : QDir::homePath();
    m_running = true;
    loadFromDisk();         // recupera sesiones previas; activa la primera
    ensureSession();        // si no había ninguna, crea una
    fetchContextLimit();
    ensureWorker();         // hilo worker (persiste toda la vida del backend)
    // (Re)configurar el worker en cada start (async, no bloquea UI).
    QMetaObject::invokeMethod(m_worker, "setConfined", Qt::QueuedConnection,
                              Q_ARG(bool, m_approvalMode != QLatin1String("super")));
    QMetaObject::invokeMethod(m_worker, "initServers", Qt::QueuedConnection,
                              Q_ARG(QVariantList, m_mcpConfig), Q_ARG(QString, m_cwd));
    emit runningChanged();
    emit logAppended(QStringLiteral("[LlamaAgent backend listo · cwd: %1]\n")
                         .arg(QDir::toNativeSeparators(m_cwd)));
}

void LlamaAgentBackend::fetchContextLimit()
{
    auto *reply = m_nam->get(QNetworkRequest(QUrl(m_ctx.serverBaseUrl + QStringLiteral("/props"))));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        int nctx = root.value(QStringLiteral("default_generation_settings"))
                       .toObject().value(QStringLiteral("n_ctx")).toInt(-1);
        if (nctx < 0) nctx = root.value(QStringLiteral("n_ctx")).toInt(-1);
        if (nctx > 0) { m_ctxLimit = nctx; emit contextUsage(0, m_ctxLimit); }
    });
}

// Tokens estimados de un único mensaje de la API (content + args de tool_calls).
static int msgTokensOf(const QJsonObject &m)
{
    int t = (m.value(QStringLiteral("content")).toString().size() + 3) / 4;
    const QJsonArray tcs = m.value(QStringLiteral("tool_calls")).toArray();
    for (const QJsonValue &v : tcs) {
        const QJsonObject fn = v.toObject().value(QStringLiteral("function")).toObject();
        t += (fn.value(QStringLiteral("name")).toString().size()
              + fn.value(QStringLiteral("arguments")).toString().size() + 3) / 4;
    }
    return t + 4;   // overhead por mensaje (roles, separadores del template)
}

int LlamaAgentBackend::estimateApiTokens() const
{
    int total = 0;
    for (const QJsonValue &v : m_apiMessages)
        total += msgTokensOf(v.toObject());
    return total;
}

// Serializa un mensaje de la API a texto legible para el resumen.
static QString serializeMsgForSummary(const QJsonObject &m)
{
    const QString role = m.value(QStringLiteral("role")).toString();
    QString out;
    const QString content = m.value(QStringLiteral("content")).toString();
    if (!content.isEmpty())
        out += role + QStringLiteral(": ") + content.left(8000) + QLatin1Char('\n');
    const QJsonArray tcs = m.value(QStringLiteral("tool_calls")).toArray();
    for (const QJsonValue &v : tcs) {
        const QJsonObject fn = v.toObject().value(QStringLiteral("function")).toObject();
        out += QStringLiteral("  → tool %1(%2)\n")
                   .arg(fn.value(QStringLiteral("name")).toString(),
                        fn.value(QStringLiteral("arguments")).toString().left(2000));
    }
    return out;
}

// Decide si hay un tramo intermedio a compactar y devuelve [head, keepFrom).
bool LlamaAgentBackend::planCompaction(int &head, int &keepFrom) const
{
    if (m_ctxLimit <= 0) return false;
    const int n = m_apiMessages.size();
    if (n <= 4) return false;

    const int outReserve   = qMin(32768, m_ctxLimit / 4);
    const int toolsReserve = (buildToolSchemas().size() ? 1500 : 0);
    const int budget = int(m_ctxLimit * 0.90) - outReserve - toolsReserve;
    if (budget <= 0) return false;

    if (estimateApiTokens() <= budget) return false;

    head = qMin(2, n);                           // system[0] + objetivo[1]
    const int tailBudget = int(budget * 0.6);    // dejar margen de crecimiento
    int acc = 0; keepFrom = n;
    for (int i = n - 1; i >= head; --i) {
        acc += msgTokensOf(m_apiMessages[i].toObject());
        if (acc > tailBudget) { keepFrom = i + 1; break; }
        keepFrom = i;
    }
    // No empezar la cola con un 'tool' huérfano (debe seguir a su assistant.tool_calls).
    while (keepFrom < n
           && m_apiMessages[keepFrom].toObject().value(QStringLiteral("role")).toString()
              == QLatin1String("tool"))
        ++keepFrom;

    return (keepFrom - head) > 0;
}

// Reemplaza m_apiMessages[head..keepFrom) por un único mensaje de resumen.
// Si summary está vacío (falló el modelo), usa una nota de poda como fallback.
void LlamaAgentBackend::applyCompaction(int head, int keepFrom, const QString &summary)
{
    const int n = m_apiMessages.size();
    if (head < 0 || keepFrom > n || keepFrom <= head) return;
    const int dropped = keepFrom - head;
    const int before = estimateApiTokens();

    QString body = summary.trimmed();
    const bool summarized = !body.isEmpty();
    if (!summarized)
        body = QStringLiteral("[Se omitieron %1 mensajes intermedios para no exceder el "
                              "contexto (resumen no disponible).]").arg(dropped);

    QJsonArray neu;
    for (int i = 0; i < head; ++i) neu.append(m_apiMessages[i]);
    neu.append(QJsonObject{
        {QStringLiteral("role"), QStringLiteral("system")},
        {QStringLiteral("content"),
         QStringLiteral("[Resumen del contexto previo (%1 mensajes compactados para no "
                        "exceder n_ctx=%2)]:\n%3").arg(dropped).arg(m_ctxLimit, 0, 10).arg(body)}});
    for (int i = keepFrom; i < n; ++i) neu.append(m_apiMessages[i]);

    m_apiMessages = neu;
    const int after = estimateApiTokens();
    emit logAppended(QStringLiteral("[compactación %1: %2 msgs · ~%3→%4 tok (n_ctx=%5)]\n")
                         .arg(summarized ? QStringLiteral("vía modelo") : QStringLiteral("poda"))
                         .arg(dropped).arg(before).arg(after).arg(m_ctxLimit));
    emit contextUsage(after, m_ctxLimit);
}

// Dispara el request de resumen del tramo [head, keepFrom). Al completar,
// aplica la compactación y reanuda el turno (runCompletion).
void LlamaAgentBackend::startCompaction(int head, int keepFrom)
{
    QString convo;
    for (int i = head; i < keepFrom; ++i)
        convo += serializeMsgForSummary(m_apiMessages[i].toObject());

    const QString sys = QStringLiteral(
        "Sos un compactador de contexto. Resumí de forma concisa pero completa el "
        "siguiente tramo de conversación entre un usuario y un agente de coding. "
        "Preservá TODO lo accionable: objetivo, decisiones tomadas, archivos "
        "creados/editados con sus rutas, comandos relevantes y su resultado, errores "
        "y estado actual de la tarea. No inventes. Respondé solo con el resumen.");

    QJsonObject payload{
        {QStringLiteral("model"), m_ctx.modelId.isEmpty() ? QStringLiteral("local") : m_ctx.modelId},
        {QStringLiteral("messages"), QJsonArray{
            QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), sys}},
            QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), convo}}}},
        {QStringLiteral("stream"), false},
        {QStringLiteral("temperature"), 0.2},
        {QStringLiteral("max_tokens"), qMin(2048, m_ctxLimit > 0 ? m_ctxLimit / 8 : 2048)}
    };

    const QString url = m_ctx.serverBaseUrl + QStringLiteral("/v1/chat/completions");
    QNetworkRequest req((QUrl(url)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));

    m_compacting = true;
    emit logAppended(QStringLiteral("[compactando contexto vía modelo: resumiendo %1 mensajes…]\n")
                         .arg(keepFrom - head));

    m_compactReply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(m_compactReply, &QNetworkReply::finished, this, [this, head, keepFrom]() {
        QNetworkReply *r = m_compactReply;
        if (!r) return;                          // abortado por cancel/stop
        m_compactReply = nullptr;
        m_compacting = false;
        r->deleteLater();

        QString summary;
        if (r->error() == QNetworkReply::NoError) {
            const QJsonObject root = QJsonDocument::fromJson(r->readAll()).object();
            const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
            if (!choices.isEmpty())
                summary = choices.first().toObject()
                              .value(QStringLiteral("message")).toObject()
                              .value(QStringLiteral("content")).toString();
            summary = stripThinkForContext(summary);
        }
        // summary vacío → applyCompaction usa fallback de poda.
        applyCompaction(head, keepFrom, summary);

        if (m_running) runCompletion();          // reanudar el turno ya compactado
    });
}

void LlamaAgentBackend::stop()
{
    if (m_compactReply) {
        QNetworkReply *cr = m_compactReply; m_compactReply = nullptr;
        cr->disconnect(this); cr->abort(); cr->deleteLater();
    }
    m_compacting = false;
    if (m_reply) { m_reply->abort(); m_reply->deleteLater(); m_reply = nullptr; }
    m_pendingCalls = {};
    m_awaitId.clear();
    saveCurrentSession();
    persistIndex();
    // Apagar servers MCP pero mantener vivo el hilo worker (se destruye en ~).
    if (m_worker) QMetaObject::invokeMethod(m_worker, "shutdown", Qt::QueuedConnection);
    if (m_running) { m_running = false; emit runningChanged(); }
}

void LlamaAgentBackend::cancelGeneration()
{
    if (m_compactReply) {
        QNetworkReply *cr = m_compactReply;
        m_compactReply = nullptr;
        cr->disconnect(this);
        cr->abort();
        cr->deleteLater();
    }
    m_compacting = false;
    if (m_reply) {
        // abort() emite finished()/readyRead SINCRÓNICAMENTE. Si dejamos m_reply
        // seteado y las conexiones vivas, los handlers de stream re-entran acá
        // (use-after / doble proceso) → crash. Anular y desconectar ANTES de abort.
        QNetworkReply *r = m_reply;
        m_reply = nullptr;
        r->disconnect(this);
        r->abort();
        r->deleteLater();
    }
    m_pendingCalls = {};
    m_awaitId.clear();
    setTyping(false);
    m_curAsstIdx = -1;
    m_execCallId.clear();   // ignorar resultado de tool tardío
}

void LlamaAgentBackend::ensureSession()
{
    if (!m_sessionId.isEmpty()) return;
    m_sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_sessionTitle = QStringLiteral("Sesión");
    AgentSession s;
    s.id = m_sessionId;
    s.title = m_sessionTitle;
    s.created = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
    s.projectDir = m_cwd;
    s.projectName = QFileInfo(m_cwd).fileName();
    m_sessions.prepend(s.toMap());

    m_apiMessages = QJsonArray{ QJsonObject{
        {QStringLiteral("role"), QStringLiteral("system")},
        {QStringLiteral("content"), buildSystemPrompt()}
    } };
    m_messages.clear();
    persistIndex();
    persistSession(m_sessionId);
    emit sessionsChanged();
    emit messagesChanged();
}

QString LlamaAgentBackend::memoryFilePath(const QString &cwd)
{
    return QDir::cleanPath(cwd + QStringLiteral("/.llamacode/memory.md"));
}

QString LlamaAgentBackend::buildSystemPrompt() const
{
#ifdef Q_OS_WIN
    const QString os = QStringLiteral("Windows");
    const QString shell = QStringLiteral(
        "El shell de run_shell es cmd.exe de Windows: usá sintaxis de Windows "
        "(mkdir sin -p, dir, copy, type, del, '&&' para encadenar). NO uses "
        "sintaxis de Unix/bash (mkdir -p, ls, cat, rm). NO asumas Linux ni WSL.");
#else
    const QString os = QStringLiteral("Linux/Unix");
    const QString shell = QStringLiteral("El shell de run_shell es sh (sintaxis POSIX).");
#endif
    const bool super = (m_approvalMode == QLatin1String("super"));
    const QString scope = super
        ? QStringLiteral("Modo SUPER AGENTE: tenés acceso a TODO el disco. Podés "
              "leer/escribir cualquier carpeta usando rutas absolutas (ej. %1) o "
              "relativas al cwd. No se piden permisos.")
              .arg(QDir::toNativeSeparators(QDir::homePath()))
        : QStringLiteral("Las rutas de las tools son relativas al cwd y están "
              "CONFINADAS a él: no podés leer/escribir fuera del proyecto. Si el "
              "usuario pide una ruta absoluta fuera del cwd, trabajá dentro del "
              "proyecto y avisale.");
    QString base = QStringLiteral(
        "Sos un agente de coding. Sistema operativo: %1. Directorio de trabajo "
        "(cwd): %2. Tenés herramientas para leer/escribir archivos, listar, buscar y "
        "ejecutar comandos de shell. Usá las tools cuando necesites información real; "
        "no inventes contenido de archivos. %3 %4 Respondé en el idioma del usuario.")
        .arg(os, QDir::toNativeSeparators(m_cwd), scope, shell);

    // Memoria por proyecto: .llamacode/memory.md o AGENTS.md (lo que exista).
    QString mem;
    for (const QString &cand : {memoryFilePath(m_cwd),
                                QDir::cleanPath(m_cwd + QStringLiteral("/AGENTS.md"))}) {
        QFile f(cand);
        if (f.open(QIODevice::ReadOnly)) {
            mem = QString::fromUtf8(f.read(64 * 1024)).trimmed();
            if (!mem.isEmpty()) break;
        }
    }
    if (!mem.isEmpty())
        base += QStringLiteral("\n\n--- Memoria del proyecto ---\n") + mem;
    if (!m_systemExtra.trimmed().isEmpty())
        base += QStringLiteral("\n\n--- Instrucciones del agente ---\n") + m_systemExtra.trimmed();
    return base;
}

void LlamaAgentBackend::setAgentTuning(const QString &systemExtra, double temperature)
{
    m_systemExtra = systemExtra;
    m_temperature = temperature;
    // Si ya hay sesión activa, refrescar el system prompt (índice 0).
    if (!m_apiMessages.isEmpty()) {
        QJsonObject sys = m_apiMessages.first().toObject();
        if (sys.value(QStringLiteral("role")).toString() == QLatin1String("system")) {
            sys[QStringLiteral("content")] = buildSystemPrompt();
            m_apiMessages.replace(0, sys);
        }
    }
}

void LlamaAgentBackend::setThinkingEnabled(bool enabled)
{
    m_thinkingEnabled = enabled;
}

void LlamaAgentBackend::setApprovalPolicy(const QString &mode)
{
    m_approvalMode = mode;
    // "super" = sin confinamiento al cwd (acceso a todo el disco).
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "setConfined", Qt::QueuedConnection,
                                  Q_ARG(bool, mode != QLatin1String("super")));
}

// ───────────────────────────── Conversación ──────────────────────────────
void LlamaAgentBackend::sendMessage(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (!m_running || trimmed.isEmpty()) return;
    if (m_reply || !m_awaitId.isEmpty()) {
        emit errorOccurred(QStringLiteral("Hay un turno en curso."));
        return;
    }
    ensureSession();

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    m_messages.append(QVariantMap{
        {QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"), trimmed},
        {QStringLiteral("typing"), false},
        {QStringLiteral("createdAt"), static_cast<double>(nowMs)},
        {QStringLiteral("completedAt"), static_cast<double>(nowMs)},
        {QStringLiteral("elapsedMs"), 0},
        {QStringLiteral("tokens"), estimateTokens(trimmed)},
        {QStringLiteral("tps"), 0.0}});
    m_apiMessages.append(QJsonObject{
        {QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"), trimmed}});

    m_messages.append(QVariantMap{
        {QStringLiteral("role"), QStringLiteral("assistant")},
        {QStringLiteral("content"), QString()},
        {QStringLiteral("typing"), true},
        {QStringLiteral("createdAt"), static_cast<double>(nowMs)},
        {QStringLiteral("elapsedMs"), 0},
        {QStringLiteral("tokens"), 0},
        {QStringLiteral("tps"), 0.0}});
    m_curAsstIdx = m_messages.size() - 1;
    emit messagesChanged();

    m_turnIters = 0;
    m_callCounts.clear();
    runCompletion();
}

void LlamaAgentBackend::runCompletion()
{
    if (!m_running) return;
    if (m_compacting) return;                    // esperando el resumen; se reanuda al terminar

    // Auto-compactación vía modelo ANTES de contar la iteración: si hay tramo a
    // compactar, dispara el resumen async y reanuda runCompletion() al terminar.
    {
        int head = 0, keepFrom = 0;
        if (planCompaction(head, keepFrom)) { startCompaction(head, keepFrom); return; }
    }

    if (++m_turnIters > kMaxTurnIters) {
        finishTurn(QStringLiteral("[corté el turno: se alcanzó el límite de %1 iteraciones de tools]")
                       .arg(kMaxTurnIters));
        return;
    }

    // Reflejar uso de contexto estimado en la UI (se actualiza con 'usage' real
    // del server si llega en el chunk final).
    if (m_ctxLimit > 0) emit contextUsage(estimateApiTokens(), m_ctxLimit);

    // Reserva de salida acotada al ctx del perfil (evita pedir más de lo que entra).
    const int outReserve = (m_ctxLimit > 0) ? qMin(32768, m_ctxLimit / 4) : 32768;

    const QString url = m_ctx.serverBaseUrl + QStringLiteral("/v1/chat/completions");
    QNetworkRequest req((QUrl(url)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));

    QJsonObject payload{
        {QStringLiteral("model"), m_ctx.modelId.isEmpty() ? QStringLiteral("local") : m_ctx.modelId},
        {QStringLiteral("messages"), m_apiMessages},
        {QStringLiteral("tools"), buildToolSchemas()},
        {QStringLiteral("tool_choice"), QStringLiteral("auto")},
        {QStringLiteral("parallel_tool_calls"), false},
        // Parsear tool calls del lado server puede devolver 500 si el modelo
        // emite argumentos JSON parciales durante el streaming. Lo manejamos
        // del lado cliente para tolerar errores y permitir reintentos.
        {QStringLiteral("parse_tool_calls"), false},
        // Cap de salida alto: el perfil server puede traer --predict 4096, que
        // trunca un write_file con archivo grande a mitad del JSON de args →
        // tool_call inválido → reintentos. max_tokens en el request pisa al
        // default del server y deja completar el contenido del archivo.
        {QStringLiteral("max_tokens"), outReserve},
        {QStringLiteral("stream"), true}
    };
    if (m_temperature >= 0.0) payload.insert(QStringLiteral("temperature"), m_temperature);
    // Razonamiento: SOLO mandar params si está activado. Si está off, no enviar
    // nada → el server respeta su propio --reasoning (igual que opencode, que con
    // estas settings es estable). Forzar reasoning_budget acá disparaba el path de
    // razonamiento del fork y desestabilizaba al server.
    if (m_thinkingEnabled) {
        payload.insert(QStringLiteral("reasoning_budget"), -1);
        payload.insert(QStringLiteral("chat_template_kwargs"),
                       QJsonObject{{QStringLiteral("enable_thinking"), true}});
    }

    emit logAppended(QStringLiteral("[turn] requesting completion (iter=%1, msgs=%2, stream)\n")
                         .arg(m_turnIters).arg(m_apiMessages.size()));

    resetStreamState();
    m_reply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::readyRead, this, [this]() { handleStreamData(); });
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *r = m_reply;
        if (!r) return;
        const bool ok = r->error() == QNetworkReply::NoError;
        const QString err = r->errorString();
        m_reply = nullptr;
        r->deleteLater();
        handleStreamFinished(ok, err);
    });
}

void LlamaAgentBackend::resetStreamState()
{
    m_sseBuf.clear();
    m_streamContent.clear();
    m_streamReason.clear();
    m_streamToolCalls.clear();
    // Base = lo que el bubble ya muestra (texto previo + marcadores 🔧 de tools).
    m_streamBase.clear();
    if (m_curAsstIdx >= 0 && m_curAsstIdx < m_messages.size())
        m_streamBase = m_messages[m_curAsstIdx].toMap().value(QStringLiteral("content")).toString();
}

// Une los deltas incrementales de tool_calls (patrón OpenAI streaming): por
// cada 'index' se acumulan id, function.name y function.arguments (string).
void LlamaAgentBackend::handleStreamData()
{
    if (!m_reply) return;
    m_sseBuf.append(m_reply->readAll());
    while (true) {
        const int nl = m_sseBuf.indexOf('\n');
        if (nl < 0) break;
        const QByteArray line = m_sseBuf.left(nl).trimmed();
        m_sseBuf.remove(0, nl + 1);
        if (!line.startsWith("data: ")) continue;
        const QByteArray data = line.mid(6).trimmed();
        if (data == "[DONE]") continue;
        const QJsonDocument d = QJsonDocument::fromJson(data);
        if (!d.isObject()) continue;
        const QJsonObject obj = d.object();
        // Algunos servers mandan 'usage' en el chunk final (stream_options).
        const QJsonObject usage = obj.value(QStringLiteral("usage")).toObject();
        const int used = usage.value(QStringLiteral("total_tokens")).toInt(-1);
        if (used >= 0) emit contextUsage(used, m_ctxLimit);

        const QJsonArray choices = obj.value(QStringLiteral("choices")).toArray();
        if (choices.isEmpty()) continue;
        const QJsonObject delta = choices.first().toObject().value(QStringLiteral("delta")).toObject();
        m_streamReason  += delta.value(QStringLiteral("reasoning_content")).toString();
        m_streamContent += delta.value(QStringLiteral("content")).toString();

        const QJsonArray tcs = delta.value(QStringLiteral("tool_calls")).toArray();
        for (const QJsonValue &tv : tcs) {
            const QJsonObject tc = tv.toObject();
            const int idx = tc.value(QStringLiteral("index")).toInt(0);
            QJsonObject acc = m_streamToolCalls.value(idx);
            if (tc.contains(QStringLiteral("id")))
                acc[QStringLiteral("id")] = tc.value(QStringLiteral("id")).toString();
            const QJsonObject fn = tc.value(QStringLiteral("function")).toObject();
            if (fn.contains(QStringLiteral("name")))
                acc[QStringLiteral("name")] = fn.value(QStringLiteral("name")).toString();
            if (fn.contains(QStringLiteral("arguments")))
                acc[QStringLiteral("arguments")] =
                    acc.value(QStringLiteral("arguments")).toString()
                    + fn.value(QStringLiteral("arguments")).toString();
            m_streamToolCalls.insert(idx, acc);
        }

        // Abrir una burbuja nueva si la anterior se cerró tras una tool.
        if ((!m_streamContent.isEmpty() || !m_streamReason.isEmpty()) && m_curAsstIdx < 0)
            ensureAssistantBubble();

        // Mostrar en vivo: base + <think>razonamiento</think> + respuesta.
        QString full = m_streamBase;
        if (!m_streamReason.isEmpty())
            full += QStringLiteral("<think>") + m_streamReason + QStringLiteral("</think>\n");
        full += m_streamContent;
        if (m_curAsstIdx >= 0 && m_curAsstIdx < m_messages.size()) {
            QVariantMap m = m_messages[m_curAsstIdx].toMap();
            m[QStringLiteral("content")] = full;
            m[QStringLiteral("tokens")] = estimateTokens(full);
            m_messages[m_curAsstIdx] = m;
            emit messagesChanged();
        }
    }
}

void LlamaAgentBackend::handleStreamFinished(bool ok, const QString &err)
{
    if (!ok) { finishTurn(QStringLiteral("[error: %1]").arg(err)); return; }

    // Reensamblar tool_calls ordenados por index.
    QJsonArray toolCalls;
    QList<int> idxs = m_streamToolCalls.keys();
    std::sort(idxs.begin(), idxs.end());
    for (int i : idxs) {
        const QJsonObject acc = m_streamToolCalls.value(i);
        QString argStr = acc.value(QStringLiteral("arguments")).toString();

        // CRÍTICO: si el modelo truncó los argumentos (p.ej. write_file con
        // contenido enorme que pega contra n_predict), el string queda como JSON
        // inválido/no terminado. Persistir ESE string gigante en m_apiMessages
        // envenena la sesión: el siguiente request lo reenvía y el server al
        // renderizar el tool_call por jinja crashea (0xC0000409). Sanitizamos:
        // si no parsea como objeto JSON, guardamos "{}" en el historial. El
        // tool fallará abajo con "argumentos inválidos" y el modelo reintenta,
        // pero el historial queda limpio y no tumba al server.
        QJsonParseError perr;
        QJsonDocument::fromJson(argStr.toUtf8(), &perr);
        if (perr.error != QJsonParseError::NoError && !argStr.trimmed().isEmpty()) {
            emit logAppended(QStringLiteral(
                "[turn] tool_call con args JSON inválidos (%1, %2 chars) → saneado a {} en historial\n")
                .arg(perr.errorString()).arg(argStr.size()));
            argStr = QStringLiteral("{}");
        }

        toolCalls.append(QJsonObject{
            {QStringLiteral("id"), acc.value(QStringLiteral("id")).toString()},
            {QStringLiteral("type"), QStringLiteral("function")},
            {QStringLiteral("function"), QJsonObject{
                {QStringLiteral("name"), acc.value(QStringLiteral("name")).toString()},
                {QStringLiteral("arguments"), argStr}
            }}
        });
    }

    // El historial de API NO lleva <think> (solo display lo lleva).
    const QString apiContent = stripThinkForContext(m_streamContent);

    if (toolCalls.isEmpty()) {
        if (!apiContent.isEmpty())
            m_apiMessages.append(QJsonObject{
                {QStringLiteral("role"), QStringLiteral("assistant")},
                {QStringLiteral("content"), apiContent}});
        finishTurn(QString());   // bubble ya tiene el texto; solo finalizar.
        return;
    }

    emit logAppended(QStringLiteral("[turn] model requested %1 tool call(s)\n").arg(toolCalls.size()));
    // Cerrar la burbuja de texto previa: las tools van como tarjetas aparte y el
    // próximo texto del modelo abrirá una burbuja nueva.
    closeAssistantBubble();
    m_apiMessages.append(QJsonObject{
        {QStringLiteral("role"), QStringLiteral("assistant")},
        {QStringLiteral("content"), apiContent},
        {QStringLiteral("tool_calls"), toolCalls}});
    m_pendingCalls = toolCalls;
    processPendingCalls();
}

void LlamaAgentBackend::processPendingCalls()
{
    if (m_pendingCalls.isEmpty()) {
        // Todas las tools resueltas → re-consultar al modelo con los resultados.
        runCompletion();
        return;
    }

    const QJsonObject call = m_pendingCalls.first().toObject();
    const QJsonObject fn   = call.value(QStringLiteral("function")).toObject();
    const QString name     = fn.value(QStringLiteral("name")).toString();
    const QString kind     = toolKind(name);
    const QString id       = call.value(QStringLiteral("id")).toString();
    const QString argStr   = toolArgumentsToString(fn.value(QStringLiteral("arguments")));

    // ── Robustez: anti-loop ──────────────────────────────────────────────
    const QString sig = name + QLatin1Char('|') + argStr;
    if (++m_callCounts[sig] > kMaxSameCall) {
        finishTurn(QStringLiteral("[corté: el modelo repitió la misma tool (%1) %2 veces]")
                       .arg(name).arg(kMaxSameCall));
        return;
    }

    // ── Robustez: tool desconocida ───────────────────────────────────────
    static const QStringList known{
        QStringLiteral("read_file"), QStringLiteral("list_dir"), QStringLiteral("grep"),
        QStringLiteral("write_file"), QStringLiteral("run_shell")};
    if (!known.contains(name) && !name.startsWith(kMcpPrefix)) {
        ++m_toolFail;
        m_pendingCalls.removeFirst();
        appendToolResult(id, name, QStringLiteral("[error: tool desconocida '%1']").arg(name));
        processPendingCalls();
        return;
    }

    // ── Robustez: args JSON malformado ───────────────────────────────────
    QJsonParseError perr;
    const QJsonObject args = QJsonDocument::fromJson(argStr.toUtf8(), &perr).object();
    if (perr.error != QJsonParseError::NoError && !argStr.trimmed().isEmpty()) {
        ++m_toolFail;
        m_pendingCalls.removeFirst();
        appendToolResult(id, name, QStringLiteral(
            "[error: argumentos JSON inválidos (%1). Reintentá con JSON válido para %2.]")
            .arg(perr.errorString(), name));
        processPendingCalls();
        return;
    }

    // ── Robustez: validación de args requeridos ──────────────────────────
    QStringList missing;
    for (const QString &req : requiredArgs(name))
        if (!args.contains(req) || args.value(req).toString().isEmpty())
            missing << req;
    if (!missing.isEmpty()) {
        ++m_toolFail;
        m_pendingCalls.removeFirst();
        appendToolResult(id, name, QStringLiteral(
            "[error: faltan argumentos requeridos para %1: %2]")
            .arg(name, missing.join(QStringLiteral(", "))));
        processPendingCalls();
        return;
    }

    const bool autoAll  = (m_approvalMode == QLatin1String("auto")
                           || m_approvalMode == QLatin1String("super"));
    const bool autoRead = (m_approvalMode == QLatin1String("ask") && kind == QLatin1String("read"));
    const bool always   = m_alwaysAllowed.contains(kind);

    if (autoAll || autoRead || always) {
        emit logAppended(QStringLiteral("[tool] auto-approving %1\n").arg(name));
        approveAndContinue(call.value(QStringLiteral("id")).toString(), QStringLiteral("once"));
        return;
    }

    // Pedir aprobación al usuario.
    m_awaitId   = call.value(QStringLiteral("id")).toString();
    m_awaitCall = call;
    QString detail = args.value(QStringLiteral("command")).toString();
    if (detail.isEmpty()) detail = args.value(QStringLiteral("path")).toString();
    if (detail.isEmpty()) detail = args.value(QStringLiteral("pattern")).toString();
    QString diff;
    if (name == QLatin1String("write_file")) {
        const QString rel = args.value(QStringLiteral("path")).toString();
        detail = rel;
        const QString abs = QDir::cleanPath(QDir(m_cwd).absoluteFilePath(rel));
        QString oldText;
        QFile prev(abs);
        if (prev.open(QIODevice::ReadOnly)) oldText = QString::fromUtf8(prev.read(4 * 1024 * 1024));
        diff = makeDiff(oldText, args.value(QStringLiteral("content")).toString());
    }
    emit toolApprovalNeeded(QVariantMap{
        {QStringLiteral("id"),        m_awaitId},
        {QStringLiteral("sessionId"), m_sessionId},
        {QStringLiteral("tool"),      name},
        {QStringLiteral("kind"),      kind},
        {QStringLiteral("title"),     name},
        {QStringLiteral("detail"),    detail},
        {QStringLiteral("diff"),      diff}
    });
    emit logAppended(QStringLiteral("[tool] waiting approval for %1\n").arg(name));
}

void LlamaAgentBackend::approveAndContinue(const QString &id, const QString &response)
{
    if (m_pendingCalls.isEmpty()) return;
    const QJsonObject call = m_pendingCalls.first().toObject();
    if (call.value(QStringLiteral("id")).toString() != id) return;
    m_pendingCalls.removeFirst();

    const QJsonObject fn = call.value(QStringLiteral("function")).toObject();
    const QString name   = fn.value(QStringLiteral("name")).toString();
    const QString argStr = toolArgumentsToString(fn.value(QStringLiteral("arguments")));
    m_awaitId.clear();
    m_awaitCall = {};

    // Comando/ruta a mostrar en la tarjeta de la tool.
    const QJsonObject a = QJsonDocument::fromJson(argStr.toUtf8()).object();
    m_execCommand = a.value(QStringLiteral("command")).toString();
    if (m_execCommand.isEmpty()) m_execCommand = a.value(QStringLiteral("path")).toString();
    if (m_execCommand.isEmpty()) m_execCommand = a.value(QStringLiteral("pattern")).toString();

    // Rechazo: no se ejecuta nada; resume sincrónico.
    if (response == QLatin1String("reject")) {
        ++m_toolFail;
        appendToolCard(name, toolKind(name), false, m_execCommand,
                       QStringLiteral("[el usuario rechazó esta acción]"));
        m_execCommand.clear();
        appendToolResult(id, name, QStringLiteral("[el usuario rechazó esta acción]"));
        processPendingCalls();
        return;
    }

    // Ejecución en el worker (no bloquea UI). Resume en onToolExecuted().
    ensureWorker();
    m_execCallId = id;
    QMetaObject::invokeMethod(m_worker, "executeTool", Qt::QueuedConnection,
                              Q_ARG(QString, id), Q_ARG(QString, name),
                              Q_ARG(QString, argStr), Q_ARG(QString, m_cwd));
}

void LlamaAgentBackend::onToolExecuted(const QVariantMap &result)
{
    const QString callId = result.value(QStringLiteral("callId")).toString();
    if (callId.isEmpty() || callId != m_execCallId) return;   // resultado tardío/ajeno
    m_execCallId.clear();

    const QString name = result.value(QStringLiteral("name")).toString();
    const bool ok      = result.value(QStringLiteral("ok")).toBool();
    const QString res  = result.value(QStringLiteral("result")).toString();
    const bool isWrite = result.value(QStringLiteral("isWrite")).toBool();
    if (ok) ++m_toolOk; else ++m_toolFail;

    // write_file se representa con la tarjeta de diff (abajo). El resto de las
    // tools van como tarjeta independiente con comando + salida colapsables, en
    // vez de apilarse en el texto del LLM.
    if (!isWrite)
        appendToolCard(name, toolKind(name), ok, m_execCommand, res);
    m_execCommand.clear();

    // write_file: snapshot (revert) + entrada de diff en el chat.
    if (isWrite) {
        const QString abs = result.value(QStringLiteral("absPath")).toString();
        const QByteArray oldContent = QByteArray::fromBase64(
            result.value(QStringLiteral("oldContentB64")).toString().toLatin1());
        if (!m_editSnapshots.contains(abs))
            m_editSnapshots.insert(abs, EditSnapshot{result.value(QStringLiteral("existed")).toBool(),
                                                     oldContent});
        m_messages.append(QVariantMap{
            {QStringLiteral("role"),    QStringLiteral("diff")},
            {QStringLiteral("path"),    result.value(QStringLiteral("relPath")).toString()},
            {QStringLiteral("absPath"), abs},
            {QStringLiteral("diff"),    result.value(QStringLiteral("diff")).toString()},
            {QStringLiteral("typing"),  false}});
        emit messagesChanged();
    }

    appendToolResult(callId, name, res);
    processPendingCalls();
}

void LlamaAgentBackend::appendToolResult(const QString &id, const QString &name, const QString &content)
{
    Q_UNUSED(name)
    m_apiMessages.append(QJsonObject{
        {QStringLiteral("role"), QStringLiteral("tool")},
        {QStringLiteral("tool_call_id"), id},
        {QStringLiteral("content"), content}
    });
}

QStringList LlamaAgentBackend::requiredArgs(const QString &name)
{
    if (name == QLatin1String("read_file"))  return {QStringLiteral("path")};
    if (name == QLatin1String("grep"))       return {QStringLiteral("pattern")};
    if (name == QLatin1String("write_file")) return {QStringLiteral("path"), QStringLiteral("content")};
    if (name == QLatin1String("run_shell"))  return {QStringLiteral("command")};
    return {};   // list_dir: path opcional
}

void LlamaAgentBackend::approveTool(const QString &id, bool always)
{
    if (id != m_awaitId) return;
    if (always) m_alwaysAllowed.insert(toolKind(
        m_awaitCall.value(QStringLiteral("function")).toObject()
            .value(QStringLiteral("name")).toString()));
    approveAndContinue(id, QStringLiteral("once"));
}

void LlamaAgentBackend::rejectTool(const QString &id)
{
    if (id != m_awaitId) return;
    approveAndContinue(id, QStringLiteral("reject"));
}

// ───────────────────────────── Display helpers ───────────────────────────
// Crea una burbuja de asistente nueva si no hay una abierta (lazy).
void LlamaAgentBackend::ensureAssistantBubble()
{
    if (m_curAsstIdx >= 0 && m_curAsstIdx < m_messages.size()) return;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    m_messages.append(QVariantMap{
        {QStringLiteral("role"), QStringLiteral("assistant")},
        {QStringLiteral("content"), QString()},
        {QStringLiteral("typing"), true},
        {QStringLiteral("createdAt"), static_cast<double>(nowMs)},
        {QStringLiteral("elapsedMs"), 0},
        {QStringLiteral("tokens"), 0},
        {QStringLiteral("tps"), 0.0}});
    m_curAsstIdx = m_messages.size() - 1;
    emit messagesChanged();
}

// Cierra la burbuja actual: descarta si está vacía, si no la finaliza. Así el
// texto del LLM y las tarjetas de tools quedan en mensajes separados.
void LlamaAgentBackend::closeAssistantBubble()
{
    if (m_curAsstIdx >= 0 && m_curAsstIdx < m_messages.size()) {
        QVariantMap m = m_messages[m_curAsstIdx].toMap();
        if (m.value(QStringLiteral("content")).toString().trimmed().isEmpty()) {
            m_messages.removeAt(m_curAsstIdx);
        } else {
            m[QStringLiteral("typing")] = false;
            m_messages[m_curAsstIdx] = m;
        }
        emit messagesChanged();
    }
    m_curAsstIdx = -1;
}

// Tarjeta independiente para una ejecución de tool (separada del texto LLM).
void LlamaAgentBackend::appendToolCard(const QString &name, const QString &kind, bool ok,
                                       const QString &command, const QString &output)
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    m_messages.append(QVariantMap{
        {QStringLiteral("role"),    QStringLiteral("toolcall")},
        {QStringLiteral("name"),    name},
        {QStringLiteral("kind"),    kind},
        {QStringLiteral("ok"),      ok},
        {QStringLiteral("command"), command},
        {QStringLiteral("output"),  output.left(64 * 1024)},
        {QStringLiteral("typing"),  false},
        {QStringLiteral("createdAt"),   static_cast<double>(nowMs)},
        {QStringLiteral("completedAt"), static_cast<double>(nowMs)},
        {QStringLiteral("elapsedMs"), 0},
        {QStringLiteral("tokens"), 0},
        {QStringLiteral("tps"), 0.0}});
    emit messagesChanged();
}

void LlamaAgentBackend::appendAssistantText(const QString &text)
{
    if (m_curAsstIdx < 0 || m_curAsstIdx >= m_messages.size()) return;
    QVariantMap m = m_messages[m_curAsstIdx].toMap();
    const QString content = m.value(QStringLiteral("content")).toString() + text;
    m[QStringLiteral("content")] = content;
    const qint64 startedAt = static_cast<qint64>(m.value(QStringLiteral("createdAt")).toDouble());
    const qint64 elapsedMs = qMax<qint64>(0, QDateTime::currentMSecsSinceEpoch() - startedAt);
    const int toks = estimateTokens(content);
    m[QStringLiteral("tokens")] = toks;
    m[QStringLiteral("elapsedMs")] = static_cast<int>(elapsedMs);
    m[QStringLiteral("tps")] = (elapsedMs > 0 && toks > 0)
        ? (1000.0 * static_cast<double>(toks) / static_cast<double>(elapsedMs))
        : 0.0;
    m_messages[m_curAsstIdx] = m;
    emit messagesChanged();
}

void LlamaAgentBackend::setTyping(bool typing)
{
    if (m_curAsstIdx < 0 || m_curAsstIdx >= m_messages.size()) return;
    QVariantMap m = m_messages[m_curAsstIdx].toMap();
    m[QStringLiteral("typing")] = typing;
    if (!typing) {
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
    }
    m_messages[m_curAsstIdx] = m;
    emit messagesChanged();
}

void LlamaAgentBackend::finishTurn(const QString &finalText)
{
    // Si hay texto final pero la burbuja se cerró tras una tool, abrir una nueva.
    if (!finalText.isEmpty() && m_curAsstIdx < 0)
        ensureAssistantBubble();
    if (m_curAsstIdx >= 0 && m_curAsstIdx < m_messages.size()) {
        QVariantMap m = m_messages[m_curAsstIdx].toMap();
        QString cur = m.value(QStringLiteral("content")).toString();
        if (!finalText.isEmpty()) {
            if (!cur.isEmpty()) cur += QStringLiteral("\n\n");
            cur += finalText;
        }
        m[QStringLiteral("content")] = cur;
        m[QStringLiteral("typing")]  = false;
        const qint64 doneAt = QDateTime::currentMSecsSinceEpoch();
        const qint64 startedAt = static_cast<qint64>(m.value(QStringLiteral("createdAt")).toDouble());
        const qint64 elapsedMs = qMax<qint64>(0, doneAt - startedAt);
        const int toks = estimateTokens(cur);
        m[QStringLiteral("completedAt")] = static_cast<double>(doneAt);
        m[QStringLiteral("tokens")] = toks;
        m[QStringLiteral("elapsedMs")] = static_cast<int>(elapsedMs);
        m[QStringLiteral("tps")] = (elapsedMs > 0 && toks > 0)
            ? (1000.0 * static_cast<double>(toks) / static_cast<double>(elapsedMs))
            : 0.0;
        m_messages[m_curAsstIdx] = m;
        emit messagesChanged();
    }
    if (!finalText.isEmpty())
        m_apiMessages.append(QJsonObject{
            {QStringLiteral("role"), QStringLiteral("assistant")},
            {QStringLiteral("content"), finalText}});
    emit logAppended(QStringLiteral("[turn] completed (finalTextChars=%1)\n").arg(finalText.size()));
    m_curAsstIdx = -1;
    m_pendingCalls = {};
    m_awaitId.clear();
    m_awaitCall = {};
    m_execCallId.clear();

    // Salud: tasa de éxito de tools en la sesión.
    const int total = m_toolOk + m_toolFail;
    if (total > 0)
        emit logAppended(QStringLiteral("[salud: %1/%2 tools ok (%3%)]\n")
                             .arg(m_toolOk).arg(total)
                             .arg(qRound(100.0 * m_toolOk / total)));

    saveCurrentSession();   // persistir al cerrar el turno
}

// ───────────────────────────── Tools ─────────────────────────────────────
QString LlamaAgentBackend::toolKind(const QString &name)
{
    if (name.startsWith(kMcpPrefix)) return QStringLiteral("mcp");
    if (name == QLatin1String("run_shell")) return QStringLiteral("shell");
    if (name == QLatin1String("write_file")) return QStringLiteral("write");
    return QStringLiteral("read");
}

QJsonArray LlamaAgentBackend::toolSchemas()
{
    auto fn = [](const QString &name, const QString &desc, const QJsonObject &props,
                 const QJsonArray &required) {
        QJsonObject params{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), props}
        };
        if (!required.isEmpty()) {
            params.insert(QStringLiteral("required"), required);
        }
        return QJsonObject{
            {QStringLiteral("type"), QStringLiteral("function")},
            {QStringLiteral("function"), QJsonObject{
                {QStringLiteral("name"), name},
                {QStringLiteral("description"), desc},
                {QStringLiteral("parameters"), params}
            }}
        };
    };
    auto strProp = [](const QString &d) {
        return QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                           {QStringLiteral("description"), d}};
    };
    return QJsonArray{
        fn(QStringLiteral("read_file"), QStringLiteral("Lee un archivo de texto del proyecto."),
           QJsonObject{{QStringLiteral("path"), strProp(QStringLiteral("Ruta relativa al proyecto."))}},
           QJsonArray{QStringLiteral("path")}),
        fn(QStringLiteral("list_dir"), QStringLiteral("Lista archivos y carpetas de un directorio."),
           QJsonObject{{QStringLiteral("path"), strProp(QStringLiteral("Ruta relativa (vacío = raíz)."))}},
           QJsonArray{}),
        fn(QStringLiteral("grep"), QStringLiteral("Busca un patrón (texto) en los archivos del proyecto."),
           QJsonObject{
               {QStringLiteral("pattern"), strProp(QStringLiteral("Texto a buscar."))},
               {QStringLiteral("path"), strProp(QStringLiteral("Subdirectorio opcional."))}},
           QJsonArray{QStringLiteral("pattern")}),
        fn(QStringLiteral("write_file"), QStringLiteral("Escribe (crea/sobrescribe) un archivo de texto."),
           QJsonObject{
               {QStringLiteral("path"), strProp(QStringLiteral("Ruta relativa al proyecto."))},
               {QStringLiteral("content"), strProp(QStringLiteral("Contenido completo del archivo."))}},
           QJsonArray{QStringLiteral("path"), QStringLiteral("content")}),
        fn(QStringLiteral("run_shell"), QStringLiteral("Ejecuta un comando de shell en el directorio del proyecto."),
           QJsonObject{{QStringLiteral("command"), strProp(QStringLiteral("Comando a ejecutar."))}},
           QJsonArray{QStringLiteral("command")})
    };
}

// ───────────────────────────── MCP / Worker ──────────────────────────────
void LlamaAgentBackend::setMcpServers(const QVariantList &servers)
{
    m_mcpConfig = servers;
    if (m_running && m_worker)
        QMetaObject::invokeMethod(m_worker, "initServers", Qt::QueuedConnection,
                                  Q_ARG(QVariantList, m_mcpConfig), Q_ARG(QString, m_cwd));
}

void LlamaAgentBackend::ensureWorker()
{
    if (m_worker) return;
    m_workerThread = new QThread(this);
    m_worker = new AgentToolRunner;          // sin parent: se mueve al hilo
    m_worker->moveToThread(m_workerThread);
    connect(m_worker, &AgentToolRunner::logAppended, this, &LlamaAgentBackend::logAppended);
    connect(m_worker, &AgentToolRunner::serversReady, this, &LlamaAgentBackend::onServersReady);
    connect(m_worker, &AgentToolRunner::toolExecuted, this, &LlamaAgentBackend::onToolExecuted);
    m_workerThread->start();
}

// Solo al destruir el backend. Apaga MCP en el hilo worker (afinidad correcta de
// QProcess), para el hilo y borra todo de forma determinista.
void LlamaAgentBackend::teardownWorker()
{
    if (!m_workerThread) return;
    if (m_worker) {
        disconnect(m_worker, nullptr, this, nullptr);   // sin señales tardías hacia this
        QMetaObject::invokeMethod(m_worker, "shutdown", Qt::BlockingQueuedConnection);
    }
    m_workerThread->quit();
    if (!m_workerThread->wait(8000)) {                  // worker bloqueado en una tool
        m_workerThread->terminate();
        m_workerThread->wait(2000);
    }
    delete m_worker;          // hilo parado + sin QProcess hijos → seguro
    delete m_workerThread;
    m_worker = nullptr;
    m_workerThread = nullptr;
    m_mcpTools.clear();
}

void LlamaAgentBackend::onServersReady(const QVariantList &toolDefs)
{
    m_mcpTools = toolDefs;   // cache para buildToolSchemas
    emit logAppended(QStringLiteral("[mcp] %1 tool(s) descubiertas\n").arg(toolDefs.size()));
}

// Built-in + tools MCP (cache de m_mcpTools, prefijo mcp__<server>__<tool>).
QJsonArray LlamaAgentBackend::buildToolSchemas() const
{
    QJsonArray all = toolSchemas();
    for (const QVariant &v : m_mcpTools) {
        const QVariantMap t = v.toMap();
        QJsonObject params = QJsonDocument::fromJson(
            t.value(QStringLiteral("schema")).toString().toUtf8()).object();
        if (params.isEmpty())
            params = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                 {QStringLiteral("properties"), QJsonObject{}}};
        all.append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("function")},
            {QStringLiteral("function"), QJsonObject{
                {QStringLiteral("name"), kMcpPrefix + t.value(QStringLiteral("server")).toString()
                                          + QStringLiteral("__") + t.value(QStringLiteral("name")).toString()},
                {QStringLiteral("description"), t.value(QStringLiteral("description")).toString()},
                {QStringLiteral("parameters"), params}
            }}
        });
    }
    return all;
}

// Diff unificado simple: recorta prefijo/sufijo de líneas comunes y marca el
// bloque cambiado con '-' (viejo) y '+' (nuevo). No es LCS, pero alcanza para ver.
QString LlamaAgentBackend::makeDiff(const QString &oldText, const QString &newText)
{
    const QStringList o = oldText.split(QLatin1Char('\n'));
    const QStringList n = newText.split(QLatin1Char('\n'));
    int p = 0;
    while (p < o.size() && p < n.size() && o[p] == n[p]) ++p;
    int s = 0;
    while (s < (o.size() - p) && s < (n.size() - p)
           && o[o.size() - 1 - s] == n[n.size() - 1 - s]) ++s;

    QStringList out;
    const int ctx = 2;
    for (int i = qMax(0, p - ctx); i < p; ++i) out << QStringLiteral("  ") + o[i];
    for (int i = p; i < o.size() - s; ++i)     out << QStringLiteral("- ") + o[i];
    for (int i = p; i < n.size() - s; ++i)     out << QStringLiteral("+ ") + n[i];
    for (int i = o.size() - s; i < qMin(o.size(), o.size() - s + ctx); ++i)
        out << QStringLiteral("  ") + o[i];
    if (out.isEmpty()) out << QStringLiteral("  (sin cambios)");
    return out.join(QLatin1Char('\n'));
}

void LlamaAgentBackend::revertEdit(const QString &path)
{
    // path puede venir relativo (UI) o absoluto. Resolver contra cwd.
    const QString abs = QFileInfo(path).isAbsolute()
        ? QDir::cleanPath(path)
        : QDir::cleanPath(QDir(m_cwd).absoluteFilePath(path));
    if (!m_editSnapshots.contains(abs)) {
        emit errorOccurred(QStringLiteral("No hay snapshot para revertir: %1").arg(path));
        return;
    }
    const EditSnapshot snap = m_editSnapshots.value(abs);
    if (!snap.existed) {
        QFile::remove(abs);
    } else {
        QFile f(abs);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(snap.oldContent);
            f.close();
        }
    }
    m_editSnapshots.remove(abs);
    emit logAppended(QStringLiteral("[revertido: %1]\n").arg(path));

    // Marcar la entrada de diff como revertida en el chat.
    for (int i = 0; i < m_messages.size(); ++i) {
        QVariantMap m = m_messages[i].toMap();
        if (m.value(QStringLiteral("role")).toString() == QLatin1String("diff")
                && m.value(QStringLiteral("absPath")).toString() == abs
                && !m.value(QStringLiteral("reverted")).toBool()) {
            m[QStringLiteral("reverted")] = true;
            m_messages[i] = m;
        }
    }
    emit messagesChanged();
}

// ───────────────────────────── Sesiones + persistencia ───────────────────
void LlamaAgentBackend::newSession()
{
    saveCurrentSession();
    m_sessionId.clear();
    m_messages.clear();
    m_apiMessages = {};
    m_curAsstIdx = -1;
    ensureSession();        // crea sesión nueva + system prompt + persiste
}

void LlamaAgentBackend::newSessionInProject(const QString &projectDir)
{
    if (!projectDir.isEmpty() && QFileInfo(projectDir).isDir()) m_cwd = projectDir;
    newSession();
}

void LlamaAgentBackend::switchSession(const QString &sessionId)
{
    if (sessionId.isEmpty() || sessionId == m_sessionId) return;
    setCurrentSession(sessionId);
}

void LlamaAgentBackend::renameSession(const QString &sessionId, const QString &title)
{
    const QString t = title.trimmed();
    if (sessionId.isEmpty() || t.isEmpty()) return;
    if (sessionId == m_sessionId) m_sessionTitle = t;
    for (int i = 0; i < m_sessions.size(); ++i) {
        QVariantMap s = m_sessions[i].toMap();
        if (s.value(QStringLiteral("id")).toString() == sessionId) {
            s[QStringLiteral("title")] = t;
            m_sessions[i] = s;
            break;
        }
    }
    persistIndex();
    persistSession(sessionId);
    emit sessionsChanged();
}

void LlamaAgentBackend::deleteSession(const QString &sessionId)
{
    if (sessionId.isEmpty()) return;
    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions[i].toMap().value(QStringLiteral("id")).toString() == sessionId) {
            m_sessions.removeAt(i);
            break;
        }
    }
    removeSessionFile(sessionId);
    if (sessionId == m_sessionId) {
        m_sessionId.clear();
        m_messages.clear();
        m_apiMessages = {};
        m_curAsstIdx = -1;
        if (!m_sessions.isEmpty())
            setCurrentSession(m_sessions.first().toMap().value(QStringLiteral("id")).toString());
        else
            ensureSession();
    }
    persistIndex();
    emit sessionsChanged();
}

void LlamaAgentBackend::deleteProject(const QString &projectDir)
{
    if (projectDir.isEmpty()) return;
    const QString target = QDir::cleanPath(projectDir);
    bool deletedCurrent = false;
    QVariantList kept;
    for (const QVariant &v : std::as_const(m_sessions)) {
        const QVariantMap s = v.toMap();
        const QString pd = QDir::cleanPath(s.value(QStringLiteral("projectDir")).toString());
        if (pd == target) {
            const QString sid = s.value(QStringLiteral("id")).toString();
            removeSessionFile(sid);
            if (sid == m_sessionId) deletedCurrent = true;
        } else {
            kept.append(v);
        }
    }
    if (kept.size() == m_sessions.size()) return;   // nada que borrar
    m_sessions = kept;
    if (deletedCurrent) {
        m_sessionId.clear();
        m_messages.clear();
        m_apiMessages = {};
        m_curAsstIdx = -1;
        // Si quedan sesiones de OTROS proyectos, abrir una. Si no queda ninguna,
        // dejar estado vacío: NO recrear sesión (recrear en el cwd borrado haría
        // reaparecer el proyecto). Se creará una al próximo mensaje/sesión nueva.
        if (!m_sessions.isEmpty())
            setCurrentSession(m_sessions.first().toMap().value(QStringLiteral("id")).toString());
        else
            emit messagesChanged();
    }
    persistIndex();
    emit sessionsChanged();
}

void LlamaAgentBackend::refreshSessions() { emit sessionsChanged(); }

void LlamaAgentBackend::setCurrentSession(const QString &sessionId)
{
    saveCurrentSession();   // vuelca la sesión actual antes de cambiar
    m_sessionId = sessionId;
    m_curAsstIdx = -1;
    m_messages.clear();
    m_apiMessages = {};

    QFile f(sessionFilePath(sessionId));
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        m_apiMessages = obj.value(QStringLiteral("api")).toArray();
        const QJsonArray msgs = obj.value(QStringLiteral("messages")).toArray();
        for (const QJsonValue &mv : msgs) {
            QVariantMap mm = mv.toObject().toVariantMap();
            mm[QStringLiteral("typing")] = false;
            m_messages.append(mm);
        }
    }
    for (const QVariant &v : std::as_const(m_sessions)) {
        const QVariantMap s = v.toMap();
        if (s.value(QStringLiteral("id")).toString() == sessionId) {
            m_sessionTitle = s.value(QStringLiteral("title")).toString();
            const QString pd = s.value(QStringLiteral("projectDir")).toString();
            if (!pd.isEmpty()) m_cwd = pd;
            break;
        }
    }
    // Si la sesión no tenía system prompt (vacía), garantizar uno.
    if (m_apiMessages.isEmpty())
        m_apiMessages = QJsonArray{ QJsonObject{
            {QStringLiteral("role"), QStringLiteral("system")},
            {QStringLiteral("content"), buildSystemPrompt()}} };
    emit sessionsChanged();
    emit messagesChanged();
}

// ───────────────────────────── Persistencia a disco ──────────────────────
QString LlamaAgentBackend::storageDir() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/agent_llamaagent");
    QDir().mkpath(dir);
    return dir;
}

QString LlamaAgentBackend::sessionFilePath(const QString &sessionId) const
{
    return storageDir() + QStringLiteral("/") + sessionId + QStringLiteral(".json");
}

void LlamaAgentBackend::loadFromDisk()
{
    if (!m_sessions.isEmpty()) return;
    QFile f(storageDir() + QStringLiteral("/index.json"));
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    f.close();
    for (const QJsonValue &v : arr) {
        const QVariantMap s = v.toObject().toVariantMap();
        if (s.value(QStringLiteral("id")).toString().isEmpty()) continue;
        m_sessions.append(s);
    }
    if (m_sessions.isEmpty()) return;
    // Cargar la primera como activa.
    const QString sid = m_sessions.first().toMap().value(QStringLiteral("id")).toString();
    setCurrentSession(sid);
}

void LlamaAgentBackend::persistIndex() const
{
    QJsonArray arr;
    for (const QVariant &v : m_sessions)
        arr.append(QJsonObject::fromVariantMap(v.toMap()));
    QFile f(storageDir() + QStringLiteral("/index.json"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(arr).toJson());
        f.close();
    }
}

void LlamaAgentBackend::persistSession(const QString &sessionId) const
{
    if (sessionId.isEmpty() || sessionId != m_sessionId) return;  // solo la activa tiene datos en RAM
    QString title;
    for (const QVariant &v : m_sessions) {
        const QVariantMap s = v.toMap();
        if (s.value(QStringLiteral("id")).toString() == sessionId) {
            title = s.value(QStringLiteral("title")).toString();
            break;
        }
    }
    QJsonArray msgs;
    for (const QVariant &mv : m_messages) {
        QVariantMap m = mv.toMap();
        m.remove(QStringLiteral("typing"));
        msgs.append(QJsonObject::fromVariantMap(m));
    }
    QJsonObject obj{
        {QStringLiteral("id"), sessionId},
        {QStringLiteral("title"), title},
        {QStringLiteral("messages"), msgs},
        {QStringLiteral("api"), m_apiMessages}
    };
    QFile f(sessionFilePath(sessionId));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(obj).toJson());
        f.close();
    }
}

void LlamaAgentBackend::saveCurrentSession()
{
    if (m_sessionId.isEmpty()) return;
    persistSession(m_sessionId);
}

void LlamaAgentBackend::persistAll() const
{
    persistIndex();
    persistSession(m_sessionId);
}

void LlamaAgentBackend::removeSessionFile(const QString &sessionId) const
{
    if (!sessionId.isEmpty()) QFile::remove(sessionFilePath(sessionId));
}
