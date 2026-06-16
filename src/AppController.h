#pragma once
#include "core/BinaryRegistry.h"
#include "core/ModelRootRegistry.h"
#include "core/ModelCatalog.h"
#include "core/profiles/ProfileManager.h"
#include "core/profiles/EffectiveProfileBuilder.h"
#include "core/tasks/TaskStore.h"
#include "core/tasks/TaskScheduler.h"
#include "core/agent/IAgentBackend.h"
#include "core/agent/MasterCli.h"
#include "core/SecretStore.h"
#include "core/voice/VoiceServerManager.h"
#include "core/voice/VoiceTypes.h"
#include "core/tuner/TunerWorker.h"
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>
#include <QJsonObject>
#include <QHash>
#include <QPointer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QFile>

class QThread;

class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(BinaryRegistry*     binaryRegistry  READ binaryRegistry  CONSTANT)
    Q_PROPERTY(ModelRootRegistry*  rootRegistry    READ rootRegistry    CONSTANT)
    Q_PROPERTY(ModelCatalog*       modelCatalog    READ modelCatalog    CONSTANT)
    Q_PROPERTY(ProfileManager*     profileManager  READ profileManager  CONSTANT)
    Q_PROPERTY(TaskStore*          taskStore       READ taskStore       CONSTANT)
    Q_PROPERTY(bool tasksSchedulerEnabled READ tasksSchedulerEnabled WRITE setTasksSchedulerEnabled NOTIFY tasksSchedulerChanged)
    Q_PROPERTY(QVariantList chatSessions    READ chatSessions    NOTIFY chatSessionsChanged)
    Q_PROPERTY(QVariantList chatMessages    READ chatMessages    NOTIFY chatMessagesChanged)
    Q_PROPERTY(QString      chatSessionId   READ chatSessionId   NOTIFY chatSessionsChanged)
    Q_PROPERTY(QString      chatSessionTitle READ chatSessionTitle NOTIFY chatSessionsChanged)
    Q_PROPERTY(bool         chatGenerating  READ chatGenerating  NOTIFY chatGeneratingChanged)
    Q_PROPERTY(bool         chatThinkingSupported READ chatThinkingSupported NOTIFY chatThinkingSupportedChanged)
    Q_PROPERTY(bool thinkingEnabled READ thinkingEnabled WRITE setThinkingEnabled NOTIFY thinkingChanged)
    // Render de diagramas Mermaid en el chat (requiere sidecar mermaid-cli).
    Q_PROPERTY(bool mermaidEnabled READ mermaidEnabled WRITE setMermaidEnabled NOTIFY mermaidEnabledChanged)
    Q_PROPERTY(bool   serverRunning   READ serverRunning   NOTIFY serverRunningChanged)
    Q_PROPERTY(bool   serverStopping  READ serverStopping  NOTIFY serverRunningChanged)
    Q_PROPERTY(bool   serverReady     READ serverReady     NOTIFY serverReadyChanged)
    Q_PROPERTY(QString serverState    READ serverState     NOTIFY serverStateChanged)
    Q_PROPERTY(QVariantMap serverStats READ serverStats    NOTIFY serverStatsChanged)
    Q_PROPERTY(QString serverLog      READ serverLog       NOTIFY serverLogChanged)
    Q_PROPERTY(QString activeLaunchId READ activeLaunchId  NOTIFY activeLaunchIdChanged)
    Q_PROPERTY(QVariantMap effectiveProfile READ effectiveProfile NOTIFY effectiveProfileChanged)
    Q_PROPERTY(bool needsSetup READ needsSetup NOTIFY setupStateChanged)
    Q_PROPERTY(bool hasAnyBinary READ hasAnyBinary NOTIFY setupStateChanged)
    Q_PROPERTY(bool hasAnyModel  READ hasAnyModel  NOTIFY setupStateChanged)
    Q_PROPERTY(QString serverBaseUrl READ serverBaseUrl NOTIFY serverRunningChanged)
    // Capacidades del modelo activo. Vision = el server se lanzó con --mmproj.
    Q_PROPERTY(bool serverHasVision READ serverHasVision NOTIFY serverHasVisionChanged)
    // git instalado (requerido por subagents para aislar en worktrees).
    Q_PROPERTY(bool gitAvailable READ gitAvailable NOTIFY gitAvailableChanged)
    Q_PROPERTY(bool installingGit READ installingGit NOTIFY gitAvailableChanged)
    Q_PROPERTY(bool installingOfficialBinary READ installingOfficialBinary NOTIFY installingOfficialBinaryChanged)
    Q_PROPERTY(QString officialBinaryInstallStatus READ officialBinaryInstallStatus NOTIFY officialBinaryInstallStatusChanged)
    Q_PROPERTY(QString officialBinaryInstallLog READ officialBinaryInstallLog NOTIFY officialBinaryInstallLogChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(int langV READ langV NOTIFY languageChanged)
    Q_PROPERTY(bool agentRunning      READ agentRunning      NOTIFY agentRunningChanged)
    Q_PROPERTY(QString agentLog       READ agentLog          NOTIFY agentLogChanged)
    Q_PROPERTY(bool agentStarting     READ agentStarting     NOTIFY agentStartingChanged)
    Q_PROPERTY(QVariantList agentMessages  READ agentMessages  NOTIFY agentMessagesChanged)
    // Streaming incremental: índice del mensaje en streaming (-1 = ninguno) y su
    // texto en vivo. La UI override sólo esa burbuja sin re-bindear toda la lista.
    Q_PROPERTY(int agentStreamingIndex READ agentStreamingIndex NOTIFY agentStreamingChanged)
    Q_PROPERTY(QString agentStreamingText READ agentStreamingText NOTIFY agentStreamingChanged)
    // Mensajes encolados pendientes (modo "encolar"), agente y chat.
    Q_PROPERTY(int agentQueuedCount READ agentQueuedCount NOTIFY agentQueueChanged)
    Q_PROPERTY(int chatQueuedCount READ chatQueuedCount NOTIFY chatQueueChanged)
    Q_PROPERTY(QVariantList agentSessions  READ agentSessions  NOTIFY agentSessionsChanged)
    Q_PROPERTY(QString opencodeSessionId   READ opencodeSessionId   NOTIFY agentSessionsChanged)
    Q_PROPERTY(QString opencodeSessionTitle READ opencodeSessionTitle NOTIFY agentSessionsChanged)
    Q_PROPERTY(QVariantMap agentPendingTool READ agentPendingTool NOTIFY agentPendingToolChanged)
    Q_PROPERTY(QString agentApprovalMode READ agentApprovalMode WRITE setAgentApprovalMode NOTIFY agentApprovalModeChanged)
    Q_PROPERTY(bool agentThinkingEnabled READ agentThinkingEnabled WRITE setAgentThinkingEnabled NOTIFY agentThinkingChanged)
    Q_PROPERTY(bool browserAutomationEnabled READ browserAutomationEnabled WRITE setBrowserAutomationEnabled NOTIFY browserAutomationChanged)
    Q_PROPERTY(QString browserMcpCommand READ browserMcpCommand WRITE setBrowserMcpCommand NOTIFY browserAutomationChanged)
    Q_PROPERTY(QString agentTeacherUrl   READ agentTeacherUrl   WRITE setAgentTeacherUrl   NOTIFY agentTeacherChanged)
    Q_PROPERTY(QString agentTeacherModel READ agentTeacherModel WRITE setAgentTeacherModel NOTIFY agentTeacherChanged)
    Q_PROPERTY(QString agentTeacherKey   READ agentTeacherKey   WRITE setAgentTeacherKey   NOTIFY agentTeacherChanged)
    Q_PROPERTY(bool mailAutoSend READ mailAutoSend WRITE setMailAutoSend NOTIFY mailAutoSendChanged)
    Q_PROPERTY(int agentContextUsed READ agentContextUsed NOTIFY agentContextChanged)
    Q_PROPERTY(int agentContextLimit READ agentContextLimit NOTIFY agentContextChanged)
    Q_PROPERTY(QString agentSystemPrompt READ agentSystemPrompt WRITE setAgentSystemPrompt NOTIFY agentTuningChanged)
    Q_PROPERTY(double agentTemperature READ agentTemperature WRITE setAgentTemperature NOTIFY agentTuningChanged)
    Q_PROPERTY(QString agentPermRules READ agentPermRules WRITE setAgentPermRules NOTIFY agentTuningChanged)
    Q_PROPERTY(QString activeAgentAdapter READ activeAgentAdapter NOTIFY agentRunningChanged)
    Q_PROPERTY(bool agentInTerminal   READ agentInTerminal   NOTIFY agentRunningChanged)
    Q_PROPERTY(bool installingHarness READ installingHarness NOTIFY harnessStatusChanged)
    Q_PROPERTY(QString harnessInstallStatus READ harnessInstallStatus NOTIFY harnessStatusChanged)
    Q_PROPERTY(int harnessCheckV READ harnessCheckV NOTIFY harnessStatusChanged)
    Q_PROPERTY(bool benchmarkRunning READ benchmarkRunning NOTIFY benchmarkRunningChanged)
    Q_PROPERTY(int benchmarkProgress READ benchmarkProgress NOTIFY benchmarkProgressChanged)
    Q_PROPERTY(QString benchmarkStatus READ benchmarkStatus NOTIFY benchmarkStatusChanged)
    Q_PROPERTY(QVariantList benchmarkResults READ benchmarkResults NOTIFY benchmarkResultsChanged)
    Q_PROPERTY(QVariantList customBenchmarks READ customBenchmarks NOTIFY customBenchmarksChanged)
    Q_PROPERTY(bool autoTuneRunning READ autoTuneRunning NOTIFY autoTuneChanged)
    Q_PROPERTY(int autoTuneProgress READ autoTuneProgress NOTIFY autoTuneChanged)
    Q_PROPERTY(QString autoTuneStatus READ autoTuneStatus NOTIFY autoTuneChanged)
    Q_PROPERTY(bool researchRunning READ researchRunning NOTIFY researchChanged)
    Q_PROPERTY(int researchProgress READ researchProgress NOTIFY researchChanged)
    Q_PROPERTY(QString researchStatus READ researchStatus NOTIFY researchChanged)
    Q_PROPERTY(QVariantList researchReports READ researchReports NOTIFY researchReportsChanged)
    Q_PROPERTY(QVariantMap hardwareSummary READ hardwareSummary NOTIFY hardwareSummaryChanged)
    Q_PROPERTY(QVariantList modelRecommendations READ modelRecommendations NOTIFY modelRecommendationsChanged)
    Q_PROPERTY(bool modelDownloadRunning READ modelDownloadRunning NOTIFY modelDownloadChanged)
    Q_PROPERTY(int modelDownloadProgress READ modelDownloadProgress NOTIFY modelDownloadChanged)
    Q_PROPERTY(QString modelDownloadStatus READ modelDownloadStatus NOTIFY modelDownloadChanged)
    // ── Modo Charla (voz-a-voz) ──
    Q_PROPERTY(QString voiceState READ voiceState NOTIFY voiceStateChanged)
    Q_PROPERTY(bool    voiceActive READ voiceActive NOTIFY voiceStateChanged)
    Q_PROPERTY(double  voiceLevel READ voiceLevel NOTIFY voiceLevelChanged)
    Q_PROPERTY(QString voiceError READ voiceError NOTIFY voiceStateChanged)
    Q_PROPERTY(QString voicePartial READ voicePartial NOTIFY voicePartialChanged)

public:
    explicit AppController(QObject *parent = nullptr);

    BinaryRegistry    *binaryRegistry()  { return &m_binaries; }
    ModelRootRegistry *rootRegistry()    { return &m_roots; }
    ModelCatalog      *modelCatalog()    { return &m_catalog; }
    ProfileManager    *profileManager()  { return &m_profiles; }
    TaskStore         *taskStore()       { return &m_tasks; }
    bool tasksSchedulerEnabled() const { return m_scheduler && m_scheduler->enabled(); }
    void setTasksSchedulerEnabled(bool on);

    QVariantList chatSessions()     const { return m_chatSessions; }
    QVariantList chatMessages()     const { return m_chatMessages; }
    QString      chatSessionId()    const { return m_chatSessionId; }
    QString      chatSessionTitle() const { return m_chatSessionTitle; }
    bool         chatGenerating()   const { return m_chatGenerating; }
    bool         chatThinkingSupported() const { return m_chatThinkingSupported; }
    bool   serverRunning()   const { return m_proc && m_proc->state() != QProcess::NotRunning; }
    bool   serverStopping()  const { return m_serverStopping; }
    bool   serverReady()     const { return m_serverReady; }
    QString serverState()    const { return m_serverState; }
    QVariantMap serverStats() const { return m_serverStats; }
    QString serverLog()      const { return m_log; }
    QString activeLaunchId() const { return m_activeLaunchId; }
    QVariantMap effectiveProfile() const { return m_effectiveProfile; }
    bool needsSetup() const { return m_binaries.count() == 0 || m_catalog.count() == 0; }
    bool hasAnyBinary() const { return m_binaries.count() > 0; }
    bool hasAnyModel()  const { return m_catalog.count()  > 0; }
    QString serverBaseUrl() const {
        const QStringList args = m_effectiveProfile.value("effectiveArgs").toStringList();
        QString host = QStringLiteral("127.0.0.1");
        int port = 8080;
        for (int i = 0; i + 1 < args.size(); ++i) {
            if (args[i] == "--host") host = args[i + 1];
            else if (args[i] == "--port") port = args[i + 1].toInt();
        }
        return QStringLiteral("http://%1:%2").arg(host).arg(port);
    }
    bool installingOfficialBinary() const { return m_installingOfficialBinary; }
    QString officialBinaryInstallStatus() const { return m_officialBinaryInstallStatus; }
    QString officialBinaryInstallLog() const { return m_officialBinaryInstallLog; }
    QString language() const { return m_language; }
    void setLanguage(const QString &lang);
    int langV() const { return 0; }
    bool agentRunning() const {
        if (m_agentBackend && m_agentBackend->running()) return true;
        if (m_piActive) return true;
        return m_agentInTerminal ? (m_agentPid != 0) : (m_agentProc && m_agentProc->state() != QProcess::NotRunning);
    }
    bool agentStarting() const { return m_agentStarting || !m_pendingAutoAgentLaunchId.isEmpty(); }
    QString agentLog() const { return m_agentLog; }
    QVariantList agentMessages()  const { return m_agentMessages; }
    int agentStreamingIndex() const { return m_agentStreamingIndex; }
    QString agentStreamingText() const { return m_agentStreamingText; }
    int agentQueuedCount() const { return m_agentQueuedCount; }
    int chatQueuedCount() const { return m_chatQueuedCount; }
    QVariantList agentSessions()  const { return m_agentSessions; }
    QString opencodeSessionId()   const { return m_opencodeSessionId; }
    QString opencodeSessionTitle() const { return m_opencodeSessionTitle; }
    QVariantMap agentPendingTool() const { return m_agentPendingTool; }
    int agentContextUsed() const { return m_agentContextUsed; }
    int agentContextLimit() const { return m_agentContextLimit; }
    QString agentSystemPrompt() const { return m_agentSystemPrompt; }
    double agentTemperature() const { return m_agentTemperature; }
    QString agentPermRules() const { return m_agentPermRules; }
    void setAgentSystemPrompt(const QString &p);
    void setAgentTemperature(double t);
    void setAgentPermRules(const QString &rules);
    QString agentApprovalMode() const { return m_agentApprovalMode; }
    void setAgentApprovalMode(const QString &mode);
    bool agentThinkingEnabled() const { return m_agentThinkingEnabled; }
    void setAgentThinkingEnabled(bool enabled);
    bool thinkingEnabled() const { return m_agentThinkingEnabled; }
    void setThinkingEnabled(bool enabled);
    // Automatización de browser vía MCP Playwright (toggle global; override por perfil).
    bool browserAutomationEnabled() const { return m_browserAutomationEnabled; }
    void setBrowserAutomationEnabled(bool enabled);
    QString browserMcpCommand() const { return m_browserMcpCommand; }
    void setBrowserMcpCommand(const QString &cmd);
    // ── Browser teach: grabar/listar/borrar skills reproducibles ──
    Q_INVOKABLE QStringList listBrowserSkills() const;
    Q_INVOKABLE bool removeBrowserSkill(const QString &name);
    Q_INVOKABLE bool browserSkillExists(const QString &name) const;
    // Lanza Playwright codegen: el usuario graba acciones; al cerrar el inspector
    // se guarda el skill. "" = ok (grabación lanzada); si no, mensaje de error.
    Q_INVOKABLE QString recordBrowserSkill(const QString &name, const QString &url);
    Q_INVOKABLE bool browserRecording() const { return m_browserRecordProc != nullptr; }
    bool mermaidEnabled() const { return m_mermaidEnabled; }
    void setMermaidEnabled(bool enabled);
    QString agentTeacherUrl()   const { return m_agentTeacherUrl; }
    QString agentTeacherModel() const { return m_agentTeacherModel; }
    QString agentTeacherKey()   const { return m_agentTeacherKey; }
    void setAgentTeacherUrl(const QString &url);
    void setAgentTeacherModel(const QString &model);
    void setAgentTeacherKey(const QString &key);
    QString activeAgentAdapter() const { return m_activeAgentAdapter; }
    bool agentInTerminal() const { return m_agentInTerminal; }
    bool installingHarness() const { return m_installingHarness; }
    QString harnessInstallStatus() const { return m_harnessInstallStatus; }
    int harnessCheckV() const { return 0; }
    bool benchmarkRunning() const { return m_benchmarkRunning; }
    int benchmarkProgress() const { return m_benchmarkProgress; }
    QString benchmarkStatus() const { return m_benchmarkStatus; }
    QVariantList benchmarkResults() const { return m_benchmarkResults; }
    QVariantList customBenchmarks() const { return m_customBenchmarks; }
    bool researchRunning() const { return m_researchRunning; }
    int researchProgress() const { return m_researchProgress; }
    QString researchStatus() const { return m_researchStatus; }
    QVariantList researchReports() const { return m_researchReports; }
    QVariantMap hardwareSummary() const { return m_hardwareSummary; }
    QVariantList modelRecommendations() const { return m_modelRecommendations; }
    bool modelDownloadRunning() const { return m_modelDownloadReply != nullptr; }
    int modelDownloadProgress() const { return m_modelDownloadProgress; }
    QString modelDownloadStatus() const { return m_modelDownloadStatus; }

    Q_INVOKABLE void newChatSession();
    Q_INVOKABLE void newChatSessionInProject(const QString &projectId, const QString &projectName);
    Q_INVOKABLE void switchChatSession(const QString &id);
    Q_INVOKABLE void deleteChatSession(const QString &id);
    Q_INVOKABLE void deleteChatProject(const QString &projectName);
    Q_INVOKABLE void moveChatToProject(const QString &id, const QString &projectId, const QString &projectName);
    Q_INVOKABLE QVariantList chatProjects() const;
    Q_INVOKABLE void renameChatSession(const QString &id, const QString &title);
    Q_INVOKABLE void renameChatProject(const QString &oldName, const QString &newName);
    Q_INVOKABLE void sendChatMessage(const QString &text);
    Q_INVOKABLE void sendChatMessageWithAttachments(const QString &text, const QStringList &paths);
    Q_INVOKABLE void steerChat(const QString &text);
    Q_INVOKABLE void queueChat(const QString &text);
    Q_INVOKABLE void clearChatQueue();
    Q_INVOKABLE QStringList pickChatAttachments();
    // Si el portapapeles tiene una imagen, la guarda a temp y devuelve la ruta; "" si no.
    Q_INVOKABLE QString pasteClipboardImage();
    Q_INVOKABLE void stopChatGeneration();
    Q_INVOKABLE void setChatThinkingEnabled(bool enabled);
    Q_INVOKABLE void startServer(const QString &launchProfileId);
    Q_INVOKABLE void startServerAndAgent(const QString &launchProfileId);
    Q_INVOKABLE void stopServer();
    Q_INVOKABLE void computeEffectiveProfile(const QString &launchProfileId);
    // Recalcula la vista previa desde valores en memoria del editor, sin persistir.
    Q_INVOKABLE void computeEffectiveProfilePreview(const QString &launchProfileId,
                                                    const QVariantMap &overrides);
    // --- Router mode (hot-swap) -----------------------------------------
    // Genera un .ini de presets desde varios launch profiles y lo escribe en disco.
    // Devuelve la ruta del .ini, o cadena vacía + serverError() si algo falla.
    Q_INVOKABLE QString generateRouterPreset(const QStringList &launchProfileIds);
    // Arranca un único llama-server en modo router con el preset generado.
    // El swap entre modelos se hace por el campo "model" del request (nombre de sección).
    Q_INVOKABLE void startRouter(const QStringList &launchProfileIds, int modelsMax = 1);
    // Nombres de sección (modelos) cargados en el router activo.
    Q_INVOKABLE QStringList routerModelNames() const { return m_routerModelNames; }
    // Modelo (sección) activo: chat/agente mandan este nombre como campo "model".
    Q_INVOKABLE QString routerActiveModel() const { return m_routerActiveModel; }
    Q_INVOKABLE void setRouterActiveModel(const QString &name);
    Q_INVOKABLE bool serverIsRouter() const { return m_serverIsRouter; }
    Q_INVOKABLE void clearLog();
    // Filtra el log del server por nivel/fuente. level: "all"|"error"|"warn"|"stderr"|
    // "stdout"|"lifecycle"|"health"|"diag". Devuelve sólo las líneas que matchean.
    Q_INVOKABLE QString serverLogByLevel(const QString &level) const;
    // Exporta una sesión de chat a archivo (Markdown o JSON). format: "md"|"json".
    // Abre diálogo de guardado; devuelve la ruta escrita ("" si cancelado/error).
    Q_INVOKABLE QString exportChatSession(const QString &id, const QString &format);
    // Variante headless: escribe directo a `path` (sin diálogo). Devuelve la ruta
    // escrita, o "" + serverError() si falla.
    Q_INVOKABLE QString exportChatSessionTo(const QString &id, const QString &format,
                                            const QString &path);
    // Busca texto en títulos y contenido de todas las sesiones de chat. Devuelve
    // lista de {id,title,projectName,snippet} de sesiones que matchean.
    Q_INVOKABLE QVariantList searchChatHistory(const QString &query) const;
    Q_INVOKABLE void copyToClipboard(const QString &text);
    // Abre el explorador en la carpeta contenedora del archivo (y lo selecciona en Windows).
    Q_INVOKABLE void openContainingFolder(const QString &path);
    Q_INVOKABLE void installOfficialBinary();
    Q_INVOKABLE void cancelOfficialBinaryInstall();
    Q_INVOKABLE void smokeTestServer(const QString &launchProfileId);
    Q_INVOKABLE bool smokeTestRunning() const { return m_smokeTestProc != nullptr; }
    Q_INVOKABLE QString resolveFlag(const QString &binaryId, const QString &flag) const;
    Q_INVOKABLE QString version() const { return QStringLiteral("0.1.0"); }
    Q_INVOKABLE QString l(const QString &key) const;
    Q_INVOKABLE QString lf(const QString &key, const QString &arg1) const { return l(key).arg(arg1); }
    Q_INVOKABLE QVariant readSetting(const QString &key, const QVariant &defaultValue = QVariant()) const;
    Q_INVOKABLE void writeSetting(const QString &key, const QVariant &value);
    Q_INVOKABLE QString exportUserData();
    Q_INVOKABLE QString importUserData();
    // Variantes headless: ruta explícita, sin diálogo.
    Q_INVOKABLE QString exportUserDataTo(const QString &path);
    Q_INVOKABLE QString importUserDataFrom(const QString &path);
    Q_INVOKABLE bool wipeUserData(const QString &kind, const QString &confirmation);
    Q_INVOKABLE QVariantList wipeCategories() const;
    Q_INVOKABLE bool isHarnessInstalled(const QString &adapter) const;
    Q_INVOKABLE void installHarness(const QString &adapter);
    // Maestro CLI (supervisor): detección + metadata para la UI de Perfiles.
    Q_INVOKABLE QStringList masterCliList() const;
    Q_INVOKABLE QVariantMap masterCliStatus(const QString &name, bool force = false);
    Q_INVOKABLE QString masterCliInstallCommand(const QString &name) const;
    Q_INVOKABLE void startAgent(const QString &launchProfileId);
    Q_INVOKABLE void stopAgent();

    // ── Tasks (macros semánticas) ──
    // Ejecuta una Task (botón manual o scheduler): compone el prompt-objetivo y lo
    // resuelve con el agente de forma adaptativa. Si el agente ya corre lo usa; si
    // no, auto-inicia server+agente (perfil de la Task o el activo), ejecuta al
    // quedar listo y lo apaga al terminar. Marca lastRun en el TaskStore.
    Q_INVOKABLE void runTask(const QString &id);
    // Previsualiza el prompt que recibiría el agente (para el editor de Tasks).
    Q_INVOKABLE QString previewTaskPrompt(const QString &id) const;
    // Graba un paso de browser (Playwright codegen) y devuelve el nombre del skill
    // generado para referenciarlo en un paso de Task. "" + serverError si falla.
    Q_INVOKABLE QString recordTaskBrowserStep(const QString &skillName, const QString &url);
    // --- Secretos cloud (API keys fuera del repo) ---
    // ¿Hay key resoluble (env var o store) para esa ref?
    Q_INVOKABLE bool hasSecret(const QString &keyRef) const { return m_secrets.has(keyRef); }
    // Guarda/actualiza la key en el store en disco (no toca env vars). value vacío = borra.
    Q_INVOKABLE void setSecret(const QString &keyRef, const QString &value) { m_secrets.set(keyRef, value); }
    // keyRef del backend cloud de un perfil ("" si el backend no es cloud).
    Q_INVOKABLE QString cloudKeyRefForProfile(const QString &launchProfileId);
    Q_INVOKABLE void sendToAgent(const QString &text);
    // Escalado manual al maestro del perfil activo. Devuelve false si no hay maestro
    // configurado o el agente no está corriendo.
    Q_INVOKABLE bool escalateToMaster(const QString &problem = QString());
    // ¿El agente activo tiene un maestro configurado? (gate del botón en la UI).
    Q_INVOKABLE bool agentMasterConfigured() const;
    Q_INVOKABLE void sendToAgentWithAttachments(const QString &text, const QStringList &paths);
    // Diálogo de adjuntos para el agente; filtra imágenes si el modelo no tiene visión.
    Q_INVOKABLE QStringList pickAgentAttachments();
    // Archivos del proyecto del agente que matchean `query` (para @-mentions). Rutas
    // relativas, salta dirs ignorados, cap 50.
    Q_INVOKABLE QStringList agentProjectFiles(const QString &query) const;
    bool serverHasVision() const { return m_serverHasVision; }
    bool gitAvailable() const { return m_gitAvailable; }
    bool installingGit() const { return m_gitInstallProc != nullptr; }
    Q_INVOKABLE void installGit();
    Q_INVOKABLE void recheckGit();
    // Steering (interrumpe el turno y manda ya) / cola (manda al terminar).
    Q_INVOKABLE void steerAgent(const QString &text);
    Q_INVOKABLE void queueAgent(const QString &text);
    Q_INVOKABLE void clearAgentQueue();
    // Rebobinar la conversación del agente al estado previo a un mensaje de usuario.
    Q_INVOKABLE void rollbackAgentToMessage(int msgIndex);
    // Igual para el chat (RawChatBackend): trunca msgs desde ese índice.
    Q_INVOKABLE void rollbackChatToMessage(int msgIndex);
    // Editar el texto de un mensaje (user o IA) y descartar lo posterior.
    Q_INVOKABLE void editAgentMessage(int msgIndex, const QString &newText);
    Q_INVOKABLE void editChatMessage(int msgIndex, const QString &newText);
    // Aborta la generación/turno en curso sin matar el backend (botón PARAR).
    Q_INVOKABLE void cancelAgentGeneration();
    Q_INVOKABLE void approveAgentTool(const QString &id, bool always = false);
    Q_INVOKABLE void rejectAgentTool(const QString &id);
    Q_INVOKABLE void revertAgentEdit(const QString &path);
    Q_INVOKABLE QString readAgentMemory(const QString &projectDir) const;
    Q_INVOKABLE bool writeAgentMemory(const QString &projectDir, const QString &text);
    Q_INVOKABLE void clearAgentLog();
    Q_INVOKABLE QString agentNativeLogDir(const QString &adapter) const;
    Q_INVOKABLE void openAgentLogDir(const QString &adapter);
    Q_INVOKABLE void openRuntimeLogDir();
    Q_INVOKABLE void newOpencodeSession();
    Q_INVOKABLE void switchOpencodeSession(const QString &sessionId);
    Q_INVOKABLE void refreshOpencodeSessionList();
    Q_INVOKABLE void renameOpencodeSession(const QString &sessionId, const QString &title);
    Q_INVOKABLE void deleteOpencodeSession(const QString &sessionId);
    Q_INVOKABLE void deleteOpencodeProject(const QString &projectDir);
    Q_INVOKABLE void newOpencodeSessionInProject(const QString &projectDir);
    Q_INVOKABLE void forkOpencodeSession(const QString &sessionId);
    // ── Tools del agente (habilitar/deshabilitar built-in) ──
    // Catálogo con metadata + estado enabled, para la UI de toggles.
    Q_INVOKABLE QVariantList agentToolCatalog() const;
    Q_INVOKABLE void setAgentToolEnabled(const QString &name, bool enabled);
    Q_INVOKABLE QString pickDirectory(const QString &title = QString());
    Q_INVOKABLE void changeAgentProject(const QString &directory);
    Q_INVOKABLE QString currentAgentProjectDir() const;

    // ── Opencode config (opencode.json global / por proyecto) ──
    Q_INVOKABLE QString opencodeConfigPath(const QString &scope, const QString &projectDir) const;
    Q_INVOKABLE QString readOpencodeConfig(const QString &scope, const QString &projectDir) const;
    Q_INVOKABLE bool    writeOpencodeConfig(const QString &scope, const QString &projectDir, const QString &jsonText);

    // ── MCP servers (sobre el bloque "mcp" del config) ──
    Q_INVOKABLE QVariantList listMcpServers(const QString &scope, const QString &projectDir) const;
    // Inserta el server MCP de Playwright en el mapa mergeado si la automatización
    // de browser está efectivamente activa: override del LaunchProfile
    // (browserAutomation "on"/"off") pisa el toggle global; "inherit" usa el global.
    void injectBrowserMcp(QMap<QString, QVariant> &merged, const QString &launchId) const;
    // Decisión pura de activación: override del perfil ("on"/"off"/"inherit") sobre
    // el toggle global. Expuesta para test unitario.
    static bool browserMcpEffective(const QString &override, bool globalEnabled);
    Q_INVOKABLE bool setMcpServer(const QString &scope, const QString &projectDir,
                                  const QString &name, const QVariantMap &def);
    Q_INVOKABLE bool removeMcpServer(const QString &scope, const QString &projectDir, const QString &name);
    Q_INVOKABLE bool toggleMcpServer(const QString &scope, const QString &projectDir,
                                     const QString &name, bool enabled);

    // ── Cuentas de correo (globales). El password va a SecretStore (ref
    // "mail/<name>"), nunca al JSON. listMailAccounts NO incluye el password.
    Q_INVOKABLE QVariantList listMailAccounts() const;
    Q_INVOKABLE bool setMailAccount(const QString &name, const QVariantMap &def);
    Q_INVOKABLE bool removeMailAccount(const QString &name);
    // Prueba la cuenta (login SMTP + recepción). "" = OK; si no, mensaje de error.
    Q_INVOKABLE QString testMailAccount(const QString &name) const;
    bool mailAutoSend() const { return m_mailAutoSend; }
    void setMailAutoSend(bool on);
    // Cuentas con el password resuelto (para inyectar al backend del agente).
    QVariantList mailAccountsResolved() const;

    // ── Integrations (registro unificado: MCP global + API services) ──
    // Lista agregada de conexiones externas. Cada item: {id,type,name,enabled,
    // summary,config{}}. type = "mcp" | "api_service".
    Q_INVOKABLE QVariantList integrations() const;
    // Alta/edición de un MCP Tool Server (escribe en el config opencode global).
    Q_INVOKABLE bool saveMcpIntegration(const QString &name, const QString &type,
                                        const QString &commandOrUrl);
    // Alta/edición de un API Service genérico (endpoint HTTP + key). id vacío = crear.
    Q_INVOKABLE bool saveApiService(const QString &id, const QString &name,
                                    const QString &baseUrl, const QString &apiKey, bool enabled);
    Q_INVOKABLE bool removeIntegration(const QString &id);
    Q_INVOKABLE bool setIntegrationEnabled(const QString &id, bool enabled);
    // Test asíncrono. Emite integrationTestResult(id, ok, message).
    Q_INVOKABLE void testIntegration(const QString &id);

    // ── Skills / comandos (.opencode/command/*.md) ──
    Q_INVOKABLE QVariantList listOpencodeCommands(const QString &scope, const QString &projectDir) const;
    Q_INVOKABLE QString readOpencodeCommand(const QString &scope, const QString &projectDir, const QString &name) const;
    Q_INVOKABLE bool writeOpencodeCommand(const QString &scope, const QString &projectDir,
                                          const QString &name, const QString &content);
    Q_INVOKABLE bool deleteOpencodeCommand(const QString &scope, const QString &projectDir, const QString &name);
    Q_INVOKABLE void startBenchmark(const QStringList &profileIds, const QString &mode, int passes = 1,
                                    const QString &target = QStringLiteral("model"), int timeoutSec = 0);
    Q_INVOKABLE void cancelBenchmark();
    Q_INVOKABLE void openBenchmarkFolder(const QString &path);
    Q_INVOKABLE void clearBenchmarkResults();
    Q_INVOKABLE void removeBenchmarkResult(int index);
    Q_INVOKABLE void removeBenchmarkResultById(const QString &id);
    Q_INVOKABLE void loadBenchmarkResults();
    // Custom benchmarks (user-defined prompt sets)
    Q_INVOKABLE void loadCustomBenchmarks();
    Q_INVOKABLE QString saveCustomBenchmark(const QVariantMap &def); // returns id
    Q_INVOKABLE void deleteCustomBenchmark(const QString &id);
    // Importa una EvalSuite JSON (src/core/eval) como custom benchmark. Devuelve
    // el id creado, o "" si falla (motivo en lastEvalImportError()).
    Q_INVOKABLE QString importEvalSuite(const QString &path);
    Q_INVOKABLE QString lastEvalImportError() const { return m_lastEvalImportError; }
    Q_INVOKABLE void startCustomBenchmark(const QStringList &profileIds, const QString &customId, int passes = 1,
                                          const QString &target = QStringLiteral("model"), int timeoutSec = 0);
    // Auto-tuning de parámetros de inferencia (AutoTuner TPE-lite + gate de
    // calidad). Lanza llama-server por candidato en un puerto scratch, mide
    // tok/s y calidad, y al terminar fusiona la mejor config en extraArgs del
    // launch profile. maxTrials/qualityGate/nPredict son opcionales.
    Q_INVOKABLE void startAutoTune(const QString &launchProfileId, int maxTrials = 24,
                                   double qualityGate = 0.0, int nPredict = 256);
    Q_INVOKABLE void cancelAutoTune();
    bool autoTuneRunning() const { return m_autoTuneRunning; }
    int autoTuneProgress() const { return m_autoTuneProgress; }
    QString autoTuneStatus() const { return m_autoTuneStatus; }

    Q_INVOKABLE void startResearch(const QString &topic, const QString &mode, int maxPages);
    Q_INVOKABLE void cancelResearch();
    Q_INVOKABLE void refreshResearchReports();
    Q_INVOKABLE QString readResearchReport(const QString &id) const;
    Q_INVOKABLE void openResearchReport(const QString &id);
    Q_INVOKABLE void deleteResearchReport(const QString &id);
    Q_INVOKABLE void rescanHardware();
    // ── GPU power limit (nvidia-smi) ──
    // Estado actual por GPU: {available:bool, gpus:[{index,name,currentW,defaultW,
    // minW,maxW,drawW}]}. Lectura síncrona (rápida, usada on-demand desde Ajustes).
    Q_INVOKABLE QVariantMap gpuPowerInfo() const;
    // Fija el power limit (W) en una GPU (gpuIndex<0 = todas). En Windows requiere
    // elevación → se relanza nvidia-smi vía powershell RunAs. Devuelve "" si OK o
    // un mensaje de error. Persiste el valor como setting global "gpuPowerLimitW".
    Q_INVOKABLE QString setGpuPowerLimit(int watts, int gpuIndex = -1);
    // Parser de la salida CSV de nvidia-smi (--query-gpu power.*). Estático para
    // testear sin GPU. Devuelve lista de QVariantMap por GPU.
    static QVariantList parseGpuPowerCsv(const QString &csv);
    // Escaneo pesado de arranque (binaries/roots/hardware/catálogo + migraciones).
    // Diferido fuera del constructor; QML lo invoca tras pintar el popup de carga.
    Q_INVOKABLE void runStartupScan();
    Q_INVOKABLE void downloadRecommendedModel(const QString &repo, const QString &fileName);
    Q_INVOKABLE void openModelRecommendation(const QString &repo);

    // ── Modo Charla (voz-a-voz) ──
    QString voiceState() const;
    bool    voiceActive() const;
    double  voiceLevel() const;
    QString voiceError() const;
    QString voicePartial() const { return m_voicePartial; }
    // Config de Charla POR LaunchProfile (vive en el perfil; la Charla usa la del
    // perfil activo). El setter persiste en el perfil y, si es el activo, la aplica
    // al controller vivo.
    Q_INVOKABLE QVariantMap voiceConfig(const QString &profileId) const;
    Q_INVOKABLE void setVoiceConfig(const QString &profileId, const QVariantMap &cfg);
    Q_INVOKABLE void startCharla();   // arranca la sesión de voz (usa el backend de chat)
    Q_INVOKABLE void stopCharla();
    Q_INVOKABLE void charlaListen();  // fuerza escucha (corta el TTS si suena)
    // Micrófonos disponibles: [{id,name,isDefault}].
    Q_INVOKABLE QVariantList audioInputDevices() const;
    // Micrófono elegido (persistido en setting "voiceInputDevice"; "" = default).
    Q_INVOKABLE QString voiceInputDevice() const;
    Q_INVOKABLE void setVoiceInputDevice(const QString &id);
    // Prueba de micrófono: captura y muestra nivel sin STT/chat (ver voiceLevel).
    Q_INVOKABLE void startMicTest();
    Q_INVOKABLE void stopMicTest();
    // ── STT gestionado (descarga + lanza whisper.cpp) ──
    Q_INVOKABLE QVariantList voiceSttCatalog() const;
    Q_INVOKABLE bool voiceModelInstalled(const QString &engineId) const;
    Q_INVOKABLE void installVoiceModel(const QString &engineId);
    Q_INVOKABLE void cancelVoiceModelInstall();
    // Ruta del binario whisper-server (setting global; "" = buscar en PATH).
    Q_INVOKABLE QString voiceWhisperServerPath() const;
    Q_INVOKABLE void setVoiceWhisperServerPath(const QString &path);
    Q_INVOKABLE QString pickVoiceWhisperServer();   // diálogo de archivo; devuelve la ruta
    // ── TTS gestionado (piper, process-mode) ──
    Q_INVOKABLE QVariantList voiceTtsCatalog() const;
    Q_INVOKABLE bool voiceTtsVoiceInstalled(const QString &voiceId) const;
    Q_INVOKABLE void installVoiceTts(const QString &voiceId);
    Q_INVOKABLE QString voicePiperPath() const;
    Q_INVOKABLE void setVoicePiperPath(const QString &path);
    Q_INVOKABLE QString pickVoicePiper();
    // Auto-descarga de binarios (kind: "whisper-server"|"piper"). Al terminar OK
    // fija la ruta correspondiente. urlOverride vacío = URL por defecto del SO.
    Q_INVOKABLE void installVoiceBinary(const QString &kind, const QString &urlOverride = QString());
    Q_INVOKABLE QString voiceBinaryDefaultUrl(const QString &kind) const;

signals:
    void voiceStateChanged();
    void voiceLevelChanged();
    void voicePartialChanged();
    void voiceInstallProgress(const QString &engineId, int pct, const QString &status);
    void voiceInstallFinished(const QString &engineId, bool ok, const QString &message);
    void voiceBinaryInstalled(const QString &kind, bool ok, const QString &message);
    // Un perfil cloud necesita su API key y no se pudo resolver (ni env var ni store):
    // la UI debe pedirla y llamar setSecret(keyRef, value) antes de reintentar.
    void cloudSecretRequired(const QString &launchProfileId, const QString &keyRef);
    void serverRunningChanged();
    void serverReadyChanged();
    void serverStateChanged();
    void serverStatsChanged();
    void serverHasVisionChanged();
    void gitAvailableChanged();
    // El agente pidió subagents pero falta git → la UI ofrece instalarlo.
    void gitRequiredForSubagents();
    void serverLogChanged();
    void activeLaunchIdChanged();
    void browserAutomationChanged();
    void browserSkillsChanged();
    void effectiveProfileChanged();
    void routerStateChanged();
    void setupStateChanged();
    void installingOfficialBinaryChanged();
    void officialBinaryInstallStatusChanged();
    void officialBinaryInstallLogChanged();
    void officialBinaryInstallFinished(bool success, const QString &message, const QString &binaryPath);
    void integrationsChanged();
    void integrationTestResult(const QString &id, bool ok, const QString &message);
    void serverError(const QString &message);
    // Diagnóstico detectado por regex en el log del server (OOM, puerto, modelo cargado…).
    // level: "error" | "warn" | "info".
    void serverDiagnostic(const QString &level, const QString &message);
    void smokeTestFinished(bool passed, const QString &output);
    void languageChanged();
    void harnessStatusChanged();
    void harnessInstallFinished(bool success, const QString &adapter, const QString &message);
    void chatSessionsChanged();
    void chatMessagesChanged();
    void chatGeneratingChanged();
    void chatThinkingSupportedChanged();
    void thinkingChanged();
    void mermaidEnabledChanged();
    void agentRunningChanged();
    void agentStartingChanged();
    void agentLogChanged();
    void agentMessagesChanged();
    void agentStreamingChanged();
    void agentQueueChanged();
    void chatQueueChanged();
    void agentSessionsChanged();
    void agentPendingToolChanged();
    void agentApprovalModeChanged();
    void agentThinkingChanged();
    void agentToolsChanged();
    void agentTeacherChanged();
    void mailAutoSendChanged();
    void agentContextChanged();
    void agentTuningChanged();
    void benchmarkRunningChanged();
    void benchmarkProgressChanged();
    void benchmarkStatusChanged();
    void benchmarkResultsChanged();
    void customBenchmarksChanged();
    void autoTuneChanged();
    // Cada trial evaluado: índice/total, tok/s, calidad [0,1], resumen de flags.
    void autoTuneTrial(int index, int total, double throughput, double quality,
                       const QString &summary);
    // Fin del tuning: ok=true si encontró config válida; bestArgs ya fusionados.
    void autoTuneFinished(bool ok, const QString &bestArgs, double throughput,
                          double quality);
    void researchChanged();
    void researchReportsChanged();
    void hardwareSummaryChanged();
    void modelRecommendationsChanged();
    void modelDownloadChanged();
    void tasksSchedulerChanged();

private:
    void appendLog(const QString &text);
    void appendServerEvent(const QString &source, const QString &text);
    // Escanea líneas del server por patrones conocidos y emite serverDiagnostic.
    void detectServerLogPatterns(const QString &text);
    void appendAgentEvent(const QString &source, const QString &text);
    QString runtimeLogDir() const;
    void rotateLogIfNeeded(const QString &path) const;
    void appendFileLog(const QString &path, const QString &line) const;
    void finishSmokeTest(bool passed, const QString &output);
    EffectiveProfileBuilder::Context buildContext(const QString &launchProfileId);
    // Resuelve la cadena de fallbacks del maestro (keys/cliPath/profile→http) a
    // una QVariantList lista para el worker del agente.
    QVariantList buildMasterChain(const MasterConfig &mc);

    BinaryRegistry    m_binaries;
    ModelCatalog      m_catalog;
    ModelRootRegistry m_roots{&m_catalog};
    ProfileManager    m_profiles;
    TaskStore         m_tasks;
    TaskScheduler    *m_scheduler = nullptr;
    // Task en ejecución (para marcar lastRun ok al terminar el turno).
    QString  m_runningTaskId;
    // Task programada esperando que el agente auto-iniciado quede listo.
    QString  m_pendingScheduledTaskId;
    QString  m_pendingScheduledLaunchId;
    // El agente fue auto-iniciado por el scheduler → apagarlo al terminar el turno.
    bool     m_scheduledAutoStop = false;
    void dispatchPendingScheduledTask();
    // Maneja el fin de turno del agente (marca lastRun, apaga si fue auto-iniciado).
    void onAgentTurnFinished();

    QProcess *m_proc = nullptr;
    QProcess *m_installerProc = nullptr;
    QProcess *m_smokeTestProc = nullptr;
    QTimer   *m_smokeTestTimer = nullptr;
    QString   m_smokeTestLog;
    bool      m_smokeTestDone = false;
    QString   m_log;
    QString   m_serverLogFilePath;
    QString   m_activeLaunchId;
    QString   m_pendingAutoAgentLaunchId;
    bool      m_serverStopping = false;
    bool      m_serverReady    = false;
    bool      m_serverHasVision = false;
    bool      m_gitAvailable = false;
    QProcess *m_gitInstallProc = nullptr;
    QTimer   *m_stopKillTimer  = nullptr;
    QTimer   *m_healthPollTimer = nullptr;
    void startHealthPolling();
    void stopHealthPolling();
    // Watchdog auto-restart on crash
    QString   m_serverState = QStringLiteral("stopped"); // stopped|running|restarting|failed
    int       m_serverRestartCount = 0;
    QTimer   *m_serverRestartTimer = nullptr;
    static constexpr int kMaxServerRestarts = 3;
    void setServerState(const QString &s);
    // Live VRAM/stats meter (nvidia-smi async poll while server runs)
    QTimer   *m_vramPollTimer = nullptr;
    QProcess *m_vramProc = nullptr;
    QVariantMap m_serverStats;
    void startVramPolling();
    void stopVramPolling();
    void pollServerStats();
    // Aplica el power limit configurado al arrancar un server: usa el override del
    // launch profile (powerLimitW>0) o, si no, el global "gpuPowerLimitW". No-op si
    // ambos son 0 o no hay nvidia-smi. Loguea a eventos del server.
    void applyConfiguredPowerLimit(const LaunchProfile &launch);
    QVariantMap m_effectiveProfile;
    QStringList m_routerModelNames;   // secciones cargadas en el router activo
    QString m_routerActiveModel;      // sección activa (campo "model" de los requests)
    int m_benchHardTimeoutSec = 0;    // 0 = sin límite; timeout duro (wall-clock) por corrida
    bool m_serverIsRouter = false;    // el m_proc actual es un router
    // Devuelve la sección activa del router si corresponde, sino el fallback.
    QString routedModelId(const QString &fallback) const {
        return (m_serverIsRouter && !m_routerActiveModel.isEmpty()) ? m_routerActiveModel : fallback;
    }
    bool m_installingOfficialBinary = false;
    QString m_officialBinaryInstallStatus;
    QString m_officialBinaryInstallLog;
    bool m_cancelingOfficialBinaryInstall = false;
    bool m_timeoutOfficialBinaryInstall = false;
    QDateTime m_lastInstallProgressAt;
    QTimer *m_installWatchdog = nullptr;
    QString m_language;
    bool m_installingHarness = false;
    QString m_harnessInstallStatus;
    QProcess *m_harnessProc = nullptr;
    QProcess *m_agentProc = nullptr;
    qint64    m_agentPid = 0;
    bool      m_agentInTerminal = false;
    QTimer   *m_agentPollTimer = nullptr;
    QString   m_agentLog;
    QString   m_agentLogFilePath;
    bool      m_agentStarting = false;
    QString   m_activeAgentAdapter;
    QString   m_agentCwdOverride;   // directory for next/current agent start
    QString   m_pendingAgentLaunchId; // used when restarting for project change
    // pi harness: modo print por-mensaje (sin proceso persistente)
    bool                m_piActive = false;
    QProcess           *m_piMsgProc = nullptr;
    QProcessEnvironment m_piEnv;
    QString             m_piExe;
    QString             m_piCwd;
    QString             m_piSessionPath;
    // Backend de agente activo (opencode/goose/raw). Dueño de su proceso/conexión.
    IAgentBackend      *m_agentBackend = nullptr;
    // Backend de chat directo (raw), separado del modo Agente.
    IAgentBackend      *m_chatBackend = nullptr;
    // Modo Charla (voz-a-voz): orquestador STT→chat→TTS. Reusa m_chatBackend.
    class VoiceController *m_voice = nullptr;
    VoiceServerManager m_voiceServers;  // catálogo + descarga de modelos STT
    QProcess *m_sttProc = nullptr;      // server STT gestionado (whisper.cpp)
    bool m_charlaActive = false;
    bool m_chatWasGenerating = false;
    QString m_voicePartial;
    void ensureVoice();                 // crea + configura el controller (lazy)
    void applyVoiceConfig();            // empuja config + keys resueltas al controller
    void startManagedStt(const VoiceConfig &c);  // lanza whisper-server del perfil activo
    void stopManagedStt();
    QString voiceConfigPath() const;
    // Chat session state
    QString       m_chatProjectIdOverride;
    QString       m_chatProjectNameOverride;
    QVariantList  m_chatSessions;
    QVariantList  m_chatMessages;
    QString       m_chatSessionId;
    QString       m_chatSessionTitle;
    bool          m_chatGenerating = false;
    bool          m_chatThinkingSupported = false;
    void fetchChatThinkingSupport();
    QNetworkReply *m_chatReply = nullptr;
    int           m_chatAssistantIdx = -1;
    QString       chatStorageDir() const;
    void          loadChatSessions();
    // Agrega la sesión activa aún no persistida (sin mensajes) al vector ANTES de
    // agrupar/ordenar, para que caiga dentro del grupo de su proyecto (no como
    // sección duplicada al tope).
    void          injectDraftSession(QVector<QVariantMap> &sessions);
    void          saveChatSession();
    void          loadChatSessionMessages(const QString &id);

    QNetworkAccessManager *m_nam = nullptr;
    // Espejos del backend de agente activo (poblados desde IAgentBackend).
    QString   m_opencodeSessionId;
    QString   m_opencodeSessionTitle;
    bool           m_agentStopping = false;   // reservado para path genérico (stdin)
    QVariantList m_agentMessages;
    int       m_agentStreamingIndex = -1;
    QString   m_agentStreamingText;
    int       m_agentQueuedCount = 0;
    int       m_chatQueuedCount = 0;
    int       m_currentAssistantIdx = -1;
    QVariantList m_agentSessions;
    QVariantMap m_agentPendingTool;   // tool esperando aprobación ({} si ninguna)
    QString   m_agentApprovalMode = QStringLiteral("ask");  // auto | ask | manual | super
    bool      m_agentThinkingEnabled = false;   // razonamiento global (chat/agente/benchmark/research)
    // Automatización de browser (MCP Playwright). Toggle global; cada LaunchProfile
    // puede forzar on/off con browserAutomation ("inherit"|"on"|"off").
    bool      m_browserAutomationEnabled = false;
    QString   m_browserMcpCommand = QStringLiteral("npx @playwright/mcp@latest");
    QProcess *m_browserRecordProc = nullptr;   // codegen en curso (modo teach)
    bool      m_mermaidEnabled = true;          // render de diagramas mermaid en el chat
    QStringList m_agentDisabledTools;           // tools built-in apagadas por el usuario
    QString   m_agentTeacherUrl;                // ask_teacher: endpoint OpenAI-compat
    QString   m_agentTeacherModel;
    QString   m_agentTeacherKey;
    MasterCli m_masterCli;                      // detección de CLIs maestro (claude/codex)
    SecretStore m_secrets;                       // API keys cloud (fuera del repo)
    bool        m_mailAutoSend = false;          // permitir email_send sin aprobación
    int       m_agentContextUsed = 0;
    int       m_agentContextLimit = -1;
    QString   m_agentSystemPrompt;
    QString   m_agentPermRules;
    double    m_agentTemperature = -1.0;
    double    m_resolvedProfileTemperature = -1.0;

    // Managed-process lifecycle
#ifdef Q_OS_WIN
    void *m_jobObject = nullptr;   // HANDLE — typed as void* to avoid pulling windows.h into header
#endif
    void createJobObject();
    void assignToJobObject(qint64 pid);
    // Reenvía las cuentas de correo (password resuelto) + flag auto-send al
    // backend del agente activo, si lo hay.
    void pushMailAccountsToAgent();
    void killManagedOrphans();
    void writeServiceState(const QString &role, qint64 pid, const QVariantMap &extra = {});
    void ensurePiConfig(const QString &openaiBaseUrl);
    void sendPiMessage(const QString &text);
    // Crea/asegura el backend para el adapter dado y conecta señales→QML.
    IAgentBackend *ensureAgentBackend(const QString &adapter);
    IAgentBackend *ensureChatBackend();
    // Helpers de config opencode
    QString ocGlobalConfigDir() const;
    QString ocConfigFilePath(const QString &scope, const QString &projectDir) const;
    QString ocCommandDir(const QString &scope, const QString &projectDir) const;
    QJsonObject ocReadConfigObj(const QString &scope, const QString &projectDir) const;
    bool ocWriteConfigObj(const QString &scope, const QString &projectDir, const QJsonObject &obj);
    void clearServiceState(const QString &role);
    QString serviceStatePath() const;

    // Integrations: API services persistidos en JSON propio.
    QString integrationsFilePath() const;
    QJsonArray readApiServices() const;
    bool writeApiServices(const QJsonArray &arr);
    QJsonObject exportFileSet(const QString &root, const QStringList &relativePaths) const;
    bool importFileSet(const QString &root, const QJsonObject &set, QStringList *written);
    bool removePathForWipe(const QString &path);
    void reloadPersistentStateAfterImportOrWipe();

    static const QHash<QString, QHash<QString, QString>> &translations();

    // Benchmark
    bool         m_benchmarkRunning  = false;
    bool         m_benchmarkCanceled = false;
    QPointer<QNetworkReply> m_benchmarkActiveReply; // in-flight req, aborted on cancel
    QPointer<IAgentBackend> m_benchmarkAgent;       // dedicated headless agent (agent target)
    int          m_benchmarkProgress = 0;
    QString      m_benchmarkStatus;
    QVariantList m_benchmarkResults;
    QVariantList m_customBenchmarks;
    QString      m_lastEvalImportError;
    // Auto-tuning
    bool         m_autoTuneRunning = false;
    int          m_autoTuneProgress = 0;
    QString      m_autoTuneStatus;
    QString      m_autoTuneLaunchId;     // perfil objetivo del tuning en curso
    QThread     *m_tuneThread = nullptr;
    TunerWorker *m_tuneWorker = nullptr;
    void onAutoTuneFinished(bool ok, const QStringList &bestArgs,
                            double throughput, double quality);
    QVariantMap  m_hardwareSummary;
    QVariantList m_modelRecommendations;
    QNetworkReply *m_modelDownloadReply = nullptr;
    QFile *m_modelDownloadFile = nullptr;
    QString m_modelDownloadPath;
    int m_modelDownloadProgress = 0;
    QString m_modelDownloadStatus;
    QString benchmarkStorageDir() const;
    QString customBenchmarkDir() const;   // dir holding custom benchmark definitions
    QString benchmarkRunsDir() const;     // root for isolated timestamped run folders
    void saveBenchmarkResult(const QVariantMap &result);
    QString benchmarkServerLogTail(int maxBytes = 24000) const;
    void saveBenchmarkFailureResult(const QString &profileId, const QString &profileName,
                                    int pass, int passes, const QString &mode,
                                    const QString &target, const QString &benchmarkName,
                                    const QString &runLabel, const QString &runDir,
                                    const QString &stage, const QString &message,
                                    const QString &detail, double elapsedSec = 0.0);
    void runBenchmarkInternal(const QStringList &profileIds, const QString &mode,
                              const QVariantList &customTasks, const QString &runLabel,
                              int passes, const QString &target = QStringLiteral("model"));
    // Agent-target benchmark: drives a dedicated headless LlamaAgentBackend so the
    // model uses tools and writes real files into an isolated per-profile workspace.
    void runAgentBenchmark(const QString &profileId, const QString &profName, int idx, int total,
                           const QVariantList &tasks, int passes, const QString &mode,
                           const QString &runLabel, const QString &runDir,
                           std::function<void()> onProfileDone);
    void benchmarkWaitServerReady(int attemptsLeft, int totalAttempts, const QString &url,
                                  const QString &statusPrefix,
                                  std::function<void(bool)> onResult,
                                  qint64 waitStartMs = 0,
                                  qint64 lastActivityMs = 0,
                                  qint64 lastLogSize = -1);
    void benchmarkWaitServerStopped(int remainingMs, std::function<void()> onStopped);
    void benchmarkKillStrayServers();
    void benchmarkRequest(const QString &url, const QString &prompt,
                          int maxTokens, bool streaming,
                          std::function<void(QVariantMap)> onDone,
                          const QString &resultType = QString());
    QPair<double, double> benchmarkMeasureResourcesNow() const;
    void benchmarkMeasureResources(std::function<void(double ramMb, double vramMb)> onDone);
    QString modelDownloadDir() const;
    void rebuildModelRecommendations();

    // Model quality benchmarks (Artificial Analysis Intelligence Index).
    // Bundled table is the offline fallback; a weekly live fetch overlays it.
    QHash<QString, double> m_benchmarkQuality;
    QHash<QString, double> m_cookbookPriority;
    bool m_benchmarkLoaded = false;
    QNetworkReply *m_benchmarkFetchReply = nullptr;
    QString benchmarkCachePath() const;       // writable cache for fetched data
    void loadBenchmarkScores();               // populate m_benchmarkQuality (cache→bundled)
    void maybeFetchBenchmarks();              // weekly live refresh, async, best-effort

    // Deep Research
    bool m_researchRunning = false;
    int m_researchProgress = 0;
    QString m_researchStatus;
    QVariantList m_researchReports;
    QNetworkReply *m_researchReply = nullptr;
    QString researchStorageDir() const;
    void setResearchState(bool running, int progress, const QString &status);
    void saveResearchReport(const QVariantMap &summary, const QString &markdown,
                            const QJsonObject &full);
};
