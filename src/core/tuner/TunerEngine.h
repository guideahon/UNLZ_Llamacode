#pragma once
// TunerEngine: capa de integración entre el core AutoTuner (TPE-lite + gate de
// calidad) y el mundo real de LlamaCode: lanza llama-server por candidato, mide
// throughput (tok/s) vía /completion y califica la salida con criterios de
// aceptación (substrings, estilo EvalSuite) y, cuando está disponible,
// llama-perplexity contra un corpus baseline. El resultado alimenta al optimizador
// y la mejor config se exporta como RuntimePreset/extraArgs.
//
// Las primitivas de medición (composeArgs / parseThroughput / scoreQuality) son
// estáticas y puras para poder testearse sin modelo real. evaluateAgainstUrl()
// hace la medición HTTP contra un server ya levantado (testeable con mock).
// evaluate() añade el ciclo lanzar/esperar/medir/matar con QProcess.

#include "AutoTuner.h"

#include <QObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>

class QNetworkAccessManager;

// Mapea un ParamSpec del optimizador a un flag de llama-server.
struct TunableParam {
    tuner::ParamSpec spec;
    QString flag;        // p.ej. "-ngl", "-b", "--cache-type-k", "--flash-attn"
    bool isSwitch = false;  // flag booleano sin valor; categórico on/off
};

struct TunerJob {
    QString binaryPath;
    QMap<QString, QString> env;  // overrides de entorno (CUDA/PATH para DLLs, etc.)
    QStringList baseArgs;        // args fijos (modelo, ctx, etc.) sin host/port/tuned
    QString host = QStringLiteral("127.0.0.1");
    int port = 18080;            // puerto scratch, distinto del server principal
    QString evalPrompt;          // prompt de medición
    int nPredict = 256;          // tokens a generar por trial
    QStringList acceptance;      // substrings esperados -> calidad [0,1]
    QString perplexityBinaryPath; // llama-perplexity(.exe), opcional
    QString perplexityCorpusPath; // corpus para gate PPL, opcional
    double perplexityThresholdPct = 3.0; // tolerancia vs baseline
    bool usePerplexityGate = false;
    bool cpuOnly = false;        // fuerza -ngl 0 y espacio CPU-friendly
    int readyTimeoutMs = 120000; // espera de arranque (carga de modelo)
    int evalTimeoutMs = 120000;  // espera de la respuesta /completion
    tuner::TunerSettings settings;
    QVector<TunableParam> params;
};

class TunerEngine : public QObject {
    Q_OBJECT
public:
    explicit TunerEngine(QObject *parent = nullptr);

    // Corre la optimización completa (bloqueante; usa QEventLoop por trial).
    // Devuelve el mejor trial. Emite trialDone() en cada iteración.
    tuner::Trial run(const TunerJob &job);

    // Pide cortar tras el trial en curso (thread-safe). El server del trial
    // activo se mata igual al terminar la medición.
    void cancel() { m_cancel.store(true); }

    // Mide un candidato contra un server YA levantado en baseUrl (sin gestionar
    // proceso). Testeable con un mock HTTP.
    tuner::TrialResult evaluateAgainstUrl(const QString &baseUrl,
                                          const QString &prompt, int nPredict,
                                          const QStringList &acceptance,
                                          int timeoutMs);

    // ── Primitivas puras (testeables sin red ni proceso) ──────────────────────

    // Compone el argv final: baseArgs + flags de los params según config + host/port.
    static QStringList composeArgs(const QStringList &baseArgs,
                                   const QVector<TunableParam> &params,
                                   const tuner::Config &config,
                                   const QString &host, int port);

    // Extrae tokens/seg de una respuesta /completion de llama.cpp. Usa
    // timings.predicted_per_second; si falta, lo deriva de tokens_predicted y
    // timings.predicted_ms. Devuelve -1 si no se puede medir.
    static double parseThroughput(const QByteArray &json);

    // Fracción de substrings de aceptación presentes en la respuesta [0,1].
    // Sin criterios -> 1.0 (calidad no penaliza).
    static double scoreQuality(const QString &content, const QStringList &acceptance);

    // Extrae el texto generado de una respuesta /completion (campo "content").
    static QString extractContent(const QByteArray &json);

    // Extrae un valor de perplexity desde stdout/stderr de llama-perplexity.
    // Devuelve -1 si no encuentra un número reconocible.
    static double parsePerplexity(const QByteArray &text);

    // Solo los flags afinados (sin baseArgs ni host/port) para fusionar en
    // LaunchProfile.extraArgs y persistir la mejor config como perfil.
    static QStringList tunedArgs(const QVector<TunableParam> &params,
                                 const tuner::Config &config);

signals:
    void trialDone(int index, int total, double throughput, double quality,
                   const QString &summary);

private:
    // Espera /health 200. Aborta apenas el proceso muere (server crasheó).
    bool waitForReady(const QString &baseUrl, int timeoutMs, class QProcess *proc);
    double runPerplexity(const TunerJob &job, const tuner::Config *config = nullptr,
                         QString *diag = nullptr);
    QStringList perplexityArgs(const TunerJob &job, const tuner::Config *config) const;
    bool configChangesQualityRisk(const TunerJob &job, const tuner::Config &config) const;

    QNetworkAccessManager *m_nam = nullptr;
    std::atomic<bool> m_cancel{false};
};
