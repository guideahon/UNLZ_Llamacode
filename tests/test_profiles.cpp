// Unit tests de perfiles:
//   - ProfileTypes: serialización JSON ida/vuelta de cada struct de perfil.
//   - ProfileManager: CRUD de backends/modelProfiles/runtimes/harness/launch,
//     getters, favoritos/alias y persistencia a disco (aislada vía
//     LLAMACODE_PROFILES_DIR).
//
// NOTA: ProfileManager::storagePath cachea la raíz en un 'static' la primera
// vez que se llama, así que el env var DEBE setearse antes de construir el
// primer ProfileManager del proceso (lo hacemos en initTestCase).

#include <QtTest>
#include <QTemporaryDir>
#include "core/profiles/ProfileTypes.h"
#include "core/profiles/ProfileManager.h"

class ProfilesTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();

    void backendProfile_jsonRoundTrip();
    void modelProfile_jsonRoundTrip();
    void runtimePreset_jsonRoundTrip();
    void launchProfile_jsonRoundTrip();
    void masterConfig_jsonRoundTrip();

    void manager_addGetRemoveBackend();
    void manager_addModelProfile();
    void manager_favoriteAndAlias();
    void manager_persistsAcrossInstances();

private:
    QTemporaryDir m_dir;
};

void ProfilesTests::initTestCase()
{
    QVERIFY(m_dir.isValid());
    qputenv("LLAMACODE_PROFILES_DIR", m_dir.path().toLocal8Bit());
}

void ProfilesTests::backendProfile_jsonRoundTrip()
{
    BackendProfile b;
    b.id = "b1"; b.name = "cuda-8080"; b.binaryId = "bin1";
    b.host = "0.0.0.0"; b.port = 9090;
    b.baseArgs = QStringList{"--ctx", "8192"};
    b.envOverrides.insert("CUDA_VISIBLE_DEVICES", "0");
    const BackendProfile r = BackendProfile::fromJson(b.toJson());
    QCOMPARE(r.id, b.id);
    QCOMPARE(r.name, b.name);
    QCOMPARE(r.binaryId, b.binaryId);
    QCOMPARE(r.host, b.host);
    QCOMPARE(r.port, b.port);
    QCOMPARE(r.baseArgs, b.baseArgs);
    QCOMPARE(r.envOverrides.value("CUDA_VISIBLE_DEVICES"), QStringLiteral("0"));
}

void ProfilesTests::modelProfile_jsonRoundTrip()
{
    ModelProfile m;
    m.id = "m1"; m.name = "qwen"; m.modelId = "cat1";
    m.mmprojId = "mm1"; m.draftModelId = "d1";
    const ModelProfile r = ModelProfile::fromJson(m.toJson());
    QCOMPARE(r.name, m.name);
    QCOMPARE(r.modelId, m.modelId);
    QCOMPARE(r.mmprojId, m.mmprojId);
    QCOMPARE(r.draftModelId, m.draftModelId);
}

void ProfilesTests::runtimePreset_jsonRoundTrip()
{
    RuntimePreset p;
    p.id = "r1"; p.name = "fast"; p.ctx = 16384; p.batch = 1024; p.ubatch = 256;
    p.threads = 8; p.gpuLayers = 99; p.flashAttention = true; p.mmap = false;
    p.mlock = true; p.contBatching = false; p.cacheType = "q8_0"; p.parallelSlots = 4;
    const RuntimePreset r = RuntimePreset::fromJson(p.toJson());
    QCOMPARE(r.ctx, p.ctx);
    QCOMPARE(r.batch, p.batch);
    QCOMPARE(r.ubatch, p.ubatch);
    QCOMPARE(r.threads, p.threads);
    QCOMPARE(r.gpuLayers, p.gpuLayers);
    QCOMPARE(r.flashAttention, p.flashAttention);
    QCOMPARE(r.mmap, p.mmap);
    QCOMPARE(r.mlock, p.mlock);
    QCOMPARE(r.contBatching, p.contBatching);
    QCOMPARE(r.cacheType, p.cacheType);
    QCOMPARE(r.parallelSlots, p.parallelSlots);
}

void ProfilesTests::launchProfile_jsonRoundTrip()
{
    LaunchProfile l;
    l.id = "l1"; l.name = "prod"; l.alias = "P"; l.favorite = true;
    l.backendProfileId = "b1"; l.modelProfileId = "m1"; l.runtimePresetId = "r1";
    l.extraArgs = QStringList{"--verbose"};
    l.master.kind = "cli"; l.master.cliName = "claude";
    const LaunchProfile r = LaunchProfile::fromJson(l.toJson());
    QCOMPARE(r.name, l.name);
    QCOMPARE(r.alias, l.alias);
    QCOMPARE(r.favorite, l.favorite);
    QCOMPARE(r.backendProfileId, l.backendProfileId);
    QCOMPARE(r.modelProfileId, l.modelProfileId);
    QCOMPARE(r.extraArgs, l.extraArgs);
    QCOMPARE(r.master.kind, QStringLiteral("cli"));
    QCOMPARE(r.master.cliName, QStringLiteral("claude"));
}

void ProfilesTests::masterConfig_jsonRoundTrip()
{
    MasterConfig m;
    m.kind = "http"; m.httpUrl = "http://x/v1"; m.httpModel = "big"; m.httpKey = "k";
    m.escalation = "auto"; m.autoAfterFails = 5; m.applyEdits = false; m.timeoutSec = 600;
    QVERIFY(m.isConfigured());
    const MasterConfig r = MasterConfig::fromJson(m.toJson());
    QCOMPARE(r.kind, m.kind);
    QCOMPARE(r.httpUrl, m.httpUrl);
    QCOMPARE(r.httpModel, m.httpModel);
    QCOMPARE(r.escalation, m.escalation);
    QCOMPARE(r.autoAfterFails, m.autoAfterFails);
    QCOMPARE(r.applyEdits, m.applyEdits);
    QCOMPARE(r.timeoutSec, m.timeoutSec);
    QVERIFY(!MasterConfig{}.isConfigured());  // default kind=none
}

void ProfilesTests::manager_addGetRemoveBackend()
{
    ProfileManager pm;
    const QString id = pm.addBackend("be", "bin1", "127.0.0.1", 8080);
    QVERIFY(!id.isEmpty());
    const QVariantMap got = pm.getBackend(id);
    QCOMPARE(got.value("name").toString(), QStringLiteral("be"));
    QCOMPARE(got.value("port").toInt(), 8080);
    QVERIFY(pm.updateBackend(id, "be2", "bin1", "0.0.0.0", 9000, QStringList{"--x"}));
    QCOMPARE(pm.getBackend(id).value("name").toString(), QStringLiteral("be2"));
    QVERIFY(pm.removeBackend(id));
    QVERIFY(pm.getBackend(id).isEmpty());
    QVERIFY(!pm.removeBackend("nope"));
}

void ProfilesTests::manager_addModelProfile()
{
    ProfileManager pm;
    const QString id = pm.addModelProfile("qwen", "cat1", "", "");
    QVERIFY(!id.isEmpty());
    QCOMPARE(pm.getModelProfile(id).value("modelId").toString(), QStringLiteral("cat1"));
    QVERIFY(pm.removeModelProfile(id));
}

void ProfilesTests::manager_favoriteAndAlias()
{
    ProfileManager pm;
    const QString id = pm.addLaunchProfile("L", "b", "m", "r");
    QVERIFY(!id.isEmpty());
    pm.setLaunchFavorite(id, true);
    pm.setLaunchAlias(id, "Alias");
    const QVariantList menu = pm.launchProfilesForMenu();
    QVERIFY(!menu.isEmpty());
    const QVariantMap top = menu.first().toMap();
    QCOMPARE(top.value("alias").toString(), QStringLiteral("Alias"));
    QVERIFY(top.value("favorite").toBool());
    // displayName antepone la estrella a los favoritos; alias tiene prioridad.
    QCOMPARE(top.value("displayName").toString(), QStringLiteral("★ Alias"));
}

void ProfilesTests::manager_persistsAcrossInstances()
{
    QString id;
    {
        ProfileManager pm;
        id = pm.addBackend("persist", "bin1", "127.0.0.1", 7777);
        pm.saveProfiles();
    }
    {
        ProfileManager pm2;  // recarga de disco en el ctor
        QCOMPARE(pm2.getBackend(id).value("name").toString(), QStringLiteral("persist"));
    }
}

QTEST_MAIN(ProfilesTests)
#include "test_profiles.moc"
