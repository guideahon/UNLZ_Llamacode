// Unit tests de EvalSuite: loader desde JSON (string y archivo), categorías
// únicas en orden, manejo de JSON inválido. Reusa el sample real del repo.

#include <QtTest>
#include <QTemporaryDir>
#include "core/eval/EvalSuite.h"

class EvalTests : public QObject
{
    Q_OBJECT
private slots:
    void loadFromJson_parsesTasks();
    void categories_uniqueInOrder();
    void invalidJson_returnsEmptyWithError();
    void loadFromFile_roundTrip();
};

static QByteArray sampleJson()
{
    return R"({
        "name": "demo",
        "description": "suite de prueba",
        "tasks": [
            {"id":"t1","category":"coding","prompt":"escribe fizzbuzz",
             "acceptance":["FizzBuzz"],"weight":2},
            {"id":"t2","category":"docs","prompt":"resume el doc",
             "acceptance":["resumen"],"attachments":["a.pdf"]},
            {"id":"t3","category":"coding","prompt":"otra de coding",
             "acceptance":[]}
        ]
    })";
}

void EvalTests::loadFromJson_parsesTasks()
{
    QString err;
    const EvalSuite s = EvalSuite::loadFromJson(sampleJson(), &err);
    QVERIFY2(err.isEmpty(), qPrintable(err));
    QCOMPARE(s.name, QStringLiteral("demo"));
    QCOMPARE(s.tasks.size(), 3);
    QCOMPARE(s.tasks.first().category, QStringLiteral("coding"));
    QCOMPARE(s.tasks.first().weight, 2);
    QVERIFY(!s.isEmpty());
}

void EvalTests::categories_uniqueInOrder()
{
    const EvalSuite s = EvalSuite::loadFromJson(sampleJson());
    const QStringList cats = s.categories();
    QCOMPARE(cats, (QStringList{"coding", "docs"}));  // únicas, en orden de aparición
}

void EvalTests::invalidJson_returnsEmptyWithError()
{
    QString err;
    const EvalSuite s = EvalSuite::loadFromJson("{ not json", &err);
    QVERIFY(s.isEmpty());
    QVERIFY(!err.isEmpty());
}

void EvalTests::loadFromFile_roundTrip()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("suite.json");
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(sampleJson());
    f.close();
    QString err;
    const EvalSuite s = EvalSuite::loadFromFile(path, &err);
    QVERIFY2(err.isEmpty(), qPrintable(err));
    QCOMPARE(s.tasks.size(), 3);
}

QTEST_MAIN(EvalTests)
#include "test_eval.moc"
