#pragma once
#include "ProfileTypes.h"
#include <QObject>
#include <QAbstractListModel>
#include <QList>

// Generic list model for a profile type
template <typename T>
class ProfileListModel : public QAbstractListModel
{
public:
    enum Roles { IdRole = Qt::UserRole + 1, NameRole, DataRole };

    explicit ProfileListModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : m_items.size();
    }
    QVariant data(const QModelIndex &idx, int role = Qt::DisplayRole) const override {
        if (!idx.isValid() || idx.row() >= m_items.size()) return {};
        switch (role) {
        case IdRole:   return m_items[idx.row()].id;
        case NameRole: return m_items[idx.row()].name;
        default:       return {};
        }
    }
    QHash<int, QByteArray> roleNames() const override {
        return {{IdRole, "profileId"}, {NameRole, "name"}};
    }

    void setItems(const QList<T> &items) {
        beginResetModel(); m_items = items; endResetModel();
    }
    const QList<T> &items() const { return m_items; }
    T findById(const QString &id) const {
        for (const auto &x : m_items) if (x.id == id) return x;
        return T{};
    }
    int indexOfId(const QString &id) const {
        for (int i = 0; i < m_items.size(); ++i) if (m_items[i].id == id) return i;
        return -1;
    }

    void add(const T &item) {
        beginInsertRows({}, m_items.size(), m_items.size());
        m_items.append(item);
        endInsertRows();
    }
    bool remove(const QString &id) {
        int idx = indexOfId(id);
        if (idx < 0) return false;
        beginRemoveRows({}, idx, idx);
        m_items.removeAt(idx);
        endRemoveRows();
        return true;
    }
    bool update(const T &item) {
        int idx = indexOfId(item.id);
        if (idx < 0) return false;
        m_items[idx] = item;
        const auto mi = index(idx);
        emit dataChanged(mi, mi);
        return true;
    }

    QList<T> m_items;
};

class ProfileManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractListModel* backendProfiles READ backendProfiles CONSTANT)
    Q_PROPERTY(QAbstractListModel* modelProfiles   READ modelProfiles   CONSTANT)
    Q_PROPERTY(QAbstractListModel* runtimePresets  READ runtimePresets  CONSTANT)
    Q_PROPERTY(QAbstractListModel* harnessProfiles READ harnessProfiles CONSTANT)
    Q_PROPERTY(QAbstractListModel* workspaceProfiles READ workspaceProfiles CONSTANT)
    Q_PROPERTY(QAbstractListModel* launchProfiles  READ launchProfiles  CONSTANT)

public:
    explicit ProfileManager(QObject *parent = nullptr);

    QAbstractListModel *backendProfiles()   { return &m_backends; }
    QAbstractListModel *modelProfiles()     { return &m_models; }
    QAbstractListModel *runtimePresets()    { return &m_runtimes; }
    QAbstractListModel *harnessProfiles()   { return &m_harnesses; }
    QAbstractListModel *workspaceProfiles() { return &m_workspaces; }
    QAbstractListModel *launchProfiles()    { return &m_launches; }

    // BackendProfile
    Q_INVOKABLE QString addBackend(const QString &name, const QString &binaryId,
                                   const QString &host, int port);
    Q_INVOKABLE bool removeBackend(const QString &id);
    Q_INVOKABLE bool updateBackend(const QString &id, const QString &name,
                                   const QString &binaryId, const QString &host,
                                   int port, const QStringList &baseArgs);
    Q_INVOKABLE QVariantMap getBackend(const QString &id) const;

    // ModelProfile
    Q_INVOKABLE QString addModelProfile(const QString &name, const QString &modelId,
                                        const QString &mmprojId, const QString &draftId);
    Q_INVOKABLE bool removeModelProfile(const QString &id);
    Q_INVOKABLE bool updateModelProfile(const QString &id, const QString &name,
                                        const QString &modelId, const QString &mmprojId,
                                        const QString &draftId);
    Q_INVOKABLE QVariantMap getModelProfile(const QString &id) const;

    // RuntimePreset
    Q_INVOKABLE QString addRuntimePreset(const QString &name, int ctx, int batch,
                                         int gpuLayers, bool flashAttn, bool contBatch);
    Q_INVOKABLE bool removeRuntimePreset(const QString &id);
    Q_INVOKABLE bool updateRuntimePreset(const QVariantMap &data);
    Q_INVOKABLE QVariantMap getRuntimePreset(const QString &id) const;

    // HarnessProfile
    Q_INVOKABLE QString addHarness(const QString &name, const QString &adapter);
    Q_INVOKABLE bool removeHarness(const QString &id);
    Q_INVOKABLE bool updateHarness(const QVariantMap &data);
    Q_INVOKABLE QVariantMap getHarness(const QString &id) const;

    // LaunchProfile
    Q_INVOKABLE QString addLaunchProfile(const QString &name,
                                         const QString &backendId,
                                         const QString &modelId,
                                         const QString &runtimeId);
    Q_INVOKABLE bool removeLaunchProfile(const QString &id);
    Q_INVOKABLE bool updateLaunchProfile(const QVariantMap &data);
    Q_INVOKABLE QVariantMap getLaunchProfile(const QString &id) const;
    Q_INVOKABLE void saveProfiles() { save(); }
    void reloadFromDisk();

    // Resolve for builder
    BackendProfile resolveBackend(const QString &id) const;
    ModelProfile resolveModelProfile(const QString &id) const;
    RuntimePreset resolveRuntime(const QString &id) const;
    HarnessProfile resolveHarness(const QString &id) const;
    WorkspaceProfile resolveWorkspace(const QString &id) const;
    LaunchProfile resolveLaunch(const QString &id) const;

signals:
    void errorOccurred(const QString &message);

private:
    void load();
    void save() const;
    QString storagePath(const QString &entity) const;

    ProfileListModel<BackendProfile>   m_backends;
    ProfileListModel<ModelProfile>     m_models;
    ProfileListModel<RuntimePreset>    m_runtimes;
    ProfileListModel<HarnessProfile>   m_harnesses;
    ProfileListModel<WorkspaceProfile> m_workspaces;
    ProfileListModel<LaunchProfile>    m_launches;

    // Set false when load() can't read an existing file (e.g. locked by another
    // instance); blocks save() so a partial/empty state can't wipe stored data.
    mutable bool m_persistAllowed = true;
};
