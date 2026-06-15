#pragma once
#include "VoiceTypes.h"
#include <QObject>
#include <QByteArray>
#include <QNetworkAccessManager>

class QNetworkReply;

// Cliente TTS contra un endpoint OpenAI-compatible (/v1/audio/speech).
// Local (openedai-speech / piper-http) o cloud (key vía Bearer). Devuelve el
// audio crudo (formato según cfg.ttsFormat) por audioReady().
class TtsEngine : public QObject
{
    Q_OBJECT
public:
    explicit TtsEngine(QObject *parent = nullptr);

    void setConfig(const VoiceConfig &cfg, const QString &resolvedKey);

    void synthesize(const QString &text);
    bool busy() const { return m_reply != nullptr; }
    void cancel();

    // ── Función pura (testeable sin red) ──
    // Body JSON del request /v1/audio/speech.
    static QByteArray buildSpeechBody(const QString &model, const QString &voice,
                                      const QString &input, const QString &format);

signals:
    // audio crudo + formato ("wav"|"mp3"|"pcm").
    void audioReady(const QByteArray &audio, const QString &format);
    void failed(const QString &error);

private:
    VoiceConfig m_cfg;
    QString m_key;
    QNetworkAccessManager m_nam;
    QNetworkReply *m_reply = nullptr;
};
