// Integration tests de ModelCatalog (persistencia SQLite, aislada vía
// QStandardPaths test mode) + CatalogModel::sizeLabel.
//   - addOrUpdate / addBatch / get / getAt / findById / allForRoot
//   - filtros rootId / family / visionOnly
//   - markRootUnavailable / removeByRootId / reload

#include <QtTest>
#include <QStandardPaths>
#include "core/CatalogModel.h"
#include "core/ModelCatalog.h"

static CatalogModel mk(const QString &id, const QString &root, const QString &family,
                       bool vision = false, qint64 size = 1024)
{
    CatalogModel m;
    m.id = id; m.rootId = root; m.absolutePath = "C:/models/" + id + ".gguf";
    m.fileName = id + ".gguf"; m.familyHint = family;
    m.isVisionCandidate = vision; m.sizeBytes = size; m.isAvailable = true;
    m.mtime = QDateTime::currentDateTime();  // mtime es NOT NULL en la DB
    return m;
}

class CatalogTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        // Borrar la DB de corridas previas (la ubicación de test es estable) para
        // que los tests partan de un catálogo limpio y determinista.
        QFile::remove(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                      + "/model_catalog.db");
    }
    void init();  // catálogo limpio por test

    void sizeLabel();
    void addOrUpdate_andGet();
    void addBatch_andFindById();
    void allForRoot();
    void filterByFamily();
    void filterVisionOnly();
    void removeByRootId();
    void persistsAcrossReload();

private:
    void clearCatalog(ModelCatalog &c);
};

void CatalogTests::clearCatalog(ModelCatalog &c)
{
    c.removeByRootId("rootA");
    c.removeByRootId("rootB");
}

void CatalogTests::init() { ModelCatalog c; clearCatalog(c); }

void CatalogTests::sizeLabel()
{
    CatalogModel m; m.sizeBytes = 1536LL * 1024 * 1024;  // ~1.5 GB
    QVERIFY(m.sizeLabel().contains("GB") || m.sizeLabel().contains("GiB"));
}

void CatalogTests::addOrUpdate_andGet()
{
    ModelCatalog c;
    c.addOrUpdate(mk("a1", "rootA", "qwen"));
    QCOMPARE(c.get("a1").value("family").toString(), QStringLiteral("qwen"));
    // update mismo id
    c.addOrUpdate(mk("a1", "rootA", "llama"));
    QCOMPARE(c.get("a1").value("family").toString(), QStringLiteral("llama"));
}

void CatalogTests::addBatch_andFindById()
{
    ModelCatalog c;
    c.addBatch({mk("a1", "rootA", "qwen"), mk("a2", "rootA", "phi")});
    QCOMPARE(c.findById("a2").familyHint, QStringLiteral("phi"));
    QVERIFY(c.findById("nope").id.isEmpty());
}

void CatalogTests::allForRoot()
{
    ModelCatalog c;
    c.addBatch({mk("a1", "rootA", "qwen"), mk("b1", "rootB", "phi")});
    QCOMPARE(c.allForRoot("rootA").size(), 1);
    QCOMPARE(c.allForRoot("rootA").first().id, QStringLiteral("a1"));
}

void CatalogTests::filterByFamily()
{
    ModelCatalog c;
    c.addBatch({mk("a1", "rootA", "qwen"), mk("a2", "rootA", "phi")});
    c.setFilterFamily("qwen");
    QCOMPARE(c.count(), 1);
    c.setFilterFamily("");  // reset
    QVERIFY(c.count() >= 2);
}

void CatalogTests::filterVisionOnly()
{
    ModelCatalog c;
    c.addBatch({mk("a1", "rootA", "qwen", false), mk("a2", "rootA", "qwen", true)});
    c.setFilterVisionOnly(true);
    QCOMPARE(c.count(), 1);
    c.setFilterVisionOnly(false);
}

void CatalogTests::removeByRootId()
{
    ModelCatalog c;
    c.addBatch({mk("a1", "rootA", "qwen"), mk("b1", "rootB", "phi")});
    c.removeByRootId("rootA");
    QVERIFY(c.findById("a1").id.isEmpty());
    QVERIFY(!c.findById("b1").id.isEmpty());
}

void CatalogTests::persistsAcrossReload()
{
    { ModelCatalog c; c.addOrUpdate(mk("a1", "rootA", "gemma")); }
    ModelCatalog c2;  // reabre la misma DB
    QCOMPARE(c2.findById("a1").familyHint, QStringLiteral("gemma"));
}

QTEST_MAIN(CatalogTests)
#include "test_catalog.moc"
