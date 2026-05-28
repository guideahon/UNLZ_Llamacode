#pragma once
#include "LlamaBinary.h"
#include <QAbstractListModel>
#include <QList>

class BinaryRegistry : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        PathRole,
        FlavorRole,
        BackendRole,
        VersionHintRole,
        PathValidRole,
        HasCapabilitiesRole,
        BinaryHashRole
    };

    explicit BinaryRegistry(QObject *parent = nullptr);

    // QAbstractListModel
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_items.size(); }

    Q_INVOKABLE QString add(const QString &path, const QString &name,
                            const QString &flavor, const QString &backend,
                            const QString &versionHint);
    Q_INVOKABLE bool remove(const QString &id);
    Q_INVOKABLE bool update(const QString &id, const QString &name,
                            const QString &flavor, const QString &backend,
                            const QString &versionHint, const QString &workingDir);
    Q_INVOKABLE void detectCapabilities(const QString &id);
    Q_INVOKABLE QVariantMap get(const QString &id) const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QStringList supportedFlags(const QString &id) const;

    LlamaBinary findById(const QString &id) const;

signals:
    void countChanged();
    void capabilitiesDetected(const QString &id, bool success, const QString &error);
    void errorOccurred(const QString &message);

private:
    void load();
    void save() const;
    QString storagePath() const;
    QString computeHash(const QString &path) const;
    int indexOfId(const QString &id) const;

    QList<LlamaBinary> m_items;
};
