#include "ModelCatalog.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QUuid>
#include <QDebug>
#include <QThread>
#include <QFileInfo>

static const char *kSchema = R"(
CREATE TABLE IF NOT EXISTS catalog_models (
    id TEXT PRIMARY KEY,
    root_id TEXT NOT NULL,
    absolute_path TEXT NOT NULL,
    file_name TEXT NOT NULL,
    size_bytes INTEGER NOT NULL DEFAULT 0,
    mtime TEXT NOT NULL,
    family_hint TEXT,
    quant_hint TEXT,
    quant_real TEXT,
    tensor_breakdown TEXT,
    bpw REAL NOT NULL DEFAULT 0,
    quant_mismatch INTEGER NOT NULL DEFAULT 0,
    is_vision_candidate INTEGER NOT NULL DEFAULT 0,
    is_draft_candidate INTEGER NOT NULL DEFAULT 0,
    sha256 TEXT,
    is_available INTEGER NOT NULL DEFAULT 1
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_path ON catalog_models(absolute_path);
)";

ModelCatalog::ModelCatalog(QObject *parent)
    : QAbstractListModel(parent)
    , m_connName(QUuid::createUuid().toString())
{
    // Load with retry: if a previous instance still holds the DB at startup,
    // db.open()/the query can briefly fail and leave us with an empty catalog
    // for the whole session (→ every profile becomes "No model selected").
    const bool dbHasData = QFileInfo(dbPath()).size() > 4096;
    for (int attempt = 0; attempt < 10; ++attempt) {
        openDb();
        m_all.clear();
        loadFromDb();
        if (!m_all.isEmpty() || !dbHasData) break;
        QThread::msleep(250);
    }
    rebuildVisible();
}

int ModelCatalog::reload()
{
    beginResetModel();
    const bool dbHasData = QFileInfo(dbPath()).size() > 4096;
    for (int attempt = 0; attempt < 10; ++attempt) {
        openDb();
        m_all.clear();
        loadFromDb();
        if (!m_all.isEmpty() || !dbHasData) break;
        QThread::msleep(250);
    }
    endResetModel();
    rebuildVisible();
    return m_all.size();
}

int ModelCatalog::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_visible.size();
}

QVariant ModelCatalog::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_visible.size()) return {};

    const CatalogModel *m = m_visible.at(index.row());
    switch (role) {
    case IdRole:               return m->id;
    case RootIdRole:           return m->rootId;
    case AbsolutePathRole:     return m->absolutePath;
    case FileNameRole:         return m->fileName;
    case SizeBytesRole:        return m->sizeBytes;
    case SizeLabelRole:        return m->sizeLabel();
    case MtimeRole:            return m->mtime.toString(Qt::ISODate);
    case FamilyHintRole:       return m->familyHint;
    case QuantHintRole:        return m->quantHint;
    case QuantRealRole:        return m->quantReal;
    case TensorBreakdownRole:  return m->tensorBreakdown;
    case BpwRole:              return m->bpw;
    case QuantMismatchRole:    return m->quantMismatch;
    case IsVisionCandidateRole: return m->isVisionCandidate;
    case IsDraftCandidateRole:  return m->isDraftCandidate;
    case IsAvailableRole:      return m->isAvailable;
    default:                   return {};
    }
}

QHash<int, QByteArray> ModelCatalog::roleNames() const
{
    return {
        {IdRole,                "modelId"},
        {RootIdRole,            "rootId"},
        {AbsolutePathRole,      "absolutePath"},
        {FileNameRole,          "fileName"},
        {SizeBytesRole,         "sizeBytes"},
        {SizeLabelRole,         "sizeLabel"},
        {MtimeRole,             "mtime"},
        {FamilyHintRole,        "family"},
        {QuantHintRole,         "quant"},
        {QuantRealRole,         "quantReal"},
        {TensorBreakdownRole,   "tensorBreakdown"},
        {BpwRole,               "bpw"},
        {QuantMismatchRole,     "quantMismatch"},
        {IsVisionCandidateRole, "isVision"},
        {IsDraftCandidateRole,  "isDraft"},
        {IsAvailableRole,       "isAvailable"},
    };
}

int ModelCatalog::count() const
{
    return m_visible.size();
}

void ModelCatalog::setFilterRootId(const QString &id)
{
    if (m_filterRootId == id) return;
    m_filterRootId = id;
    beginResetModel();
    rebuildVisible();
    endResetModel();
    emit filterChanged();
    emit countChanged();
}

void ModelCatalog::setFilterFamily(const QString &f)
{
    if (m_filterFamily == f) return;
    m_filterFamily = f;
    beginResetModel();
    rebuildVisible();
    endResetModel();
    emit filterChanged();
    emit countChanged();
}

void ModelCatalog::setFilterVisionOnly(bool v)
{
    if (m_filterVisionOnly == v) return;
    m_filterVisionOnly = v;
    beginResetModel();
    rebuildVisible();
    endResetModel();
    emit filterChanged();
    emit countChanged();
}

void ModelCatalog::addOrUpdate(const CatalogModel &model)
{
    // El scanner ahora genera ids DETERMINISTAS por ruta (UUIDv5), así que el id
    // entrante para un mismo archivo es siempre el mismo. Adoptarlo en match por
    // ruta hace converger filas viejas (ids aleatorios legacy) al id estable.
    const int idx = indexOfId(model.id);
    if (idx >= 0) {
        m_all[idx] = model;
    } else {
        // Check by path
        int pathIdx = -1;
        for (int i = 0; i < m_all.size(); ++i) {
            if (m_all[i].absolutePath == model.absolutePath) { pathIdx = i; break; }
        }
        if (pathIdx >= 0) m_all[pathIdx] = model;
        else m_all.append(model);
    }
    saveToDb(model);
    beginResetModel();
    rebuildVisible();
    endResetModel();
    emit countChanged();
}

void ModelCatalog::addBatch(const QList<CatalogModel> &models)
{
    auto db = QSqlDatabase::database(m_connName);
    db.transaction();
    for (const auto &m : models) {
        const int idx = indexOfId(m.id);
        if (idx >= 0) m_all[idx] = m;
        else {
            int pathIdx = -1;
            for (int i = 0; i < m_all.size(); ++i)
                if (m_all[i].absolutePath == m.absolutePath) { pathIdx = i; break; }
            if (pathIdx >= 0) m_all[pathIdx] = m;   // converge a id determinista por ruta
            else m_all.append(m);
        }
        saveToDb(m);
    }
    db.commit();
    beginResetModel();
    rebuildVisible();
    endResetModel();
    emit countChanged();
}

void ModelCatalog::markRootUnavailable(const QString &rootId)
{
    bool changed = false;
    for (auto &m : m_all) {
        if (m.rootId == rootId && m.isAvailable) {
            m.isAvailable = false;
            saveToDb(m);
            changed = true;
        }
    }
    if (changed) {
        beginResetModel();
        rebuildVisible();
        endResetModel();
    }
}

void ModelCatalog::removeByRootId(const QString &rootId)
{
    auto db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare("DELETE FROM catalog_models WHERE root_id = ?");
    q.addBindValue(rootId);
    q.exec();

    m_all.erase(std::remove_if(m_all.begin(), m_all.end(),
        [&](const CatalogModel &m) { return m.rootId == rootId; }), m_all.end());

    beginResetModel();
    rebuildVisible();
    endResetModel();
    emit countChanged();
}

QVariantMap ModelCatalog::get(const QString &id) const
{
    const int idx = indexOfId(id);
    if (idx < 0) return {};
    const CatalogModel &m = m_all.at(idx);
    return {
        {"id", m.id}, {"rootId", m.rootId}, {"absolutePath", m.absolutePath},
        {"fileName", m.fileName}, {"sizeBytes", m.sizeBytes},
        {"sizeLabel", m.sizeLabel()}, {"family", m.familyHint},
        {"quant", m.quantHint}, {"quantReal", m.quantReal},
        {"tensorBreakdown", m.tensorBreakdown}, {"bpw", m.bpw},
        {"quantMismatch", m.quantMismatch}, {"isVision", m.isVisionCandidate},
        {"isDraft", m.isDraftCandidate}, {"isAvailable", m.isAvailable}
    };
}

QVariantMap ModelCatalog::getAt(int row) const
{
    if (row < 0 || row >= m_visible.size()) return {};
    return get(m_visible.at(row)->id);
}

CatalogModel ModelCatalog::findById(const QString &id) const
{
    const int idx = indexOfId(id);
    if (idx < 0) return {};
    return m_all.at(idx);
}

QList<CatalogModel> ModelCatalog::allForRoot(const QString &rootId) const
{
    QList<CatalogModel> res;
    for (const auto &m : m_all)
        if (m.rootId == rootId) res.append(m);
    return res;
}

bool ModelCatalog::openDb()
{
    QDir().mkpath(QFileInfo(dbPath()).absolutePath());
    auto db = QSqlDatabase::contains(m_connName)
                  ? QSqlDatabase::database(m_connName, /*open=*/false)
                  : QSqlDatabase::addDatabase("QSQLITE", m_connName);
    db.setDatabaseName(dbPath());
    if (!db.isOpen() && !db.open()) {
        qWarning() << "ModelCatalog: cannot open DB:" << db.lastError().text();
        return false;
    }
    QSqlQuery q(db);
    q.exec(QStringLiteral("PRAGMA busy_timeout=3000"));
    const QStringList stmts = QString::fromUtf8(kSchema).split(';', Qt::SkipEmptyParts);
    for (const auto &s : stmts) {
        if (!s.trimmed().isEmpty())
            q.exec(s.trimmed());
    }
    // Migración: DBs viejas no tienen las columnas de composición real. ALTER
    // falla silencioso si la columna ya existe (lo ignoramos).
    for (const char *alter : {
             "ALTER TABLE catalog_models ADD COLUMN quant_real TEXT",
             "ALTER TABLE catalog_models ADD COLUMN tensor_breakdown TEXT",
             "ALTER TABLE catalog_models ADD COLUMN bpw REAL NOT NULL DEFAULT 0",
             "ALTER TABLE catalog_models ADD COLUMN quant_mismatch INTEGER NOT NULL DEFAULT 0" }) {
        q.exec(QString::fromUtf8(alter));
    }
    return true;
}

void ModelCatalog::loadFromDb()
{
    auto db = QSqlDatabase::database(m_connName);
    QSqlQuery q("SELECT id, root_id, absolute_path, file_name, size_bytes, mtime, "
                "family_hint, quant_hint, is_vision_candidate, is_draft_candidate, "
                "sha256, is_available, quant_real, tensor_breakdown, bpw, "
                "quant_mismatch FROM catalog_models", db);
    while (q.next()) {
        CatalogModel m;
        m.id = q.value(0).toString();
        m.rootId = q.value(1).toString();
        m.absolutePath = q.value(2).toString();
        m.fileName = q.value(3).toString();
        m.sizeBytes = q.value(4).toLongLong();
        m.mtime = QDateTime::fromString(q.value(5).toString(), Qt::ISODate);
        m.familyHint = q.value(6).toString();
        m.quantHint = q.value(7).toString();
        m.isVisionCandidate = q.value(8).toBool();
        m.isDraftCandidate = q.value(9).toBool();
        m.sha256 = q.value(10).toString();
        m.isAvailable = q.value(11).toBool();
        m.quantReal = q.value(12).toString();
        m.tensorBreakdown = q.value(13).toString();
        m.bpw = q.value(14).toDouble();
        m.quantMismatch = q.value(15).toBool();
        m_all.append(m);
    }
}

void ModelCatalog::saveToDb(const CatalogModel &m)
{
    auto db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(R"(
        INSERT OR REPLACE INTO catalog_models
        (id, root_id, absolute_path, file_name, size_bytes, mtime,
         family_hint, quant_hint, is_vision_candidate, is_draft_candidate,
         sha256, is_available, quant_real, tensor_breakdown, bpw, quant_mismatch)
        VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
    )");
    q.addBindValue(m.id);
    q.addBindValue(m.rootId);
    q.addBindValue(m.absolutePath);
    q.addBindValue(m.fileName);
    q.addBindValue(m.sizeBytes);
    q.addBindValue(m.mtime.toString(Qt::ISODate));
    q.addBindValue(m.familyHint);
    q.addBindValue(m.quantHint);
    q.addBindValue(m.isVisionCandidate ? 1 : 0);
    q.addBindValue(m.isDraftCandidate ? 1 : 0);
    q.addBindValue(m.sha256);
    q.addBindValue(m.isAvailable ? 1 : 0);
    q.addBindValue(m.quantReal);
    q.addBindValue(m.tensorBreakdown);
    q.addBindValue(m.bpw);
    q.addBindValue(m.quantMismatch ? 1 : 0);
    if (!q.exec())
        qWarning() << "saveToDb failed:" << q.lastError().text();
}

void ModelCatalog::rebuildVisible()
{
    m_visible.clear();
    for (const auto &m : m_all) {
        if (matchesFilter(m))
            m_visible.append(&m);
    }
}

bool ModelCatalog::matchesFilter(const CatalogModel &m) const
{
    if (!m_filterRootId.isEmpty() && m.rootId != m_filterRootId) return false;
    if (!m_filterFamily.isEmpty() && m.familyHint != m_filterFamily) return false;
    if (m_filterVisionOnly && !m.isVisionCandidate) return false;
    return true;
}

QString ModelCatalog::dbPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + "/model_catalog.db";
}

int ModelCatalog::indexOfId(const QString &id) const
{
    for (int i = 0; i < m_all.size(); ++i)
        if (m_all[i].id == id) return i;
    return -1;
}
