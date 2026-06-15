#include "SttEngine.h"
#include "AudioCodec.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

SttEngine::SttEngine(QObject *parent) : QObject(parent) {}

void SttEngine::setConfig(const VoiceConfig &cfg, const QString &resolvedKey)
{
    m_cfg = cfg;
    m_key = resolvedKey;
}

QByteArray SttEngine::buildMultipart(const QByteArray &boundary, const QByteArray &wav,
                                     const QString &model, const QString &language)
{
    const QByteArray dash = "--" + boundary + "\r\n";
    QByteArray body;
    body += dash;
    body += "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n";
    body += "Content-Type: audio/wav\r\n\r\n";
    body += wav;
    body += "\r\n";
    body += dash;
    body += "Content-Disposition: form-data; name=\"model\"\r\n\r\n";
    body += model.toUtf8();
    body += "\r\n";
    if (!language.isEmpty() && language != QLatin1String("auto")) {
        body += dash;
        body += "Content-Disposition: form-data; name=\"language\"\r\n\r\n";
        body += language.toUtf8();
        body += "\r\n";
    }
    body += "--" + boundary + "--\r\n";
    return body;
}

QString SttEngine::parseTranscript(const QByteArray &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (doc.isObject()) {
        const QJsonObject o = doc.object();
        if (o.contains("text")) return o.value("text").toString().trimmed();
        // Algunos servers devuelven {error:{message}}.
        if (o.contains("error"))
            return QString();
    }
    return QString();
}

void SttEngine::transcribe(const QByteArray &pcm16, int sampleRate)
{
    if (m_reply) { emit failed(QStringLiteral("STT ocupado")); return; }
    const QByteArray wav = AudioCodec::pcm16ToWav(pcm16, sampleRate);
    const QByteArray boundary = "----LlamaCodeVoiceSTT";
    const QByteArray body = buildMultipart(boundary, wav, m_cfg.sttModel, m_cfg.sttLanguage);

    QString base = m_cfg.sttBaseUrl;
    while (base.endsWith('/')) base.chop(1);
    QNetworkRequest req(QUrl(base + QStringLiteral("/v1/audio/transcriptions")));
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  "multipart/form-data; boundary=" + boundary);
    if (m_cfg.sttIsCloud() && !m_key.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + m_key.toUtf8());

    m_reply = m_nam.post(req, body);
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *r = m_reply;
        m_reply = nullptr;
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            emit failed(r->errorString());
            return;
        }
        const QString text = parseTranscript(r->readAll());
        if (text.isEmpty()) emit failed(QStringLiteral("transcripción vacía"));
        else emit transcribed(text);
    });
}

void SttEngine::cancel()
{
    if (m_reply) { m_reply->abort(); }
}
