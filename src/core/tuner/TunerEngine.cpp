#include "TunerEngine.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QTimer>
#include <QUrl>

namespace {

bool truthy(const std::string &v)
{
    return v == "on" || v == "1" || v == "true" || v == "yes";
}

}  // namespace

TunerEngine::TunerEngine(QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this))
{
}

QStringList TunerEngine::composeArgs(const QStringList &baseArgs,
                                     const QVector<TunableParam> &params,
                                     const tuner::Config &config,
                                     const QString &host, int port)
{
    QStringList args = baseArgs;
    args << tunedArgs(params, config);
    args << QStringLiteral("--host") << host
         << QStringLiteral("--port") << QString::number(port);
    return args;
}

QStringList TunerEngine::tunedArgs(const QVector<TunableParam> &params,
                                   const tuner::Config &config)
{
    QStringList args;
    for (const TunableParam &p : params) {
        const auto it = config.find(p.spec.name);
        if (it == config.end()) continue;
        const std::string val = p.spec.optionValue(it->second);
        if (val.empty()) continue;
        if (p.isSwitch) {
            if (truthy(val)) args << p.flag;
        } else {
            args << p.flag << QString::fromStdString(val);
        }
    }
    return args;
}

double TunerEngine::parseThroughput(const QByteArray &json)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return -1.0;
    const QJsonObject root = doc.object();
    const QJsonObject timings = root.value(QStringLiteral("timings")).toObject();
    if (timings.contains(QStringLiteral("predicted_per_second"))) {
        const double v = timings.value(QStringLiteral("predicted_per_second")).toDouble(-1.0);
        if (v > 0.0) return v;
    }
    // Fallback: tokens_predicted / (predicted_ms/1000).
    const double predMs = timings.value(QStringLiteral("predicted_ms")).toDouble(-1.0);
    const double predTok = timings.value(QStringLiteral("predicted_n"))
                               .toDouble(root.value(QStringLiteral("tokens_predicted")).toDouble(-1.0));
    if (predMs > 0.0 && predTok > 0.0) return predTok / (predMs / 1000.0);
    return -1.0;
}

QString TunerEngine::extractContent(const QByteArray &json)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return {};
    const QJsonObject root = doc.object();
    // /completion -> "content"; /v1/chat/completions -> choices[0].message.content
    if (root.contains(QStringLiteral("content")))
        return root.value(QStringLiteral("content")).toString();
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (!choices.isEmpty()) {
        const QJsonObject msg = choices.first().toObject()
                                    .value(QStringLiteral("message")).toObject();
        return msg.value(QStringLiteral("content")).toString();
    }
    return {};
}

double TunerEngine::scoreQuality(const QString &content, const QStringList &acceptance)
{
    if (acceptance.isEmpty()) return 1.0;
    int hits = 0;
    for (const QString &needle : acceptance)
        if (!needle.isEmpty() && content.contains(needle, Qt::CaseInsensitive)) ++hits;
    return static_cast<double>(hits) / acceptance.size();
}

bool TunerEngine::waitForReady(const QString &baseUrl, int timeoutMs)
{
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < timeoutMs) {
        QNetworkRequest req(QUrl(baseUrl + QStringLiteral("/health")));
        QNetworkReply *reply = m_nam->get(req);
        QEventLoop loop;
        QTimer poll;
        poll.setSingleShot(true);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&poll, &QTimer::timeout, &loop, &QEventLoop::quit);
        poll.start(2000);
        loop.exec();
        const bool ok = reply->isFinished() &&
                        reply->error() == QNetworkReply::NoError &&
                        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        reply->deleteLater();
        if (ok) return true;
        // backoff corto antes de reintentar
        QTimer pause;
        pause.setSingleShot(true);
        QEventLoop wloop;
        QObject::connect(&pause, &QTimer::timeout, &wloop, &QEventLoop::quit);
        pause.start(250);
        wloop.exec();
    }
    return false;
}

tuner::TrialResult TunerEngine::evaluateAgainstUrl(const QString &baseUrl,
                                                   const QString &prompt, int nPredict,
                                                   const QStringList &acceptance,
                                                   int timeoutMs)
{
    tuner::TrialResult r;

    QJsonObject payload;
    payload[QStringLiteral("prompt")] = prompt;
    payload[QStringLiteral("n_predict")] = nPredict;
    payload[QStringLiteral("stream")] = false;
    payload[QStringLiteral("cache_prompt")] = false;

    QNetworkRequest req(QUrl(baseUrl + QStringLiteral("/completion")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    QNetworkReply *reply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));

    QEventLoop loop;
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    guard.start(timeoutMs);
    loop.exec();

    if (!reply->isFinished() || reply->error() != QNetworkReply::NoError) {
        r.failed = true;
        reply->deleteLater();
        return r;
    }
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    r.throughput = parseThroughput(body);
    if (r.throughput < 0.0) { r.failed = true; return r; }
    r.quality = scoreQuality(extractContent(body), acceptance);
    return r;
}

tuner::Trial TunerEngine::run(const TunerJob &job)
{
    std::vector<tuner::ParamSpec> space;
    space.reserve(job.params.size());
    for (const TunableParam &p : job.params) space.push_back(p.spec);

    const QString baseUrl =
        QStringLiteral("http://%1:%2").arg(job.host).arg(job.port);

    tuner::AutoTuner tuner(space, job.settings);
    int index = 0;
    const int total = job.settings.maxTrials;

    auto eval = [&](const tuner::Config &cfg) -> tuner::TrialResult {
        ++index;
        const QStringList args =
            composeArgs(job.baseArgs, job.params, cfg, job.host, job.port);

        QProcess proc;
        proc.start(job.binaryPath, args);
        if (!proc.waitForStarted(15000)) {
            emit trialDone(index, total, 0, 0, QStringLiteral("server no arrancó"));
            return {0, 0, true};
        }

        tuner::TrialResult r;
        if (!waitForReady(baseUrl, job.readyTimeoutMs)) {
            r.failed = true;
        } else {
            r = evaluateAgainstUrl(baseUrl, job.evalPrompt, job.nPredict,
                                   job.acceptance, job.evalTimeoutMs);
        }

        proc.terminate();
        if (!proc.waitForFinished(8000)) {
            proc.kill();
            proc.waitForFinished(3000);
        }

        QString summary;
        for (const TunableParam &p : job.params) {
            const auto it = cfg.find(p.spec.name);
            if (it != cfg.end())
                summary += QStringLiteral("%1=%2 ")
                               .arg(QString::fromStdString(p.spec.name),
                                    QString::fromStdString(p.spec.optionValue(it->second)));
        }
        emit trialDone(index, total, r.throughput, r.quality, summary.trimmed());
        return r;
    };

    return tuner.run(eval);
}
