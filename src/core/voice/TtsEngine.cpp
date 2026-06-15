#include "TtsEngine.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

TtsEngine::TtsEngine(QObject *parent) : QObject(parent) {}

void TtsEngine::setConfig(const VoiceConfig &cfg, const QString &resolvedKey)
{
    m_cfg = cfg;
    m_key = resolvedKey;
}

QByteArray TtsEngine::buildSpeechBody(const QString &model, const QString &voice,
                                      const QString &input, const QString &format)
{
    QJsonObject o;
    o["model"] = model;
    o["voice"] = voice;
    o["input"] = input;
    o["response_format"] = format;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

void TtsEngine::synthesize(const QString &text)
{
    if (m_reply) { emit failed(QStringLiteral("TTS ocupado")); return; }
    if (text.trimmed().isEmpty()) { emit failed(QStringLiteral("texto vacío")); return; }

    QString base = m_cfg.ttsBaseUrl;
    while (base.endsWith('/')) base.chop(1);
    QNetworkRequest req(QUrl(base + QStringLiteral("/v1/audio/speech")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (m_cfg.ttsIsCloud() && !m_key.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + m_key.toUtf8());

    const QByteArray body = buildSpeechBody(m_cfg.ttsModel, m_cfg.ttsVoice, text, m_cfg.ttsFormat);
    const QString fmt = m_cfg.ttsFormat;
    m_reply = m_nam.post(req, body);
    connect(m_reply, &QNetworkReply::finished, this, [this, fmt]() {
        QNetworkReply *r = m_reply;
        m_reply = nullptr;
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            emit failed(r->errorString());
            return;
        }
        const QByteArray audio = r->readAll();
        // Algunos servers devuelven JSON de error con 200; detectar.
        if (audio.startsWith('{')) {
            const QJsonObject o = QJsonDocument::fromJson(audio).object();
            if (o.contains("error")) {
                emit failed(o.value("error").toObject().value("message").toString(
                                QStringLiteral("error TTS")));
                return;
            }
        }
        if (audio.isEmpty()) { emit failed(QStringLiteral("audio vacío")); return; }
        emit audioReady(audio, fmt);
    });
}

void TtsEngine::cancel()
{
    if (m_reply) m_reply->abort();
}
