#pragma once
#include <QString>
#include <QStringList>
#include <QMap>
#include <QJsonObject>

struct BackendProfile {
    QString id;
    QString name;
    QString binaryId;
    QString host = "127.0.0.1";
    int port = 8080;
    QStringList baseArgs;
    QMap<QString, QString> envOverrides;

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

// Config del "maestro" (supervisor): modelo más capaz al que el agente local
// escala un sub-problema que no resuelve. Vive por LaunchProfile; si kind=="none"
// se usa el fallback global (Ajustes). Ver tool ask_teacher.
struct MasterConfig {
    QString kind = "none";           // none | http | cli
    QString cliName;                 // claude | codex   (kind==cli)
    QString httpUrl;                 // endpoint OpenAI-compat (kind==http)
    QString httpModel;
    QString httpKey;
    QString escalation = "manual";   // manual | auto | both
    int     autoAfterFails = 3;      // gatillo auto: N fallos de la misma tool/firma
    bool    applyEdits = true;       // CLI edita archivos directo vs sólo devolver plan
    int     timeoutSec = 300;

    bool isConfigured() const { return kind != QLatin1String("none"); }

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
