// Integration tests del ControlApi (HTTP headless que espeja un QObject vía
// meta-object). Levantamos el server en un puerto libre contra un target de
// prueba y hacemos requests HTTP reales por QTcpSocket.
//   GET /health, GET /methods, GET /prop, POST /setprop, POST /invoke + errores.

#include <QtTest>
#include <QTcpSocket>
#include <QTcpServer>
#include <QJsonDocument>
#include <QJsonObject>
#include "core/ControlApi.h"

// Target de prueba: una propiedad escribible y un método invocable con retorno.
class FakeTarget : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int counter MEMBER m_counter)
public:
    Q_INVOKABLE int addNums(int a, int b) { return a + b; }
    int m_counter = 7;
};

class ControlApiTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();

    void health();
    void getProperty();
    void setProperty();
    void invokeMethodWithReturn();
    void invokeUnknownMethodErrors();

private:
    QJsonObject request(const QByteArray &method, const QString &path,
                        const QByteArray &body = {});
    static quint16 freePort();

    FakeTarget m_target;
    ControlApi *m_api = nullptr;
    quint16 m_port = 0;
};

quint16 ControlApiTests::freePort()
{
    QTcpServer s;
    s.listen(QHostAddress::LocalHost, 0);
    const quint16 p = s.serverPort();
    s.close();
    return p;
}

void ControlApiTests::initTestCase()
{
    m_port = freePort();
    m_api = new ControlApi(&m_target, this);
    QVERIFY(m_api->start(m_port));
}

QJsonObject ControlApiTests::request(const QByteArray &method, const QString &path,
                                     const QByteArray &body)
{
    // Server y client viven en el MISMO hilo, así que NO podemos usar
    // waitForReadyRead (no atiende el socket de escucha del server). Bombeamos
    // el event loop global hasta tener la respuesta completa o timeout.
    QByteArray resp;
    QTcpSocket sock;
    QObject::connect(&sock, &QTcpSocket::readyRead, [&] { resp += sock.readAll(); });
    sock.connectToHost(QHostAddress::LocalHost, m_port);

    QByteArray req = method + " " + path.toUtf8() + " HTTP/1.1\r\n";
    req += "Host: localhost\r\n";
    if (!body.isEmpty()) {
        req += "Content-Type: application/json\r\n";
        req += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    }
    req += "Connection: close\r\n\r\n";
    req += body;

    auto responseComplete = [&]() -> bool {
        const int hdr = resp.indexOf("\r\n\r\n");
        if (hdr < 0) return false;
        int clen = 0;
        for (const QByteArray &l : resp.left(hdr).split('\n'))
            if (l.toLower().startsWith("content-length:"))
                clen = l.mid(l.indexOf(':') + 1).trimmed().toInt();
        return resp.size() - (hdr + 4) >= clen;
    };

    QElapsedTimer timer; timer.start();
    bool sent = false;
    while (timer.elapsed() < 3000 && !responseComplete()) {
        if (!sent && sock.state() == QAbstractSocket::ConnectedState) {
            sock.write(req); sock.flush(); sent = true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    const int hdr = resp.indexOf("\r\n\r\n");
    if (hdr < 0) return {};
    return QJsonDocument::fromJson(resp.mid(hdr + 4)).object();
}

void ControlApiTests::health()
{
    const QJsonObject o = request("GET", "/health");
    QCOMPARE(o.value("ok").toBool(), true);
}

void ControlApiTests::getProperty()
{
    const QJsonObject o = request("GET", "/prop?name=counter");
    QCOMPARE(o.value("value").toInt(), 7);
}

void ControlApiTests::setProperty()
{
    const QJsonObject o = request("POST", "/setprop",
                                  R"({"name":"counter","value":42})");
    QCOMPARE(o.value("ok").toBool(), true);
    QCOMPARE(m_target.m_counter, 42);
}

void ControlApiTests::invokeMethodWithReturn()
{
    const QJsonObject o = request("POST", "/invoke",
                                  R"({"method":"addNums","args":[2,40]})");
    QCOMPARE(o.value("ok").toBool(), true);
    QCOMPARE(o.value("result").toInt(), 42);
}

void ControlApiTests::invokeUnknownMethodErrors()
{
    const QJsonObject o = request("POST", "/invoke",
                                  R"({"method":"nope","args":[]})");
    QVERIFY(o.contains("error"));
}

QTEST_MAIN(ControlApiTests)
#include "test_control_api.moc"
