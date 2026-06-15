#pragma once
#include <QString>
#include <QStringList>
#include <QMap>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>

struct BackendProfile {
    QString id;
    QString name;
    QString binaryId;
    QString host = "127.0.0.1";
    int port = 8080;
    QStringList baseArgs;
    QMap<QString, QString> envOverrides;

    // Provider del backend. "local" = llama-server propio (binaryId/host/port).
    // "cloud" = endpoint OpenAI-compat externo (OpenAI/OpenRouter/Groq/DeepSeek…);
    // no lanza proceso ni binario, el agente pega directo al cloudBaseUrl.
    QString kind = "local";    // local | cloud
    QString cloudBaseUrl;      // sin /v1, ej https://api.openai.com
    // Nombre de la referencia al secreto (NO el secreto). El valor se resuelve vía
    // SecretStore (env var o store en disco fuera del repo). Nunca se serializa la key.
    QString cloudKeyRef;
    QString cloudModel;        // nombre de modelo a enviar (ej gpt-4o, anthropic/claude-...)
    int     cloudCtx = 0;      // n_ctx fallback (cloud no expone /props); 0 = default

    bool isCloud() const { return kind == QLatin1String("cloud"); }

    QJsonObject toJson() const;
    static BackendProfile fromJson(const QJsonObject &obj);
    static QString generateId();
};

struct ModelProfile {
    QString id;
    QString name;
    QString modelId;
    QString mmprojId;
    QString draftModelId;
    // Speculative decoding / MTP (solo aplican si draftModelId != ""). Ver los
    // flags spec-draft de llama-server. Vacío/0 = no emitir (default del binario).
    QString specType;          // "" | "draft-mtp" (Gemma4 QAT assistant heads)
    int     specDraftNMax = 0; // --spec-draft-n-max (0 = no emitir)
    QString specDraftNgl;      // --spec-draft-ngl  ("" | "all" | número de capas)
    QString specDraftTypeK;    // --spec-draft-type-k ("" | "q8_0" | "f16"...)
    QString specDraftTypeV;    // --spec-draft-type-v

    QJsonObject toJson() const;
    static ModelProfile fromJson(const QJsonObject &obj);
    static QString generateId();
};

struct RuntimePreset {
    QString id;
    QString name;
    int ctx = 4096;
    int batch = 512;
    int ubatch = 512;
    int threads = -1;
    int gpuLayers = -1;
    bool flashAttention = false;
    bool mmap = true;
    bool mlock = false;
    bool contBatching = true;
    QString cacheType = "f16";
    int parallelSlots = 1;

    QJsonObject toJson() const;
    static RuntimePreset fromJson(const QJsonObject &obj);
    static QString generateId();
};

struct HarnessProfile {
    QString id;
    QString name;
    QString adapter;  // "none", "opencode", "aider", "llamaagent"
    QStringList args;
    QMap<QString, QString> env;

    QJsonObject toJson() const;
    static HarnessProfile fromJson(const QJsonObject &obj);
    static QString generateId();
};

struct WorkspaceProfile {
    QString id;
    QString name;
    QString cwd;
    QStringList allowedPaths;
    QStringList blockedPaths;
    bool allowShellCommands = false;

    QJsonObject toJson() const;
    static WorkspaceProfile fromJson(const QJsonObject &obj);
    static QString generateId();
};

// Un nivel de la cadena de fallbacks del maestro. El agente local escala el
// problema al primero; si ese también falla, al siguiente, y así hasta agotar
// la lista (ver tool ask_teacher). Ordenados de primero a último.
struct MasterFallback {
    QString type = "http";           // profile | http | cli
    QString label;                   // nombre opcional para la UI
    // type==profile: referencia a otro LaunchProfile del mismo LlamaCode.
    QString profileId;
    // type==http: endpoint OpenAI-compatible. httpKeyRef es una *referencia*
    // a SecretStore (nunca la key en claro en el JSON del perfil).
    QString httpUrl;
    QString httpModel;
    QString httpKeyRef;
    // type==cli: claude | codex.
    QString cliName;
    // Overrides por nivel.
    bool    applyEdits = true;       // CLI edita archivos directo vs sólo plan
    int     timeoutSec = 300;

    QJsonObject toJson() const;
    static MasterFallback fromJson(const QJsonObject &obj);
};

// Config del "maestro" (supervisor): cadena de modelos/CLIs más capaces a los
// que el agente local escala un sub-problema que no resuelve. Vive por
// LaunchProfile; si la cadena está vacía se usa el fallback global (Ajustes).
// Los campos legacy (kind/cliName/http*) se migran a un fallback único al leer.
struct MasterConfig {
    QList<MasterFallback> fallbacks;  // cadena ordenada (primero → último)
    QString escalation = "manual";   // manual | auto | both
    int     autoAfterFails = 3;      // gatillo auto: N fallos de la misma tool/firma

    // --- Legacy (un solo maestro). Se mantienen para migración/lectura vieja. ---
    QString kind = "none";           // none | http | cli
    QString cliName;                 // claude | codex   (kind==cli)
    QString httpUrl;                 // endpoint OpenAI-compat (kind==http)
    QString httpModel;
    QString httpKey;
    bool    applyEdits = true;
    int     timeoutSec = 300;

    bool isConfigured() const { return !fallbacks.isEmpty(); }

    QJsonObject toJson() const;
    static MasterConfig fromJson(const QJsonObject &obj);
};

struct LaunchProfile {
    QString id;
    QString name;
    QString alias;            // opcional; tiene prioridad sobre name en la UI
    bool    favorite = false; // marcados con estrella y ordenados arriba
    QString backendProfileId;
    QString modelProfileId;
    QString runtimePresetId;
    QString harnessProfileId;
    QString workspaceProfileId;
    QStringList extraArgs;
    QMap<QString, QString> envOverrides;
    MasterConfig master;      // supervisor opcional (maestro CLI/HTTP)
    // Límite de potencia de GPU (W) aplicado vía nvidia-smi al arrancar el server
    // de este perfil. 0 = sin override (usa el global de Ajustes, si hay).
    int powerLimitW = 0;
    // Override del toggle global de automatización de browser (MCP Playwright).
    // "inherit" = usar el global de Ajustes; "on"/"off" = forzar por perfil.
    QString browserAutomation = QStringLiteral("inherit");

    QJsonObject toJson() const;
    static LaunchProfile fromJson(const QJsonObject &obj);
    static QString generateId();
};

struct EffectiveProfile {
    QStringList effectiveArgs;
    QMap<QString, QString> effectiveEnv;
    QStringList warnings;
    QStringList blockingErrors;
    QString binaryPath;
    QString commandLine;

    bool isValid() const { return blockingErrors.isEmpty(); }
};
