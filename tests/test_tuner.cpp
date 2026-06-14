// Unit tests del AutoTuner (TPE-lite con gate de calidad). C++ puro: la
// evaluación de un candidato se inyecta como callback, sin servidor ni modelo.
// Foco: el gate de calidad evita el colapso al quant más bajo (bug clásico de
// tunear sólo por velocidad).

#include <QtTest>
#include "core/tuner/AutoTuner.h"

using namespace tuner;

class TunerTests : public QObject
{
    Q_OBJECT
private slots:
    void paramSpec_intRange();
    void paramSpec_categorical();
    void computeLoss_penalizesSubGate();
    void run_qualityGateAvoidsLowestQuant();
};

void TunerTests::paramSpec_intRange()
{
    const ParamSpec p = ParamSpec::intRange("ngl", 0, 100, 25);
    QCOMPARE(p.optionCount(), 5);            // 0,25,50,75,100
    QCOMPARE(p.optionValue(0), std::string("0"));
    QCOMPARE(p.optionValue(4), std::string("100"));
}

void TunerTests::paramSpec_categorical()
{
    const ParamSpec p = ParamSpec::categorical("cache", {"f16", "q8_0", "q4_0"}, true);
    QCOMPARE(p.optionCount(), 3);
    QCOMPARE(p.optionValue(2), std::string("q4_0"));
    QVERIFY(p.qualityRisk);
}

void TunerTests::computeLoss_penalizesSubGate()
{
    TunerSettings s; s.qualityGate = 0.7;
    AutoTuner t({ParamSpec::intRange("ngl", 0, 1, 1)}, s);

    TrialResult good; good.throughput = 100; good.quality = 0.8;
    TrialResult fast; fast.throughput = 1000; fast.quality = 0.4;  // viola gate
    // Pese a 10x throughput, romper el gate debe dar PEOR (mayor) loss.
    QVERIFY(t.computeLoss(fast) > t.computeLoss(good));
}

void TunerTests::run_qualityGateAvoidsLowestQuant()
{
    TunerSettings s;
    s.maxTrials = 40; s.startupTrials = 10; s.qualityGate = 0.6; s.seed = 1234;
    std::vector<ParamSpec> space{
        ParamSpec::categorical("cache", {"f16", "q8_0", "q4_0"}, true),
    };
    AutoTuner t(space, s);

    // Modelo sintético: el quant más bajo (índice 2 = q4_0) es el más rápido
    // pero su calidad cae por debajo del gate. El óptimo real es f16/q8_0.
    auto eval = [](const Config &c) {
        const int idx = c.at("cache");
        TrialResult r;
        r.throughput = 100.0 + idx * 100.0;          // q4_0 el más rápido
        r.quality = (idx == 2) ? 0.40 : 0.85;        // q4_0 rompe calidad
        return r;
    };

    const Trial best = t.run(eval);
    QVERIFY(best.config.at("cache") != 2);           // NO colapsó al peor quant
    QVERIFY(best.result.quality >= s.qualityGate);
}

QTEST_MAIN(TunerTests)
#include "test_tuner.moc"
