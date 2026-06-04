#pragma once
#include <QObject>
#include <QByteArray>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QList>
#include <QVariantList>
#include <QVariantMap>

class McpClient;
class QProcess;
class QTimer;

// Ejecuta las tools del agente (nativas + MCP) en un hilo worker, para no
// bloquear el hilo de UI (run_shell, descarga de npx en el handshake MCP y
// tools/call pueden tardar). El loop ReAct vive en el hilo principal
// (LlamaAgentBackend) y se comunica con este worker por señales/slots en cola.
class AgentToolRunner : public QObject
{
    Q_OBJECT
public:
    explicit AgentToolRunner(QObject *parent = nullptr);
    ~AgentToolRunner() override;

public slots:
    // Lanza los servers MCP (config = lista de {name,command,type,enabled}) y
    // emite serversReady con las tool-defs descubiertas.
    void initServers(const QVariantList &cfg, const QString &cwd);
    // Ejecuta una tool. Emite toolExecuted(result) con: callId, name, result,
    // ok y (si es write) isWrite/relPath/absPath/diff/existed/oldContentB64.
    void executeTool(const QString &callId, const QString &name,
                     const QString &argsJson, const QString &cwd);
    // Confinamiento al cwd. false = "Super Agente" (acceso a todo el disco).
    void setConfined(bool confined);
    // URL base del llama-server (para /v1/embeddings en semantic_search).
    void setServerBaseUrl(const QString &url);
    // Config del modelo maestro (tool ask_teacher). Vacío = usar env vars.
    void setTeacherConfig(const QString &url, const QString &model, const QString &key);
    // Mata el run_shell en curso (cancelación real desde PARAR/steer).
    void cancelShell();
    void shutdown();

signals:
    void logAppended(const QString &chunk);
    void serversReady(const QVariantList &toolDefs); // {server,name,description,schema}
    void toolExecuted(const QVariantMap &result);
    // run_shell async: arranque (crea tarjeta en vivo) y chunks de salida.
    void toolStarted(const QVariantMap &info);       // {callId,name,kind,command}
    void toolOutputChunk(const QString &callId, const QString &chunk);

private slots:
    void onShellReadyRead();
    void onShellFinished();
    void onShellTimeout();

private:
    QString runNative(const QString &name, const QJsonObject &args,
                      const QString &cwd, QVariantMap &out, bool *ok);
    void startShell(const QString &callId, const QString &command,
                    const QString &cwd, int timeoutS);
    void finishShell(bool timedOut, bool cancelled);

    QList<McpClient *> m_mcp;
    bool m_confined = true;
    QString m_serverBaseUrl;
    QString m_teacherUrl, m_teacherModel, m_teacherKey;   // ask_teacher (override de env)

    // Estado del run_shell async en curso (uno a la vez; el loop es secuencial).
    QProcess   *m_shellProc = nullptr;
    QTimer     *m_shellTimer = nullptr;
    QString     m_shellCallId;
    QByteArray  m_shellOut;          // salida acumulada
    QElapsedTimer m_shellClock;
    int         m_shellTimeoutS = 120;
};
