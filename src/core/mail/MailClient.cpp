#include "MailClient.h"

#include <QByteArray>
#include <QDateTime>
#include <QRegularExpression>
#include <QSslSocket>
#include <QTcpSocket>
#include <QUuid>

namespace {

constexpr int kConnectTimeoutMs = 15000;
constexpr int kReplyTimeoutMs   = 20000;

// Lee líneas del socket hasta timeout o desconexión. Devuelve lo acumulado.
QByteArray readAvailable(QAbstractSocket *sock, int timeoutMs)
{
    QByteArray buf;
    while (sock->waitForReadyRead(timeoutMs))
        buf += sock->readAll();
    buf += sock->readAll();
    return buf;
}

// Lee una respuesta SMTP completa (puede ser multilínea: "250-..." hasta "250 ").
QByteArray readSmtpReply(QAbstractSocket *sock)
{
    QByteArray buf;
    while (true) {
        if (!sock->canReadLine() && !sock->waitForReadyRead(kReplyTimeoutMs))
            break;
        while (sock->canReadLine()) {
            const QByteArray line = sock->readLine();
            buf += line;
            // Última línea: "NNN <sp>" (4to char espacio), no "NNN-".
            const QByteArray t = line.trimmed();
            if (t.size() >= 4 && t.at(3) == ' ')
                return buf;
        }
    }
    return buf;
}

bool sendLine(QAbstractSocket *sock, const QByteArray &line)
{
    sock->write(line + "\r\n");
    return sock->waitForBytesWritten(kReplyTimeoutMs);
}

// Diálogo SMTP: manda `cmd`, lee la respuesta, verifica que empiece con `expect`.
bool smtpCmd(QAbstractSocket *sock, const QByteArray &cmd, const char *expect,
             QString *err, const QString &stage)
{
    if (!cmd.isEmpty() && !sendLine(sock, cmd)) {
        if (err) *err = QStringLiteral("SMTP write falló en %1").arg(stage);
        return false;
    }
    const QByteArray reply = readSmtpReply(sock);
    if (!reply.startsWith(expect)) {
        if (err) *err = QStringLiteral("SMTP %1: respuesta inesperada: %2")
                            .arg(stage, QString::fromUtf8(reply.trimmed()));
        return false;
    }
    return true;
}

} // namespace

// ─────────────────────────── Funciones puras ─────────────────────────────
MailClient::Account MailClient::applyPreset(Account in)
{
    QString prov = in.provider.trimmed().toLower();
    const QString domain = in.email.section(QLatin1Char('@'), 1).toLower();
    if (prov.isEmpty() || prov == QLatin1String("auto")) {
        if (domain.contains(QLatin1String("gmail")) || domain.contains(QLatin1String("googlemail")))
            prov = QStringLiteral("gmail");
        else if (domain.contains(QLatin1String("outlook")) || domain.contains(QLatin1String("hotmail"))
                 || domain.contains(QLatin1String("live")) || domain.contains(QLatin1String("msn")))
            prov = QStringLiteral("outlook");
        else
            prov = QStringLiteral("custom");
    }
    in.provider = prov;

    auto fillSmtp = [&](const QString &host, int port, const QString &sec) {
        if (in.smtpHost.isEmpty()) in.smtpHost = host;
        if (in.smtpPort == 0)      in.smtpPort = port;
        if (in.smtpSecurity.isEmpty()) in.smtpSecurity = sec;
    };
    auto fillRecv = [&](const QString &imapHost, int imapPort, const QString &popHost, int popPort) {
        if (in.recvProto.isEmpty()) in.recvProto = QStringLiteral("imap");
        const bool imap = in.recvProto.toLower() != QLatin1String("pop3");
        if (in.recvHost.isEmpty()) in.recvHost = imap ? imapHost : popHost;
        if (in.recvPort == 0)      in.recvPort = imap ? imapPort : popPort;
    };

    if (prov == QLatin1String("gmail")) {
        fillSmtp(QStringLiteral("smtp.gmail.com"), 465, QStringLiteral("ssl"));
        fillRecv(QStringLiteral("imap.gmail.com"), 993, QStringLiteral("pop.gmail.com"), 995);
    } else if (prov == QLatin1String("outlook")) {
        fillSmtp(QStringLiteral("smtp-mail.outlook.com"), 587, QStringLiteral("starttls"));
        fillRecv(QStringLiteral("outlook.office365.com"), 993,
                 QStringLiteral("outlook.office365.com"), 995);
    } else {
        // custom: defaults razonables si faltan.
        if (in.smtpSecurity.isEmpty()) in.smtpSecurity = QStringLiteral("ssl");
        if (in.recvProto.isEmpty())    in.recvProto = QStringLiteral("imap");
    }
    return in;
}

QString MailClient::buildMime(const Account &acct, const QString &to, const QString &cc,
                              const QString &subject, const QString &body)
{
    auto encWord = [](const QString &s) -> QString {
        // RFC 2047 sólo si hay no-ASCII; si no, texto plano.
        bool ascii = true;
        for (const QChar c : s) if (c.unicode() > 127) { ascii = false; break; }
        if (ascii) return s;
        return QStringLiteral("=?utf-8?B?")
               + QString::fromLatin1(s.toUtf8().toBase64()) + QStringLiteral("?=");
    };
    const QString from = acct.displayName.isEmpty()
        ? acct.email
        : QStringLiteral("%1 <%2>").arg(encWord(acct.displayName), acct.email);
    const QString date = QDateTime::currentDateTime()
                             .toString(QStringLiteral("ddd, dd MMM yyyy HH:mm:ss "));
    const QString offset = QDateTime::currentDateTime().toString(QStringLiteral("t"));
    const QString msgId = QStringLiteral("<%1@llamacode>")
                              .arg(QUuid::createUuid().toString(QUuid::Id128));

    QString h;
    h += QStringLiteral("From: %1\r\n").arg(from);
    h += QStringLiteral("To: %1\r\n").arg(to);
    if (!cc.trimmed().isEmpty()) h += QStringLiteral("Cc: %1\r\n").arg(cc);
    h += QStringLiteral("Subject: %1\r\n").arg(encWord(subject));
    h += QStringLiteral("Date: %1%2\r\n").arg(date, offset);
    h += QStringLiteral("Message-ID: %1\r\n").arg(msgId);
    h += QStringLiteral("MIME-Version: 1.0\r\n");
    h += QStringLiteral("Content-Type: text/plain; charset=utf-8\r\n");
    h += QStringLiteral("Content-Transfer-Encoding: 8bit\r\n");
    h += QStringLiteral("\r\n");

    // Normalizar saltos a CRLF y dot-stuffing (líneas que empiezan con '.').
    QString b = body;
    b.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    b.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const QStringList lines = b.split(QLatin1Char('\n'));
    QString out = h;
    for (const QString &ln : lines) {
        if (ln.startsWith(QLatin1Char('.'))) out += QLatin1Char('.');
        out += ln + QStringLiteral("\r\n");
    }
    return out;
}

QString MailClient::decodeHeaderWord(const QString &raw)
{
    static const QRegularExpression re(
        QStringLiteral("=\\?([^?]+)\\?([BbQq])\\?([^?]*)\\?="));
    QString out;
    int last = 0;
    auto it = re.globalMatch(raw);
    bool any = false;
    while (it.hasNext()) {
        any = true;
        const auto m = it.next();
        out += raw.mid(last, m.capturedStart() - last);
        const QString enc = m.captured(2).toUpper();
        const QByteArray payload = m.captured(3).toLatin1();
        QByteArray bytes;
        if (enc == QLatin1String("B")) {
            bytes = QByteArray::fromBase64(payload);
        } else { // Q-encoding
            QByteArray q = payload;
            q.replace('_', ' ');
            bytes = QByteArray::fromPercentEncoding(q, '=');
        }
        out += QString::fromUtf8(bytes);
        last = m.capturedEnd();
    }
    if (!any) return raw;
    out += raw.mid(last);
    return out;
}

QVariantMap MailClient::parseImapEnvelope(const QString &fetchLine)
{
    // ENVELOPE (date subject ((from-name nil mbox host)) ...). Parser tolerante:
    // extrae los tokens entre paréntesis del ENVELOPE en orden date, subject, from.
    QVariantMap out;
    const int env = fetchLine.indexOf(QStringLiteral("ENVELOPE ("));
    if (env < 0) return out;
    const QString s = fetchLine.mid(env + 10);

    // Tokeniza respetando cadenas "..." y NIL.
    QStringList toks;
    int i = 0, depth = 0;
    while (i < s.size()) {
        const QChar c = s.at(i);
        if (c == QLatin1Char('"')) {
            int j = i + 1; QString val;
            while (j < s.size() && s.at(j) != QLatin1Char('"')) {
                if (s.at(j) == QLatin1Char('\\') && j + 1 < s.size()) ++j;
                val += s.at(j); ++j;
            }
            toks << val; i = j + 1;
            if (depth == 0 && toks.size() >= 2) break; // ya tenemos date+subject
        } else if (c == QLatin1Char('(')) { ++depth; ++i; }
        else if (c == QLatin1Char(')')) { --depth; ++i; }
        else if (s.mid(i, 3) == QLatin1String("NIL")) { toks << QString(); i += 3; }
        else ++i;
    }
    if (toks.size() >= 1) out[QStringLiteral("date")] = toks.value(0);
    if (toks.size() >= 2) out[QStringLiteral("subject")] = decodeHeaderWord(toks.value(1));

    // From: primer grupo de address tras subject. Buscar mailbox@host.
    static const QRegularExpression addr(
        QStringLiteral("\"([^\"]*)\"\\s+NIL\\s+\"([^\"]*)\"\\s+\"([^\"]*)\""));
    const auto m = addr.match(s);
    if (m.hasMatch()) {
        const QString nm = decodeHeaderWord(m.captured(1));
        const QString email = m.captured(2) + QLatin1Char('@') + m.captured(3);
        out[QStringLiteral("from")] = nm.isEmpty() ? email
                                        : QStringLiteral("%1 <%2>").arg(nm, email);
    }
    return out;
}

QVariantList MailClient::parsePop3List(const QString &listResponse)
{
    QVariantList out;
    const QStringList lines = listResponse.split(QRegularExpression(QStringLiteral("\r?\n")),
                                                 Qt::SkipEmptyParts);
    for (const QString &ln : lines) {
        const QString t = ln.trimmed();
        if (t.startsWith(QLatin1Char('+')) || t.startsWith(QLatin1Char('-'))
            || t == QLatin1String(".")) continue;
        const QStringList parts = t.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            bool okI = false, okS = false;
            const int idx = parts.at(0).toInt(&okI);
            const int sz  = parts.at(1).toInt(&okS);
            if (okI && okS)
                out.append(QVariantMap{{QStringLiteral("index"), idx},
                                       {QStringLiteral("size"), sz}});
        }
    }
    return out;
}

// ─────────────────────────── SMTP (enviar) ───────────────────────────────
bool MailClient::sendSmtp(const Account &acctIn, const QString &to, const QString &cc,
                          const QString &subject, const QString &body, QString *err)
{
    const Account acct = applyPreset(acctIn);
    const bool implicitTls = acct.smtpSecurity.toLower() != QLatin1String("starttls");

    QSslSocket sock;
    if (implicitTls) {
        sock.connectToHostEncrypted(acct.smtpHost, quint16(acct.smtpPort));
        if (!sock.waitForEncrypted(kConnectTimeoutMs)) {
            if (err) *err = QStringLiteral("SMTP TLS falló: %1").arg(sock.errorString());
            return false;
        }
    } else {
        sock.connectToHost(acct.smtpHost, quint16(acct.smtpPort));
        if (!sock.waitForConnected(kConnectTimeoutMs)) {
            if (err) *err = QStringLiteral("SMTP conexión falló: %1").arg(sock.errorString());
            return false;
        }
    }

    if (!smtpCmd(&sock, QByteArray(), "220", err, QStringLiteral("saludo"))) return false;
    if (!smtpCmd(&sock, "EHLO llamacode", "250", err, QStringLiteral("EHLO"))) return false;

    if (!implicitTls) {
        if (!smtpCmd(&sock, "STARTTLS", "220", err, QStringLiteral("STARTTLS"))) return false;
        sock.startClientEncryption();
        if (!sock.waitForEncrypted(kConnectTimeoutMs)) {
            if (err) *err = QStringLiteral("STARTTLS handshake falló: %1").arg(sock.errorString());
            return false;
        }
        if (!smtpCmd(&sock, "EHLO llamacode", "250", err, QStringLiteral("EHLO/TLS"))) return false;
    }

    if (!smtpCmd(&sock, "AUTH LOGIN", "334", err, QStringLiteral("AUTH"))) return false;
    if (!smtpCmd(&sock, acct.loginUser().toUtf8().toBase64(), "334", err,
                 QStringLiteral("usuario"))) return false;
    if (!smtpCmd(&sock, acct.password.toUtf8().toBase64(), "235", err,
                 QStringLiteral("contraseña (¿app password?)"))) return false;

    if (!smtpCmd(&sock, "MAIL FROM:<" + acct.email.toUtf8() + ">", "250", err,
                 QStringLiteral("MAIL FROM"))) return false;
    // Un RCPT por destinatario (to + cc, separados por coma).
    const QString rcptAll = cc.trimmed().isEmpty() ? to : to + QStringLiteral(",") + cc;
    static const QRegularExpression splitRe(QStringLiteral("[,;]"));
    const QStringList rcpts = rcptAll.split(splitRe, Qt::SkipEmptyParts);
    for (const QString &r : rcpts) {
        const QString addr = r.contains(QLatin1Char('<'))
            ? r.section(QLatin1Char('<'), 1, 1).section(QLatin1Char('>'), 0, 0)
            : r.trimmed();
        if (!smtpCmd(&sock, "RCPT TO:<" + addr.toUtf8() + ">", "250", err,
                     QStringLiteral("RCPT TO"))) return false;
    }

    if (!smtpCmd(&sock, "DATA", "354", err, QStringLiteral("DATA"))) return false;
    const QString mime = buildMime(acct, to, cc, subject, body);
    sock.write(mime.toUtf8());
    if (!smtpCmd(&sock, ".", "250", err, QStringLiteral("fin de mensaje"))) return false;
    sendLine(&sock, "QUIT");
    sock.disconnectFromHost();
    return true;
}

// ─────────────────────────── IMAP / POP3 (recibir) ───────────────────────
namespace {

// Lee una respuesta IMAP hasta encontrar la línea de tag "<tag> OK/NO/BAD".
QByteArray readImapUntilTag(QAbstractSocket *sock, const QByteArray &tag)
{
    QByteArray buf;
    while (true) {
        if (!sock->canReadLine() && !sock->waitForReadyRead(kReplyTimeoutMs)) break;
        while (sock->canReadLine()) {
            buf += sock->readLine();
            // ¿Llegó la línea de cierre del tag?
            const QByteArray needle = tag + " ";
            for (const QByteArray &kw : {QByteArray("OK"), QByteArray("NO"), QByteArray("BAD")}) {
                if (buf.contains(needle + kw)) return buf;
            }
        }
    }
    return buf;
}

QSslSocket *imapConnect(const MailClient::Account &acct, QString *err)
{
    auto *sock = new QSslSocket;
    if (acct.recvSsl) {
        sock->connectToHostEncrypted(acct.recvHost, quint16(acct.recvPort));
        if (!sock->waitForEncrypted(kConnectTimeoutMs)) {
            if (err) *err = QStringLiteral("IMAP TLS falló: %1").arg(sock->errorString());
            delete sock; return nullptr;
        }
    } else {
        sock->connectToHost(acct.recvHost, quint16(acct.recvPort));
        if (!sock->waitForConnected(kConnectTimeoutMs)) {
            if (err) *err = QStringLiteral("IMAP conexión falló: %1").arg(sock->errorString());
            delete sock; return nullptr;
        }
    }
    readAvailable(sock, 3000); // saludo del server
    return sock;
}

} // namespace

QVariantList MailClient::fetchInbox(const Account &acctIn, const QString &folder,
                                    int limit, bool unreadOnly, QString *err)
{
    const Account acct = applyPreset(acctIn);
    QVariantList out;
    if (limit <= 0) limit = 10;

    if (acct.recvProto.toLower() == QLatin1String("pop3")) {
        QSslSocket sock;
        sock.connectToHostEncrypted(acct.recvHost, quint16(acct.recvPort));
        if (!sock.waitForEncrypted(kConnectTimeoutMs)) {
            if (err) *err = QStringLiteral("POP3 TLS falló: %1").arg(sock.errorString());
            return out;
        }
        readAvailable(&sock, 3000);
        sendLine(&sock, "USER " + acct.loginUser().toUtf8()); readAvailable(&sock, kReplyTimeoutMs);
        sendLine(&sock, "PASS " + acct.password.toUtf8());
        const QByteArray pass = readAvailable(&sock, kReplyTimeoutMs);
        if (!pass.startsWith("+OK")) {
            if (err) *err = QStringLiteral("POP3 login falló (¿app password?)");
            return out;
        }
        sendLine(&sock, "LIST");
        const QString list = QString::fromUtf8(readAvailable(&sock, kReplyTimeoutMs));
        QVariantList msgs = parsePop3List(list);
        const int start = qMax(0, msgs.size() - limit);
        for (int i = msgs.size() - 1; i >= start; --i) {
            const int idx = msgs.at(i).toMap().value(QStringLiteral("index")).toInt();
            sendLine(&sock, "TOP " + QByteArray::number(idx) + " 0");
            const QString hdr = QString::fromUtf8(readAvailable(&sock, kReplyTimeoutMs));
            QVariantMap m{{QStringLiteral("uid"), QString::number(idx)}};
            for (const QString &ln : hdr.split(QRegularExpression(QStringLiteral("\r?\n")))) {
                if (ln.startsWith(QLatin1String("From:"), Qt::CaseInsensitive))
                    m[QStringLiteral("from")] = decodeHeaderWord(ln.mid(5).trimmed());
                else if (ln.startsWith(QLatin1String("Subject:"), Qt::CaseInsensitive))
                    m[QStringLiteral("subject")] = decodeHeaderWord(ln.mid(8).trimmed());
                else if (ln.startsWith(QLatin1String("Date:"), Qt::CaseInsensitive))
                    m[QStringLiteral("date")] = ln.mid(5).trimmed();
            }
            out.append(m);
        }
        sendLine(&sock, "QUIT");
        return out;
    }

    // IMAP
    QSslSocket *sock = imapConnect(acct, err);
    if (!sock) return out;
    auto cleanup = [&] { sock->disconnectFromHost(); delete sock; };

    auto cmd = [&](const QByteArray &tag, const QByteArray &c) -> QByteArray {
        sock->write(tag + " " + c + "\r\n");
        sock->waitForBytesWritten(kReplyTimeoutMs);
        return readImapUntilTag(sock, tag);
    };

    const QByteArray login = cmd("a1", "LOGIN \"" + acct.loginUser().toUtf8() + "\" \""
                                 + acct.password.toUtf8() + "\"");
    if (!login.contains("a1 OK")) {
        if (err) *err = QStringLiteral("IMAP login falló (¿app password?)");
        cleanup(); return out;
    }
    const QByteArray fldr = (folder.isEmpty() ? QStringLiteral("INBOX") : folder).toUtf8();
    cmd("a2", "SELECT \"" + fldr + "\"");

    const QByteArray searchResp = cmd("a3", unreadOnly ? "SEARCH UNSEEN" : "SEARCH ALL");
    // Línea "* SEARCH 1 2 3 ..."
    QList<int> ids;
    for (const QByteArray &ln : searchResp.split('\n')) {
        const QByteArray t = ln.trimmed();
        if (t.startsWith("* SEARCH")) {
            for (const QByteArray &n : t.mid(8).split(' ')) {
                bool ok = false; const int v = n.trimmed().toInt(&ok);
                if (ok) ids << v;
            }
        }
    }
    const int start = qMax(0, ids.size() - limit);
    for (int i = ids.size() - 1; i >= start; --i) {
        const QByteArray seq = QByteArray::number(ids.at(i));
        const QByteArray resp = cmd("a4", seq + " FETCH (UID ENVELOPE)");
        const QString line = QString::fromUtf8(resp);
        QVariantMap m = parseImapEnvelope(line);
        // UID
        static const QRegularExpression uidRe(QStringLiteral("UID (\\d+)"));
        const auto um = uidRe.match(line);
        m[QStringLiteral("uid")] = um.hasMatch() ? um.captured(1) : QString::number(ids.at(i));
        out.append(m);
    }
    cmd("a5", "LOGOUT");
    cleanup();
    return out;
}

QString MailClient::readMessage(const Account &acctIn, const QString &folder,
                                const QString &uid, QString *err)
{
    const Account acct = applyPreset(acctIn);

    if (acct.recvProto.toLower() == QLatin1String("pop3")) {
        QSslSocket sock;
        sock.connectToHostEncrypted(acct.recvHost, quint16(acct.recvPort));
        if (!sock.waitForEncrypted(kConnectTimeoutMs)) {
            if (err) *err = QStringLiteral("POP3 TLS falló: %1").arg(sock.errorString());
            return {};
        }
        readAvailable(&sock, 3000);
        sendLine(&sock, "USER " + acct.loginUser().toUtf8()); readAvailable(&sock, kReplyTimeoutMs);
        sendLine(&sock, "PASS " + acct.password.toUtf8());     readAvailable(&sock, kReplyTimeoutMs);
        sendLine(&sock, "RETR " + uid.toUtf8());
        const QString full = QString::fromUtf8(readAvailable(&sock, kReplyTimeoutMs));
        sendLine(&sock, "QUIT");
        return full;
    }

    QSslSocket *sock = imapConnect(acct, err);
    if (!sock) return {};
    auto cmd = [&](const QByteArray &tag, const QByteArray &c) -> QByteArray {
        sock->write(tag + " " + c + "\r\n");
        sock->waitForBytesWritten(kReplyTimeoutMs);
        return readImapUntilTag(sock, tag);
    };
    const QByteArray login = cmd("b1", "LOGIN \"" + acct.loginUser().toUtf8() + "\" \""
                                 + acct.password.toUtf8() + "\"");
    if (!login.contains("b1 OK")) {
        if (err) *err = QStringLiteral("IMAP login falló");
        sock->disconnectFromHost(); delete sock; return {};
    }
    const QByteArray fldr = (folder.isEmpty() ? QStringLiteral("INBOX") : folder).toUtf8();
    cmd("b2", "SELECT \"" + fldr + "\"");
    const QByteArray resp = cmd("b3", "UID FETCH " + uid.toUtf8() + " (BODY.PEEK[TEXT])");
    cmd("b4", "LOGOUT");
    sock->disconnectFromHost(); delete sock;

    // Extraer el literal entre la primera línea "{n}" y el cierre.
    const QString s = QString::fromUtf8(resp);
    const int brace = s.indexOf(QLatin1Char('{'));
    if (brace >= 0) {
        const int nl = s.indexOf(QLatin1Char('\n'), brace);
        if (nl >= 0) {
            QString body = s.mid(nl + 1);
            // Recortar la línea de cierre ")" y el tag final.
            const int tail = body.lastIndexOf(QStringLiteral(")\r\nb3"));
            if (tail >= 0) body = body.left(tail);
            return body.trimmed();
        }
    }
    return s;
}

bool MailClient::testAccount(const Account &acctIn, QString *err)
{
    const Account acct = applyPreset(acctIn);
    // 1) SMTP: conectar + autenticar (sin enviar).
    {
        const bool implicitTls = acct.smtpSecurity.toLower() != QLatin1String("starttls");
        QSslSocket sock;
        if (implicitTls) {
            sock.connectToHostEncrypted(acct.smtpHost, quint16(acct.smtpPort));
            if (!sock.waitForEncrypted(kConnectTimeoutMs)) {
                if (err) *err = QStringLiteral("SMTP TLS: %1").arg(sock.errorString());
                return false;
            }
        } else {
            sock.connectToHost(acct.smtpHost, quint16(acct.smtpPort));
            if (!sock.waitForConnected(kConnectTimeoutMs)) {
                if (err) *err = QStringLiteral("SMTP conexión: %1").arg(sock.errorString());
                return false;
            }
        }
        if (!smtpCmd(&sock, QByteArray(), "220", err, QStringLiteral("saludo"))) return false;
        if (!smtpCmd(&sock, "EHLO llamacode", "250", err, QStringLiteral("EHLO"))) return false;
        if (!implicitTls) {
            if (!smtpCmd(&sock, "STARTTLS", "220", err, QStringLiteral("STARTTLS"))) return false;
            sock.startClientEncryption();
            if (!sock.waitForEncrypted(kConnectTimeoutMs)) {
                if (err) *err = QStringLiteral("STARTTLS: %1").arg(sock.errorString());
                return false;
            }
            if (!smtpCmd(&sock, "EHLO llamacode", "250", err, QStringLiteral("EHLO/TLS"))) return false;
        }
        if (!smtpCmd(&sock, "AUTH LOGIN", "334", err, QStringLiteral("AUTH"))) return false;
        if (!smtpCmd(&sock, acct.loginUser().toUtf8().toBase64(), "334", err,
                     QStringLiteral("usuario"))) return false;
        if (!smtpCmd(&sock, acct.password.toUtf8().toBase64(), "235", err,
                     QStringLiteral("contraseña (¿app password?)"))) return false;
        sendLine(&sock, "QUIT");
    }
    // 2) Recepción: login.
    QString recvErr;
    QVariantList probe = fetchInbox(acct, QStringLiteral("INBOX"), 1, false, &recvErr);
    Q_UNUSED(probe);
    if (!recvErr.isEmpty()) { if (err) *err = recvErr; return false; }
    return true;
}
