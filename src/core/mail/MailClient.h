#pragma once
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// Cliente de correo minimalista (SMTP enviar + IMAP/POP3 recibir) sobre sockets.
// Qt no trae cliente de correo: implementamos el diálogo de línea a mano sobre
// QSslSocket (TLS implícito) o QTcpSocket + STARTTLS. Los métodos son
// BLOQUEANTES por diseño: corren en el worker thread del AgentToolRunner (igual
// que run_shell), nunca en el hilo de UI.
//
// Las funciones puras (applyPreset/buildMime/decodeHeaderWord/parse*) están
// expuestas como static para poder testearlas sin red (ver tests/test_mail.cpp).
class MailClient
{
public:
    struct Account {
        QString name;          // alias interno (clave de config)
        QString email;         // dirección From
        QString displayName;   // nombre a mostrar (opcional)
        QString provider;      // gmail | outlook | custom
        // Envío
        QString smtpHost;
        int     smtpPort = 0;
        QString smtpSecurity;  // ssl | starttls
        // Recepción
        QString recvProto;     // imap | pop3
        QString recvHost;
        int     recvPort = 0;
        bool    recvSsl = true;
        // Credenciales
        QString user;          // si vacío, se usa email
        QString password;      // resuelto vía SecretStore antes de llegar acá

        QString loginUser() const { return user.isEmpty() ? email : user; }
    };

    // ── Funciones puras (testeables sin red) ─────────────────────────────
    // Autocompleta host/puerto/seguridad según provider o dominio del email.
    static Account applyPreset(Account in);
    // Construye el mensaje MIME (text/plain utf-8, CRLF, headers estándar).
    static QString buildMime(const Account &acct, const QString &to, const QString &cc,
                             const QString &subject, const QString &body);
    // Decodifica encoded-words MIME (=?utf-8?B?...?= / =?utf-8?Q?...?=).
    static QString decodeHeaderWord(const QString &raw);
    // Parsea una respuesta IMAP FETCH (ENVELOPE) en {from,subject,date}.
    static QVariantMap parseImapEnvelope(const QString &fetchLine);
    // Parsea la respuesta de POP3 LIST: "n octets" → lista de {index,size}.
    static QVariantList parsePop3List(const QString &listResponse);

    // ── Operaciones de red (bloqueantes) ─────────────────────────────────
    // Envía un correo. Devuelve true/false; *err con el detalle si falla.
    static bool sendSmtp(const Account &acct, const QString &to, const QString &cc,
                         const QString &subject, const QString &body, QString *err);
    // Lista los últimos `limit` correos. Devuelve [{uid,from,subject,date,snippet}].
    static QVariantList fetchInbox(const Account &acct, const QString &folder,
                                   int limit, bool unreadOnly, QString *err);
    // Lee el cuerpo completo de un correo por uid (IMAP) o índice (POP3).
    static QString readMessage(const Account &acct, const QString &folder,
                               const QString &uid, QString *err);
    // Prueba la cuenta (login SMTP + login recepción). true = ambos OK.
    static bool testAccount(const Account &acct, QString *err);
};
