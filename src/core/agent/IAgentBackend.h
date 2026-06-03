#pragma once
#include "AgentTypes.h"
#include <QObject>
#include <QStringList>
#include <QVariantList>

// Interfaz común para todos los runtimes de agente (opencode, goose, raw, ...).
// Cada backend es un QObject que gestiona su proceso/conexión y emite señales
// que AppController repropaga a QML. La UI nunca conoce el backend concreto.
class IAgentBackend : public QObject
{
    Q_OBJECT
public:
    explicit IAgentBackend(QObject *parent = nullptr) : QObject(parent) {}
    ~IAgentBackend() override = default;

    virtual QString adapter() const = 0;
    virtual bool running() const = 0;

    // Ciclo de vida
    virtual void start(const AgentContext &ctx) = 0;
    virtual void stop() = 0;

    // Conversación
    virtual void sendMessage(const QString &text) = 0;
    virtual void cancelGeneration() {}
    // Steering: interrumpe el turno/generación en curso (cancela tools/aprobación
    // pendientes) y envía `text` como turno nuevo de inmediato. Default: si no hay
    // soporte específico, equivale a enviar normal.
    virtual void steerMessage(const QString &text) { sendMessage(text); }
    // Cola: si hay un turno en curso, guarda `text` y lo envía cuando el turno
    // termine por completo; si no hay turno, lo envía ya. Default: enviar normal.
    virtual void queueMessage(const QString &text) { sendMessage(text); }
    virtual int queuedCount() const { return 0; }
    virtual QStringList queuedMessages() const { return {}; }
    virtual void clearQueue() {}

    // Sesiones (no todos los backends las soportan; default: no-op)
    virtual void newSession() {}
    virtual void newSessionInProject(const QString &projectDir) { Q_UNUSED(projectDir) }
    virtual void switchSession(const QString &sessionId) { Q_UNUSED(sessionId) }
    virtual void renameSession(const QString &sessionId, const QString &title)
        { Q_UNUSED(sessionId) Q_UNUSED(title) }
    virtual void deleteSession(const QString &sessionId) { Q_UNUSED(sessionId) }
    // Borra TODAS las sesiones de un proyecto (default: no-op).
    virtual void deleteProject(const QString &projectDir) { Q_UNUSED(projectDir) }
    virtual void forkSession(const QString &sessionId) { Q_UNUSED(sessionId) }
    virtual void refreshSessions() {}

    // Aprobación de herramientas (human-in-the-loop)
    virtual void approveTool(const QString &id, bool always = false)
        { Q_UNUSED(id) Q_UNUSED(always) }
    virtual void rejectTool(const QString &id) { Q_UNUSED(id) }
    // Política: "auto" (aprueba todo), "ask" (auto lectura, pide escritura+shell),
    // "manual" (pide todo). Default del backend: "ask".
    virtual void setApprovalPolicy(const QString &mode) { Q_UNUSED(mode) }
    // Revertir una edición de archivo aplicada por el agente (default: no-op).
    virtual void revertEdit(const QString &path) { Q_UNUSED(path) }
    // Ajuste del agente: system prompt extra + temperatura (<0 = default del server).
    virtual void setAgentTuning(const QString &systemExtra, double temperature)
        { Q_UNUSED(systemExtra) Q_UNUSED(temperature) }

    // Estado expuesto a la UI
    virtual QString currentSessionId() const { return {}; }
    virtual QString currentSessionTitle() const { return {}; }
    virtual QString currentProjectDir() const { return {}; }
    virtual QVariantList messages() const { return {}; }
    virtual QVariantList sessions() const { return {}; }

signals:
    void runningChanged();
    void messagesChanged();
    // Update incremental SOLO del texto del mensaje en `index` durante streaming.
    // Permite a la UI refrescar una burbuja sin reconstruir toda la lista (evita
    // el jank de re-instanciar todos los delegates por cada token). Estructura
    // (mensajes nuevos, tarjetas de tool, fin de turno) sigue por messagesChanged.
    void streamingText(int index, const QString &content);
    // La cola de mensajes pendientes cambió (encolar/desencolar/limpiar).
    void queueChanged();
    void sessionsChanged();
    void logAppended(const QString &chunk);
    void toolApprovalNeeded(const QVariantMap &toolCall);
    void errorOccurred(const QString &message);
    // Uso de contexto del último turno: tokens usados / límite (n_ctx). -1 = desconocido.
    void contextUsage(int used, int limit);
};
