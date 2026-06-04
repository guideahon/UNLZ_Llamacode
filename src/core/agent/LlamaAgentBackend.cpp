#include "LlamaAgentBackend.h"
#include "AgentToolRunner.h"
#include "SubAgentRunner.h"

#include <QCryptographicHash>

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

// Finaliza las métricas de una burbuja de asistente (tiempo/tokens/tps).
// El tps mide VELOCIDAD DE GENERACIÓN: el cronómetro arranca en el primer token
// (genStartMs), no cuando se creó la burbuja. Así no se contamina con el
// prompt-processing del modelo (TTFT), que en local puede tardar minutos y
// hundía el tps. Fallback a createdAt si nunca llegó a streamear.
// srvTokens/srvGenMs: métricas REALES del server (timings.predicted_n /
// predicted_ms). Si están (>0) se usan para tokens y para el tps; el tps queda
// como velocidad de GENERACIÓN pura (excluye prompt-processing por definición).
// Sin ellas, fallback al estimado: tokens=chars/4 y tps sobre el wall desde el
// primer token (genStartMs) — que SÍ incluye stalls/TTFT residual y subestima.
static void finalizeMsgMetrics(QVariantMap &m, int srvTokens = 0, double srvGenMs = 0.0)
{
    const qint64 doneAt = QDateTime::currentMSecsSinceEpoch();
    qint64 startMs = static_cast<qint64>(m.value(QStringLiteral("genStartMs")).toDouble());
    if (startMs <= 0)
        startMs = static_cast<qint64>(m.value(QStringLiteral("createdAt")).toDouble());
    const qint64 wallMs = qMax<qint64>(0, doneAt - startMs);
    const int toks = (srvTokens > 0) ? srvTokens
                                     : estimateTokens(m.value(QStringLiteral("content")).toString());
    // Tiempo para el tps: el de generación del server si lo tenemos; si no, wall.
    const double genMs = (srvGenMs > 0.0) ? srvGenMs : static_cast<double>(wallMs);
    m[QStringLiteral("completedAt")] = static_cast<double>(doneAt);
    m[QStringLiteral("tokens")] = toks;
    m[QStringLiteral("elapsedMs")] = static_cast<int>(wallMs);   // wall honesto (incluye TTFT)
    m[QStringLiteral("tps")] = (genMs > 0.0 && toks > 0)
        ? (1000.0 * static_cast<double>(toks) / genMs)
        : 0.0;
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

// Data-URI base64 si el archivo es imagen soportada por mmproj; "" si no.
static QString imageDataUri(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    QString mime;
    if (ext == QLatin1String("png")) mime = QStringLiteral("image/png");
    else if (ext == QLatin1String("jpg") || ext == QLatin1String("jpeg")) mime = QStringLiteral("image/jpeg");
    else if (ext == QLatin1String("webp")) mime = QStringLiteral("image/webp");
    else if (ext == QLatin1String("gif")) mime = QStringLiteral("image/gif");
    else if (ext == QLatin1String("bmp")) mime = QStringLiteral("image/bmp");
    else return {};
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QStringLiteral("data:%1;base64,%2").arg(mime, QString::fromLatin1(f.readAll().toBase64()));
}

// Texto de un archivo (UTF-8), "" si binario/imagen.
static QString readAttachText(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray raw = f.read(2 * 1024 * 1024);
    if (raw.contains('\0')) return {};
    return QString::fromUtf8(raw);
}

// Recorta la salida de una tool ANTES de meterla al contexto (m_apiMessages).
// La tarjeta de la UI conserva la salida completa; al modelo le mandamos una
// versión acotada. Idea tomada de los "tool budgets" de caveman-code: en local
// (gen ~40 tok/s + SWA que reprocesa todo el prompt cada iter) cada línea de
// salida que no aporta es contexto que se re-evalúa una y otra vez.
static QString budgetToolOutput(const QString &name, const QString &raw)
{
    QString s = raw;
    // 1) Sacar secuencias de escape ANSI (colores/cursor de build/test/git).
    s.remove(QRegularExpression(QStringLiteral("\x1B\\[[0-9;?]*[A-Za-z]")));
    // 2) Colapsar runs de líneas en blanco a una sola.
    s.replace(QRegularExpression(QStringLiteral("\n[ \t]*(?:\n[ \t]*)+")),
              QStringLiteral("\n\n"));
    // 3) Cap por líneas según la tool. run_shell mantiene más cola (los errores
    //    suelen estar al final). El resto, cap razonable.
    int cap;
    if (name == QLatin1String("read_file"))      cap = 300;
    else if (name == QLatin1String("grep"))      cap = 120;
    else if (name == QLatin1String("list_dir"))  cap = 200;
    else if (name == QLatin1String("run_shell")) cap = 120;
    else                                          cap = 200;   // mcp/otras
    QStringList lines = s.split(QLatin1Char('\n'));
    if (lines.size() > cap) {
        const int head = cap * 2 / 3;
        const int tail = cap - head;
        QStringList kept = lines.mid(0, head);
        kept << QStringLiteral("… [%1 líneas omitidas para ahorrar contexto] …")
                    .arg(lines.size() - cap);
        kept += lines.mid(lines.size() - tail);
        s = kept.join(QLatin1Char('\n'));
    }
    // 4) Tope duro de caracteres como red de seguridad.
    if (s.size() > 24000)
        s = s.left(24000) + QStringLiteral("\n… [truncado]");
    return s;
}

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
    QMetaObject::invokeMethod(m_worker, "setServerBaseUrl", Qt::QueuedConnection,
                              Q_ARG(QString, m_ctx.serverBaseUrl));
    QMetaObject::invokeMethod(m_worker, "setTeacherConfig", Qt::QueuedConnection,
                              Q_ARG(QString, m_teacherUrl), Q_ARG(QString, m_teacherModel),
                              Q_ARG(QString, m_teacherKey));
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
        {QStringLiteral("max_tokens"), qMin(2048, m_ctxLimit > 0 ? m_ctxLimit / 8 : 2048)},
        {QStringLiteral("cache_prompt"), true}
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

        if (!m_running) return;                  // backend detenido durante la compactación

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
    cancelAllSubs();
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
    // Matar el run_shell async en vuelo y cerrar su tarjeta en vivo.
    if (m_worker) QMetaObject::invokeMethod(m_worker, "cancelShell", Qt::QueuedConnection);
    finalizeLiveToolCard(true);
    cancelAllSubs();
    // PARAR = detener todo, incluida la cola de mensajes pendientes.
    if (!m_msgQueue.isEmpty()) { m_msgQueue.clear(); emit queueChanged(); }
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
    m_readFingerprints.clear();
    m_checkpoints.clear();
    if (!m_msgQueue.isEmpty()) { m_msgQueue.clear(); emit queueChanged(); }
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
        "no inventes contenido de archivos. %3 %4 Respondé en el idioma del usuario.\n\n"
        "EFICIENCIA (importante): Resolvé la tarea en la MENOR cantidad de pasos/tool "
        "calls posible. Hacé lo justo que pidió el usuario, sin sobre-ingeniería ni "
        "features extra. Para CREAR un archivo: write_file UNA vez. Para MODIFICAR un "
        "archivo existente usá edit_file (reemplazo puntual de un fragmento), NO "
        "reescribas todo el archivo con write_file: es mucho más lento. Para leer "
        "archivos grandes usá read_file con offset/limit. Buscá con grep (regex) y "
        "glob. No "
        "verifiques de más: no re-leas ni re-ejecutes pruebas que ya pasaron, no "
        "corras el mismo comando varias veces. Una sola verificación rápida alcanza si "
        "hace falta. Cuando la tarea está hecha, terminá: no sigas iterando.\n\n")
        .arg(os, QDir::toNativeSeparators(m_cwd), scope, shell);

    if (m_approvalMode == QLatin1String("plan"))
        base += QStringLiteral(
            "MODO PLAN (read-only): NO podés editar archivos ni correr comandos; solo "
            "leer/buscar. Investigá lo necesario y entregá un PLAN claro y accionable "
            "(pasos, archivos a tocar, riesgos). write_file/edit_file/run_shell están "
            "deshabilitadas.\n\n");

    base += QStringLiteral(
        "ESTILO: respondé en fragmentos técnicos concisos. Sin relleno, sin "
        "cortesías, sin repetir lo que ya dijiste o lo que es obvio del código. "
        "Preferí listas y comandos antes que prosa. No expliques lo que vas a hacer "
        "antes de hacerlo: usá la tool directamente. El texto que generás también "
        "cuesta tiempo (generación local lenta): cada palabra de más es latencia.");

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

// Glob → regex anclada (mismo criterio que el worker: ** = recursivo).
static QRegularExpression permGlobToRegex(const QString &glob)
{
    // Construido a mano (NO QRegularExpression::escape: escapa '/' y rompe los
    // tokens de glob). '/' y '\' = cualquier separador. Matchea rutas rel y abs.
    QString rx = QStringLiteral("^");
    const int n = glob.size();
    int i = 0;
    while (i < n) {
        const QChar c = glob.at(i);
        if (c == QLatin1Char('*') && i + 1 < n && glob.at(i + 1) == QLatin1Char('*')) {
            if (i + 2 < n && (glob.at(i + 2) == QLatin1Char('/') || glob.at(i + 2) == QLatin1Char('\\'))) {
                rx += QStringLiteral("(?:.*[/\\\\])?");
                i += 3;   // **/ -> cero o mas dirs
                continue;
            }
            rx += QStringLiteral(".*");
            i += 2;        // ** -> cualquier cosa
            continue;
        } else if (c == QLatin1Char('*')) {
            rx += QStringLiteral("[^/\\\\]*");
            i += 1;        // * -> segmento
            continue;
        } else if (c == QLatin1Char('?')) {
            rx += QStringLiteral("[^/\\\\]");
            i += 1;
            continue;
        } else if (c == QLatin1Char('/') || c == QLatin1Char('\\')) {
            rx += QStringLiteral("[/\\\\]");
            i += 1;        // separador: slash o backslash
            continue;
        }
        if (QStringLiteral(".^$+(){}[]|").contains(c)) rx += QLatin1Char('\\');
        rx += c;
        i += 1;
    }
    rx += QLatin1Char('$');
    return QRegularExpression(rx);
}

// Reglas: una por línea. "allow|deny|ask [kind:]<glob>". kind ∈ read|write|shell.
// Ej: "deny **/.env", "allow write:src/**", "ask shell:rm *". '#' = comentario.
void LlamaAgentBackend::setPermissionRules(const QString &rules)
{
    m_permRules.clear();
    const QStringList lines = rules.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        const int sp = line.indexOf(QRegularExpression(QStringLiteral("\\s")));
        if (sp < 0) continue;
        const QString act = line.left(sp).toLower();
        QString rest = line.mid(sp).trimmed();
        PermAction action;
        if (act == QLatin1String("deny")) action = PermDeny;
        else if (act == QLatin1String("allow")) action = PermAllow;
        else if (act == QLatin1String("ask")) action = PermAsk;
        else continue;
        QString kind;
        const int colon = rest.indexOf(QLatin1Char(':'));
        if (colon > 0) {
            const QString k = rest.left(colon).toLower();
            if (k == QLatin1String("read") || k == QLatin1String("write")
                || k == QLatin1String("shell") || k == QLatin1String("mcp")) {
                kind = k;
                rest = rest.mid(colon + 1).trimmed();
            }
        }
        if (rest.isEmpty()) continue;
        m_permRules.append(PermRule{action, kind, permGlobToRegex(rest), rest});
    }
    emit logAppended(QStringLiteral("[permisos: %1 regla(s) cargadas]\n").arg(m_permRules.size()));
}

void LlamaAgentBackend::setApprovalPolicy(const QString &mode)
{
    m_approvalMode = mode;
    // "super" = sin confinamiento al cwd (acceso a todo el disco).
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "setConfined", Qt::QueuedConnection,
                                  Q_ARG(bool, mode != QLatin1String("super")));
    // Plan mode cambia tanto el system prompt como el set de tools → refrescar el
    // system prompt (índice 0) para que el cambio aplique en el próximo turno.
    if (!m_apiMessages.isEmpty()) {
        QJsonObject sys = m_apiMessages.first().toObject();
        if (sys.value(QStringLiteral("role")).toString() == QLatin1String("system")) {
            sys[QStringLiteral("content")] = buildSystemPrompt();
            m_apiMessages.replace(0, sys);
        }
    }
}

// ───────────────────────────── Conversación ──────────────────────────────
void LlamaAgentBackend::sendMessage(const QString &text)
{
    const QString trimmed = text.trimmed();
    const QStringList attachments = m_pendingAttachments;
    m_pendingAttachments.clear();
    if (!m_running || (trimmed.isEmpty() && attachments.isEmpty())) return;
    // Bloquear si hay turno en curso, una tool esperando aprobación, o una
    // compactación async en vuelo. Sin esto, mandar durante la compactación
    // corrompe m_apiMessages (reentrancy) → crash en Qt6Core.
    if (m_reply || m_compactReply || m_compacting || !m_awaitId.isEmpty()) {
        emit errorOccurred(QStringLiteral("Hay un turno en curso."));
        return;
    }
    ensureSession();
    pushCheckpoint();   // snapshot ANTES de agregar el nuevo turno (para rollback)

    // Contenido a mostrar en la UI: texto + chips de adjuntos.
    QString display = trimmed;
    for (const QString &p : attachments)
        display += QStringLiteral("\n📎 ") + QFileInfo(p).fileName();

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    QVariantMap userMsg{
        {QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"), display},
        {QStringLiteral("typing"), false},
        {QStringLiteral("createdAt"), static_cast<double>(nowMs)},
        {QStringLiteral("completedAt"), static_cast<double>(nowMs)},
        {QStringLiteral("elapsedMs"), 0},
        {QStringLiteral("tokens"), estimateTokens(display)},
        {QStringLiteral("tps"), 0.0}};
    if (!attachments.isEmpty()) userMsg[QStringLiteral("attachments")] = attachments;
    m_messages.append(userMsg);

    // Contenido para la API: si hay adjuntos, mensaje multimodal (texto + imágenes
    // inline + docs de texto inlineados); si no, string plano.
    if (attachments.isEmpty()) {
        m_apiMessages.append(QJsonObject{
            {QStringLiteral("role"), QStringLiteral("user")},
            {QStringLiteral("content"), trimmed}});
    } else {
        QString textPart = trimmed;
        QJsonArray images;
        for (const QString &p : attachments) {
            // Los @-mentions vienen relativos al cwd; el picker, absolutos.
            const QString path = QFileInfo(p).isAbsolute()
                ? p : QDir(m_cwd).absoluteFilePath(p);
            const QString uri = imageDataUri(path);
            if (!uri.isEmpty()) {
                images.append(QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("image_url")},
                    {QStringLiteral("image_url"), QJsonObject{{QStringLiteral("url"), uri}}}});
            } else {
                const QString doc = readAttachText(path);
                if (!doc.isEmpty())
                    textPart += QStringLiteral("\n\n--- %1 ---\n%2")
                                    .arg(QFileInfo(path).fileName(), doc);
            }
        }
        QJsonArray parts;
        if (!textPart.trimmed().isEmpty())
            parts.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                     {QStringLiteral("text"), textPart}});
        for (const QJsonValue &iv : images) parts.append(iv);
        m_apiMessages.append(QJsonObject{
            {QStringLiteral("role"), QStringLiteral("user")},
            {QStringLiteral("content"), parts}});
    }

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

// Snapshot del estado actual (antes de un turno de usuario) para poder rebobinar.
void LlamaAgentBackend::pushCheckpoint()
{
    m_checkpoints.append(Checkpoint{
        static_cast<int>(m_apiMessages.size()), static_cast<int>(m_messages.size()),
        m_editSnapshots.keys()});
}

// Rebobina la conversación al estado previo al mensaje de usuario en `msgIndex`.
void LlamaAgentBackend::rollbackToMessage(int msgIndex)
{
    if (!m_running) return;
    if (isBusy()) { emit errorOccurred(QStringLiteral("No se puede rebobinar con un turno en curso.")); return; }

    // Buscar el checkpoint cuyo msgLen == msgIndex (el tomado justo antes de ese
    // mensaje de usuario).
    int ci = -1;
    for (int i = 0; i < m_checkpoints.size(); ++i)
        if (m_checkpoints[i].msgLen == msgIndex) { ci = i; break; }
    if (ci < 0) { emit errorOccurred(QStringLiteral("No hay checkpoint para ese mensaje.")); return; }

    const Checkpoint cp = m_checkpoints[ci];
    if (cp.msgLen > m_messages.size() || cp.apiLen > m_apiMessages.size()) return;

    // Revertir archivos editados DESPUÉS del checkpoint (los que no estaban en
    // editKeys). Sólo se puede restaurar al contenido original (1er snapshot).
    const QSet<QString> before(cp.editKeys.begin(), cp.editKeys.end());
    const QStringList nowKeys = m_editSnapshots.keys();
    for (const QString &abs : nowKeys)
        if (!before.contains(abs))
            revertEdit(abs);   // restaura original + saca el snapshot

    // Truncar conversación (UI + API) al estado del checkpoint.
    while (m_messages.size() > cp.msgLen) m_messages.removeLast();
    while (m_apiMessages.size() > cp.apiLen) m_apiMessages.removeLast();

    // Descartar checkpoints desde éste en adelante.
    while (m_checkpoints.size() > ci) m_checkpoints.removeLast();

    m_curAsstIdx = -1;
    emit logAppended(QStringLiteral("[rebobinado al mensaje %1 (%2 msgs, %3 ctx)]\n")
                         .arg(msgIndex).arg(m_messages.size()).arg(m_apiMessages.size()));
    emit messagesChanged();
    saveCurrentSession();
}

// ¿Hay algo en vuelo? (request, compactación, tool ejecutándose o esperando
// aprobación, o tool_calls pendientes de procesar).
bool LlamaAgentBackend::isBusy() const
{
    return m_reply || m_compactReply || m_compacting
           || !m_awaitId.isEmpty() || !m_execCallId.isEmpty()
           || !m_pendingCalls.isEmpty() || subsActive();
}

// Encolar: si está ocupado, guarda y se enviará al terminar el turno. Si no,
// envía ya.
void LlamaAgentBackend::queueMessage(const QString &text)
{
    const QString t = text.trimmed();
    if (!m_running || t.isEmpty()) return;
    if (!isBusy()) { sendMessage(t); return; }
    m_msgQueue << t;
    emit queueChanged();
    emit logAppended(QStringLiteral("[encolado (%1 en cola): %2]\n")
                         .arg(m_msgQueue.size()).arg(t.left(80)));
}

// Steering: interrumpe el turno actual (cancela tools/aprobación, repara el
// historial) y envía el mensaje nuevo de inmediato.
void LlamaAgentBackend::steerMessage(const QString &text)
{
    const QString t = text.trimmed();
    if (!m_running || t.isEmpty()) return;
    if (isBusy()) {
        interruptForSteer();
        emit logAppended(QStringLiteral("[steering: turno interrumpido por nuevo mensaje]\n"));
    }
    sendMessage(t);
}

// Envía el próximo mensaje encolado, si lo hay y ya no hay nada en vuelo.
void LlamaAgentBackend::flushQueue()
{
    if (!m_running || m_msgQueue.isEmpty() || isBusy()) return;
    const QString t = m_msgQueue.takeFirst();
    emit queueChanged();
    sendMessage(t);
}

void LlamaAgentBackend::clearQueue()
{
    if (m_msgQueue.isEmpty()) return;
    m_msgQueue.clear();
    emit queueChanged();
}

// Aborta request/compactación, descarta la burbuja parcial y deja m_apiMessages
// consistente (cada assistant.tool_calls con su respuesta) para que el próximo
// request no sea inválido.
void LlamaAgentBackend::interruptForSteer()
{
    if (m_compactReply) {
        QNetworkReply *cr = m_compactReply; m_compactReply = nullptr;
        cr->disconnect(this); cr->abort(); cr->deleteLater();
    }
    m_compacting = false;
    if (m_reply) {
        // Anular y desconectar ANTES de abort() (abort emite finished/readyRead
        // sincrónico → reentrancy). Igual criterio que cancelGeneration.
        QNetworkReply *r = m_reply; m_reply = nullptr;
        r->disconnect(this); r->abort(); r->deleteLater();
    }
    // Matar el run_shell async en vuelo y cerrar su tarjeta en vivo.
    if (m_worker) QMetaObject::invokeMethod(m_worker, "cancelShell", Qt::QueuedConnection);
    finalizeLiveToolCard(true);
    cancelAllSubs();
    // Cerrar la burbuja en curso (descarta si está vacía; si tenía texto parcial
    // lo deja como mensaje finalizado).
    if (m_curAsstIdx >= 0) closeAssistantBubble();
    m_curAsstIdx = -1;
    // Reparar tool_calls colgados ANTES de limpiar el estado pendiente.
    repairDanglingToolCalls();
    m_pendingCalls = {};
    m_awaitId.clear();
    m_awaitCall = {};
    m_execCallId.clear();   // ignora resultado tardío de una tool en vuelo
    setTyping(false);
}

// Cierra la tarjeta de run_shell "en vivo" si quedó abierta (al cancelar/interrumpir).
void LlamaAgentBackend::finalizeLiveToolCard(bool cancelled)
{
    if (m_liveToolMsgIdx < 0 || m_liveToolMsgIdx >= m_messages.size()) {
        m_liveToolCallId.clear(); m_liveToolMsgIdx = -1; return;
    }
    QVariantMap card = m_messages[m_liveToolMsgIdx].toMap();
    card[QStringLiteral("typing")] = false;
    if (cancelled) {
        card[QStringLiteral("ok")] = false;
        const QString o = card.value(QStringLiteral("output")).toString();
        card[QStringLiteral("output")] = o + (o.isEmpty() ? QString() : QStringLiteral("\n"))
                                         + QStringLiteral("[cancelado por el usuario]");
    }
    m_messages[m_liveToolMsgIdx] = card;
    emit messagesChanged();
    m_liveToolCallId.clear();
    m_liveToolMsgIdx = -1;
}

// Si el último assistant tiene tool_calls sin su mensaje 'tool' de respuesta,
// agrega stubs "[interrumpido]" para que el historial quede válido.
void LlamaAgentBackend::repairDanglingToolCalls()
{
    int lastAsst = -1;
    for (int i = m_apiMessages.size() - 1; i >= 0; --i) {
        const QJsonObject o = m_apiMessages[i].toObject();
        const QString role = o.value(QStringLiteral("role")).toString();
        if (role == QLatin1String("assistant")
            && o.contains(QStringLiteral("tool_calls"))) { lastAsst = i; break; }
        if (role == QLatin1String("user")) return;   // no hay turno de tools abierto
    }
    if (lastAsst < 0) return;

    QSet<QString> unanswered;
    for (const QJsonValue &v : m_apiMessages[lastAsst].toObject()
                                  .value(QStringLiteral("tool_calls")).toArray())
        unanswered.insert(v.toObject().value(QStringLiteral("id")).toString());
    for (int i = lastAsst + 1; i < m_apiMessages.size(); ++i) {
        const QJsonObject o = m_apiMessages[i].toObject();
        if (o.value(QStringLiteral("role")).toString() == QLatin1String("tool"))
            unanswered.remove(o.value(QStringLiteral("tool_call_id")).toString());
    }
    for (const QString &id : std::as_const(unanswered))
        if (!id.isEmpty())
            appendToolResult(id, QString(),
                             QStringLiteral("[interrumpido por el usuario]"));
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
        // Habilitado para que el modelo pueda lanzar varias `task` en paralelo.
        // El loop serializa los tools normales igual (uno por onToolExecuted).
        {QStringLiteral("parallel_tool_calls"), true},
        // Parsear tool calls del lado server puede devolver 500 si el modelo
        // emite argumentos JSON parciales durante el streaming. Lo manejamos
        // del lado cliente para tolerar errores y permitir reintentos.
        {QStringLiteral("parse_tool_calls"), false},
        // Cap de salida alto: el perfil server puede traer --predict 4096, que
        // trunca un write_file con archivo grande a mitad del JSON de args →
        // tool_call inválido → reintentos. max_tokens en el request pisa al
        // default del server y deja completar el contenido del archivo.
        {QStringLiteral("max_tokens"), outReserve},
        {QStringLiteral("stream"), true},
        // Reutilizar el KV/prompt-cache del server entre iteraciones del loop de
        // tools. En cada follow-up el prefijo (system+tools+historial) es estable,
        // así el server procesa sólo el sufijo nuevo en vez de re-evaluar todo el
        // contexto. No dependemos del default del fork (que puede venir en false).
        {QStringLiteral("cache_prompt"), true},
        // Pedir el bloque `usage` en el chunk final del stream → tokens reales de
        // generación (en vez de estimar chars/4) para métricas/tps fiables.
        {QStringLiteral("stream_options"), QJsonObject{{QStringLiteral("include_usage"), true}}}
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

    // Crear la burbuja del asistente (typing) ANTES de disparar el request, no
    // recién al primer token. En turnos de follow-up (tras una tool) la burbuja
    // previa quedó cerrada, así que durante el hueco "request enviado → primer
    // token" no había ningún mensaje con typing=true: la UI parecía idle y el
    // botón seguía en "Enviar", dejando mandar un mensaje que el backend rechaza
    // con "Hay un turno en curso". Con la burbuja creada ya, el botón pasa a
    // "PARAR" y se ve el indicador de generación. Si la respuesta resulta ser
    // sólo tool-calls (sin texto), closeAssistantBubble descarta la burbuja vacía.
    ensureAssistantBubble();

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
    m_genTokens = 0;
    m_genMs = 0.0;
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
        // Métricas reales de generación. `usage.completion_tokens` = tokens
        // generados (exacto). `timings` (llama.cpp) trae predicted_n/predicted_ms
        // → tiempo de generación puro, sin prompt-processing. Preferir timings.
        const int compTok = usage.value(QStringLiteral("completion_tokens")).toInt(-1);
        if (compTok >= 0) m_genTokens = compTok;
        const QJsonObject timings = obj.value(QStringLiteral("timings")).toObject();
        if (!timings.isEmpty()) {
            const int pn = timings.value(QStringLiteral("predicted_n")).toInt(-1);
            const double pms = timings.value(QStringLiteral("predicted_ms")).toDouble(-1.0);
            if (pn >= 0)  m_genTokens = pn;
            if (pms > 0.0) m_genMs = pms;
        }

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

        // Progreso de tool_calls en streaming. Cuando el modelo está generando
        // una tool (p.ej. write_file con un archivo grande), los tokens llegan
        // como delta.tool_calls.arguments, NO como delta.content. Sin esto el
        // bubble no mostraba NADA durante toda la generación —que a ~40 tok/s
        // para un archivo de ~6k tokens son ~2-3 min— y la UI parecía colgada.
        // Mostramos un indicador en vivo con la tool y el tamaño acumulado.
        const bool toolStreaming = !m_streamToolCalls.isEmpty();

        // Abrir una burbuja nueva si la anterior se cerró tras una tool.
        if ((!m_streamContent.isEmpty() || !m_streamReason.isEmpty() || toolStreaming)
            && m_curAsstIdx < 0)
            ensureAssistantBubble();

        // Mostrar en vivo: base + <think>razonamiento</think> + respuesta.
        QString full = m_streamBase;
        if (!m_streamReason.isEmpty())
            full += QStringLiteral("<think>") + m_streamReason + QStringLiteral("</think>\n");
        full += m_streamContent;
        // Indicador transitorio mientras se generan args de tool (sin texto aún).
        // Se limpia en handleStreamFinished antes de cerrar/finalizar el bubble,
        // así no queda pegado en el chat ni envenena m_streamContent.
        if (toolStreaming && m_streamContent.isEmpty()) {
            int argChars = 0;
            QString toolName;
            const QList<int> ks = m_streamToolCalls.keys();
            for (int k : ks) {
                const QJsonObject tc = m_streamToolCalls.value(k);
                argChars += tc.value(QStringLiteral("arguments")).toString().size();
                if (toolName.isEmpty())
                    toolName = tc.value(QStringLiteral("name")).toString();
            }
            if (toolName.isEmpty()) toolName = QStringLiteral("tool");
            if (!full.isEmpty()) full += QLatin1Char('\n');
            full += QStringLiteral("⏳ preparando `%1`… (~%2 tokens generados)")
                        .arg(toolName).arg((argChars + 3) / 4);
        }
        if (m_curAsstIdx >= 0 && m_curAsstIdx < m_messages.size()) {
            QVariantMap m = m_messages[m_curAsstIdx].toMap();
            // Marca el inicio real de la generación (primer token) para medir tps
            // sin contar el prompt-processing previo.
            if (m.value(QStringLiteral("genStartMs")).toDouble() <= 0.0)
                m[QStringLiteral("genStartMs")] =
                    static_cast<double>(QDateTime::currentMSecsSinceEpoch());
            m[QStringLiteral("content")] = full;
            m[QStringLiteral("tokens")] = estimateTokens(full);
            m_messages[m_curAsstIdx] = m;
            // Throttle a ~30 fps. Durante el stream NO emitimos messagesChanged
            // (reconstruye todo el ListView por cada token → jank). Mandamos sólo
            // el delta de texto de ESTA burbuja vía streamingText; la UI refresca
            // un único delegate. El estado final/estructural lo cierra
            // handleStreamFinished/finishTurn con messagesChanged.
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - m_lastUiEmitMs >= 33) {
                m_lastUiEmitMs = now;
                emit streamingText(m_curAsstIdx, full);
            }
        }
    }
}

void LlamaAgentBackend::handleStreamFinished(bool ok, const QString &err)
{
    if (!ok) { finishTurn(QStringLiteral("[error: %1]").arg(err)); return; }

    // Quitar el indicador "⏳ preparando…" de tool en streaming: dejar el bubble
    // con el contenido REAL del modelo antes de cerrarlo/finalizarlo. Sin esto el
    // texto transitorio quedaría pegado en el chat y haría que closeAssistantBubble
    // no descarte una burbuja que en realidad está vacía (sólo tool_calls).
    if (m_curAsstIdx >= 0 && m_curAsstIdx < m_messages.size()) {
        QString clean = m_streamBase;
        if (!m_streamReason.isEmpty())
            clean += QStringLiteral("<think>") + m_streamReason + QStringLiteral("</think>\n");
        clean += m_streamContent;
        QVariantMap m = m_messages[m_curAsstIdx].toMap();
        m[QStringLiteral("content")] = clean;
        m_messages[m_curAsstIdx] = m;
    }

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
    if (subsActive()) return;   // esperando que terminen los sub-agentes

    if (m_pendingCalls.isEmpty()) {
        // Todas las tools resueltas → re-consultar al modelo con los resultados.
        runCompletion();
        return;
    }

    // ── Subagents: extraer TODAS las tool_calls `task` y lanzarlas en paralelo.
    {
        QJsonArray taskCalls, rest;
        for (const QJsonValue &v : std::as_const(m_pendingCalls)) {
            if (v.toObject().value(QStringLiteral("function")).toObject()
                    .value(QStringLiteral("name")).toString() == QLatin1String("task"))
                taskCalls.append(v);
            else
                rest.append(v);
        }
        if (!taskCalls.isEmpty()) {
            m_pendingCalls = rest;       // los no-task se procesan al terminar los subs
            spawnTasks(taskCalls);
            return;
        }
    }

    const QJsonObject call = m_pendingCalls.first().toObject();
    const QJsonObject fn   = call.value(QStringLiteral("function")).toObject();
    const QString name     = fn.value(QStringLiteral("name")).toString();
    QString kind           = toolKind(name);
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
        QStringLiteral("write_file"), QStringLiteral("edit_file"),
        QStringLiteral("glob"), QStringLiteral("run_shell"), QStringLiteral("web_fetch"),
        QStringLiteral("web_search"), QStringLiteral("deep_research"),
        QStringLiteral("search_docs"), QStringLiteral("semantic_search"),
        QStringLiteral("memory"), QStringLiteral("ask_teacher"),
        QStringLiteral("task")};
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

    // memory: 'save' muta el archivo de memoria → tratar como write (requiere
    // aprobación). 'recall' (o sin action) es lectura pura → auto.
    if (name == QLatin1String("memory")
        && args.value(QStringLiteral("action")).toString().toLower() == QLatin1String("save"))
        kind = QStringLiteral("write");

    // PLAN MODE: bloquear cualquier tool que mute (write/shell/mcp). Las read
    // ya están filtradas del schema, pero defendemos por si el modelo igual la pide.
    if (m_approvalMode == QLatin1String("plan")
        && kind != QLatin1String("read")) {
        ++m_toolFail;
        m_pendingCalls.removeFirst();
        appendToolResult(id, name, QStringLiteral(
            "[MODO PLAN: '%1' bloqueada (solo lectura). Proponé el cambio en texto; "
            "el usuario saldrá de plan para ejecutarlo.]").arg(name));
        processPendingCalls();
        return;
    }

    // ── Permisos por patrón (antes de la política global) ─────────────────
    // subject = ruta (read/write) o comando (shell). Primera regla que matchea gana.
    bool forceAsk = false;
    {
        QString subject = (kind == QLatin1String("shell"))
            ? args.value(QStringLiteral("command")).toString()
            : args.value(QStringLiteral("path")).toString();
        if (subject.isEmpty()) subject = args.value(QStringLiteral("pattern")).toString();
        emit logAppended(QStringLiteral("[perm-eval] kind=%1 subject='%2' rules=%3\n")
                             .arg(kind, subject).arg(m_permRules.size()));
        if (!subject.isEmpty()) {
            for (const PermRule &r : std::as_const(m_permRules)) {
                const bool kindOk = r.kind.isEmpty() || r.kind == kind;
                const bool rxOk = r.rx.match(subject).hasMatch();
                emit logAppended(QStringLiteral("[perm-eval]   glob='%1' rx=[%2] valid=%3 kindOk=%4 rxOk=%5\n")
                                     .arg(r.glob, r.rx.pattern()).arg(r.rx.isValid()).arg(kindOk).arg(rxOk));
                if (!r.kind.isEmpty() && r.kind != kind) continue;
                if (!r.rx.match(subject).hasMatch()) continue;
                if (r.action == PermDeny) {
                    ++m_toolFail;
                    m_pendingCalls.removeFirst();
                    const QString denied = QStringLiteral(
                        "[permiso denegado por regla '%1': '%2' no está permitido]")
                        .arg(r.glob, subject);
                    appendToolCard(name, kind, false, subject, denied);
                    appendToolResult(id, name, denied);
                    processPendingCalls();
                    return;
                }
                if (r.action == PermAllow) {
                    emit logAppended(QStringLiteral("[permiso: regla '%1' permite %2]\n").arg(r.glob, name));
                    approveAndContinue(id, QStringLiteral("once"));
                    return;
                }
                forceAsk = true;   // PermAsk: pedir aprobación aunque el modo sea auto
                break;
            }
        }
    }

    const bool autoAll  = (m_approvalMode == QLatin1String("auto")
                           || m_approvalMode == QLatin1String("super")
                           || m_approvalMode == QLatin1String("plan"));   // plan = todo read → auto
    const bool autoRead = (m_approvalMode == QLatin1String("ask") && kind == QLatin1String("read"));
    const bool always   = m_alwaysAllowed.contains(kind);

    if (!forceAsk && (autoAll || autoRead || always)) {
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
    if (name == QLatin1String("write_file") || name == QLatin1String("edit_file")) {
        const QString rel = args.value(QStringLiteral("path")).toString();
        detail = rel;
        const QString abs = QDir::cleanPath(QDir(m_cwd).absoluteFilePath(rel));
        QString oldText;
        QFile prev(abs);
        if (prev.open(QIODevice::ReadOnly)) oldText = QString::fromUtf8(prev.read(4 * 1024 * 1024));
        QString newText;
        if (name == QLatin1String("write_file")) {
            newText = args.value(QStringLiteral("content")).toString();
        } else {
            // edit_file: previsualizar el reemplazo (igual lógica que el worker).
            const QString oldS = args.value(QStringLiteral("old_string")).toString();
            const QString newS = args.value(QStringLiteral("new_string")).toString();
            newText = oldText;
            if (!oldS.isEmpty()) {
                if (args.value(QStringLiteral("replace_all")).toBool()) {
                    newText.replace(oldS, newS);
                } else {
                    const int idx = oldText.indexOf(oldS);
                    if (idx >= 0)
                        newText = oldText.left(idx) + newS + oldText.mid(idx + oldS.size());
                }
            }
        }
        diff = makeDiff(oldText, newText);
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
    QString res        = result.value(QStringLiteral("result")).toString();
    const bool isWrite = result.value(QStringLiteral("isWrite")).toBool();
    if (ok) ++m_toolOk; else ++m_toolFail;

    // Read-dedup: si el modelo re-lee un archivo que ya leyó y NO cambió, no
    // reenviamos el contenido (el server reprocesa todo el prompt cada iter por
    // SWA → re-leer 300 líneas idénticas es puro desperdicio). Devolvemos un stub.
    bool dedup = false;
    if (name == QLatin1String("read_file") && ok) {
        const QString rel = result.value(QStringLiteral("readRel")).toString();
        const QString fp  = result.value(QStringLiteral("readFp")).toString();
        if (!rel.isEmpty() && !fp.isEmpty()) {
            if (m_readFingerprints.value(rel) == fp) {
                res = QStringLiteral("[read_file: '%1' ya leído antes y sin cambios; "
                                     "contenido omitido para ahorrar contexto]").arg(rel);
                dedup = true;
            } else {
                m_readFingerprints.insert(rel, fp);
            }
        }
    }

    // run_shell async: ya hay una tarjeta "en vivo" (onToolStarted) con la salida
    // streameada → finalizarla en vez de crear otra.
    if (callId == m_liveToolCallId && m_liveToolMsgIdx >= 0
        && m_liveToolMsgIdx < m_messages.size()) {
        QVariantMap card = m_messages[m_liveToolMsgIdx].toMap();
        card[QStringLiteral("ok")] = ok;
        card[QStringLiteral("typing")] = false;
        // Mostrar la salida real final (ya recortada por el worker).
        card[QStringLiteral("output")] = res.left(64 * 1024);
        m_messages[m_liveToolMsgIdx] = card;
        emit messagesChanged();
        m_liveToolCallId.clear();
        m_liveToolMsgIdx = -1;
    } else if (!isWrite) {
        // write_file/edit_file → tarjeta de diff (abajo). El resto → tarjeta propia.
        appendToolCard(name, toolKind(name), ok, m_execCommand, res);
    }
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

    // Al contexto va la versión acotada (salvo que ya sea un stub de dedup).
    appendToolResult(callId, name, dedup ? res : budgetToolOutput(name, res));
    processPendingCalls();
}

// run_shell async: arranca → crear tarjeta "en vivo" (typing) con el comando.
void LlamaAgentBackend::onToolStarted(const QVariantMap &info)
{
    const QString callId = info.value(QStringLiteral("callId")).toString();
    if (callId.isEmpty() || callId != m_execCallId) return;   // ajeno/tardío
    const QString name = info.value(QStringLiteral("name")).toString();
    const QString cmd  = info.value(QStringLiteral("command")).toString();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    m_messages.append(QVariantMap{
        {QStringLiteral("role"),    QStringLiteral("toolcall")},
        {QStringLiteral("name"),    name},
        {QStringLiteral("kind"),    info.value(QStringLiteral("kind"))},
        {QStringLiteral("ok"),      true},
        {QStringLiteral("command"), cmd},
        {QStringLiteral("output"),  QString()},
        {QStringLiteral("typing"),  true},          // en ejecución
        {QStringLiteral("createdAt"),   static_cast<double>(nowMs)},
        {QStringLiteral("completedAt"), static_cast<double>(nowMs)},
        {QStringLiteral("elapsedMs"), 0},
        {QStringLiteral("tokens"), 0},
        {QStringLiteral("tps"), 0.0}});
    m_liveToolMsgIdx = m_messages.size() - 1;
    m_liveToolCallId = callId;
    m_lastToolEmitMs = 0;
    emit messagesChanged();
}

// run_shell async: chunk de salida → anexar a la tarjeta en vivo (throttle).
void LlamaAgentBackend::onToolOutputChunk(const QString &callId, const QString &chunk)
{
    if (callId != m_liveToolCallId || m_liveToolMsgIdx < 0
        || m_liveToolMsgIdx >= m_messages.size()) return;
    QVariantMap card = m_messages[m_liveToolMsgIdx].toMap();
    QString out = card.value(QStringLiteral("output")).toString() + chunk;
    if (out.size() > 64 * 1024) out = out.right(64 * 1024);   // cola visible
    card[QStringLiteral("output")] = out;
    m_messages[m_liveToolMsgIdx] = card;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastToolEmitMs >= 100) {        // ~10 fps
        m_lastToolEmitMs = now;
        emit messagesChanged();
    }
}

// ───────────────────────────── Subagents (tool `task`) ───────────────────
static QString runGitCap(const QStringList &args, const QString &dir, int *exitCode = nullptr)
{
    QProcess p;
    p.setWorkingDirectory(dir);
    p.start(QStringLiteral("git"), args);
    if (!p.waitForStarted(5000)) { if (exitCode) *exitCode = -1; return QStringLiteral("git no disponible"); }
    p.waitForFinished(20000);
    if (exitCode) *exitCode = p.exitCode();
    return QString::fromUtf8(p.readAllStandardOutput() + p.readAllStandardError());
}

// ¿git instalado? (cacheado). Subagents lo requieren para aislar en worktrees.
static bool gitInstalled()
{
    static int cached = -1;   // -1 desconocido, 0 no, 1 sí
    if (cached < 0)
        cached = QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty() ? 0 : 1;
    return cached == 1;
}

// Aísla al sub-agente en una git worktree (rama nueva desde HEAD). Inicializa el
// repo si el cwd aún no lo es. Devuelve "" si git NO está instalado (el caller
// pide instalarlo). isolated=true si quedó aislado.
QString LlamaAgentBackend::createWorktree(const QString &callId, bool &isolated)
{
    isolated = false;
    if (!gitInstalled()) return QString();   // → launchSub pide instalar git

    const QString sid = QString(callId).remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9]"))).left(10);
    int ex = 0;
    runGitCap({QStringLiteral("rev-parse"), QStringLiteral("--is-inside-work-tree")}, m_cwd, &ex);
    if (ex != 0) {
        // cwd no es repo git → inicializarlo + commit base (necesario para worktree).
        runGitCap({QStringLiteral("init")}, m_cwd, &ex);
        runGitCap({QStringLiteral("-c"), QStringLiteral("user.email=agent@llamacode"),
                   QStringLiteral("-c"), QStringLiteral("user.name=llamacode"),
                   QStringLiteral("commit"), QStringLiteral("--allow-empty"), QStringLiteral("--no-verify"),
                   QStringLiteral("-m"), QStringLiteral("llamacode: init para subagents")}, m_cwd, &ex);
        // Si había archivos sin trackear, snapshot inicial para que HEAD los tenga.
        runGitCap({QStringLiteral("add"), QStringLiteral("-A")}, m_cwd);
        runGitCap({QStringLiteral("-c"), QStringLiteral("user.email=agent@llamacode"),
                   QStringLiteral("-c"), QStringLiteral("user.name=llamacode"),
                   QStringLiteral("commit"), QStringLiteral("--no-verify"),
                   QStringLiteral("-m"), QStringLiteral("llamacode: snapshot inicial")}, m_cwd, &ex);
    }

    const QString base = QDir::tempPath() + QStringLiteral("/llamacode-worktrees");
    QDir().mkpath(base);
    const QString path = QDir::cleanPath(base + QStringLiteral("/") + sid);
    QDir(path).removeRecursively();
    const QString branch = QStringLiteral("llamacode-sub/") + sid;
    runGitCap({QStringLiteral("worktree"), QStringLiteral("prune")}, m_cwd);
    runGitCap({QStringLiteral("worktree"), QStringLiteral("add"), QStringLiteral("-b"), branch,
               path, QStringLiteral("HEAD")}, m_cwd, &ex);
    if (ex != 0) return QString();   // falló crear la worktree
    isolated = true;
    m_subBranch.insert(callId, branch);
    return path;
}

// Commit en la worktree + merge a la rama actual (abort si conflicto, preservando
// la rama). Limpia worktree.
QString LlamaAgentBackend::mergeAndCleanupWorktree(const QString &callId, bool ok, bool isolated)
{
    if (!isolated) return QString();
    const QString path = m_subWorktree.value(callId);
    const QString branch = m_subBranch.value(callId);
    QString note;
    bool preserveBranch = false;

    if (ok && !path.isEmpty()) {
        runGitCap({QStringLiteral("add"), QStringLiteral("-A")}, path);
        int cex = 0;
        runGitCap({QStringLiteral("-c"), QStringLiteral("user.email=sub@llamacode"),
                   QStringLiteral("-c"), QStringLiteral("user.name=subagent"),
                   QStringLiteral("commit"), QStringLiteral("--no-verify"),
                   QStringLiteral("-m"), QStringLiteral("subagent ") + callId.left(8)}, path, &cex);
        if (cex == 0) {
            int mex = 0;
            const QString mout = runGitCap({QStringLiteral("merge"), QStringLiteral("--no-edit"),
                                            QStringLiteral("--no-ff"), branch}, m_cwd, &mex);
            if (mex == 0) {
                note = QStringLiteral("\n[✓ cambios del sub-agente mergeados a la rama actual]");
            } else {
                // Conflicto: repo LIMPIO (abort) + preservar rama para merge manual.
                runGitCap({QStringLiteral("merge"), QStringLiteral("--abort")}, m_cwd);
                preserveBranch = true;
                note = QStringLiteral("\n[⚠ conflicto de merge — abortado, repo intacto. "
                                      "Los cambios quedaron en la rama '%1': mergeala a mano.\n%2]")
                           .arg(branch, mout.left(300));
            }
        } else {
            note = QStringLiteral("\n[el sub-agente no dejó cambios]");
        }
    }
    if (!path.isEmpty())
        runGitCap({QStringLiteral("worktree"), QStringLiteral("remove"), QStringLiteral("--force"), path}, m_cwd);
    if (!branch.isEmpty() && !preserveBranch)
        runGitCap({QStringLiteral("branch"), QStringLiteral("-D"), branch}, m_cwd);
    return note;
}

// Encola las task y lanza hasta kMaxParallelSubs en paralelo.
void LlamaAgentBackend::spawnTasks(const QJsonArray &taskCalls)
{
    for (const QJsonValue &v : taskCalls) m_subQueue.append(v);
    emit logAppended(QStringLiteral("[subagents: %1 encoladas (máx %2 en paralelo)]\n")
                         .arg(taskCalls.size()).arg(kMaxParallelSubs));
    pumpSubs();
}

// Lanza subs de la cola hasta llenar el cap. Si no queda nada activo, continúa el turno.
void LlamaAgentBackend::pumpSubs()
{
    while (m_subs.size() < kMaxParallelSubs && !m_subQueue.isEmpty()) {
        const QJsonObject call = m_subQueue.takeAt(0).toObject();
        launchSub(call);
    }
    if (!subsActive()) processPendingCalls();   // todas resueltas/ inválidas → seguir
}

void LlamaAgentBackend::launchSub(const QJsonObject &call)
{
    const QString id = call.value(QStringLiteral("id")).toString();
    const QJsonObject fn = call.value(QStringLiteral("function")).toObject();
    const QString argStr = toolArgumentsToString(fn.value(QStringLiteral("arguments")));
    const QJsonObject a = QJsonDocument::fromJson(argStr.toUtf8()).object();
    const QString prompt = a.value(QStringLiteral("prompt")).toString();
    const QString desc = a.value(QStringLiteral("description")).toString();

    if (prompt.trimmed().isEmpty()) {
        ++m_toolFail;
        appendToolResult(id, QStringLiteral("task"), QStringLiteral("[error: 'prompt' vacío para task]"));
        return;   // no arranca runner; pumpSubs sigue
    }

    bool isolated = false;
    const QString wt = createWorktree(id, isolated);
    if (wt.isEmpty()) {
        // git no instalado (o falló la worktree) → pedir instalación, no ejecutar.
        ++m_toolFail;
        emit gitRequired();
        appendToolCard(QStringLiteral("task"), QStringLiteral("task"), false,
                       desc.isEmpty() ? prompt.left(60) : desc,
                       QStringLiteral("[Git no está instalado. Los subagents necesitan git para "
                                      "aislar el trabajo en una worktree. Instalá git y reintentá.]"));
        appendToolResult(id, QStringLiteral("task"),
                         QStringLiteral("[error: subagents requieren git instalado. Se le pidió al "
                                        "usuario instalarlo. Resolvé la subtarea vos directamente "
                                        "en vez de usar task.]"));
        return;
    }
    m_subWorktree.insert(id, wt);
    m_subIsolated.insert(id, isolated);

    appendToolCard(QStringLiteral("task"), QStringLiteral("task"), true,
                   desc.isEmpty() ? prompt.left(60) : desc,
                   QStringLiteral("[worktree git aislada]\n"));
    if (!m_messages.isEmpty()) {
        QVariantMap card = m_messages.last().toMap();
        card[QStringLiteral("typing")] = true;
        m_messages[m_messages.size() - 1] = card;
        m_subMsgIdx.insert(id, m_messages.size() - 1);
    }
    emit messagesChanged();

    auto *sub = new SubAgentRunner(id, m_ctx.serverBaseUrl, m_ctx.modelId,
                                   wt, prompt, m_temperature, this);
    connect(sub, &SubAgentRunner::finished, this, &LlamaAgentBackend::onSubFinished);
    connect(sub, &SubAgentRunner::progressed, this, &LlamaAgentBackend::onSubProgress);
    m_subs.insert(id, sub);
    sub->start();
}

void LlamaAgentBackend::onSubProgress(const QString &id, const QString &note)
{
    const int idx = m_subMsgIdx.value(id, -1);
    if (idx < 0 || idx >= m_messages.size()) return;
    QVariantMap card = m_messages[idx].toMap();
    QString out = card.value(QStringLiteral("output")).toString() + note + QStringLiteral("\n");
    if (out.size() > 16 * 1024) out = out.right(16 * 1024);
    card[QStringLiteral("output")] = out;
    m_messages[idx] = card;
    emit messagesChanged();
}

void LlamaAgentBackend::onSubFinished(const QString &id, const QString &result, bool ok)
{
    if (!m_subs.contains(id)) return;
    SubAgentRunner *sub = m_subs.take(id);
    if (sub) sub->deleteLater();

    const bool isolated = m_subIsolated.value(id, false);
    const QString mergeNote = mergeAndCleanupWorktree(id, ok, isolated);

    const int idx = m_subMsgIdx.value(id, -1);
    if (idx >= 0 && idx < m_messages.size()) {
        QVariantMap card = m_messages[idx].toMap();
        card[QStringLiteral("typing")] = false;
        card[QStringLiteral("ok")] = ok;
        QString out = card.value(QStringLiteral("output")).toString()
                      + QStringLiteral("\n") + result + mergeNote;
        card[QStringLiteral("output")] = out.left(64 * 1024);
        m_messages[idx] = card;
    }
    emit messagesChanged();

    if (ok) ++m_toolOk; else ++m_toolFail;
    appendToolResult(id, QStringLiteral("task"), (result + mergeNote).left(16 * 1024));

    m_subWorktree.remove(id); m_subBranch.remove(id);
    m_subIsolated.remove(id); m_subMsgIdx.remove(id);
    pumpSubs();   // lanza el siguiente encolado; si no queda nada → sigue el turno
}

// Cancela y limpia todos los sub-agentes en vuelo + la cola (PARAR/interrupt/stop).
void LlamaAgentBackend::cancelAllSubs()
{
    m_subQueue = QJsonArray();
    const auto ids = m_subs.keys();
    for (const QString &id : ids) {
        SubAgentRunner *sub = m_subs.take(id);
        if (sub) { disconnect(sub, nullptr, this, nullptr); sub->cancel(); sub->deleteLater(); }
        mergeAndCleanupWorktree(id, false, m_subIsolated.value(id, false));
        m_subWorktree.remove(id); m_subBranch.remove(id);
        m_subIsolated.remove(id); m_subMsgIdx.remove(id);
    }
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
    if (name == QLatin1String("glob"))       return {QStringLiteral("pattern")};
    if (name == QLatin1String("write_file")) return {QStringLiteral("path"), QStringLiteral("content")};
    // edit_file: new_string puede ser vacío (borrar texto) → NO requerirlo aquí
    // (el chequeo de requeridos rechaza strings vacíos).
    if (name == QLatin1String("edit_file")) return {QStringLiteral("path"), QStringLiteral("old_string")};
    if (name == QLatin1String("run_shell"))  return {QStringLiteral("command")};
    if (name == QLatin1String("web_fetch"))  return {QStringLiteral("url")};
    if (name == QLatin1String("web_search")) return {QStringLiteral("query")};
    if (name == QLatin1String("deep_research")) return {QStringLiteral("query")};
    if (name == QLatin1String("search_docs")) return {QStringLiteral("query")};
    if (name == QLatin1String("semantic_search")) return {QStringLiteral("query")};
    if (name == QLatin1String("task"))       return {QStringLiteral("prompt")};
    if (name == QLatin1String("ask_teacher")) return {QStringLiteral("question")};
    return {};   // list_dir, memory: args opcionales
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
            // Finalizar métricas: en modo agente esta es la vía habitual de cierre
            // (la respuesta va seguida de tool calls), así que el cálculo de
            // tiempo/tps debe hacerse aquí igual que en finishTurn/setTyping.
            finalizeMsgMetrics(m, m_genTokens, m_genMs);
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
    if (m.value(QStringLiteral("genStartMs")).toDouble() <= 0.0)
        m[QStringLiteral("genStartMs")] =
            static_cast<double>(QDateTime::currentMSecsSinceEpoch());
    m[QStringLiteral("content")] = content;
    finalizeMsgMetrics(m);
    m_messages[m_curAsstIdx] = m;
    emit messagesChanged();
}

void LlamaAgentBackend::setTyping(bool typing)
{
    if (m_curAsstIdx < 0 || m_curAsstIdx >= m_messages.size()) return;
    QVariantMap m = m_messages[m_curAsstIdx].toMap();
    m[QStringLiteral("typing")] = typing;
    if (!typing)
        finalizeMsgMetrics(m);
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
        finalizeMsgMetrics(m, m_genTokens, m_genMs);
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

    // Turno cerrado → si hay mensajes encolados, enviar el próximo. Async (cola)
    // para no anidar runCompletion dentro del stack del turno que recién terminó.
    if (!m_msgQueue.isEmpty())
        QMetaObject::invokeMethod(this, "flushQueue", Qt::QueuedConnection);
}

// ───────────────────────────── Tools ─────────────────────────────────────
QString LlamaAgentBackend::toolKind(const QString &name)
{
    if (name.startsWith(kMcpPrefix)) {
        const QString bare = name.section(QStringLiteral("__"), -1);
        if (bare == QLatin1String("write_file") || bare == QLatin1String("edit_file")
            || bare == QLatin1String("create_directory") || bare == QLatin1String("move_file"))
            return QStringLiteral("write");
        if (bare == QLatin1String("run_shell") || bare == QLatin1String("shell"))
            return QStringLiteral("shell");
        return QStringLiteral("read");
    }
    if (name == QLatin1String("run_shell")) return QStringLiteral("shell");
    if (name == QLatin1String("task")) return QStringLiteral("task");
    if (name == QLatin1String("write_file") || name == QLatin1String("edit_file"))
        return QStringLiteral("write");
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
    auto intProp = [](const QString &d) {
        return QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                           {QStringLiteral("description"), d}};
    };
    auto boolProp = [](const QString &d) {
        return QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                           {QStringLiteral("description"), d}};
    };
    return QJsonArray{
        fn(QStringLiteral("read_file"),
           QStringLiteral("Lee un archivo de texto del proyecto. Para archivos grandes, "
                          "leé sólo el tramo que necesites con offset/limit en vez de todo."),
           QJsonObject{
               {QStringLiteral("path"), strProp(QStringLiteral("Ruta relativa al proyecto."))},
               {QStringLiteral("offset"), intProp(QStringLiteral("Línea inicial (1-based). Opcional."))},
               {QStringLiteral("limit"), intProp(QStringLiteral("Cantidad de líneas a leer desde offset. Opcional."))}},
           QJsonArray{QStringLiteral("path")}),
        fn(QStringLiteral("list_dir"), QStringLiteral("Lista archivos y carpetas de un directorio."),
           QJsonObject{
               {QStringLiteral("path"), strProp(QStringLiteral("Ruta relativa (vacío = raíz)."))},
               {QStringLiteral("recursive"), boolProp(QStringLiteral("Listar recursivo (ignora node_modules/.git/etc). Default false."))}},
           QJsonArray{}),
        fn(QStringLiteral("grep"),
           QStringLiteral("Busca una EXPRESIÓN REGULAR en los archivos del proyecto (recursivo). "
                          "Ignora node_modules/.git/build/dist/venv/__pycache__ y binarios."),
           QJsonObject{
               {QStringLiteral("pattern"), strProp(QStringLiteral("Regex a buscar (sintaxis estilo PCRE)."))},
               {QStringLiteral("path"), strProp(QStringLiteral("Subdirectorio opcional."))}},
           QJsonArray{QStringLiteral("pattern")}),
        fn(QStringLiteral("glob"),
           QStringLiteral("Lista archivos que matchean un patrón glob (recursivo). Ej: '**/*.qml', "
                          "'src/**/*.cpp', '*.json'. Usá '**' para cualquier subcarpeta."),
           QJsonObject{
               {QStringLiteral("pattern"), strProp(QStringLiteral("Patrón glob. '*'=segmento, '**'=recursivo, '?'=1 char."))},
               {QStringLiteral("path"), strProp(QStringLiteral("Subdirectorio base opcional."))}},
           QJsonArray{QStringLiteral("pattern")}),
        fn(QStringLiteral("write_file"), QStringLiteral("Escribe (crea/sobrescribe) un archivo de texto. "
                          "Para CAMBIOS PUNTUALES en un archivo existente preferí edit_file (mucho más rápido)."),
           QJsonObject{
               {QStringLiteral("path"), strProp(QStringLiteral("Ruta relativa al proyecto."))},
               {QStringLiteral("content"), strProp(QStringLiteral("Contenido completo del archivo."))}},
           QJsonArray{QStringLiteral("path"), QStringLiteral("content")}),
        fn(QStringLiteral("edit_file"),
           QStringLiteral("Edita un archivo existente reemplazando un fragmento EXACTO de texto. "
                          "old_string debe aparecer una sola vez (incluí contexto suficiente) salvo "
                          "que uses replace_all. new_string vacío = borrar el fragmento. Preferí esto "
                          "a reescribir el archivo entero con write_file."),
           QJsonObject{
               {QStringLiteral("path"), strProp(QStringLiteral("Ruta relativa al proyecto."))},
               {QStringLiteral("old_string"), strProp(QStringLiteral("Texto exacto a reemplazar (con su indentación)."))},
               {QStringLiteral("new_string"), strProp(QStringLiteral("Texto nuevo (vacío = borrar)."))},
               {QStringLiteral("replace_all"), boolProp(QStringLiteral("Reemplazar TODAS las apariciones. Default false."))}},
           QJsonArray{QStringLiteral("path"), QStringLiteral("old_string")}),
        fn(QStringLiteral("run_shell"),
           QStringLiteral("Ejecuta un comando de shell en el directorio del proyecto. "
                          "Para builds/tests largos pasá timeout_s alto (default 120, máx 1800)."),
           QJsonObject{
               {QStringLiteral("command"), strProp(QStringLiteral("Comando a ejecutar."))},
               {QStringLiteral("timeout_s"), intProp(QStringLiteral("Timeout en segundos (default 120, máx 1800)."))}},
           QJsonArray{QStringLiteral("command")}),
        fn(QStringLiteral("web_fetch"),
           QStringLiteral("Descarga una URL http(s) y devuelve su texto (HTML limpiado a texto plano). "
                          "Para docs/referencias online."),
           QJsonObject{
               {QStringLiteral("url"), strProp(QStringLiteral("URL completa (http:// o https://)."))}},
           QJsonArray{QStringLiteral("url")}),
        fn(QStringLiteral("web_search"),
           QStringLiteral("Busca en la web y devuelve los mejores resultados (título, URL, snippet). "
                          "Usalo para encontrar páginas/docs relevantes; después usá web_fetch sobre "
                          "las URLs que sirvan. Proveedor: DuckDuckGo (sin key) o SearXNG si está "
                          "configurado el env LLAMACODE_SEARXNG_URL."),
           QJsonObject{
               {QStringLiteral("query"), strProp(QStringLiteral("Términos de búsqueda."))},
               {QStringLiteral("count"), intProp(QStringLiteral("Cantidad de resultados (default 5, máx 10)."))}},
           QJsonArray{QStringLiteral("query")}),
        fn(QStringLiteral("deep_research"),
           QStringLiteral("Investigación web profunda: busca por varios ángulos, descarga las mejores "
                          "páginas y devuelve un DOSSIER (fuentes numeradas + contenido limpio) para que "
                          "VOS lo sintetices citando [n]. Usalo para preguntas que requieren cruzar varias "
                          "fuentes. Pasá 'angles' con sub-consultas distintas para mejor cobertura."),
           QJsonObject{
               {QStringLiteral("query"), strProp(QStringLiteral("Pregunta/tema a investigar."))},
               {QStringLiteral("angles"), QJsonObject{
                   {QStringLiteral("type"), QStringLiteral("array")},
                   {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                   {QStringLiteral("description"), QStringLiteral("Sub-consultas/ángulos distintos (opcional, máx 4).")}}},
               {QStringLiteral("max_pages"), intProp(QStringLiteral("Páginas a descargar (default 5, máx 10)."))}},
           QJsonArray{QStringLiteral("query")}),
        fn(QStringLiteral("search_docs"),
           QStringLiteral("Búsqueda semántica-lite en los archivos del proyecto: rankea fragmentos por "
                          "relevancia a la consulta (keywords) y devuelve los top-k con archivo:línea. "
                          "Mejor que grep para 'dónde se maneja X' cuando no sabés el término exacto. "
                          "Para recuperar contexto del repo."),
           QJsonObject{
               {QStringLiteral("query"), strProp(QStringLiteral("Qué buscás (lenguaje natural o keywords)."))},
               {QStringLiteral("k"), intProp(QStringLiteral("Cantidad de fragmentos (default 5, máx 15)."))},
               {QStringLiteral("path"), strProp(QStringLiteral("Subdirectorio a acotar (opcional)."))}},
           QJsonArray{QStringLiteral("query")}),
        fn(QStringLiteral("semantic_search"),
           QStringLiteral("Búsqueda SEMÁNTICA en los archivos del proyecto usando embeddings del propio "
                          "server (cosine similarity, cache de vectores en SQLite). Encuentra código por "
                          "SIGNIFICADO, no por palabras exactas. Requiere un server con embeddings "
                          "(--embeddings). Si no, usá search_docs. args: query/k/path."),
           QJsonObject{
               {QStringLiteral("query"), strProp(QStringLiteral("Qué buscás (lenguaje natural)."))},
               {QStringLiteral("k"), intProp(QStringLiteral("Cantidad de fragmentos (default 5, máx 15)."))},
               {QStringLiteral("path"), strProp(QStringLiteral("Subdirectorio a acotar (opcional)."))}},
           QJsonArray{QStringLiteral("query")}),
        fn(QStringLiteral("memory"),
           QStringLiteral("Memoria PERSISTENTE del proyecto (sobrevive entre sesiones). "
                          "action='save' anexa un hecho/decisión que querés recordar; "
                          "action='recall' devuelve todo lo guardado. Usala para preferencias del "
                          "usuario, decisiones de diseño y datos no obvios del repo. Recordá al inicio "
                          "de tareas largas."),
           QJsonObject{
               {QStringLiteral("action"), strProp(QStringLiteral("'save' o 'recall' (default 'recall')."))},
               {QStringLiteral("content"), strProp(QStringLiteral("Texto a guardar (sólo para action='save')."))}},
           QJsonArray{}),
        fn(QStringLiteral("ask_teacher"),
           QStringLiteral("Consultá a un modelo MÁS capaz (endpoint aparte) una sub-pregunta difícil: "
                          "diseño, algoritmo tricky, bug que no resolvés. Devuelve su respuesta. "
                          "Requiere la env LLAMACODE_TEACHER_URL configurada. Usalo con criterio "
                          "(es más lento/caro): pasá 'context' con lo mínimo necesario."),
           QJsonObject{
               {QStringLiteral("question"), strProp(QStringLiteral("La pregunta concreta para el modelo maestro."))},
               {QStringLiteral("context"), strProp(QStringLiteral("Contexto relevante (código, error). Opcional."))}},
           QJsonArray{QStringLiteral("question")}),
        fn(QStringLiteral("task"),
           QStringLiteral("Delega una SUBTAREA independiente a un sub-agente autónomo que trabaja en "
                          "una copia aislada del proyecto (git worktree). Devuelve un resumen de lo que "
                          "hizo y sus cambios se mergean al terminar. Podés invocar VARIAS task en el "
                          "mismo turno para correrlas EN PARALELO. Usalo para subtareas separables "
                          "(ej. 'implementá X en módulo A' y 'agregá tests a B'). El prompt debe ser "
                          "autocontenido: el sub-agente NO ve esta conversación."),
           QJsonObject{
               {QStringLiteral("description"), strProp(QStringLiteral("Título corto de la subtarea (para la tarjeta)."))},
               {QStringLiteral("prompt"), strProp(QStringLiteral("Instrucción completa y autocontenida para el sub-agente."))}},
           QJsonArray{QStringLiteral("prompt")})
    };
}

// Metadata de las tools built-in para la UI de habilitar/deshabilitar. approxTokens
// = costo aproximado del schema en el prompt (para estimar ahorro de contexto).
QVariantList LlamaAgentBackend::toolCatalog()
{
    auto mk = [](const char *name, const char *group, const char *desc, int tok) {
        return QVariantMap{
            {QStringLiteral("name"), QString::fromLatin1(name)},
            {QStringLiteral("group"), QString::fromLatin1(group)},
            {QStringLiteral("description"), QString::fromLatin1(desc)},
            {QStringLiteral("approxTokens"), tok}};
    };
    return QVariantList{
        mk("read_file", "Archivos", "Lee un archivo de texto (offset/limit).", 90),
        mk("list_dir",  "Archivos", "Lista archivos y carpetas.", 80),
        mk("glob",      "Archivos", "Lista archivos por patrón glob.", 110),
        mk("grep",      "Búsqueda", "Busca una regex en el proyecto.", 100),
        mk("search_docs", "Búsqueda", "Ranking de fragmentos por keywords (semántica-lite).", 120),
        mk("semantic_search", "Búsqueda", "Búsqueda por significado vía embeddings del server.", 130),
        mk("web_search","Web", "Busca en la web (DuckDuckGo/SearXNG).", 140),
        mk("web_fetch", "Web", "Descarga una URL y devuelve su texto.", 90),
        mk("deep_research", "Web", "Investigación web multi-ángulo (dossier de fuentes).", 200),
        mk("write_file","Código",   "Crea o sobrescribe un archivo.", 90),
        mk("edit_file", "Código",   "Reemplazo exacto en un archivo existente.", 160),
        mk("run_shell", "Código",   "Ejecuta un comando de shell.", 110),
        mk("memory",    "Conocimiento", "Memoria persistente del proyecto (save/recall).", 110),
        mk("ask_teacher", "Multi-Agente", "Consulta a un modelo más capaz (endpoint aparte).", 130),
        mk("task",      "Multi-Agente", "Delega una subtarea a un sub-agente en worktree.", 180),
    };
}

void LlamaAgentBackend::setDisabledTools(const QStringList &names)
{
    m_disabledTools = QSet<QString>(names.cbegin(), names.cend());
}

void LlamaAgentBackend::setTeacherConfig(const QString &url, const QString &model, const QString &key)
{
    m_teacherUrl = url; m_teacherModel = model; m_teacherKey = key;
    if (m_running && m_worker)
        QMetaObject::invokeMethod(m_worker, "setTeacherConfig", Qt::QueuedConnection,
                                  Q_ARG(QString, url), Q_ARG(QString, model), Q_ARG(QString, key));
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
    connect(m_worker, &AgentToolRunner::toolStarted, this, &LlamaAgentBackend::onToolStarted);
    connect(m_worker, &AgentToolRunner::toolOutputChunk, this, &LlamaAgentBackend::onToolOutputChunk);
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
    // Filtra del array las tools deshabilitadas por el usuario (built-in y MCP).
    auto dropDisabled = [this](QJsonArray in) {
        if (m_disabledTools.isEmpty()) return in;
        QJsonArray out;
        for (const QJsonValue &v : in) {
            const QString n = v.toObject().value(QStringLiteral("function"))
                                  .toObject().value(QStringLiteral("name")).toString();
            if (!m_disabledTools.contains(n)) out.append(v);
        }
        return out;
    };

    // PLAN MODE: solo lectura. Excluir write_file/edit_file/run_shell y MCP
    // (no podemos saber si una tool MCP muta), dejar read_file/list_dir/grep/
    // glob/web_fetch.
    if (m_approvalMode == QLatin1String("plan")) {
        static const QSet<QString> planAllowed{
            QStringLiteral("read_file"), QStringLiteral("list_dir"),
            QStringLiteral("grep"), QStringLiteral("glob"), QStringLiteral("web_fetch"),
            QStringLiteral("web_search"), QStringLiteral("deep_research"),
            QStringLiteral("search_docs"), QStringLiteral("semantic_search")};
        QJsonArray ro;
        for (const QJsonValue &v : toolSchemas()) {
            const QString n = v.toObject().value(QStringLiteral("function"))
                                  .toObject().value(QStringLiteral("name")).toString();
            if (planAllowed.contains(n)) ro.append(v);
        }
        return dropDisabled(ro);
    }

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
    return dropDisabled(all);
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
    m_readFingerprints.clear();
    m_checkpoints.clear();
    if (!m_msgQueue.isEmpty()) { m_msgQueue.clear(); emit queueChanged(); }

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
