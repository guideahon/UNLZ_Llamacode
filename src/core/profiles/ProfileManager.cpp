#include "ProfileManager.h"
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QTimer>
#include <QDebug>
#include <QRegularExpression>
#include <algorithm>

// Launch profile display names carry a stable, non-repeating incremental id as a
// "<n>_" prefix. The number never changes for an existing profile and is never
// reused: a new profile always takes (max existing number + 1), even across
// deletions. These helpers parse/strip that prefix.
namespace {
const QRegularExpression kSeqPrefix(QStringLiteral("^(\\d+)_"));

int seqOf(const QString &name) {
    const auto m = kSeqPrefix.match(name);
    return m.hasMatch() ? m.captured(1).toInt() : 0;
}
QString stripSeq(const QString &name) {
    return QString(name).remove(kSeqPrefix);
}
}

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

    setupWatcher();
}

void ProfileManager::reloadFromDisk()
{
    load();
    emit profilesReloaded();
}

void ProfileManager::setupWatcher()
{
    connect(&m_watcher, &QFileSystemWatcher::fileChanged,
            this, &ProfileManager::onProfileFileChanged);
    for (const QString &ent : {QStringLiteral("backends"), QStringLiteral("models"),
                               QStringLiteral("runtimes"), QStringLiteral("harnesses"),
                               QStringLiteral("workspaces"), QStringLiteral("launches")}) {
        const QString p = storagePath(ent);
        if (QFile::exists(p)) m_watcher.addPath(p);
    }
}

void ProfileManager::onProfileFileChanged(const QString &path)
{
    // Re-armar el watch: una escritura atómica (rename) o algunos editores
    // reemplazan el archivo y el watcher pierde el path.
    if (QFile::exists(path) && !m_watcher.files().contains(path))
        m_watcher.addPath(path);

    if (m_saving) return;   // cambio provocado por nuestro propio save(): ignorar

    // Cambio externo (otra instancia / edición manual): recargar para no pisar.
    qInfo() << "[ProfileManager] cambio externo detectado, recargando:" << path;
    reloadFromDisk();
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
    // Assign a stable, non-repeating incremental id = max existing + 1.
    int maxSeq = 0;
    for (const auto &x : m_launches.m_items) maxSeq = std::max(maxSeq, seqOf(x.name));
    const QString base = stripSeq(name.isEmpty() ? QStringLiteral("Launch") : name);
    p.name = QStringLiteral("%1_%2").arg(maxSeq + 1).arg(base);
    p.backendProfileId = backendId;
    p.modelProfileId = modelId;
    p.runtimePresetId = runtimeId;
    m_launches.add(p);
    save();
    emit launchesChanged();
    return p.id;
}

bool ProfileManager::removeLaunchProfile(const QString &id)
{
    bool ok = m_launches.remove(id);
    if (ok) { save(); emit launchesChanged(); }
    return ok;
}

bool ProfileManager::updateLaunchProfile(const QVariantMap &data)
{
    LaunchProfile p = m_launches.findById(data["id"].toString());
    if (p.id.isEmpty()) return false;
    // Preserve this profile's stable incremental id even if the edited name
    // drops or changes the "<n>_" prefix.
    int seq = seqOf(p.name);
    if (seq == 0) {
        for (const auto &x : m_launches.m_items) seq = std::max(seq, seqOf(x.name));
        seq += 1;
    }
    const QString newName = data.value("name", p.name).toString();
    p.name = QStringLiteral("%1_%2").arg(seq).arg(stripSeq(newName));
    if (data.contains("alias"))    p.alias = data.value("alias").toString();
    if (data.contains("favorite")) p.favorite = data.value("favorite").toBool();
    p.backendProfileId = data.value("backendProfileId", p.backendProfileId).toString();
    p.modelProfileId = data.value("modelProfileId", p.modelProfileId).toString();
    p.runtimePresetId = data.value("runtimePresetId", p.runtimePresetId).toString();
    p.harnessProfileId = data.value("harnessProfileId", p.harnessProfileId).toString();
    p.workspaceProfileId = data.value("workspaceProfileId", p.workspaceProfileId).toString();
    p.extraArgs = data.value("extraArgs", p.extraArgs).toStringList();
    if (data.contains("master")) {
        const QVariantMap m = data.value("master").toMap();
        MasterConfig mc = p.master;
        mc.kind = m.value("kind", mc.kind).toString();
        mc.cliName = m.value("cliName", mc.cliName).toString();
        mc.httpUrl = m.value("httpUrl", mc.httpUrl).toString();
        mc.httpModel = m.value("httpModel", mc.httpModel).toString();
        mc.httpKey = m.value("httpKey", mc.httpKey).toString();
        mc.escalation = m.value("escalation", mc.escalation).toString();
        mc.autoAfterFails = m.value("autoAfterFails", mc.autoAfterFails).toInt();
        mc.applyEdits = m.value("applyEdits", mc.applyEdits).toBool();
        mc.timeoutSec = m.value("timeoutSec", mc.timeoutSec).toInt();
        p.master = mc;
    }
    bool ok = m_launches.update(p);
    if (ok) { save(); emit launchesChanged(); }
    return ok;
}

QVariantMap ProfileManager::getLaunchProfile(const QString &id) const
{
    const auto p = m_launches.findById(id);
    if (p.id.isEmpty()) return {};
    return {{"id", p.id}, {"name", p.name},
            {"alias", p.alias}, {"favorite", p.favorite},
            {"displayName", p.alias.isEmpty() ? p.name : p.alias},
            {"backendProfileId", p.backendProfileId},
            {"modelProfileId", p.modelProfileId},
            {"runtimePresetId", p.runtimePresetId},
            {"harnessProfileId", p.harnessProfileId},
            {"workspaceProfileId", p.workspaceProfileId},
            {"extraArgs", p.extraArgs},
            {"master", QVariantMap{
                {"kind", p.master.kind}, {"cliName", p.master.cliName},
                {"httpUrl", p.master.httpUrl}, {"httpModel", p.master.httpModel},
                {"httpKey", p.master.httpKey}, {"escalation", p.master.escalation},
                {"autoAfterFails", p.master.autoAfterFails},
                {"applyEdits", p.master.applyEdits},
                {"timeoutSec", p.master.timeoutSec}}}};
}

void ProfileManager::setLaunchFavorite(const QString &id, bool favorite)
{
    LaunchProfile p = m_launches.findById(id);
    if (p.id.isEmpty() || p.favorite == favorite) return;
    p.favorite = favorite;
    if (m_launches.update(p)) { save(); emit launchesChanged(); }
}

void ProfileManager::setLaunchAlias(const QString &id, const QString &alias)
{
    LaunchProfile p = m_launches.findById(id);
    if (p.id.isEmpty() || p.alias == alias) return;
    p.alias = alias;
    if (m_launches.update(p)) { save(); emit launchesChanged(); }
}

// Lista de perfiles de lanzamiento para dropdowns: favoritos primero (estrella),
// luego por id incremental; displayName = alias || name.
QVariantList ProfileManager::launchProfilesForMenu() const
{
    QList<LaunchProfile> items = m_launches.m_items;
    std::stable_sort(items.begin(), items.end(),
        [](const LaunchProfile &a, const LaunchProfile &b) {
            if (a.favorite != b.favorite) return a.favorite;     // favoritos arriba
            return seqOf(a.name) < seqOf(b.name);                // luego por nº incremental
        });
    QVariantList out;
    for (const auto &p : items) {
        const QString base = p.alias.isEmpty() ? p.name : p.alias;
        out.append(QVariantMap{
            {"id", p.id}, {"name", p.name}, {"alias", p.alias},
            {"favorite", p.favorite},
            // displayName lleva la estrella para que se vea en el dropdown y en
            // el texto seleccionado; alias tiene prioridad sobre name.
            {"displayName", p.favorite ? QStringLiteral("★ ") + base : base}});
    }
    return out;
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

    // Marca que los próximos cambios de archivo son nuestros, para no auto-recargar.
    m_saving = true;

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
        const QByteArray data = QJsonDocument(arr).toJson();
        QDir().mkpath(QFileInfo(path).absolutePath());

        // Rolling backup of the current on-disk file BEFORE overwriting, so any
        // bad/lossy write is always recoverable. Keep the last N per entity.
        QFileInfo fi(path);
        if (fi.exists()) {
            const QString base = fi.completeBaseName();               // e.g. "launches"
            const QString bdir = fi.absolutePath() + "/.backups";
            QDir().mkpath(bdir);
            const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz");
            QFile::copy(path, bdir + "/" + base + "." + stamp + ".json");
            // Prune to the most recent 15 backups per entity (sorted by name == time).
            QDir d(bdir);
            QStringList olds = d.entryList(QStringList{base + ".*.json"}, QDir::Files, QDir::Name);
            while (olds.size() > 15)
                QFile::remove(bdir + "/" + olds.takeFirst());
            // Warn loudly on a large count drop (possible accidental loss).
            QFile prev(path);
            if (prev.open(QIODevice::ReadOnly)) {
                const int curCount = QJsonDocument::fromJson(prev.readAll()).array().size();
                prev.close();
                if (curCount > 5 && arr.size() < curCount / 2)
                    qWarning() << "[ProfileManager::save] LARGE COUNT DROP" << path
                               << curCount << "->" << arr.size()
                               << "(backup kept in" << bdir << ")";
            }
        }

        // Atomic write: write to a temp file, then rename over the target so a
        // crash mid-write can never leave a truncated/corrupt profiles file.
        const QString tmp = path + ".tmp";
        QFile f(tmp);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qWarning() << "[ProfileManager::save] FAILED to open temp" << tmp << f.errorString();
            return;
        }
        const qint64 written = f.write(data);
        f.flush();
        f.close();
        if (written != data.size()) {
            qWarning() << "[ProfileManager::save] short write, aborting rename" << tmp;
            QFile::remove(tmp);
            return;
        }
        QFile::remove(path);                 // Windows: rename requires absent target
        if (!QFile::rename(tmp, path))
            qWarning() << "[ProfileManager::save] FAILED to rename" << tmp << "->" << path;
    };

    saveList(storagePath("backends"),   m_backends.m_items);
    saveList(storagePath("models"),     m_models.m_items);
    saveList(storagePath("runtimes"),   m_runtimes.m_items);
    saveList(storagePath("harnesses"),  m_harnesses.m_items);
    saveList(storagePath("workspaces"), m_workspaces.m_items);
    saveList(storagePath("launches"),   m_launches.m_items);

    // La escritura atómica (rename) hace que el watcher pierda los paths: re-armarlos.
    for (const QString &ent : {QStringLiteral("backends"), QStringLiteral("models"),
                               QStringLiteral("runtimes"), QStringLiteral("harnesses"),
                               QStringLiteral("workspaces"), QStringLiteral("launches")}) {
        const QString p = storagePath(ent);
        if (QFile::exists(p) && !m_watcher.files().contains(p)) m_watcher.addPath(p);
    }
    // Limpiar el flag tras drenar los eventos fileChanged de nuestro propio save().
    QTimer::singleShot(400, const_cast<ProfileManager*>(this), [this]() { m_saving = false; });
}

QString ProfileManager::storagePath(const QString &entity) const
{
    // Profiles live in the project root (Documents\LlamaCode\profiles) so they
    // are easy to inspect and back up alongside the source. Overridable via the
    // LLAMACODE_PROFILES_DIR env var; otherwise the fixed project path is used.
    static const QString root = []() {
        const QByteArray env = qgetenv("LLAMACODE_PROFILES_DIR");
        if (!env.isEmpty())
            return QString::fromLocal8Bit(env);
        return QStringLiteral("C:/Users/cristian/Documents/LlamaCode/profiles");
    }();
    return root + "/" + entity + ".json";
}
