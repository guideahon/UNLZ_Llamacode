#include "ProfileManager.h"
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>

ProfileManager::ProfileManager(QObject *parent) : QObject(parent)
{
    load();

    // Seed default runtime preset if empty
    if (m_runtimes.m_items.isEmpty()) {
        RuntimePreset def;
        def.id = RuntimePreset::generateId();
        def.name = "Default";
        m_runtimes.add(def);
        save();
    }
}

void ProfileManager::reloadFromDisk()
{
    load();
}

// ---- BackendProfile ----

QString ProfileManager::addBackend(const QString &name, const QString &binaryId,
                                    const QString &host, int port)
{
    BackendProfile p;
    p.id = BackendProfile::generateId();
    p.name = name.isEmpty() ? "Backend" : name;
    p.binaryId = binaryId;
    p.host = host.isEmpty() ? "127.0.0.1" : host;
    p.port = port > 0 ? port : 8080;
    m_backends.add(p);
    save();
    return p.id;
}

bool ProfileManager::removeBackend(const QString &id)
{
    bool ok = m_backends.remove(id);
    if (ok) save();
    return ok;
}

bool ProfileManager::updateBackend(const QString &id, const QString &name,
                                    const QString &binaryId, const QString &host,
                                    int port, const QStringList &baseArgs)
{
    BackendProfile p = m_backends.findById(id);
    if (p.id.isEmpty()) return false;
    p.name = name; p.binaryId = binaryId;
    p.host = host; p.port = port;
    p.baseArgs = baseArgs;
    bool ok = m_backends.update(p);
    if (ok) save();
    return ok;
}

QVariantMap ProfileManager::getBackend(const QString &id) const
{
    const auto p = m_backends.findById(id);
    if (p.id.isEmpty()) return {};
    return {{"id", p.id}, {"name", p.name}, {"binaryId", p.binaryId},
            {"host", p.host}, {"port", p.port}, {"baseArgs", p.baseArgs}};
}

// ---- ModelProfile ----

QString ProfileManager::addModelProfile(const QString &name, const QString &modelId,
                                         const QString &mmprojId, const QString &draftId)
{
    ModelProfile p;
    p.id = ModelProfile::generateId();
    p.name = name.isEmpty() ? "Model" : name;
    p.modelId = modelId;
    p.mmprojId = mmprojId;
    p.draftModelId = draftId;
    m_models.add(p);
    save();
    return p.id;
}

bool ProfileManager::removeModelProfile(const QString &id)
{
    bool ok = m_models.remove(id);
    if (ok) save();
    return ok;
}

bool ProfileManager::updateModelProfile(const QString &id, const QString &name,
                                         const QString &modelId, const QString &mmprojId,
                                         const QString &draftId)
{
    ModelProfile p = m_models.findById(id);
    if (p.id.isEmpty()) return false;
    p.name = name; p.modelId = modelId;
    p.mmprojId = mmprojId; p.draftModelId = draftId;
    bool ok = m_models.update(p);
    if (ok) save();
    return ok;
}

QVariantMap ProfileManager::getModelProfile(const QString &id) const
{
    const auto p = m_models.findById(id);
    if (p.id.isEmpty()) return {};
    return {{"id", p.id}, {"name", p.name}, {"modelId", p.modelId},
            {"mmprojId", p.mmprojId}, {"draftModelId", p.draftModelId}};
}

// ---- RuntimePreset ----

QString ProfileManager::addRuntimePreset(const QString &name, int ctx, int batch,
                                          int gpuLayers, bool flashAttn, bool contBatch)
{
    RuntimePreset p;
    p.id = RuntimePreset::generateId();
    p.name = name.isEmpty() ? "Runtime" : name;
    p.ctx = ctx > 0 ? ctx : 4096;
    p.batch = batch > 0 ? batch : 512;
    p.gpuLayers = gpuLayers;
    p.flashAttention = flashAttn;
    p.contBatching = contBatch;
    m_runtimes.add(p);
    save();
    return p.id;
}

bool ProfileManager::removeRuntimePreset(const QString &id)
{
    bool ok = m_runtimes.remove(id);
    if (ok) save();
    return ok;
}

bool ProfileManager::updateRuntimePreset(const QVariantMap &data)
{
    RuntimePreset p = m_runtimes.findById(data["id"].toString());
    if (p.id.isEmpty()) return false;
    p.name = data.value("name", p.name).toString();
    p.ctx = data.value("ctx", p.ctx).toInt();
    p.batch = data.value("batch", p.batch).toInt();
    p.ubatch = data.value("ubatch", p.ubatch).toInt();
    p.threads = data.value("threads", p.threads).toInt();
    p.gpuLayers = data.value("gpuLayers", p.gpuLayers).toInt();
    p.flashAttention = data.value("flashAttention", p.flashAttention).toBool();
    p.mmap = data.value("mmap", p.mmap).toBool();
    p.mlock = data.value("mlock", p.mlock).toBool();
    p.contBatching = data.value("contBatching", p.contBatching).toBool();
    p.cacheType = data.value("cacheType", p.cacheType).toString();
    p.parallelSlots = data.value("parallelSlots", p.parallelSlots).toInt();
    bool ok = m_runtimes.update(p);
    if (ok) save();
    return ok;
}

QVariantMap ProfileManager::getRuntimePreset(const QString &id) const
{
    const auto p = m_runtimes.findById(id);
    if (p.id.isEmpty()) return {};
    return {{"id", p.id}, {"name", p.name}, {"ctx", p.ctx}, {"batch", p.batch},
            {"ubatch", p.ubatch}, {"threads", p.threads}, {"gpuLayers", p.gpuLayers},
            {"flashAttention", p.flashAttention}, {"mmap", p.mmap}, {"mlock", p.mlock},
            {"contBatching", p.contBatching}, {"cacheType", p.cacheType},
            {"parallelSlots", p.parallelSlots}};
}

// ---- HarnessProfile ----

QString ProfileManager::addHarness(const QString &name, const QString &adapter)
{
    HarnessProfile p;
    p.id = HarnessProfile::generateId();
    p.name = name.isEmpty() ? adapter : name;
    p.adapter = adapter;
    m_harnesses.add(p);
    save();
    return p.id;
}

bool ProfileManager::removeHarness(const QString &id)
{
    bool ok = m_harnesses.remove(id);
    if (ok) save();
    return ok;
}

bool ProfileManager::updateHarness(const QVariantMap &data)
{
    HarnessProfile p = m_harnesses.findById(data["id"].toString());
    if (p.id.isEmpty()) return false;
    p.name    = data.value("name",    p.name).toString();
    p.adapter = data.value("adapter", p.adapter).toString();
    const QVariant argsV = data.value("args");
    if (argsV.isValid()) p.args = argsV.toStringList();
    bool ok = m_harnesses.update(p);
    if (ok) save();
    return ok;
}

QVariantMap ProfileManager::getHarness(const QString &id) const
{
    const auto p = m_harnesses.findById(id);
    if (p.id.isEmpty()) return {};
    return {{"id", p.id}, {"name", p.name}, {"adapter", p.adapter}, {"args", p.args}};
}

// ---- LaunchProfile ----

QString ProfileManager::addLaunchProfile(const QString &name,
                                          const QString &backendId,
                                          const QString &modelId,
                                          const QString &runtimeId)
{
    LaunchProfile p;
    p.id = LaunchProfile::generateId();
    p.name = name.isEmpty() ? "Launch" : name;
    p.backendProfileId = backendId;
    p.modelProfileId = modelId;
    p.runtimePresetId = runtimeId;
    m_launches.add(p);
    save();
    return p.id;
}

bool ProfileManager::removeLaunchProfile(const QString &id)
{
    bool ok = m_launches.remove(id);
    if (ok) save();
    return ok;
}

bool ProfileManager::updateLaunchProfile(const QVariantMap &data)
{
    LaunchProfile p = m_launches.findById(data["id"].toString());
    if (p.id.isEmpty()) return false;
    p.name = data.value("name", p.name).toString();
    p.backendProfileId = data.value("backendProfileId", p.backendProfileId).toString();
    p.modelProfileId = data.value("modelProfileId", p.modelProfileId).toString();
    p.runtimePresetId = data.value("runtimePresetId", p.runtimePresetId).toString();
    p.harnessProfileId = data.value("harnessProfileId", p.harnessProfileId).toString();
    p.workspaceProfileId = data.value("workspaceProfileId", p.workspaceProfileId).toString();
    p.extraArgs = data.value("extraArgs", p.extraArgs).toStringList();
    bool ok = m_launches.update(p);
    if (ok) save();
    return ok;
}

QVariantMap ProfileManager::getLaunchProfile(const QString &id) const
{
    const auto p = m_launches.findById(id);
    if (p.id.isEmpty()) return {};
    return {{"id", p.id}, {"name", p.name},
            {"backendProfileId", p.backendProfileId},
            {"modelProfileId", p.modelProfileId},
            {"runtimePresetId", p.runtimePresetId},
            {"harnessProfileId", p.harnessProfileId},
            {"workspaceProfileId", p.workspaceProfileId},
            {"extraArgs", p.extraArgs}};
}

// ---- Resolvers ----

BackendProfile   ProfileManager::resolveBackend(const QString &id)    const { return m_backends.findById(id); }
ModelProfile     ProfileManager::resolveModelProfile(const QString &id) const { return m_models.findById(id); }
RuntimePreset    ProfileManager::resolveRuntime(const QString &id)     const { return m_runtimes.findById(id); }
HarnessProfile   ProfileManager::resolveHarness(const QString &id)     const { return m_harnesses.findById(id); }
WorkspaceProfile ProfileManager::resolveWorkspace(const QString &id)   const { return m_workspaces.findById(id); }
LaunchProfile    ProfileManager::resolveLaunch(const QString &id)      const { return m_launches.findById(id); }

// ---- Persistence ----

void ProfileManager::load()
{
    bool loadFailed = false;
    auto loadList = [&loadFailed](const QString &path, auto &model, auto fromJsonFn) {
        QFile f(path);
        if (!f.exists()) return;                 // first run / never saved — fine
        if (!f.open(QIODevice::ReadOnly)) {
            // File exists but couldn't be read (e.g. locked by another instance).
            // Treat as a load failure so we never overwrite it with an empty list.
            qWarning() << "[ProfileManager::load] could not open existing file"
                       << path << f.errorString();
            loadFailed = true;
            return;
        }
        const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
        QList<std::decay_t<decltype(model.m_items[0])>> items;
        for (const auto &v : arr)
            items.append(fromJsonFn(v.toObject()));
        model.setItems(items);
    };

    loadList(storagePath("backends"),   m_backends,   &BackendProfile::fromJson);
    loadList(storagePath("models"),     m_models,     &ModelProfile::fromJson);
    loadList(storagePath("runtimes"),   m_runtimes,   &RuntimePreset::fromJson);
    loadList(storagePath("harnesses"),  m_harnesses,  &HarnessProfile::fromJson);
    loadList(storagePath("workspaces"), m_workspaces, &WorkspaceProfile::fromJson);
    loadList(storagePath("launches"),   m_launches,   &LaunchProfile::fromJson);

    // Only allow persistence once we've loaded cleanly. If any existing file
    // failed to load, block all saves so a partial/empty in-memory state can
    // never wipe the user's profiles on disk.
    m_persistAllowed = !loadFailed;
    if (loadFailed)
        qWarning() << "[ProfileManager] load incomplete — saving DISABLED to protect existing data";
}

void ProfileManager::save() const
{
    if (!m_persistAllowed) {
        qWarning() << "[ProfileManager::save] skipped — persistence disabled after a failed load";
        return;
    }

    auto saveList = [](const QString &path, const auto &items) {
        // Anti-wipe guard: never overwrite a non-empty file with an empty list.
        if (items.isEmpty()) {
            QFile existing(path);
            if (existing.exists() && existing.open(QIODevice::ReadOnly)) {
                const QJsonArray cur = QJsonDocument::fromJson(existing.readAll()).array();
                existing.close();
                if (!cur.isEmpty()) {
                    qWarning() << "[ProfileManager::save] refusing to wipe non-empty file with empty list:"
                               << path;
                    return;
                }
            }
        }

        QJsonArray arr;
        for (const auto &item : items) arr.append(item.toJson());
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(QJsonDocument(arr).toJson());
        else
            qWarning() << "[ProfileManager::save] FAILED to open" << path << f.errorString();
    };

    saveList(storagePath("backends"),   m_backends.m_items);
    saveList(storagePath("models"),     m_models.m_items);
    saveList(storagePath("runtimes"),   m_runtimes.m_items);
    saveList(storagePath("harnesses"),  m_harnesses.m_items);
    saveList(storagePath("workspaces"), m_workspaces.m_items);
    saveList(storagePath("launches"),   m_launches.m_items);
}

QString ProfileManager::storagePath(const QString &entity) const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + "/profiles/" + entity + ".json";
}
