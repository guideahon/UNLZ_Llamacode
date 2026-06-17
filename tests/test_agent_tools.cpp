// Integration tests de AgentToolRunner (tools nativas del agente). Sin MCP ni
// red ni modelo: ejercitamos el dispatch de tools deterministas contra un cwd
// temporal y verificamos el resultado vía la señal toolExecuted.
//   - write_file → read_file → edit_file (con metadata de diff).
//   - confinamiento al cwd (bloquea rutas fuera del proyecto).
//   - grep / glob.
//   - run_shell async (echo trivial).

#include <QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDir>
#include <QFile>
#include "core/agent/AgentToolRunner.h"

class AgentToolsTests : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void writeReadEditCycle();
    void confinement_blocksOutsideCwd();
    void editFile_missingFails();
    void grep_findsMatch();
    void glob_listsFiles();
    void runShell_echo();
    void hybridSearch_depGraphAndBudget();

private:
    QVariantMap call(const QString &name, const QJsonObject &args);
    AgentToolRunner *m_runner = nullptr;
    QTemporaryDir m_dir;
};

void AgentToolsTests::init()
{
    m_runner = new AgentToolRunner;
    m_runner->setConfined(true);
}

void AgentToolsTests::cleanup()
{
    delete m_runner;
    m_runner = nullptr;
}

// Ejecuta una tool síncrona y devuelve el map de toolExecuted.
QVariantMap AgentToolsTests::call(const QString &name, const QJsonObject &args)
{
    QSignalSpy spy(m_runner, &AgentToolRunner::toolExecuted);
    const QString argsJson = QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact));
    m_runner->executeTool("c1", name, argsJson, m_dir.path());
    if (spy.isEmpty()) spy.wait(3000);
    if (spy.isEmpty()) return {};
    return spy.last().first().toMap();
}

void AgentToolsTests::writeReadEditCycle()
{
    QVariantMap w = call("write_file", {{"path", "sub/a.txt"}, {"content", "hello world"}});
    QVERIFY(w.value("ok").toBool());
    QVERIFY(w.value("isWrite").toBool());
    QVERIFY(QFile::exists(m_dir.filePath("sub/a.txt")));

    QVariantMap r = call("read_file", {{"path", "sub/a.txt"}});
    QVERIFY(r.value("ok").toBool());
    QVERIFY(r.value("result").toString().contains("hello world"));

    QVariantMap e = call("edit_file", {{"path", "sub/a.txt"},
                                       {"old_string", "world"}, {"new_string", "qt"}});
    QVERIFY(e.value("ok").toBool());
    QVERIFY(e.value("diff").toString().length() > 0);

    QVariantMap r2 = call("read_file", {{"path", "sub/a.txt"}});
    QVERIFY(r2.value("result").toString().contains("hello qt"));
}

void AgentToolsTests::confinement_blocksOutsideCwd()
{
    QVariantMap w = call("write_file", {{"path", "../escape.txt"}, {"content", "x"}});
    QVERIFY(!w.value("ok").toBool());
    QVERIFY(w.value("result").toString().contains("fuera del proyecto"));
    QVERIFY(!QFile::exists(QDir(m_dir.path()).filePath("../escape.txt")));
}

void AgentToolsTests::editFile_missingFails()
{
    QVariantMap e = call("edit_file", {{"path", "nope.txt"},
                                       {"old_string", "a"}, {"new_string", "b"}});
    QVERIFY(!e.value("ok").toBool());
    QVERIFY(e.value("result").toString().contains("no existe"));
}

void AgentToolsTests::grep_findsMatch()
{
    call("write_file", {{"path", "code.txt"}, {"content", "alpha\nNEEDLE here\nomega"}});
    QVariantMap g = call("grep", {{"pattern", "NEEDLE"}});
    QVERIFY(g.value("result").toString().contains("NEEDLE") ||
            g.value("result").toString().contains("code.txt"));
}

void AgentToolsTests::glob_listsFiles()
{
    call("write_file", {{"path", "one.cpp"}, {"content", "1"}});
    call("write_file", {{"path", "two.cpp"}, {"content", "2"}});
    QVariantMap g = call("glob", {{"pattern", "*.cpp"}});
    QVERIFY(g.value("result").toString().contains("one.cpp"));
    QVERIFY(g.value("result").toString().contains("two.cpp"));
}

void AgentToolsTests::runShell_echo()
{
    QSignalSpy spy(m_runner, &AgentToolRunner::toolExecuted);
    m_runner->executeTool("sh1", "run_shell",
                          R"({"command":"echo hola_test","timeout_s":30})", m_dir.path());
    QVERIFY(spy.wait(15000));
    const QVariantMap out = spy.last().first().toMap();
    QVERIFY(out.value("result").toString().contains("hola_test"));
}

// hybrid_search sin server: cae a BM25 puro, pero igual debe empaquetar por
// token_budget y expandir el dep-graph (vecino vía #include). util.h es el vecino
// que main.cpp incluye y que NO contiene el término buscado.
void AgentToolsTests::hybridSearch_depGraphAndBudget()
{
    call("write_file", {{"path", "util.h"},
                        {"content", "int helper_widget_count();\n"}});
    call("write_file", {{"path", "main.cpp"},
                        {"content", "#include \"util.h\"\n"
                                    "// WIDGETKEY marker for retrieval\n"
                                    "int main(){ return helper_widget_count(); }\n"}});

    // budget chico → solo el chunk top (main.cpp); util.h queda fuera del
    // resultado y debe emerger como vecino del dep-graph.
    QVariantMap h = call("hybrid_search", {{"query", "WIDGETKEY"},
                                           {"token_budget", 25},
                                           {"expand_graph", true}});
    QVERIFY(h.value("ok").toBool());
    const QString res = h.value("result").toString();
    QVERIFY(res.contains("main.cpp"));            // hit BM25
    QVERIFY(res.contains("~"));                   // header con ~N tok (budget activo)
    QVERIFY(res.contains("dep-graph"));           // footer de vecinos
    QVERIFY(res.contains("util.h"));              // vecino vía #include
}

QTEST_MAIN(AgentToolsTests)
#include "test_agent_tools.moc"
