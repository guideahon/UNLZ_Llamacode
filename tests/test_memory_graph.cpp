// Unit tests de la memoria estructurada del agente:
//   - MemoryStore: save / recall (ranking por keywords + filtro scope) / forget
//     (stale vs delete) sobre el JSONL en <cwd>/.llamacode/.
//   - GraphStore: addEntity / link / query (depth 1 y 2) + normalización de nombres.
// Cada test usa un cwd temporal aislado.

#include <QtTest>
#include <QTemporaryDir>
#include "core/agent/MemoryStore.h"
#include "core/agent/GraphStore.h"

class MemoryGraphTests : public QObject
{
    Q_OBJECT
private slots:
    void memory_saveThenRecall();
    void memory_recallFiltersByScope();
    void memory_recallRanksByQuery();
    void memory_forgetStale();
    void memory_forgetDelete();

    void graph_addEntityAndQuery();
    void graph_linkRelation();
    void graph_queryDepth2();
    void graph_normalizesNames();
};

void MemoryGraphTests::memory_saveThenRecall()
{
    QTemporaryDir dir;
    MemoryStore::save(dir.path(), "el build usa cmake con BUILD_TESTS",
                      "project", "fact", 0.9, "user");
    const QString out = MemoryStore::recall(dir.path(), "", "", 10);
    QVERIFY(out.contains("cmake"));
    QVERIFY(QFile::exists(MemoryStore::jsonlPath(dir.path())));
}

void MemoryGraphTests::memory_recallFiltersByScope()
{
    QTemporaryDir dir;
    MemoryStore::save(dir.path(), "regla del proyecto X", "project", "decision", 0.8, "");
    MemoryStore::save(dir.path(), "preferencia personal Y", "personal", "preference", 0.8, "");
    const QString proj = MemoryStore::recall(dir.path(), "", "project", 10);
    QVERIFY(proj.contains("proyecto X"));
    QVERIFY(!proj.contains("personal Y"));
}

void MemoryGraphTests::memory_recallRanksByQuery()
{
    QTemporaryDir dir;
    MemoryStore::save(dir.path(), "tema sobre vulkan backend", "project", "fact", 0.5, "");
    MemoryStore::save(dir.path(), "tema sobre cuda backend", "project", "fact", 0.5, "");
    const QString out = MemoryStore::recall(dir.path(), "vulkan", "", 1);
    QVERIFY(out.contains("vulkan"));
    QVERIFY(!out.contains("cuda"));
}

void MemoryGraphTests::memory_forgetStale()
{
    QTemporaryDir dir;
    MemoryStore::save(dir.path(), "dato obsoleto deprecado", "project", "fact", 0.5, "");
    MemoryStore::forget(dir.path(), "obsoleto", "", "stale");
    const QString out = MemoryStore::recall(dir.path(), "", "", 10);
    QVERIFY(!out.contains("dato obsoleto"));  // ya no se recupera
}

void MemoryGraphTests::memory_forgetDelete()
{
    QTemporaryDir dir;
    MemoryStore::save(dir.path(), "borrar esta linea entera", "project", "fact", 0.5, "");
    MemoryStore::save(dir.path(), "conservar esta", "project", "fact", 0.5, "");
    MemoryStore::forget(dir.path(), "borrar", "", "delete");
    QFile f(MemoryStore::jsonlPath(dir.path()));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QString content = QString::fromUtf8(f.readAll());
    QVERIFY(!content.contains("borrar esta linea"));
    QVERIFY(content.contains("conservar esta"));
}

void MemoryGraphTests::graph_addEntityAndQuery()
{
    QTemporaryDir dir;
    GraphStore::addEntity(dir.path(), "AppController", "module");
    const QString out = GraphStore::query(dir.path(), "AppController", 1);
    QVERIFY(out.contains("AppController"));
    QVERIFY(QFile::exists(GraphStore::jsonlPath(dir.path())));
}

void MemoryGraphTests::graph_linkRelation()
{
    QTemporaryDir dir;
    GraphStore::link(dir.path(), "AppController", "usa", "ProfileManager");
    const QString out = GraphStore::query(dir.path(), "AppController", 1);
    QVERIFY(out.contains("ProfileManager"));
    QVERIFY(out.contains("usa"));
}

void MemoryGraphTests::graph_queryDepth2()
{
    QTemporaryDir dir;
    GraphStore::link(dir.path(), "A", "usa", "B");
    GraphStore::link(dir.path(), "B", "usa", "C");
    const QString d1 = GraphStore::query(dir.path(), "A", 1);
    const QString d2 = GraphStore::query(dir.path(), "A", 2);
    QVERIFY(d1.contains("B"));
    QVERIFY(d2.contains("C"));  // vecino de vecino visible con depth=2
}

void MemoryGraphTests::graph_normalizesNames()
{
    QTemporaryDir dir;
    GraphStore::link(dir.path(), "  MyModule  ", "rel", "Other");
    // Misma entidad normalizada (trim/lowercase) → query la encuentra y ve la relación.
    const QString out = GraphStore::query(dir.path(), "mymodule", 1);
    QVERIFY(!out.isEmpty());
    QVERIFY(out.contains("Other"));
}

QTEST_MAIN(MemoryGraphTests)
#include "test_memory_graph.moc"
