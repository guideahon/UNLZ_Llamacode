#pragma once
#include <QObject>
#include <QHostAddress>

class QTcpServer;
class QTcpSocket;

// Control API HTTP headless: espeja TODO AppController vía meta-object
// (Q_INVOKABLE + Q_PROPERTY ya expuestos a QML). Permite manejar la app sin GUI
// para tests automatizados. Sólo escucha en localhost.
//
// Endpoints:
//   GET  /methods            → lista de métodos invocables + propiedades (JSON)
//   GET  /prop?name=X        → valor de la propiedad X (JSON)
//   POST /invoke {method,args}→ invoca método en AppController; {ok,result}
//   GET  /health             → {ok:true}
//
// Targeting de sub-objetos: todo endpoint acepta un `target` (query param en GET,
// campo JSON en POST) con una ruta de propiedades QObject* separadas por punto,
// p.ej. "profileManager" o "binaryRegistry". Sin target = AppController raíz.
// Esto expone headless TODO sub-objeto QObject hijo (registries, profileManager,
// catalog, …) sin tener que espejar sus métodos a mano. /methods sin target
// además lista los `targets` hijos disponibles para descubrimiento.
class ControlApi : public QObject
{
    Q_OBJECT
public:
    explicit ControlApi(QObject *target, QObject *parent = nullptr);
    bool start(quint16 port, const QHostAddress &addr = QHostAddress::LocalHost);

private slots:
    void onNewConnection();

private:
    void handleRequest(QTcpSocket *sock, const QByteArray &method,
                       const QString &path, const QByteArray &body);
    // Resuelve una ruta de props QObject* (dot-separated) desde m_target. Ruta
    // vacía = m_target. Devuelve nullptr si algún segmento no es un QObject* válido.
    QObject *resolveTarget(const QString &path) const;
    QByteArray jsonMethods(const QString &targetPath) const;
    QByteArray jsonProperty(QObject *target, const QString &name) const;
    QByteArray setProperty(QObject *target, const QByteArray &jsonBody, bool *ok) const;
    QByteArray invokeMethod(QObject *target, const QByteArray &jsonBody, bool *ok) const;

    QObject *m_target = nullptr;
    QTcpServer *m_server = nullptr;
};
