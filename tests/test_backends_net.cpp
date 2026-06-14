// Integration tests del RawChatBackend (chat directo a llama-server). Cubrimos
// el ciclo de sesiones y la PERSISTENCIA a disco SIN red ni modelo (sendMessage
// requiere servidor y queda fuera). Almacenamiento aislado vía test mode.
//
// Las funciones de stream SSE / tool-call son privadas; la cobertura de red real
// se hará cuando se agregue un stub de /v1/chat/completions (ver plan).

#include <QtTest>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QSignalSpy>
#include "core/agent/RawChatBackend.h"
#include "core/agent/AgentTypes.h"

class BackendsNetTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void start_createsInitialSession();
    void newSession_addsSession();
    void renameSession_updatesTitle();
    void switchSession_changesCurrent();
    void deleteSession_removes();
    void persistsAcrossRestart();

private:
    AgentContext ctx(const QString &cwd);
};

AgentContext BackendsNetTests::ctx(const QString &cwd)
{
    AgentContext c;
    c.adapter = "raw";
    c.cwd = cwd;
    c.serverBaseUrl = "http://127.0.0.1:1";  // nunca se contacta en estos tests
    c.modelId = "test-model";
    return c;
}

void BackendsNetTests::start_createsInitialSession()
{
    QTemporaryDir dir;
    RawChatBackend be;
    be.start(ctx(dir.path()));
    QVERIFY(be.running());
    QVERIFY(!be.currentSessionId().isEmpty());
    QVERIFY(!be.sessions().isEmpty());
}

void BackendsNetTests::newSession_addsSession()
{
    QTemporaryDir dir;
    RawChatBackend be;
    be.start(ctx(dir.path()));
    const int before = be.sessions().size();
    be.newSession();
    QCOMPARE(be.sessions().size(), before + 1);
}

void BackendsNetTests::renameSession_updatesTitle()
{
    QTemporaryDir dir;
    RawChatBackend be;
    be.start(ctx(dir.path()));
    const QString id = be.currentSessionId();
    QSignalSpy spy(&be, &RawChatBackend::sessionsChanged);
    be.renameSession(id, "Mi sesión");
    QCOMPARE(be.currentSessionTitle(), QStringLiteral("Mi sesión"));
    QVERIFY(spy.count() >= 1);
}

void BackendsNetTests::switchSession_changesCurrent()
{
    QTemporaryDir dir;
    RawChatBackend be;
    be.start(ctx(dir.path()));
    const QString first = be.currentSessionId();
    be.newSession();
    const QString second = be.currentSessionId();
    QVERIFY(first != second);
    be.switchSession(first);
    QCOMPARE(be.currentSessionId(), first);
}

void BackendsNetTests::deleteSession_removes()
{
    QTemporaryDir dir;
    RawChatBackend be;
    be.start(ctx(dir.path()));
    be.newSession();
    const QString toDelete = be.currentSessionId();
    const int before = be.sessions().size();
    be.deleteSession(toDelete);
    QCOMPARE(be.sessions().size(), before - 1);
}

void BackendsNetTests::persistsAcrossRestart()
{
    QTemporaryDir dir;
    QString id;
    {
        RawChatBackend be;
        be.start(ctx(dir.path()));
        be.renameSession(be.currentSessionId(), "Persistida");
        id = be.currentSessionId();
        be.stop();
    }
    {
        RawChatBackend be2;
        be2.start(ctx(dir.path()));
        bool found = false;
        for (const QVariant &v : be2.sessions())
            if (v.toMap().value("id").toString() == id) {
                QCOMPARE(v.toMap().value("title").toString(), QStringLiteral("Persistida"));
                found = true;
            }
        QVERIFY(found);
    }
}

QTEST_MAIN(BackendsNetTests)
#include "test_backends_net.moc"
