#include "ProfileTypes.h"
#include <QJsonArray>
#include <QUuid>

// ---- helpers ----
static QMap<QString, QString> mapFromJson(const QJsonObject &obj) {
    QMap<QString, QString> m;
    for (auto it = obj.begin(); it != obj.end(); ++it)
        m[it.key()] = it.value().toString();
    return m;
}
static QJsonObject mapToJson(const QMap<QString, QString> &m) {
    QJsonObject obj;
    for (auto it = m.begin(); it != m.end(); ++it)
        obj[it.key()] = it.value();
    return obj;
}
static QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

// ---- BackendProfile ----
QJsonObject BackendProfile::toJson() const {
    QJsonObject o;
    o["id"] = id; o["name"] = name; o["binaryId"] = binaryId;
    o["host"] = host; o["port"] = port;
    o["baseArgs"] = QJsonArray::fromStringList(baseArgs);
    o["envOverrides"] = mapToJson(envOverrides);
    return o;
}
BackendProfile BackendProfile::fromJson(const QJsonObject &o) {
    BackendProfile p;
    p.id = o["id"].toString(); p.name = o["name"].toString();
    p.binaryId = o["binaryId"].toString();
    p.host = o["host"].toString("127.0.0.1");
    p.port = o["port"].toInt(8080);
    for (const auto &v : o["baseArgs"].toArray()) p.baseArgs.append(v.toString());
    p.envOverrides = mapFromJson(o["envOverrides"].toObject());
    return p;
}
QString BackendProfile::generateId() { return newId(); }

// ---- ModelProfile ----
QJsonObject ModelProfile::toJson() const {
    QJsonObject o;
    o["id"] = id; o["name"] = name; o["modelId"] = modelId;
    o["mmprojId"] = mmprojId; o["draftModelId"] = draftModelId;
    return o;
}
ModelProfile ModelProfile::fromJson(const QJsonObject &o) {
    ModelProfile p;
    p.id = o["id"].toString(); p.name = o["name"].toString();
    p.modelId = o["modelId"].toString();
    p.mmprojId = o["mmprojId"].toString();
    p.draftModelId = o["draftModelId"].toString();
    return p;
}
QString ModelProfile::generateId() { return newId(); }

// ---- RuntimePreset ----
QJsonObject RuntimePreset::toJson() const {
    QJsonObject o;
    o["id"] = id; o["name"] = name;
    o["ctx"] = ctx; o["batch"] = batch; o["ubatch"] = ubatch;
    o["threads"] = threads; o["gpuLayers"] = gpuLayers;
    o["flashAttention"] = flashAttention; o["mmap"] = mmap;
    o["mlock"] = mlock; o["contBatching"] = contBatching;
    o["cacheType"] = cacheType; o["parallelSlots"] = parallelSlots;
    return o;
}
RuntimePreset RuntimePreset::fromJson(const QJsonObject &o) {
    RuntimePreset p;
    p.id = o["id"].toString(); p.name = o["name"].toString();
    p.ctx = o["ctx"].toInt(4096); p.batch = o["batch"].toInt(512);
    p.ubatch = o["ubatch"].toInt(512); p.threads = o["threads"].toInt(-1);
    p.gpuLayers = o["gpuLayers"].toInt(-1);
    p.flashAttention = o["flashAttention"].toBool(false);
    p.mmap = o["mmap"].toBool(true); p.mlock = o["mlock"].toBool(false);
    p.contBatching = o["contBatching"].toBool(true);
    p.cacheType = o["cacheType"].toString("f16");
    p.parallelSlots = o["parallelSlots"].toInt(1);
    return p;
}
QString RuntimePreset::generateId() { return newId(); }

// ---- HarnessProfile ----
QJsonObject HarnessProfile::toJson() const {
    QJsonObject o;
    o["id"] = id; o["name"] = name; o["adapter"] = adapter;
    o["args"] = QJsonArray::fromStringList(args);
    o["env"] = mapToJson(env);
    return o;
}
HarnessProfile HarnessProfile::fromJson(const QJsonObject &o) {
    HarnessProfile p;
    p.id = o["id"].toString(); p.name = o["name"].toString();
    p.adapter = o["adapter"].toString("none");
    for (const auto &v : o["args"].toArray()) p.args.append(v.toString());
    p.env = mapFromJson(o["env"].toObject());
    return p;
}
QString HarnessProfile::generateId() { return newId(); }

// ---- WorkspaceProfile ----
QJsonObject WorkspaceProfile::toJson() const {
    QJsonObject o;
    o["id"] = id; o["name"] = name; o["cwd"] = cwd;
    o["allowedPaths"] = QJsonArray::fromStringList(allowedPaths);
    o["blockedPaths"] = QJsonArray::fromStringList(blockedPaths);
    o["allowShellCommands"] = allowShellCommands;
    return o;
}
WorkspaceProfile WorkspaceProfile::fromJson(const QJsonObject &o) {
    WorkspaceProfile p;
    p.id = o["id"].toString(); p.name = o["name"].toString();
    p.cwd = o["cwd"].toString();
    for (const auto &v : o["allowedPaths"].toArray()) p.allowedPaths.append(v.toString());
    for (const auto &v : o["blockedPaths"].toArray()) p.blockedPaths.append(v.toString());
    p.allowShellCommands = o["allowShellCommands"].toBool(false);
    return p;
}
QString WorkspaceProfile::generateId() { return newId(); }

// ---- LaunchProfile ----
QJsonObject LaunchProfile::toJson() const {
    QJsonObject o;
    o["id"] = id; o["name"] = name;
    o["backendProfileId"] = backendProfileId;
    o["modelProfileId"] = modelProfileId;
    o["runtimePresetId"] = runtimePresetId;
    o["harnessProfileId"] = harnessProfileId;
    o["workspaceProfileId"] = workspaceProfileId;
    o["extraArgs"] = QJsonArray::fromStringList(extraArgs);
    o["envOverrides"] = mapToJson(envOverrides);
    return o;
}
LaunchProfile LaunchProfile::fromJson(const QJsonObject &o) {
    LaunchProfile p;
    p.id = o["id"].toString(); p.name = o["name"].toString();
    p.backendProfileId = o["backendProfileId"].toString();
    p.modelProfileId = o["modelProfileId"].toString();
    p.runtimePresetId = o["runtimePresetId"].toString();
    p.harnessProfileId = o["harnessProfileId"].toString();
    p.workspaceProfileId = o["workspaceProfileId"].toString();
    for (const auto &v : o["extraArgs"].toArray()) p.extraArgs.append(v.toString());
    p.envOverrides = mapFromJson(o["envOverrides"].toObject());
    return p;
}
QString LaunchProfile::generateId() { return newId(); }
