#pragma once
#include "VoiceTypes.h"
#include <QObject>
#include <QByteArray>
#include <QNetworkAccessManager>

class QNetworkReply;

// Cliente STT contra un endpoint OpenAI-compatible (/v1/audio/transcriptions).
// Sirve igual para un server local (whisper.cpp) o cloud (key vía Bearer).
class SttEngine : public QObject
{
    Q_OBJECT
public:
    explicit SttEngine(QObject *parent = nullptr);

    // resolvedKey: API key ya resuelta (vacía para local). cfg trae baseUrl/model/lang.
    void setConfig(const VoiceConfig &cfg, const QString &resolvedKey);

    // Transcribe PCM16 mono crudo. Lo envuelve en WAV y lo postea como multipart.
    void transcribe(const QByteArray &pcm16, int sampleRate);
    bool busy() const { return m_reply != nullptr; }
    void cancel();

    // ── Funciones puras (testeables sin red) ──
    // Cuerpo multipart/form-data con los campos file/model[/language].
    static QByteArray buildMultipart(const QByteArray &boundary, const QByteArray &wav,
                                     const QString &model, const QString &language);
    // Extrae el campo "text" de la respuesta JSON de transcripción.
    static QString parseTranscript(const QByteArray &json);

signals:
    void transcribed(const QString &text);
    void failed(const QString &error);

private:
    VoiceConfig m_cfg;
    QString m_key;
    QNetworkAccessManager m_nam;
    QNetworkReply *m_reply = nullptr;
};
