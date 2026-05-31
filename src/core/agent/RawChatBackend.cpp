#include "RawChatBackend.h"
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

void RawChatBackend::sendMessage(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (!m_running || trimmed.isEmpty()) return;
    if (m_reply) {
        emit errorOccurred(QStringLiteral("Hay una respuesta en curso."));
        return;
    }
    if (m_sessionId.isEmpty())
        createSession(m_projectDir);

    AgentMessage um; um.role = QStringLiteral("user"); um.content = trimmed;
    AgentMessage am; am.role = QStringLiteral("assistant"); am.typing = true;
    m_messages.append(um.toMap());
    m_messages.append(am.toMap());
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
    for (const QVariant &v : std::as_const(m_messages)) {
        const QVariantMap m = v.toMap();
        const QString role = m.value(QStringLiteral("role")).toString();
        if (role != QLatin1String("user") && role != QLatin1String("assistant")
            && role != QLatin1String("system")) {
            continue;
        }
        reqMsgs.append(QJsonObject{
            {QStringLiteral("role"), role},
            {QStringLiteral("content"), m.value(QStringLiteral("content")).toString()}
        });
    }

    QNetworkRequest req(QUrl(m_ctx.serverBaseUrl + QStringLiteral("/v1/chat/completions")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    QJsonObject payload{
        {QStringLiteral("model"), m_ctx.modelId.isEmpty() ? QStringLiteral("raw") : m_ctx.modelId},
        {QStringLiteral("messages"), reqMsgs},
        {QStringLiteral("stream"), true}
    };
    // Native thinking toggle. Different backends/templates read different keys.
    payload.insert(QStringLiteral("enable_thinking"), m_thinkingEnabled);
    payload.insert(QStringLiteral("thinking"), m_thinkingEnabled);
    QJsonObject reasoningObj;
    reasoningObj.insert(QStringLiteral("enabled"), m_thinkingEnabled);
    payload.insert(QStringLiteral("reasoning"), reasoningObj);
    QJsonObject tmplKw;
    tmplKw.insert(QStringLiteral("enable_thinking"), m_thinkingEnabled);
    payload.insert(QStringLiteral("chat_template_kwargs"), tmplKw);

    m_sseBuf.clear();
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
            const QString chunk = delta.value(QStringLiteral("content")).toString();
            if (chunk.isEmpty()) continue;
            if (m_curAsstIdx >= 0 && m_curAsstIdx < m_messages.size()) {
                QVariantMap asst = m_messages[m_curAsstIdx].toMap();
                asst[QStringLiteral("content")] = asst.value(QStringLiteral("content")).toString() + chunk;
                m_messages[m_curAsstIdx] = asst;
                saveCurrentMessages();
                emit messagesChanged();
            }
        }
    });

    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        if (!m_reply) return;
        const bool ok = m_reply->error() == QNetworkReply::NoError;
        const QString err = m_reply->errorString();
        m_reply->deleteLater();
        m_reply = nullptr;
        if (m_curAsstIdx >= 0 && m_curAsstIdx < m_messages.size()) {
            QVariantMap asst = m_messages[m_curAsstIdx].toMap();
            asst[QStringLiteral("typing")] = false;
            if (!ok && asst.value(QStringLiteral("content")).toString().isEmpty())
                asst[QStringLiteral("content")] = QStringLiteral("[error: %1]").arg(err);
            m_messages[m_curAsstIdx] = asst;
            saveCurrentMessages();
            emit messagesChanged();
        }
        if (!ok && !m_stopping)
            emit errorOccurred(QStringLiteral("raw chat error: %1").arg(err));
        m_curAsstIdx = -1;
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
        m_messages[m_curAsstIdx] = asst;
        saveCurrentMessages();
        emit messagesChanged();
    }
    m_curAsstIdx = -1;
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
                msgs.append(QVariantMap{
                    {QStringLiteral("role"), mo.value(QStringLiteral("role")).toString()},
                    {QStringLiteral("content"), mo.value(QStringLiteral("content")).toString()},
                    {QStringLiteral("typing"), false}
                });
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
        const QVariantMap m = mv.toMap();
        msgs.append(QJsonObject{
            {QStringLiteral("role"), m.value(QStringLiteral("role")).toString()},
            {QStringLiteral("content"), m.value(QStringLiteral("content")).toString()}
        });
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
