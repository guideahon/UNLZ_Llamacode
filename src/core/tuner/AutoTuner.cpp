#include "AutoTuner.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace tuner {

// ── ParamSpec ──────────────────────────────────────────────────────────────

ParamSpec ParamSpec::intRange(std::string n, long lo, long hi, long step, bool risk)
{
    ParamSpec p;
    p.name = std::move(n);
    p.kind = ParamKind::IntRange;
    p.lo = lo;
    p.hi = hi;
    p.step = step <= 0 ? 1 : step;
    p.qualityRisk = risk;
    return p;
}

ParamSpec ParamSpec::categorical(std::string n, std::vector<std::string> opts, bool risk)
{
    ParamSpec p;
    p.name = std::move(n);
    p.kind = ParamKind::Categorical;
    p.choices = std::move(opts);
    p.qualityRisk = risk;
    return p;
}

int ParamSpec::optionCount() const
{
    if (kind == ParamKind::Categorical)
        return static_cast<int>(choices.size());
    if (hi < lo) return 0;
    return static_cast<int>((hi - lo) / step) + 1;
}

std::string ParamSpec::optionValue(int i) const
{
    if (kind == ParamKind::Categorical) {
        if (i < 0 || i >= static_cast<int>(choices.size())) return {};
        return choices[i];
    }
    return std::to_string(lo + static_cast<long>(i) * step);
}

// ── AutoTuner ──────────────────────────────────────────────────────────────

AutoTuner::AutoTuner(std::vector<ParamSpec> space, TunerSettings settings)
    : m_space(std::move(space)), m_settings(settings)
{
    uint64_t seed = m_settings.seed;
    if (seed == 0) seed = std::random_device{}();
    m_rng.seed(seed);
}

const ParamSpec &AutoTuner::spec(const std::string &name) const
{
    for (const auto &s : m_space)
        if (s.name == name) return s;
    throw std::runtime_error("AutoTuner: parámetro desconocido: " + name);
}

double AutoTuner::computeLoss(const TrialResult &r) const
{
    if (r.failed)
        return 1e9;  // candidato inviable: peor que cualquier configuración real

    // Base: queremos MAXIMIZAR throughput -> minimizamos su negativo.
    double loss = -r.throughput;

    // Gate de calidad: si cae bajo el umbral, penalización grande proporcional
    // al déficit. Escala >> rango típico de throughput para que ninguna
    // ganancia de velocidad compense degradar el modelo (el fallo de
    // llama-launcher: colapsar al quant KV más bajo).
    if (r.quality < m_settings.qualityGate) {
        const double deficit = m_settings.qualityGate - r.quality;
        loss += 1e6 * deficit;
    }
    return loss;
}

Config AutoTuner::sampleRandom()
{
    Config c;
    for (const auto &s : m_space) {
        const int n = s.optionCount();
        if (n <= 0) { c[s.name] = 0; continue; }
        std::uniform_int_distribution<int> d(0, n - 1);
        c[s.name] = d(m_rng);
    }
    return c;
}

// TPE-lite: por cada parámetro, parte el historial en "bueno" (mejor fracción
// gamma por loss) y "malo". Modela l(x) y g(x) como distribuciones discretas
// (Parzen sobre opciones) con suavizado de Laplace. Muestrea candidatos de l(x)
// y elige el que maximiza l(x)/g(x) (Expected Improvement aproximado).
Config AutoTuner::sampleTPE()
{
    // Ordenar historial por loss ascendente.
    std::vector<const Trial *> sorted;
    sorted.reserve(m_history.size());
    for (const auto &t : m_history) sorted.push_back(&t);
    std::sort(sorted.begin(), sorted.end(),
              [](const Trial *a, const Trial *b) { return a->loss < b->loss; });

    const int total = static_cast<int>(sorted.size());
    int nGood = static_cast<int>(std::ceil(m_settings.gamma * total));
    nGood = std::max(1, std::min(nGood, total - 1));

    std::vector<const Trial *> good(sorted.begin(), sorted.begin() + nGood);
    std::vector<const Trial *> bad(sorted.begin() + nGood, sorted.end());

    const double prior = m_settings.prior;
    Config out;

    for (const auto &s : m_space) {
        const int n = s.optionCount();
        if (n <= 0) { out[s.name] = 0; continue; }

        // Distribuciones l (good) y g (bad) sobre las n opciones, suavizadas.
        std::vector<double> lw(n, prior), gw(n, prior);
        double lSum = prior * n, gSum = prior * n;
        for (const Trial *t : good) {
            auto it = t->config.find(s.name);
            if (it != t->config.end() && it->second >= 0 && it->second < n) {
                lw[it->second] += 1.0; lSum += 1.0;
            }
        }
        for (const Trial *t : bad) {
            auto it = t->config.find(s.name);
            if (it != t->config.end() && it->second >= 0 && it->second < n) {
                gw[it->second] += 1.0; gSum += 1.0;
            }
        }
        for (int i = 0; i < n; ++i) { lw[i] /= lSum; gw[i] /= gSum; }

        // Muestrear candidatos de l(x) y quedarse con el de mayor l/g.
        std::discrete_distribution<int> ld(lw.begin(), lw.end());
        int bestOpt = -1;
        double bestEI = -1.0;
        const int draws = std::max(1, m_settings.eiCandidates);
        for (int k = 0; k < draws; ++k) {
            const int cand = ld(m_rng);
            const double ei = lw[cand] / gw[cand];  // gw[cand] >= prior/gSum > 0
            if (ei > bestEI) { bestEI = ei; bestOpt = cand; }
        }
        out[s.name] = bestOpt < 0 ? 0 : bestOpt;
    }
    return out;
}

Trial AutoTuner::run(const EvalFn &eval)
{
    for (int i = 0; i < m_settings.maxTrials; ++i) {
        const bool startup =
            static_cast<int>(m_history.size()) < m_settings.startupTrials;
        Config cfg = startup ? sampleRandom() : sampleTPE();

        Trial t;
        t.config = cfg;
        t.result = eval(cfg);
        t.loss = computeLoss(t.result);
        m_history.push_back(t);

        if (!m_haveBest || t.loss < m_best.loss) {
            m_best = t;
            m_haveBest = true;
        }
    }
    return m_best;
}

}  // namespace tuner
