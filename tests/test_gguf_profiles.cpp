// Tests unitarios de la lógica pura del núcleo:
//   - GGUFScanner: inferencia familia/quant/vision/draft por nombre de archivo.
//   - EffectiveProfileBuilder: composición de args + degradación/bloqueo por flags.
//
// Build: cmake -DBUILD_TESTS=ON ...  → target LlamaCodeTests.
// Run:   ctest --test-dir build  (o ejecutar LlamaCodeTests directo).

#include <QtTest>
#include "core/GGUFScanner.h"
#include "core/profiles/EffectiveProfileBuilder.h"
#include "core/profiles/ProfileTypes.h"
#include "core/LlamaBinary.h"
#include "core/CatalogModel.h"

class CoreTests : public QObject
{
    Q_OBJECT

private slots:
    // ── GGUFScanner::inferFamily ──
    void inferFamily_data();
    void inferFamily();

    // ── GGUFScanner::inferQuant ──
    void inferQuant_data();
    void inferQuant();

    // ── GGUFScanner candidatos vision/draft ──
    void visionCandidate();
    void draftCandidate();

    // ── EffectiveProfileBuilder ──
    void builder_emitsHostPort();
    void builder_dropsUnsupportedFlag();
    void builder_missingModelIsBlocking();
    void builder_emitsSpecFlags();
    void builder_forcesF16KvWithDraft();
};

void CoreTests::inferFamily_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<QString>("family");
    QTest::newRow("qwen")    << "Qwen2.5-7B-Instruct-Q4_K_M.gguf" << "qwen";
    QTest::newRow("llama")   << "Meta-Llama-3.1-8B.Q5_K_M.gguf"   << "llama";
    QTest::newRow("mistral") << "Mistral-7B-v0.3-Q6_K.gguf"       << "mistral";
    QTest::newRow("gemma")   << "gemma-2-9b-it-Q4_0.gguf"         << "gemma";
    QTest::newRow("phi")     << "Phi-3.5-mini-instruct-Q8_0.gguf" << "phi";
    QTest::newRow("deepseek")<< "DeepSeek-Coder-V2-Q4_K_M.gguf"   << "deepseek";
}

void CoreTests::inferFamily()
{
    QFETCH(QString, file);
    QFETCH(QString, family);
    QCOMPARE(GGUFScanner::inferFamily(file).toLower(), family);
}

void CoreTests::inferQuant_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<QString>("quant");
    QTest::newRow("q4km") << "model-Q4_K_M.gguf" << "Q4_K_M";
    QTest::newRow("q8")   << "model-Q8_0.gguf"   << "Q8_0";
    QTest::newRow("iq3")  << "model-IQ3_XS.gguf" << "IQ3_XS";
    QTest::newRow("bf16") << "model-BF16.gguf"   << "BF16";
}

void CoreTests::inferQuant()
{
    QFETCH(QString, file);
    QFETCH(QString, quant);
    QCOMPARE(GGUFScanner::inferQuant(file).toUpper(), quant);
}

void CoreTests::visionCandidate()
{
    QVERIFY(GGUFScanner::isVisionCandidate("llava-v1.6-mmproj-f16.gguf"));
    QVERIFY(!GGUFScanner::isVisionCandidate("Qwen2.5-7B-Q4_K_M.gguf"));
}

void CoreTests::draftCandidate()
{
    // "draft" en el nombre, o tamaño chico (<2GB).
    QVERIFY(GGUFScanner::isDraftCandidate("qwen-0.5b-draft-Q4.gguf", 400LL * 1024 * 1024));
    QVERIFY(GGUFScanner::isDraftCandidate("tiny.gguf", 1LL * 1024 * 1024 * 1024));
    QVERIFY(!GGUFScanner::isDraftCandidate("Qwen2.5-32B-Q5_K_M.gguf", 20LL * 1024 * 1024 * 1024));
}

// build() valida que el binario exista en disco → necesitamos un archivo real.
static QString existingBinaryPath()
{
    static QString p;
    if (p.isEmpty()) {
        p = QDir(QDir::tempPath()).filePath("llamacode_test_bin.exe");
        QFile f(p);
        if (f.open(QIODevice::WriteOnly)) { f.write("x"); f.close(); }
    }
    return p;
}

// Construye un Context mínimo válido (binario + modelo presentes).
static EffectiveProfileBuilder::Context makeCtx()
{
    EffectiveProfileBuilder::Context ctx;
    ctx.binary.id = "bin1";
    ctx.binary.path = existingBinaryPath();
    ctx.binary.name = "test";
    ctx.backend.host = "127.0.0.1";
    ctx.backend.port = 9099;
    ctx.backend.binaryId = "bin1";
    ctx.catalogModel.id = "m1";
    ctx.catalogModel.absolutePath = "C:/models/test.gguf";
    ctx.model.modelId = "m1";
    return ctx;
}

void CoreTests::builder_emitsHostPort()
{
    auto ctx = makeCtx();
    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
    const int hi = ep.effectiveArgs.indexOf("--host");
    const int pi = ep.effectiveArgs.indexOf("--port");
    QVERIFY(hi >= 0 && hi + 1 < ep.effectiveArgs.size());
    QCOMPARE(ep.effectiveArgs[hi + 1], QStringLiteral("127.0.0.1"));
    QVERIFY(pi >= 0 && pi + 1 < ep.effectiveArgs.size());
    QCOMPARE(ep.effectiveArgs[pi + 1], QStringLiteral("9099"));
}

void CoreTests::builder_dropsUnsupportedFlag()
{
    auto ctx = makeCtx();
    // Binario que sólo soporta --host/--port/--model → flash-attn debe dropearse.
    ctx.binary.supportedFlags = QStringList{"--host", "--port", "--model"};
    ctx.runtime.flashAttention = true;
    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
    QVERIFY(!ep.effectiveArgs.contains("--flash-attn"));
    QVERIFY(!ep.warnings.isEmpty());  // degradación reportada
}

void CoreTests::builder_missingModelIsBlocking()
{
    auto ctx = makeCtx();
    ctx.model.modelId.clear();
    ctx.catalogModel = CatalogModel{};  // sin modelo resuelto
    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
    QVERIFY(!ep.blockingErrors.isEmpty());
}

// Con draft model resuelto, los flags spec-draft seteados deben emitirse.
void CoreTests::builder_emitsSpecFlags()
{
    auto ctx = makeCtx();
    ctx.model.draftModelId = "d1";
    ctx.model.specType = "draft-mtp";
    ctx.model.specDraftNMax = 3;
    ctx.model.specDraftNgl = "all";
    ctx.model.specDraftTypeK = "q8_0";
    ctx.model.specDraftTypeV = "q8_0";
    ctx.draftModel.id = "d1";
    ctx.draftModel.isAvailable = true;
    ctx.draftModel.absolutePath = "C:/models/draft.gguf";

    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
    const QStringList &a = ep.effectiveArgs;
    QVERIFY(a.contains("--draft-model"));
    int i = a.indexOf("--spec-type");
    QVERIFY(i >= 0 && a[i + 1] == "draft-mtp");
    i = a.indexOf("--spec-draft-n-max");
    QVERIFY(i >= 0 && a[i + 1] == "3");
    i = a.indexOf("--spec-draft-ngl");
    QVERIFY(i >= 0 && a[i + 1] == "all");
    QVERIFY(a.contains("--spec-draft-type-k"));
    QVERIFY(a.contains("--spec-draft-type-v"));
}

// Spec decoding activo + KV cache cuantizado → forzar f16 (no emitir el flag) y
// avisar. Sin draft, el quant pasa normal.
void CoreTests::builder_forcesF16KvWithDraft()
{
    // Caso sin draft: el quant se emite.
    {
        auto ctx = makeCtx();
        ctx.runtime.cacheType = "q4_0";
        const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
        QVERIFY(ep.effectiveArgs.contains("--cache-type-k"));
    }
    // Caso con draft disponible: se descarta el quant y se avisa.
    {
        auto ctx = makeCtx();
        ctx.runtime.cacheType = "q4_0";
        ctx.model.draftModelId = "d1";
        ctx.draftModel.id = "d1";
        ctx.draftModel.isAvailable = true;
        ctx.draftModel.absolutePath = "C:/models/draft.gguf";
        const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
        QVERIFY(!ep.effectiveArgs.contains("--cache-type-k"));
        bool warned = false;
        for (const QString &w : ep.warnings)
            if (w.contains("f16")) warned = true;
        QVERIFY(warned);
    }
}

QTEST_MAIN(CoreTests)
#include "test_gguf_profiles.moc"
