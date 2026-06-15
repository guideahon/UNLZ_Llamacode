// Unit tests de MailClient (funciones puras, sin red):
//   - applyPreset: gmail / outlook / hotmail / custom (autocompleta host/puerto).
//   - buildMime: headers, encoding utf-8, CRLF, dot-stuffing.
//   - decodeHeaderWord: encoded-words MIME (B y Q).
//   - parseImapEnvelope / parsePop3List: parseo de respuestas canned.
// El envío/recepción real (SMTP/IMAP/POP3) necesita un server → no se cubre acá.

#include <QtTest>
#include "core/mail/MailClient.h"

class MailTests : public QObject
{
    Q_OBJECT
private slots:
    void preset_gmail();
    void preset_outlookByHotmail();
    void preset_customRespected();
    void buildMime_headersAndEncoding();
    void buildMime_dotStuffing();
    void decodeHeaderWord_base64AndQ();
    void parseImapEnvelope_basic();
    void parsePop3List_basic();
};

void MailTests::preset_gmail()
{
    MailClient::Account a;
    a.email = QStringLiteral("alguien@gmail.com");
    a = MailClient::applyPreset(a);
    QCOMPARE(a.provider, QString("gmail"));
    QCOMPARE(a.smtpHost, QString("smtp.gmail.com"));
    QCOMPARE(a.smtpPort, 465);
    QCOMPARE(a.smtpSecurity, QString("ssl"));
    QCOMPARE(a.recvHost, QString("imap.gmail.com"));
    QCOMPARE(a.recvPort, 993);
}

void MailTests::preset_outlookByHotmail()
{
    MailClient::Account a;
    a.email = QStringLiteral("alguien@hotmail.com");
    a = MailClient::applyPreset(a);
    QCOMPARE(a.provider, QString("outlook"));
    QCOMPARE(a.smtpHost, QString("smtp-mail.outlook.com"));
    QCOMPARE(a.smtpPort, 587);
    QCOMPARE(a.smtpSecurity, QString("starttls"));
    QCOMPARE(a.recvHost, QString("outlook.office365.com"));
}

void MailTests::preset_customRespected()
{
    MailClient::Account a;
    a.email = QStringLiteral("yo@midominio.com");
    a.smtpHost = QStringLiteral("mail.midominio.com");
    a.smtpPort = 2525;
    a.smtpSecurity = QStringLiteral("starttls");
    a.recvProto = QStringLiteral("pop3");
    a.recvHost = QStringLiteral("pop.midominio.com");
    a.recvPort = 995;
    a = MailClient::applyPreset(a);
    QCOMPARE(a.provider, QString("custom"));
    QCOMPARE(a.smtpHost, QString("mail.midominio.com"));   // no pisado
    QCOMPARE(a.smtpPort, 2525);
    QCOMPARE(a.recvProto, QString("pop3"));
    QCOMPARE(a.recvHost, QString("pop.midominio.com"));
}

void MailTests::buildMime_headersAndEncoding()
{
    MailClient::Account a;
    a.email = QStringLiteral("yo@example.com");
    a.displayName = QStringLiteral("José Pérez");   // no-ASCII → encoded-word
    const QString mime = MailClient::buildMime(a, QStringLiteral("dest@example.com"),
                                               QString(), QStringLiteral("Hola"),
                                               QStringLiteral("cuerpo"));
    QVERIFY(mime.contains(QStringLiteral("To: dest@example.com\r\n")));
    QVERIFY(mime.contains(QStringLiteral("Subject: Hola\r\n")));
    QVERIFY(mime.contains(QStringLiteral("MIME-Version: 1.0\r\n")));
    QVERIFY(mime.contains(QStringLiteral("charset=utf-8")));
    QVERIFY(mime.contains(QStringLiteral("=?utf-8?B?")));   // displayName codificado
    QVERIFY(mime.contains(QStringLiteral("\r\n\r\n")));     // separador headers/body
    QVERIFY(mime.endsWith(QStringLiteral("cuerpo\r\n")));
}

void MailTests::buildMime_dotStuffing()
{
    MailClient::Account a;
    a.email = QStringLiteral("yo@example.com");
    const QString mime = MailClient::buildMime(a, QStringLiteral("d@e.com"), QString(),
                                               QStringLiteral("s"),
                                               QStringLiteral(".linea con punto"));
    // La línea que empezaba con '.' debe quedar con '.' duplicado.
    QVERIFY(mime.contains(QStringLiteral("..linea con punto\r\n")));
}

void MailTests::decodeHeaderWord_base64AndQ()
{
    const QString b64 = QStringLiteral("=?utf-8?B?")
        + QString::fromLatin1(QStringLiteral("Holá").toUtf8().toBase64())
        + QStringLiteral("?=");
    QCOMPARE(MailClient::decodeHeaderWord(b64), QString::fromUtf8("Holá"));

    const QString q = QStringLiteral("=?utf-8?Q?Hola=20mundo?=");
    QCOMPARE(MailClient::decodeHeaderWord(q), QString("Hola mundo"));

    // Sin encoded-word: pasa tal cual.
    QCOMPARE(MailClient::decodeHeaderWord(QStringLiteral("plano")), QString("plano"));
}

void MailTests::parseImapEnvelope_basic()
{
    const QString line = QStringLiteral(
        "* 3 FETCH (UID 42 ENVELOPE (\"Mon, 1 Jan 2024 10:00:00 +0000\" "
        "\"Asunto de prueba\" ((\"Juan\" NIL \"juan\" \"example.com\")) "
        "NIL NIL NIL NIL NIL NIL))");
    const QVariantMap m = MailClient::parseImapEnvelope(line);
    QCOMPARE(m.value("subject").toString(), QString("Asunto de prueba"));
    QVERIFY(m.value("date").toString().contains(QStringLiteral("2024")));
    QVERIFY(m.value("from").toString().contains(QStringLiteral("juan@example.com")));
}

void MailTests::parsePop3List_basic()
{
    const QString resp = QStringLiteral("+OK 2 messages\r\n1 1200\r\n2 3400\r\n.\r\n");
    const QVariantList l = MailClient::parsePop3List(resp);
    QCOMPARE(l.size(), 2);
    QCOMPARE(l.at(0).toMap().value("index").toInt(), 1);
    QCOMPARE(l.at(0).toMap().value("size").toInt(), 1200);
    QCOMPARE(l.at(1).toMap().value("index").toInt(), 2);
}

QTEST_MAIN(MailTests)
#include "test_mail.moc"
