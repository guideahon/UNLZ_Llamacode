#pragma once
#include "IAgentBackend.h"
#include <QHash>
#include <QList>
#include <QSet>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class QThread;
class AgentToolRunner;

// Backend propio ("llamaagent"): loop ReAct con tool-calling OpenAI contra
// llama-server. No lanza proceso externo; ejecuta tools nativas (lectura/
// escritura/shell/grep) con aprobación human-in-the-loop vía IAgentBackend.
class LlamaAgentBackend : public IAgentBackend
{
    Q_OBJECT
public:
    explicit LlamaAgentBackend(QObject *parent = nullptr);
    ~LlamaAgentBackend() override;

    QString adapter() const override { return QStringLiteral("llamaagent"); }
    bool running() const override { return m_running; }

    void start(const AgentContext &ctx) override;
    void stop() override;
    void sendMessage(const QString &text) override;
    void cancelGeneration() override;

    void newSession() override;
    void newSessionInProject(const QString &projectDir) override;
    void switchSession(const QString &sessionId) override;
    void renameSession(const QString &sessionId, const QString &title) override;
    void deleteSession(const QString &sessionId) override;
    void refreshSessions() override;

    void approveTool(const QString &id, bool always = false) override;
    void rejectTool(const QString &id) override;
    void setApprovalPolicy(const QString &mode) override;
    void revertEdit(const QString &path) override;
    void setAgentTuning(const QString &systemExtra, double temperature) override;

    // Razonamiento (Qwen3): on por defecto para que el agente piense las tools.
    void setThinkingEnabled(bool enabled);

    // Servers MCP (stdio) a usar. Cada entrada: {name, command, type, enabled}.
    // Se relanzan en start(). Sus tools se inyectan con prefijo mcp__<server>__<tool>.
    void setMcpServers(const QVariantList &servers);

    // Diff unificado simple (prefijo/sufijo común; líneas +/-/ ).
    static QString makeDiff(const QString &oldText, const QString &newText);

    // Memoria por proyecto: ruta del archivo de memoria dentro de un cwd.
    static QString memoryFilePath(const QString &cwd);

    QString currentSessionId() const override { return m_sessionId; }
    QString currentSessionTitle() const override { return m_sessionTitle; }
    QString currentProjectDir() const override { return m_cwd; }
    QVariantList messages() const override { return m_messages; }
    QVariantList sessions() const override { return m_sessions; }

private:
    // Loop
    void runCompletion();
    void processPendingCalls();     // procesa m_pendingCalls (approval/exec)
    void finishTurn(const QString &finalText);
    void appendAssistantText(const QString &text);
    void setTyping(bool typing);

    // Tools
    static QJsonArray toolSchemas();                 // solo built-in
    QJsonArray buildToolSchemas() const;             // built-in + MCP (cache)
    static QString toolKind(const QString &name);    // read | write | shell | mcp
    static QStringList requiredArgs(const QString &name);

    // Worker de ejecución de tools (hilo aparte; no bloquea UI).
    void ensureWorker();
    void teardownWorker();

private slots:
    void onServersReady(const QVariantList &toolDefs);
    void onToolExecuted(const QVariantMap &result);

private:
    void approveAndContinue(const QString &id, const QString &response); // once|always|reject
    void appendToolResult(const QString &id, const QString &name, const QString &content);

    // Streaming SSE (igual patrón que RawChatBackend)
    void handleStreamData();             // parsea m_sseBuf incremental
    void handleStreamFinished(bool ok, const QString &err);
    void resetStreamState();

    // Sesión + persistencia a disco (patrón RawChatBackend)
    void ensureSession();
    QString buildSystemPrompt() const;   // prompt base + memoria del proyecto
    void fetchContextLimit();            // n_ctx desde /props
    QString storageDir() const;
    QString sessionFilePath(const QString &sessionId) const;
    void loadFromDisk();
    void persistIndex() const;
    void persistSession(const QString &sessionId) const;
    void persistAll() const;
    void removeSessionFile(const QString &sessionId) const;
    void saveCurrentSession();           // vuelca m_messages+m_apiMessages al store
    void setCurrentSession(const QString &sessionId);

    AgentContext m_ctx;
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    bool m_running = false;
    QString m_cwd;
    QString m_approvalMode = QStringLiteral("ask");
    QString m_systemExtra;          // instrucciones extra del usuario (perfil de agente)
    double  m_temperature = -1.0;   // <0 = no enviar (default del server)
    bool    m_thinkingEnabled = false;

    QVariantList m_mcpConfig;        // config de servers MCP (de AppController)
    QVariantList m_mcpTools;         // cache de tool-defs MCP del worker {server,name,description,schema}

    // Worker thread para ejecución de tools (nativas + MCP).
    QThread *m_workerThread = nullptr;
    AgentToolRunner *m_worker = nullptr;
    QString m_execCallId;            // tool_call en ejecución ("" = ninguno)

    QString m_sessionId;
    QString m_sessionTitle;
    QVariantList m_sessions;
    QVariantList m_messages;        // para UI: {role, content, typing}
    int m_curAsstIdx = -1;

    // Estado de streaming del turno en curso.
    QByteArray m_sseBuf;
    QString    m_streamBase;        // contenido del bubble al iniciar el stream (se le concatena)
    QString    m_streamContent;     // delta.content acumulado
    QString    m_streamReason;      // delta.reasoning_content acumulado
    QHash<int, QJsonObject> m_streamToolCalls; // index → {id,name,arguments} mergeado

    QJsonArray m_apiMessages;       // conversación real para la API (incluye tool/assistant tool_calls)
    QJsonArray m_pendingCalls;      // tool_calls restantes del turno actual
    QSet<QString> m_alwaysAllowed;  // kinds aprobados con "Siempre"

    // Robustez (Etapa 7)
    int m_turnIters = 0;                 // completions consumidas en el turno actual
    QHash<QString, int> m_callCounts;    // firma de tool_call → veces vista (anti-loop)
    int m_toolOk = 0;                    // salud: tools exitosas en la sesión
    int m_toolFail = 0;                  // salud: tools con error/inválidas
    int m_ctxLimit = -1;                 // n_ctx del server (vía /props)
    // Tope de seguridad MUY alto: no cortar trabajo legítimo. El loop infinito
    // real lo frena kMaxSameCall (misma tool + mismos args repetidos). Que el
    // agente haga tantas iteraciones como necesite.
    static constexpr int kMaxTurnIters = 1000;
    static constexpr int kMaxSameCall  = 3;

    // Aprobación en curso (1 tool a la vez)
    QString m_awaitId;              // id del tool_call esperando respuesta ("" = ninguno)
    QJsonObject m_awaitCall;

    // Snapshots para revertir ediciones: path absoluto → {existía, contenido viejo}
    struct EditSnapshot { bool existed = false; QByteArray oldContent; };
    QHash<QString, EditSnapshot> m_editSnapshots;
};
