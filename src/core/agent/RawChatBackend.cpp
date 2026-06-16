#include "RawChatBackend.h"
#include "core/DocumentExtractor.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <QRegularExpression>

static int estimateTokens(const QString &text)
{
    const int n = text.trimmed().size();
    if (n <= 0) return 0;
    return (n + 3) / 4; // aproximación simple chars/4
}

// Quita bloques <think>...</think> (razonamiento) del texto antes de reenviarlo
// al modelo: el thinking es solo para mostrar, no debe contaminar el contexto.
static QString stripThinkForContext(const QString &s)
{
    QString out = s;
    out.remove(QRegularExpression(QStringLiteral("<think>[\\s\\S]*?</think>"),
                                  QRegularExpression::CaseInsensitiveOption));
    // Restos sueltos de turnos viejos.
    out.remove(QRegularExpression(QStringLiteral("</?think>"),
                                  QRegularExpression::CaseInsensitiveOption));
    return out.trimmed();
}

// Para display/historial cuando el usuario desactiva "Pensar": durante streaming
// puede llegar un bloque <think> incompleto, así que se descarta desde la apertura
// hasta que aparezca el cierre. Si el modelo mandó el bloque completo, queda sólo
// la respuesta final.
static QString stripThinkForOutput(const QString &s)
{
    QString out = s;
    out.remove(QRegularExpression(QStringLiteral("<think>[\\s\\S]*?</think>"),
                                  QRegularExpression::CaseInsensitiveOption));
    const QRegularExpression openRe(QStringLiteral("<think\\b[^>]*>"),
                                    QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression closeRe(QStringLiteral("</think>"),
                                     QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch open = openRe.match(out);
    while (open.hasMatch()) {
        const QRegularExpressionMatch close = closeRe.match(out, open.capturedEnd());
        if (!close.hasMatch()) {
            out.truncate(open.capturedStart());
            break;
        }
        out.remove(open.capturedStart(), close.capturedEnd() - open.capturedStart());
        open = openRe.match(out);
    }
    out.remove(closeRe);
    out.remove(openRe);
    return out.trimmed();
}

// Devuelve un data URI base64 si el archivo es imagen soportada por mmproj; "" si no.
static QString imageDataUri(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    QString mime;
    if (ext == "png") mime = "image/png";
    else if (ext == "jpg" || ext == "jpeg") mime = "image/jpeg";
    else if (ext == "webp") mime = "image/webp";
    else if (ext == "gif") mime = "image/gif";
    else if (ext == "bmp") mime = "image/bmp";
    else return {};
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray b64 = f.readAll().toBase64();
    return QStringLiteral("data:%1;base64,%2").arg(mime, QString::fromLatin1(b64));
}

// Lee un archivo de texto/código (UTF-8). Vacío si binario/imagen.
static QString readTextFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QByteArray raw = f.read(2 * 1024 * 1024);  // cap 2MB
    if (raw.contains('\0')) return {};          // binario → no inline
    return QString::fromUtf8(raw);
}

RawChatBackend::RawChatBackend(QObject *parent) : IAgentBackend(parent)
{
    m_nam = new QNetworkAccessManager(this);
}

RawChatBackend::~RawChatBackend()
{
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

void RawChatBackend::start(const AgentContext &ctx)
{
    if (m_running) return;
    m_ctx = ctx;
    m_stopping = false;
    m_running = true;
    loadFromDisk();
    // Sync initial state with UI even when sessions were loaded from disk.
    emit sessionsChanged();
    emit messagesChanged();
    emit logAppended(QStringLiteral("[raw backend ready]\n"));
    if (m_sessionId.isEmpty())
        createSession(ctx.cwd);
    emit runningChanged();
}

void RawChatBackend::stop()
{
    m_stopping = true;
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    saveCurrentMessages();
    persistAll();
    m_running = false;
    m_curAsstIdx = -1;
    emit runningChanged();
}

void RawChatBackend::createSession(const QString &projectDir)
{
    createSession(projectDir, projectDir.isEmpty() ? QStringLiteral("(sin proyecto)") : QFileInfo(projectDir).fileName(), projectDir);
}

void RawChatBackend::createSession(const QString &projectId, const QString &projectName, const QString &projectDir)
{
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString title = QStringLiteral("Nueva sesión");
    const double now = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
    QVariantMap s;
    s[QStringLiteral("id")] = id;
    s[QStringLiteral("title")] = title;
    s[QStringLiteral("created")] = now;
    s[QStringLiteral("updated")] = now;
    s[QStringLiteral("projectId")] = projectId;
    s[QStringLiteral("projectDir")] = projectDir;
    s[QStringLiteral("projectName")] = projectName.isEmpty() ? QStringLiteral("(sin proyecto)") : projectName;
    m_sessions.prepend(s);
    m_sessionMessages.insert(id, {});
    m_sessionId = id;
    m_sessionTitle = title;
    m_projectDir = projectDir;
    m_messages.clear();
    m_curAsstIdx = -1;
    if (!m_msgQueue.isEmpty()) { m_msgQueue.clear(); emit queueChanged(); }
    persistIndex();
    persistSession(id);
    emit sessionsChanged();
    emit messagesChanged();
}

void RawChatBackend::setCurrentSession(const QString &sessionId)
{
    if (sessionId.isEmpty()) return;
    saveCurrentMessages();
    m_sessionId = sessionId;
    m_messages = loadMessagesForSession(sessionId);
    m_curAsstIdx = -1;
    if (!m_msgQueue.isEmpty()) { m_msgQueue.clear(); emit queueChanged(); }
    for (const QVariant &v : std::as_const(m_sessions)) {
        const QVariantMap s = v.toMap();
        if (s.value(QStringLiteral("id")).toString() == sessionId) {
            m_sessionTitle = s.value(QStringLiteral("title")).toString();
            m_projectDir = s.value(QStringLiteral("projectDir")).toString();
            break;
        }
    }
    emit sessionsChanged();
    emit messagesChanged();
}

void RawChatBackend::saveCurrentMessages()
{
    if (!m_sessionId.isEmpty()) {
        m_sessionMessages[m_sessionId] = m_messages;
        persistSession(m_sessionId);
    }
}

QVariantList RawChatBackend::loadMessagesForSession(const QString &sessionId) const
{
    return m_sessionMessages.value(sessionId);
}

void RawChatBackend::newSession()
{
    createSession(m_projectDir);
}

void RawChatBackend::newSessionInProject(const QString &projectDir)
{
    // AppController passes "projectId|projectName|projectDir" for chat.
    const QStringList parts = projectDir.split(QLatin1Char('|'));
    if (parts.size() >= 3)
        createSession(parts[0], parts[1], parts[2]);
    else if (parts.size() == 2)
        createSession(parts[0], parts[1], QString());
    else
        createSession(projectDir);
}

void RawChatBackend::switchSession(const QString &sessionId)
{
    if (sessionId == m_sessionId) return;
    setCurrentSession(sessionId);
}

void RawChatBackend::renameSession(const QString &sessionId, const QString &title)
{
    const QString t = title.trimmed();
    if (sessionId.isEmpty() || t.isEmpty()) return;
    for (int i = 0; i < m_sessions.size(); ++i) {
        QVariantMap s = m_sessions[i].toMap();
        if (s.value(QStringLiteral("id")).toString() == sessionId) {
            s[QStringLiteral("title")] = t;
            m_sessions[i] = s;
            break;
        }
    }
    if (sessionId == m_sessionId) m_sessionTitle = t;
    persistIndex();
    persistSession(sessionId);
    emit sessionsChanged();
}

void RawChatBackend::deleteSession(const QString &sessionId)
{
    if (sessionId.isEmpty()) return;
    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions[i].toMap().value(QStringLiteral("id")).toString() == sessionId) {
            m_sessions.removeAt(i);
            break;
        }
    }
    m_sessionMessages.remove(sessionId);
    removeSessionFile(sessionId);
    if (sessionId == m_sessionId) {
        m_sessionId.clear();
        m_sessionTitle.clear();
        m_projectDir.clear();
        m_messages.clear();
        m_curAsstIdx = -1;
        if (!m_sessions.isEmpty())
            setCurrentSession(m_sessions.first().toMap().value(QStringLiteral("id")).toString());
        else
            emit messagesChanged();
    }
    persistIndex();
    emit sessionsChanged();
}

void RawChatBackend::forkSession(const QString &sessionId)
{
    const QVariantList src = loadMessagesForSession(sessionId);
    const QString srcProjectDir = [this, sessionId]() {
        for (const QVariant &v : std::as_const(m_sessions)) {
            const QVariantMap s = v.toMap();
            if (s.value(QStringLiteral("id")).toString() == sessionId)
                return s.value(QStringLiteral("projectDir")).toString();
        }
        return QString();
    }();
    createSession(srcProjectDir);
    m_messages = src;
    saveCurrentMessages();
    persistIndex();
    emit messagesChanged();
}

void RawChatBackend::refreshSessions()
{
    emit sessionsChanged();
}

void RawChatBackend::rollbackToMessage(int msgIndex)
{
    if (m_reply) { emit errorOccurred(QStringLiteral("No se puede rebobinar con una respuesta en curso.")); return; }
    if (msgIndex < 0 || msgIndex >= m_messages.size()) return;
    // Descarta este mensaje de usuario y todo lo posterior. El request se arma
    // de m_messages, así que truncar = rebobinar el contexto.
    while (m_messages.size() > msgIndex) m_messages.removeLast();
    m_curAsstIdx = -1;
    saveCurrentMessages();
    emit messagesChanged();
}

void RawChatBackend::editMessage(int msgIndex, const QString &newText)
{
    if (m_reply) { emit errorOccurred(QStringLiteral("No se puede editar con una respuesta en curso.")); return; }
    if (msgIndex < 0 || msgIndex >= m_messages.size()) return;
    // Conserva este mensaje con el texto editado y descarta todo lo posterior.
    QVariantMap m = m_messages[msgIndex].toMap();
    m[QStringLiteral("content")] = newText;
    m[QStringLiteral("typing")] = false;
    m_messages[msgIndex] = m;
    while (m_messages.size() > msgIndex + 1) m_messages.removeLast();
    m_curAsstIdx = -1;
    saveCurrentMessages();
    emit messagesChanged();
}

void RawChatBackend::sendMessage(const QString &text)
{
    const QString trimmed = text.trimmed();
    const QStringList attachments = m_pendingAttachments;
    m_pendingAttachments.clear();
    if (!m_running || (trimmed.isEmpty() && attachments.isEmpty())) return;
    if (m_reply) {
        emit errorOccurred(QStringLiteral("Hay una respuesta en curso."));
        return;
    }
    if (m_sessionId.isEmpty())
        createSession(m_projectDir);

    // Contenido para mostrar: texto + chips de adjuntos.
    QString display = trimmed;
    for (const QString &p : attachments)
        display += QStringLiteral("\n📎 ") + QFileInfo(p).fileName();

    QVariantMap umMap{
        {QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"), display},
        {QStringLiteral("typing"), false}
    };
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    umMap[QStringLiteral("createdAt")] = static_cast<double>(nowMs);
    umMap[QStringLiteral("completedAt")] = static_cast<double>(nowMs);
    umMap[QStringLiteral("elapsedMs")] = 0;
    umMap[QStringLiteral("tokens")] = estimateTokens(display);
    umMap[QStringLiteral("tps")] = 0.0;
    if (!attachments.isEmpty()) {
        umMap[QStringLiteral("attachments")] = attachments;
        umMap[QStringLiteral("rawText")] = trimmed;
    }
    AgentMessage am; am.role = QStringLiteral("assistant"); am.typing = true;
    QVariantMap amMap = am.toMap();
    amMap[QStringLiteral("createdAt")] = static_cast<double>(nowMs);
    amMap[QStringLiteral("elapsedMs")] = 0;
    amMap[QStringLiteral("tokens")] = 0;
    amMap[QStringLiteral("tps")] = 0.0;
    m_messages.append(umMap);
    m_messages.append(amMap);
    m_curAsstIdx = m_messages.size() - 1;
    for (int i = 0; i < m_sessions.size(); ++i) {
        QVariantMap s = m_sessions[i].toMap();
        if (s.value(QStringLiteral("id")).toString() == m_sessionId) {
            s[QStringLiteral("updated")] = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
            m_sessions[i] = s;
            break;
        }
    }
    saveCurrentMessages();
    emit sessionsChanged();
    emit messagesChanged();

    QJsonArray reqMsgs;
    if (!m_thinkingEnabled) {
        reqMsgs.append(QJsonObject{
            {QStringLiteral("role"), QStringLiteral("system")},
            {QStringLiteral("content"),
             QStringLiteral("Responde solo con la respuesta final. No incluyas razonamiento interno ni etiquetas <think> o </think>.")}
        });
    }
    int lastUserIdx = -1;
    for (int i = 0; i < m_messages.size(); ++i) {
        if (m_messages[i].toMap().value(QStringLiteral("role")).toString() == QLatin1String("user"))
            lastUserIdx = i;
    }
    for (int i = 0; i < m_messages.size(); ++i) {
        const QVariantMap m = m_messages[i].toMap();
        const QString role = m.value(QStringLiteral("role")).toString();
        if (role != QLatin1String("user") && role != QLatin1String("assistant")
            && role != QLatin1String("system")) {
            continue;
        }
        const QStringList atts = m.value(QStringLiteral("attachments")).toStringList();
        if (role == QLatin1String("user") && !atts.isEmpty()) {
            // Mensaje multimodal: texto + documentos inline + imágenes (mmproj).
            QString textPart = m.value(QStringLiteral("rawText")).toString();
            if (!m_thinkingEnabled && i == lastUserIdx)
                textPart += QStringLiteral("\n\n/no_think");
            QJsonArray images;
            for (const QString &path : atts) {
                const QString uri = imageDataUri(path);
                if (!uri.isEmpty()) {
                    images.append(QJsonObject{
                        {QStringLiteral("type"), QStringLiteral("image_url")},
                        {QStringLiteral("image_url"), QJsonObject{{QStringLiteral("url"), uri}}}
                    });
                } else {
                    // Documento (txt/pdf/docx/xlsx/pptx/html/…) → markdown inline
                    // vía DocumentExtractor (markitdown para formatos ricos).
                    QString exErr;
                    const QString doc = DocumentExtractor::extract(path, &exErr);
                    const QString fn = QFileInfo(path).fileName();
                    if (!doc.isEmpty())
                        textPart += QStringLiteral("\n\n--- %1 ---\n%2").arg(fn, doc);
                    else
                        textPart += QStringLiteral("\n\n[adjunto %1: %2]")
                                        .arg(fn, exErr.isEmpty()
                                                     ? QStringLiteral("sin texto extraíble")
                                                     : exErr);
                }
            }
            QJsonArray parts;
            if (!textPart.trimmed().isEmpty())
                parts.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                         {QStringLiteral("text"), textPart}});
            for (const QJsonValue &iv : images) parts.append(iv);
            reqMsgs.append(QJsonObject{{QStringLiteral("role"), role}, {QStringLiteral("content"), parts}});
            continue;
        }
        // Reenviar SIN el bloque <think>: si no, el modelo se ceba y sigue pensando.
        QString content = m.value(QStringLiteral("content")).toString();
        if (role == QLatin1String("assistant"))
            content = stripThinkForContext(content);
        else if (!m_thinkingEnabled && role == QLatin1String("user") && i == lastUserIdx)
            content += QStringLiteral("\n\n/no_think");
        if (content.trimmed().isEmpty()) continue;
        reqMsgs.append(QJsonObject{
            {QStringLiteral("role"), role},
            {QStringLiteral("content"), content}
        });
    }

    QNetworkRequest req(QUrl(m_ctx.serverBaseUrl + QStringLiteral("/v1/chat/completions")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    QJsonObject payload{
        {QStringLiteral("model"), m_ctx.modelId.isEmpty() ? QStringLiteral("raw") : m_ctx.modelId},
        {QStringLiteral("messages"), reqMsgs},
        {QStringLiteral("stream"), true}
    };
    // Control de thinking (llama.cpp).
    //  - reasoning_budget: per-request, NO depende del chat template. 0 = sin thinking, -1 = ilimitado.
    //  - chat_template_kwargs.enable_thinking: switch oficial Qwen3 (requiere --jinja + template que lo soporte).
    payload.insert(QStringLiteral("reasoning_budget"), m_thinkingEnabled ? -1 : 0);
    QJsonObject tmplKw;
    tmplKw.insert(QStringLiteral("enable_thinking"), m_thinkingEnabled);
    payload.insert(QStringLiteral("chat_template_kwargs"), tmplKw);

    m_sseBuf.clear();
    m_reasonBuf.clear();
    m_answerBuf.clear();
    m_reply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
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
            const QJsonArray choices = obj.value(QStringLiteral("choices")).toArray();
            if (choices.isEmpty()) continue;
            const QJsonObject delta = choices.first().toObject().value(QStringLiteral("delta")).toObject();
            const QString reasoning = delta.value(QStringLiteral("reasoning_content")).toString();
            const QString chunk = delta.value(QStringLiteral("content")).toString();
            if (reasoning.isEmpty() && chunk.isEmpty()) continue;
            if (m_thinkingEnabled)
                m_reasonBuf += reasoning;
            m_answerBuf += chunk;
            // El thinking (reasoning_content) se envuelve en <think> para que la UI lo muestre.
            QString full;
            if (m_thinkingEnabled && !m_reasonBuf.isEmpty())
                full = QStringLiteral("<think>") + m_reasonBuf + QStringLiteral("</think>\n");
            full += m_thinkingEnabled ? m_answerBuf : stripThinkForOutput(m_answerBuf);
            if (m_curAsstIdx >= 0 && m_curAsstIdx < m_messages.size()) {
                QVariantMap asst = m_messages[m_curAsstIdx].toMap();
                asst[QStringLiteral("content")] = full;
                const qint64 startedAt = static_cast<qint64>(asst.value(QStringLiteral("createdAt")).toDouble());
                const qint64 elapsedMs = qMax<qint64>(0, QDateTime::currentMSecsSinceEpoch() - startedAt);
                const int toks = estimateTokens(full);
                asst[QStringLiteral("tokens")] = toks;
                asst[QStringLiteral("elapsedMs")] = static_cast<int>(elapsedMs);
                asst[QStringLiteral("tps")] = (elapsedMs > 0 && toks > 0)
                    ? (1000.0 * static_cast<double>(toks) / static_cast<double>(elapsedMs))
                    : 0.0;
                m_messages[m_curAsstIdx] = asst;
                // NO guardar a disco por token (persistSession serializa + escribe
                // → congelaba el hilo GUI). Se guarda al terminar. El emit se
                // throttlea para no reconstruir todos los delegates por token.
                if (!m_streamEmitTimer) {
                    m_streamEmitTimer = new QTimer(this);
                    m_streamEmitTimer->setInterval(40);
                    m_streamEmitTimer->setSingleShot(true);
                    connect(m_streamEmitTimer, &QTimer::timeout, this, [this]() { emit messagesChanged(); });
                }
                if (!m_streamEmitTimer->isActive())
                    m_streamEmitTimer->start();
            }
        }
    });

    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        if (!m_reply) return;
        if (m_streamEmitTimer) m_streamEmitTimer->stop();  // el emit final va abajo
        const bool ok = m_reply->error() == QNetworkReply::NoError;
        const QString err = m_reply->errorString();
        m_reply->deleteLater();
        m_reply = nullptr;
        if (m_curAsstIdx >= 0 && m_curAsstIdx < m_messages.size()) {
            QVariantMap asst = m_messages[m_curAsstIdx].toMap();
            asst[QStringLiteral("typing")] = false;
            if (!ok && asst.value(QStringLiteral("content")).toString().isEmpty())
                asst[QStringLiteral("content")] = QStringLiteral("[error: %1]").arg(err);
            if (!m_thinkingEnabled)
                asst[QStringLiteral("content")] = stripThinkForOutput(asst.value(QStringLiteral("content")).toString());
            const qint64 doneAt = QDateTime::currentMSecsSinceEpoch();
            const qint64 startedAt = static_cast<qint64>(asst.value(QStringLiteral("createdAt")).toDouble());
            const qint64 elapsedMs = qMax<qint64>(0, doneAt - startedAt);
            const QString finalText = asst.value(QStringLiteral("content")).toString();
            const int toks = estimateTokens(finalText);
            asst[QStringLiteral("completedAt")] = static_cast<double>(doneAt);
            asst[QStringLiteral("tokens")] = toks;
            asst[QStringLiteral("elapsedMs")] = static_cast<int>(elapsedMs);
            asst[QStringLiteral("tps")] = (elapsedMs > 0 && toks > 0)
                ? (1000.0 * static_cast<double>(toks) / static_cast<double>(elapsedMs))
                : 0.0;
            m_messages[m_curAsstIdx] = asst;
            saveCurrentMessages();
            emit messagesChanged();
        }
        if (!ok && !m_stopping)
            emit errorOccurred(QStringLiteral("raw chat error: %1").arg(err));
        m_curAsstIdx = -1;
        // Respuesta cerrada → enviar el próximo encolado (async para no anidar).
        if (!m_msgQueue.isEmpty())
            QMetaObject::invokeMethod(this, "flushQueue", Qt::QueuedConnection);
    });
}

void RawChatBackend::cancelGeneration()
{
    if (!m_reply) return;
    m_reply->abort();
    m_reply->deleteLater();
    m_reply = nullptr;
    if (m_curAsstIdx >= 0 && m_curAsstIdx < m_messages.size()) {
        QVariantMap asst = m_messages[m_curAsstIdx].toMap();
        asst[QStringLiteral("typing")] = false;
        const qint64 doneAt = QDateTime::currentMSecsSinceEpoch();
        const qint64 startedAt = static_cast<qint64>(asst.value(QStringLiteral("createdAt")).toDouble());
        const qint64 elapsedMs = qMax<qint64>(0, doneAt - startedAt);
        const QString finalText = asst.value(QStringLiteral("content")).toString();
        const int toks = estimateTokens(finalText);
        asst[QStringLiteral("completedAt")] = static_cast<double>(doneAt);
        asst[QStringLiteral("tokens")] = toks;
        asst[QStringLiteral("elapsedMs")] = static_cast<int>(elapsedMs);
        asst[QStringLiteral("tps")] = (elapsedMs > 0 && toks > 0)
            ? (1000.0 * static_cast<double>(toks) / static_cast<double>(elapsedMs))
            : 0.0;
        m_messages[m_curAsstIdx] = asst;
        saveCurrentMessages();
        emit messagesChanged();
    }
    m_curAsstIdx = -1;
    if (!m_msgQueue.isEmpty()) { m_msgQueue.clear(); emit queueChanged(); }
}

// Steering: cortar la respuesta en curso y enviar el mensaje nuevo de inmediato.
void RawChatBackend::steerMessage(const QString &text)
{
    const QString t = text.trimmed();
    if (!m_running || t.isEmpty()) return;
    if (m_reply) cancelGeneration();
    sendMessage(t);
}

// Cola: si hay una respuesta en curso, guardar y enviar al terminar; si no, ya.
void RawChatBackend::queueMessage(const QString &text)
{
    const QString t = text.trimmed();
    if (!m_running || t.isEmpty()) return;
    if (!m_reply) { sendMessage(t); return; }
    m_msgQueue << t;
    emit queueChanged();
    emit logAppended(QStringLiteral("[encolado (%1 en cola)]\n").arg(m_msgQueue.size()));
}

void RawChatBackend::flushQueue()
{
    if (!m_running || m_reply || m_msgQueue.isEmpty()) return;
    const QString t = m_msgQueue.takeFirst();
    emit queueChanged();
    sendMessage(t);
}

void RawChatBackend::clearQueue()
{
    if (m_msgQueue.isEmpty()) return;
    m_msgQueue.clear();
    emit queueChanged();
}

bool RawChatBackend::updateSessionProject(const QString &sessionId, const QString &projectId,
                                          const QString &projectName, const QString &projectDir)
{
    if (sessionId.isEmpty()) return false;
    for (int i = 0; i < m_sessions.size(); ++i) {
        QVariantMap s = m_sessions[i].toMap();
        if (s.value(QStringLiteral("id")).toString() != sessionId) continue;
        s[QStringLiteral("projectId")] = projectId;
        s[QStringLiteral("projectName")] = projectName;
        if (!projectDir.isEmpty())
            s[QStringLiteral("projectDir")] = projectDir;
        m_sessions[i] = s;
        if (sessionId == m_sessionId) {
            m_projectDir = s.value(QStringLiteral("projectDir")).toString();
        }
        persistIndex();
        emit sessionsChanged();
        return true;
    }
    return false;
}

bool RawChatBackend::renameProject(const QString &oldName, const QString &newName)
{
    if (oldName.isEmpty() || newName.isEmpty() || oldName == newName) return false;
    bool changed = false;
    for (int i = 0; i < m_sessions.size(); ++i) {
        QVariantMap s = m_sessions[i].toMap();
        if (s.value(QStringLiteral("projectName")).toString() != oldName) continue;
        s[QStringLiteral("projectName")] = newName;
        m_sessions[i] = s;
        changed = true;
    }
    if (changed) {
        persistIndex();
        emit sessionsChanged();
    }
    return changed;
}

QString RawChatBackend::storageDir() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/chat_raw");
    QDir().mkpath(dir);
    return dir;
}

QString RawChatBackend::sessionFilePath(const QString &sessionId) const
{
    return storageDir() + QStringLiteral("/") + sessionId + QStringLiteral(".json");
}

void RawChatBackend::loadFromDisk()
{
    if (!m_sessions.isEmpty()) return;

    QFile f(storageDir() + QStringLiteral("/index.json"));
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    f.close();

    for (const QJsonValue &v : arr) {
        const QVariantMap s = v.toObject().toVariantMap();
        const QString sid = s.value(QStringLiteral("id")).toString();
        if (sid.isEmpty()) continue;
        m_sessions.append(s);

        QFile sf(sessionFilePath(sid));
        QVariantList msgs;
        if (sf.open(QIODevice::ReadOnly)) {
            const QJsonObject obj = QJsonDocument::fromJson(sf.readAll()).object();
            sf.close();
            const QJsonArray m = obj.value(QStringLiteral("messages")).toArray();
            for (const QJsonValue &mv : m) {
                const QJsonObject mo = mv.toObject();
                QVariantMap mm = mo.toVariantMap();
                mm[QStringLiteral("typing")] = false;
                msgs.append(mm);
            }
        }
        m_sessionMessages.insert(sid, msgs);
    }

    if (!m_sessions.isEmpty()) {
        const QVariantMap s0 = m_sessions.first().toMap();
        m_sessionId = s0.value(QStringLiteral("id")).toString();
        m_sessionTitle = s0.value(QStringLiteral("title")).toString();
        m_projectDir = s0.value(QStringLiteral("projectDir")).toString();
        m_messages = m_sessionMessages.value(m_sessionId);
    }
}

void RawChatBackend::persistIndex() const
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

void RawChatBackend::persistSession(const QString &sessionId) const
{
    if (sessionId.isEmpty()) return;
    QVariantMap sess;
    for (const QVariant &v : m_sessions) {
        const QVariantMap s = v.toMap();
        if (s.value(QStringLiteral("id")).toString() == sessionId) {
            sess = s;
            break;
        }
    }
    QJsonArray msgs;
    const QVariantList ml = m_sessionMessages.value(sessionId);
    for (const QVariant &mv : ml) {
        QVariantMap m = mv.toMap();
        m.remove(QStringLiteral("typing"));
        msgs.append(QJsonObject::fromVariantMap(m));
    }
    QJsonObject obj;
    obj[QStringLiteral("id")] = sessionId;
    obj[QStringLiteral("title")] = sess.value(QStringLiteral("title")).toString();
    obj[QStringLiteral("messages")] = msgs;
    QFile f(sessionFilePath(sessionId));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(obj).toJson());
        f.close();
    }
}

void RawChatBackend::removeSessionFile(const QString &sessionId) const
{
    if (sessionId.isEmpty()) return;
    QFile::remove(sessionFilePath(sessionId));
}

void RawChatBackend::persistAll() const
{
    persistIndex();
    for (const QVariant &v : m_sessions)
        persistSession(v.toMap().value(QStringLiteral("id")).toString());
}
