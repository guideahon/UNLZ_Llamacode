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

    // ── GGUFScanner::readComposition (parser binario) ──
    void readComposition_realTensors();
    void readComposition_rejectsGarbage();

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

// ── Helpers para construir un GGUF sintético en disco ──────────────────────
namespace {
void putU32(QByteArray &b, quint32 v) {
    for (int i = 0; i < 4; ++i) b.append(char((v >> (8*i)) & 0xFF));
}
void putU64(QByteArray &b, quint64 v) {
    for (int i = 0; i < 8; ++i) b.append(char((v >> (8*i)) & 0xFF));
}
void putStr(QByteArray &b, const QByteArray &s) {
    putU64(b, quint64(s.size()));
    b.append(s);
}
// Escribe un tensor info: name, n_dims, dims[], type, offset.
void putTensor(QByteArray &b, const QByteArray &name,
               const QList<quint64> &dims, quint32 type) {
    putStr(b, name);
    putU32(b, quint32(dims.size()));
    for (quint64 d : dims) putU64(b, d);
    putU32(b, type);
    putU64(b, 0); // offset
}
// GGUF v3 mínimo: magic, version, tensorCount, kvCount=0, luego tensor infos.
QString writeGgufFixture(const QString &name, const QList<QPair<quint32, quint64>> &tensors)
{
    QByteArray b;
    putU32(b, 0x46554747u); // "GGUF"
    putU32(b, 3);           // version
    putU64(b, quint64(tensors.size()));
    putU64(b, 0);           // kv count
    int idx = 0;
    for (const auto &t : tensors)
        putTensor(b, QByteArray("t") + QByteArray::number(idx++),
                  {t.second}, t.first);
    const QString path = QDir(QDir::tempPath()).filePath(name);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) { f.write(b); f.close(); }
    return path;
}
} // namespace

void CoreTests::readComposition_realTensors()
{
    // Archivo llamado "Q4_K_XL" pero contenido = mayoría q4_0 (caso unsloth).
    // type ids: 2=q4_0, 14=q6_k, 0=f32.
    const QString path = writeGgufFixture(
        "gemma-fake-Q4_K_XL.gguf",
        { {2, 1000000}, {2, 2000000}, {14, 50000}, {0, 1000} });

    const auto c = GGUFScanner::readComposition(path, QFileInfo(path).size());
    QVERIFY(c.valid);
    QCOMPARE(c.dominantQuant, QStringLiteral("q4_0")); // por elementos, no por nombre
    QCOMPARE(c.typeTensors.value("q4_0"), 2);
    QCOMPARE(c.typeTensors.value("q6_k"), 1);
    QVERIFY(c.totalElements == 3051000);
    QVERIFY(c.bpw > 0.0);
    QVERIFY(c.breakdown().contains("q4_0:2"));
}

void CoreTests::readComposition_rejectsGarbage()
{
    const QString path = QDir(QDir::tempPath()).filePath("not_a_gguf.bin");
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("this is definitely not a gguf header at all");
    f.close();
    const auto c = GGUFScanner::readComposition(path, QFileInfo(path).size());
    QVERIFY(!c.valid);
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
