// Unit tests de CapabilityDetector::parse (PURO: parsea el texto de --help y
// extrae flags soportados + alias corto→largo). Sin proceso ni binario real.

#include <QtTest>
#include "core/CapabilityDetector.h"

class CapabilityTests : public QObject
{
    Q_OBJECT
private slots:
    void parse_extractsLongFlags();
    void parse_shortAliasesToLong();
    void parse_stripsAnsi();
    void parse_legacyDraftAlias();
    void parse_ignoresDescriptionContinuation();
};

void CapabilityTests::parse_extractsLongFlags()
{
    const QString help =
        "  --host HOST          listen address\n"
        "  --port PORT          listen port\n"
        "  --flash-attn         enable flash attention\n";
    const auto cap = CapabilityDetector::parse(help);
    QVERIFY(cap.success);
    QVERIFY(cap.hasFlag("--host"));
    QVERIFY(cap.hasFlag("--port"));
    QVERIFY(cap.hasFlag("--flash-attn"));
}

void CapabilityTests::parse_shortAliasesToLong()
{
    const QString help = "  -c, --ctx-size N     context size\n";
    const auto cap = CapabilityDetector::parse(help);
    QVERIFY(cap.hasFlag("--ctx-size"));
    QCOMPARE(cap.flagAliases.value("-c"), QStringLiteral("--ctx-size"));
}

void CapabilityTests::parse_stripsAnsi()
{
    const QString help = "  \x1B[1m--mmap\x1B[0m   use mmap\n";
    const auto cap = CapabilityDetector::parse(help);
    QVERIFY(cap.hasFlag("--mmap"));
}

void CapabilityTests::parse_legacyDraftAlias()
{
    const QString help = "  --spec-draft-model PATH   speculative draft model\n";
    const auto cap = CapabilityDetector::parse(help);
    QVERIFY(cap.hasFlag("--spec-draft-model"));
    QCOMPARE(cap.flagAliases.value("--draft-model"), QStringLiteral("--spec-draft-model"));
}

void CapabilityTests::parse_ignoresDescriptionContinuation()
{
    // Líneas con 17+ espacios de indent son continuación de descripción, no flags.
    const QString help =
        "  --host HOST\n"
        "                     this --not-a-flag is part of the description\n";
    const auto cap = CapabilityDetector::parse(help);
    QVERIFY(cap.hasFlag("--host"));
    QVERIFY(!cap.hasFlag("--not-a-flag"));
}

QTEST_MAIN(CapabilityTests)
#include "test_capability.moc"
