#pragma once
#include "IAgentBackend.h"
#include <QHash>
#include <QList>
#include <QSet>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>

class QThread;
class AgentToolRunner;
class SubAgentRunner;

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
    void steerMessage(const QString &text) override;
    void queueMessage(const QString &text) override;
    int queuedCount() const override { return m_msgQueue.size(); }
    QStringList queuedMessages() const override { return m_msgQueue; }
    void clearQueue() override;

    // Rebobinar la conversación al estado previo a un mensaje de usuario (índice
    // en la lista de UI). Trunca mensajes + contexto y revierte edits posteriores.
    void rollbackToMessage(int msgIndex);
    // Editar el texto de un mensaje (user o IA) en msgIndex y descartar todo lo
    // posterior. Rebuildea m_apiMessages (system + turnos de texto) → pierde la
    // estructura de tool_calls pero deja el contexto válido para continuar.
    void editMessage(int msgIndex, const QString &newText);

    void newSession() override;
    void newSessionInProject(const QString &projectDir) override;
    void switchSession(const QString &sessionId) override;
    void renameSession(const QString &sessionId, const QString &title) override;
    void deleteSession(const QString &sessionId) override;
    void deleteProject(const QString &projectDir) override;
    void refreshSessions() override;

    void approveTool(const QString &id, bool always = false) override;
    void rejectTool(const QString &id) override;
    void setApprovalPolicy(const QString &mode) override;
    void setPermissionRules(const QString &rules) override;
    void revertEdit(const QString &path) override;
    void setAgentTuning(const QString &systemExtra, double temperature) override;

    // Razonamiento (Qwen3): on por defecto para que el agente piense las tools.
    void setThinkingEnabled(bool enabled);
    void setEphemeralSessions(bool enabled) { m_ephemeralSessions = enabled; }

    // Adjuntos (imágenes/docs) a incluir en el PRÓXIMO sendMessage (modelo VL).
    void setPendingAttachments(const QStringList &paths) { m_pendingAttachments = paths; }

    // Servers MCP (stdio) a usar. Cada entrada: {name, command, type, enabled}.
    // Se relanzan en start(). Sus tools se inyectan con prefijo mcp__<server>__<tool>.
    void setMcpServers(const QVariantList &servers);

    // Diff unificado simple (prefijo/sufijo común; líneas +/-/ ).
    static QString makeDiff(const QString &oldText, const QString &newText);

    // Schemas de las tools built-in (sin MCP). Público para reusar en sub-agentes.
    static QJsonArray toolSchemas();

    // Catálogo de tools built-in con metadata para la UI de habilitar/deshabilitar:
    // lista de {name, group, description, approxTokens}. El orden define el de la UI.
    static QVariantList toolCatalog();

    // Tools deshabilitadas por el usuario (nombres built-in y/o mcp__server__tool).
    // Se excluyen de buildToolSchemas() → no se ofrecen al modelo (ahorra contexto).
    void setDisabledTools(const QStringList &names);

    // Config del modelo maestro (tool ask_teacher). Se reenvía al worker.
    void setTeacherConfig(const QString &url, const QString &model, const QString &key);
    // Config de maestro tipo CLI (claude-code / codex). Se reenvía al worker.
    // escalation: "manual"|"auto"|"both"; autoAfterFails dispara el escalado auto.
    void setMasterCli(const QString &kind, const QString &cliName, const QString &cliPath,
                      const QString &escalation, int autoAfterFails,
                      bool applyEdits, int timeoutSec);
    // Escalado manual: el usuario pide pasar el problema actual al maestro.
    // Envía un turno que instruye al agente a usar ask_teacher. Devuelve false si
    // no hay maestro configurado.
    bool escalateToMaster(const QString &problem);
    bool masterConfigured() const { return m_masterKind != QLatin1String("none"); }

    // Memoria por proyecto: ruta del archivo de memoria dentro de un cwd.
    static QString memoryFilePath(const QString &cwd);

    // Consolidación de memoria (background): corre 1 completion sobre el transcript
    // actual y extrae hechos durables → MemoryStore (source="consolidation"). Async,
    // fire-and-forget. Se dispara solo al dejar una sesión y puede invocarse manual.
    void consolidateMemory();

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
    QJsonArray buildToolSchemas() const;             // built-in + MCP (cache)
    static QString toolKind(const QString &name);    // read | write | shell | mcp
    static QStringList requiredArgs(const QString &name);

    // Worker de ejecución de tools (hilo aparte; no bloquea UI).
    void ensureWorker();
    void teardownWorker();

private slots:
    void onServersReady(const QVariantList &toolDefs);
    void onToolExecuted(const QVariantMap &result);
    void onToolStarted(const QVariantMap &info);          // run_shell async: tarjeta en vivo
    void onToolOutputChunk(const QString &callId, const QString &chunk);
    void onSubFinished(const QString &id, const QString &result, bool ok);
    void onSubProgress(const QString &id, const QString &note);
    void flushQueue();                 // envía el próximo mensaje encolado (si lo hay)

private:
    // Subagents (tool `task`): spawn paralelo en git worktrees aisladas.
    void spawnTasks(const QJsonArray &taskCalls);
    void pumpSubs();                   // lanza subs de la cola hasta el cap
    void launchSub(const QJsonObject &call);
    bool subsActive() const { return !m_subQueue.isEmpty() || !m_subs.isEmpty(); }
    QString createWorktree(const QString &callId, bool &isolated);
    QString mergeAndCleanupWorktree(const QString &callId, bool ok, bool isolated);
    void cancelAllSubs();
    static constexpr int kMaxParallelSubs = 3;   // subs concurrentes (no saturar el server)

    bool isBusy() const;               // turno/tool/compactación en curso
    void interruptForSteer();          // aborta y deja m_apiMessages consistente
    void repairDanglingToolCalls();    // cierra tool_calls sin respuesta tras abortar
    void finalizeLiveToolCard(bool cancelled);  // cierra tarjeta run_shell en vivo

private:
    void approveAndContinue(const QString &id, const QString &response); // once|always|reject
    void appendToolResult(const QString &id, const QString &name, const QString &content);
    // Burbuja de asistente: crear/cerrar por iteración para no apilar texto LLM
    // con las tarjetas de tools.
    void ensureAssistantBubble();
    void closeAssistantBubble();
    // Tarjeta separada para una ejecución de tool (nombre, comando, salida).
    void appendToolCard(const QString &name, const QString &kind, bool ok,
                        const QString &command, const QString &output);

    // Streaming SSE (igual patrón que RawChatBackend)
    void handleStreamData();             // parsea m_sseBuf incremental
    void handleStreamFinished(bool ok, const QString &err);
    void resetStreamState();

    // Sesión + persistencia a disco (patrón RawChatBackend)
    void ensureSession();
    QString buildSystemPrompt() const;   // prompt base + memoria del proyecto
    void fetchContextLimit();            // n_ctx desde /props
    // Auto-compactación vía modelo: cuando el historial se acerca al n_ctx del
    // perfil, resume el tramo intermedio con el propio LLM y lo reemplaza por un
    // mensaje de resumen, conservando system + objetivo inicial + cola reciente.
    int  estimateApiTokens() const;      // estimación de tokens del historial API
    bool planCompaction(int &head, int &keepFrom) const;  // ¿hay tramo a compactar?
    void startCompaction(int head, int keepFrom);         // dispara resumen (async)
    void applyCompaction(int head, int keepFrom, const QString &summary); // reemplaza tramo
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
    QNetworkReply *m_compactReply = nullptr;   // request de resumen (compactación)
    bool m_compacting = false;                 // compactación en curso
    QNetworkReply *m_consolidateReply = nullptr;     // request de consolidación de memoria
    QHash<QString, int> m_consolidatedLen;     // sessionId → nº de msgs ya consolidados (dedupe)
    QNetworkReply *m_reply = nullptr;
    bool m_running = false;
    QString m_cwd;
    QString m_approvalMode = QStringLiteral("ask");
    QString m_systemExtra;          // instrucciones extra del usuario (perfil de agente)
    double  m_temperature = -1.0;   // <0 = no enviar (default del server)
    bool    m_thinkingEnabled = false;

    QStringList m_pendingAttachments;  // adjuntos para el próximo sendMessage

    // Permisos por patrón (evaluados antes de la política global).
    enum PermAction { PermDeny, PermAllow, PermAsk };
    struct PermRule { PermAction action; QString kind; QRegularExpression rx; QString glob; };
    QList<PermRule> m_permRules;

    QSet<QString> m_disabledTools;   // tools off por el usuario (built-in y MCP)
    QString m_teacherUrl, m_teacherModel, m_teacherKey;  // ask_teacher (config UI)
    // Maestro CLI (claude-code / codex) por perfil.
    QString m_masterKind = QStringLiteral("none");
    QString m_masterCliName, m_masterCliPath, m_masterEscalation = QStringLiteral("manual");
    int     m_masterAutoAfterFails = 3;
    bool    m_masterApplyEdits = true;
    int     m_masterTimeoutS = 300;
    QSet<QString> m_escalatedSigs;   // firmas ya escaladas al maestro (anti-recursión)
    bool masterAutoEnabled() const {
        return m_masterKind != QLatin1String("none")
            && (m_masterEscalation == QLatin1String("auto")
                || m_masterEscalation == QLatin1String("both"));
    }

    QVariantList m_mcpConfig;        // config de servers MCP (de AppController)
    QVariantList m_mcpTools;         // cache de tool-defs MCP del worker {server,name,description,schema}

    // Worker thread para ejecución de tools (nativas + MCP).
    QThread *m_workerThread = nullptr;
    AgentToolRunner *m_worker = nullptr;
    QString m_execCallId;            // tool_call en ejecución ("" = ninguno)
    QString m_execCommand;           // comando/ruta del tool en ejecución (para la tarjeta)
    qint64  m_lastUiEmitMs = 0;       // throttle de messagesChanged durante streaming
    // run_shell async: tarjeta de tool "en vivo" (creada al arrancar, actualizada
    // con chunks de salida, finalizada al terminar).
    QString m_liveToolCallId;
    int     m_liveToolMsgIdx = -1;
    qint64  m_lastToolEmitMs = 0;     // throttle de chunks de salida del shell
    // Métricas REALES de generación del server (no estimadas chars/4 + wall):
    // del objeto `timings`/`usage` del último completion. predicted_n tokens en
    // predicted_ms ms → tps de generación pura (sin prompt-processing/TTFT).
    int     m_genTokens = 0;
    double  m_genMs = 0.0;

    QString m_sessionId;
    QString m_sessionTitle;
    QVariantList m_sessions;
    bool m_ephemeralSessions = false;
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

    // Read-dedup: ruta relativa → huella (md5) del último contenido leído en la
    // sesión. Re-lectura con la misma huella → stub en el contexto. Se limpia al
    // cambiar/crear sesión.
    QHash<QString, QString> m_readFingerprints;

    // Mensajes encolados (modo "encolar"): se envían uno por uno al terminar cada
    // turno. Se limpia al cambiar/crear sesión.
    QStringList m_msgQueue;

    // Checkpoints para rollback: uno por turno de usuario (antes de enviar). Guarda
    // longitudes de m_messages/m_apiMessages y qué archivos ya estaban editados
    // (para revertir solo los editados DESPUÉS al rebobinar).
    struct Checkpoint { int apiLen; int msgLen; QStringList editKeys; };
    QList<Checkpoint> m_checkpoints;
    void pushCheckpoint();

    // Sub-agentes en vuelo (callId → runner/worktree/branch/tarjeta). Mientras
    // m_subsOutstanding>0 el loop principal espera a que terminen todos.
    QHash<QString, SubAgentRunner *> m_subs;   // corriendo (callId → runner)
    QJsonArray              m_subQueue;        // task calls esperando un slot
    QHash<QString, QString> m_subWorktree;
    QHash<QString, QString> m_subBranch;
    QHash<QString, bool>    m_subIsolated;
    QHash<QString, int>     m_subMsgIdx;
};
