#include "ControlApi.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QMetaObject>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QUrlQuery>
#include <QVariant>

ControlApi::ControlApi(QObject *target, QObject *parent)
    : QObject(parent), m_target(target) {}

bool ControlApi::start(quint16 port, const QHostAddress &addr)
{
    if (port == 0) return false;
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &ControlApi::onNewConnection);
    if (!m_server->listen(addr, port)) {
        qWarning("ControlApi: no se pudo escuchar en %s:%u", qPrintable(addr.toString()), port);
        return false;
    }
    qInfo("ControlApi: escuchando en http://%s:%u", qPrintable(addr.toString()), port);
    return true;
}

void ControlApi::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *sock = m_server->nextPendingConnection();
        connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
            // Acumular hasta tener headers completos + body (Content-Length).
            sock->setProperty("buf", sock->property("buf").toByteArray() + sock->readAll());
            QByteArray data = sock->property("buf").toByteArray();
            const int hdrEnd = data.indexOf("\r\n\r\n");
            if (hdrEnd < 0) return;
            const QByteArray headers = data.left(hdrEnd);
            int contentLen = 0;
            const QList<QByteArray> lines = headers.split('\n');
            for (const QByteArray &l : lines) {
                if (l.toLower().startsWith("content-length:"))
                    contentLen = l.mid(l.indexOf(':') + 1).trimmed().toInt();
            }
            const QByteArray body = data.mid(hdrEnd + 4);
            if (body.size() < contentLen) return;   // esperar resto

            const QByteArray reqLine = lines.isEmpty() ? QByteArray() : lines.first().trimmed();
            const QList<QByteArray> parts = reqLine.split(' ');
            if (parts.size() < 2) { sock->disconnectFromHost(); return; }
            const QByteArray method = parts.at(0);
            const QString path = QString::fromUtf8(parts.at(1));
            handleRequest(sock, method, path, body.left(contentLen));
        });
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
    }
}

static QByteArray httpResponse(int code, const QByteArray &json)
{
    const char *reason = code == 200 ? "OK" : (code == 404 ? "Not Found" : "Bad Request");
    QByteArray r = "HTTP/1.1 " + QByteArray::number(code) + " " + reason + "\r\n";
    r += "Content-Type: application/json\r\n";
    r += "Access-Control-Allow-Origin: *\r\n";
    r += "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n";
    r += json;
    return r;
}

void ControlApi::handleRequest(QTcpSocket *sock, const QByteArray &method,
                               const QString &path, const QByteArray &body)
{
    QByteArray resp;
    const QString p = path.section('?', 0, 0);
    const QUrlQuery query(path.section('?', 1));
    // target: query param (GET) o campo JSON (POST). Ruta de QObject* hijos.
    QString targetPath = query.queryItemValue(QStringLiteral("target"));
    if (method == "POST" && targetPath.isEmpty())
        targetPath = QJsonDocument::fromJson(body).object()
                         .value(QStringLiteral("target")).toString();

    if (p == QLatin1String("/health")) {
        resp = httpResponse(200, "{\"ok\":true}");
    } else if (p == QLatin1String("/methods")) {
        resp = httpResponse(200, jsonMethods(targetPath));
    } else if (p == QLatin1String("/prop")) {
        QObject *t = resolveTarget(targetPath);
        resp = t ? httpResponse(200, jsonProperty(t, query.queryItemValue(QStringLiteral("name"))))
                 : httpResponse(400, "{\"error\":\"target desconocido\"}");
    } else if (p == QLatin1String("/setprop") && method == "POST") {
        bool ok = false;
        QObject *t = resolveTarget(targetPath);
        const QByteArray out = t ? setProperty(t, body, &ok)
                                 : QByteArray("{\"error\":\"target desconocido\"}");
        resp = httpResponse(ok ? 200 : 400, out);
    } else if (p == QLatin1String("/invoke") && method == "POST") {
        bool ok = false;
        QObject *t = resolveTarget(targetPath);
        const QByteArray out = t ? invokeMethod(t, body, &ok)
                                 : QByteArray("{\"error\":\"target desconocido\"}");
        resp = httpResponse(ok ? 200 : 400, out);
    } else {
        resp = httpResponse(404, "{\"error\":\"unknown endpoint\"}");
    }

    sock->write(resp);
    sock->flush();
    sock->disconnectFromHost();
}

QObject *ControlApi::resolveTarget(const QString &path) const
{
    QObject *cur = m_target;
    if (path.isEmpty()) return cur;
    const QStringList segs = path.split('.', Qt::SkipEmptyParts);
    for (const QString &seg : segs) {
        if (!cur) return nullptr;
        const QVariant v = cur->property(seg.toUtf8().constData());
        QObject *child = v.value<QObject *>();
        if (!child) return nullptr;
        cur = child;
    }
    return cur;
}

// Lista las propiedades que son QObject* (sub-targets navegables headless).
static QJsonArray childTargets(QObject *obj)
{
    QJsonArray out;
    const QMetaObject *mo = obj->metaObject();
    for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
        const QMetaProperty pr = mo->property(i);
        const QVariant v = obj->property(pr.name());
        if (v.value<QObject *>())
            out.append(QString::fromUtf8(pr.name()));
    }
    return out;
}

QByteArray ControlApi::jsonMethods(const QString &targetPath) const
{
    QObject *target = resolveTarget(targetPath);
    if (!target)
        return "{\"error\":\"target desconocido\"}";
    const QMetaObject *mo = target->metaObject();
    QJsonArray methods;
    for (int i = mo->methodOffset(); i < mo->methodCount(); ++i) {
        const QMetaMethod m = mo->method(i);
        if (m.methodType() != QMetaMethod::Method && m.methodType() != QMetaMethod::Slot)
            continue;
        if (m.access() != QMetaMethod::Public) continue;
        QJsonArray params;
        const QList<QByteArray> names = m.parameterNames();
        for (int j = 0; j < m.parameterCount(); ++j)
            params.append(QString::fromUtf8(m.parameterTypeName(j)) + QStringLiteral(" ")
                          + QString::fromUtf8(j < names.size() ? names.at(j) : QByteArray()));
        methods.append(QJsonObject{
            {QStringLiteral("name"), QString::fromUtf8(m.name())},
            {QStringLiteral("returns"), QString::fromUtf8(m.typeName())},
            {QStringLiteral("params"), params}
        });
    }
    QJsonArray props;
    for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
        const QMetaProperty pr = mo->property(i);
        props.append(QJsonObject{
            {QStringLiteral("name"), QString::fromUtf8(pr.name())},
            {QStringLiteral("type"), QString::fromUtf8(pr.typeName())}
        });
    }
    return QJsonDocument(QJsonObject{
        {QStringLiteral("methods"), methods},
        {QStringLiteral("properties"), props},
        {QStringLiteral("targets"), childTargets(target)}
    }).toJson(QJsonDocument::Compact);
}

QByteArray ControlApi::jsonProperty(QObject *target, const QString &name) const
{
    const QVariant v = target->property(name.toUtf8().constData());
    if (!v.isValid())
        return "{\"error\":\"propiedad desconocida: " + name.toUtf8() + "\"}";
    const QJsonValue jv = QJsonValue::fromVariant(v);
    return QJsonDocument(QJsonObject{
        {QStringLiteral("name"), name},
        {QStringLiteral("value"), jv}
    }).toJson(QJsonDocument::Compact);
}

QByteArray ControlApi::setProperty(QObject *target, const QByteArray &jsonBody, bool *ok) const
{
    if (ok) *ok = false;
    const QJsonObject req = QJsonDocument::fromJson(jsonBody).object();
    const QString name = req.value(QStringLiteral("name")).toString();
    if (name.isEmpty()) return "{\"error\":\"falta 'name'\"}";
    const QVariant val = req.value(QStringLiteral("value")).toVariant();
    const bool done = target->setProperty(name.toUtf8().constData(), val);
    if (!done)
        return "{\"error\":\"propiedad no escribible o desconocida: " + name.toUtf8() + "\"}";
    if (ok) *ok = true;
    return "{\"ok\":true}";
}

QByteArray ControlApi::invokeMethod(QObject *targetObj, const QByteArray &jsonBody, bool *ok) const
{
    if (ok) *ok = false;
    const QJsonObject req = QJsonDocument::fromJson(jsonBody).object();
    const QString name = req.value(QStringLiteral("method")).toString();
    const QJsonArray args = req.value(QStringLiteral("args")).toArray();
    if (name.isEmpty())
        return "{\"error\":\"falta 'method'\"}";

    const QMetaObject *mo = targetObj->metaObject();
    QMetaMethod target;
    for (int i = 0; i < mo->methodCount(); ++i) {
        const QMetaMethod m = mo->method(i);
        if (m.access() != QMetaMethod::Public) continue;
        if (QString::fromUtf8(m.name()) != name) continue;
        if (m.parameterCount() != args.size()) continue;
        target = m;
        break;
    }
    if (!target.isValid())
        return "{\"error\":\"método no encontrado o aridad incorrecta: " + name.toUtf8() + "\"}";

    // Convertir args JSON al tipo de cada parámetro; mantener vivos los QVariant.
    QList<QVariant> store;
    store.reserve(args.size());
    QVariantList genArgsHolder;
    for (int i = 0; i < args.size(); ++i) {
        QVariant v = args.at(i).toVariant();
        const int t = target.parameterType(i);
        if (t != QMetaType::UnknownType && v.metaType().id() != t)
            v.convert(QMetaType(t));
        store.append(v);
    }

    // IMPORTANTE: mantener vivos los nombres de tipo; QGenericArgument guarda el
    // puntero const char*, no copia. Un QByteArray temporal dejaría un dangling.
    QList<QByteArray> typeNames;
    typeNames.reserve(store.size());
    for (int i = 0; i < store.size(); ++i)
        typeNames.append(target.parameterTypeName(i));

    QGenericArgument ga[8];
    for (int i = 0; i < store.size() && i < 8; ++i)
        ga[i] = QGenericArgument(typeNames[i].constData(), store[i].constData());

    // Tipo de retorno.
    const int retType = target.returnType();
    QVariant ret;
    bool invoked = false;
    if (retType == QMetaType::Void || retType == QMetaType::UnknownType) {
        invoked = target.invoke(targetObj, Qt::DirectConnection,
                                ga[0], ga[1], ga[2], ga[3], ga[4], ga[5], ga[6], ga[7]);
    } else {
        ret = QVariant(QMetaType(retType));
        const QByteArray retName = target.typeName();
        QGenericReturnArgument rga(retName.constData(), ret.data());
        invoked = target.invoke(targetObj, Qt::DirectConnection, rga,
                                ga[0], ga[1], ga[2], ga[3], ga[4], ga[5], ga[6], ga[7]);
    }
    if (!invoked)
        return "{\"error\":\"invoke falló\"}";

    if (ok) *ok = true;
    QJsonObject out{{QStringLiteral("ok"), true}};
    if (ret.isValid()) out[QStringLiteral("result")] = QJsonValue::fromVariant(ret);
    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}
