#pragma once
#include <QString>
#include <QJsonObject>

// Config del modo "Charla" (voz-a-voz). STT y TTS van SIEMPRE por endpoints
// OpenAI-compatibles (/v1/audio/transcriptions y /v1/audio/speech). La única
// diferencia entre 100% local y cloud es la baseUrl + si requiere API key:
//   - local: baseUrl http://127.0.0.1:<puerto> de un server local
//            (whisper.cpp server, openedai-speech, piper-http...). Sin key.
//   - cloud: baseUrl remota (OpenAI/Groq/...) + keyRef resuelto vía SecretStore.
// keyRef es una *referencia* al secreto (nunca la key en claro en el JSON).
struct VoiceConfig {
    bool enabled = false;

    // ── STT (speech-to-text) ──
    QString sttProvider = QStringLiteral("local");  // local | cloud
    QString sttBaseUrl  = QStringLiteral("http://127.0.0.1:8081");
    QString sttModel    = QStringLiteral("whisper-1");
    QString sttKeyRef;                               // "" salvo cloud
    QString sttLanguage = QStringLiteral("auto");    // "auto" = no enviar param

    // ── TTS (text-to-speech) ──
    QString ttsProvider = QStringLiteral("local");  // local | cloud
    QString ttsBaseUrl  = QStringLiteral("http://127.0.0.1:8082");
    QString ttsModel    = QStringLiteral("tts-1");
    QString ttsVoice    = QStringLiteral("alloy");
    QString ttsKeyRef;
    QString ttsFormat   = QStringLiteral("wav");     // wav | mp3 | pcm

    // ── Captura / VAD (detección de fin de habla) ──
    // Umbral de energía RMS [0..1] por debajo del cual el frame es "silencio".
    double  vadThreshold = 0.012;
    // Silencio continuo (ms) tras voz para dar el turno por terminado.
    int     vadSilenceMs = 800;
    // Energía mínima que tuvo que superarse para considerar que hubo voz.
    double  vadActivationLevel = 0.03;
    // Reanudar escucha automáticamente tras hablar la respuesta.
    bool    autoListen = true;
    // Cortar el TTS si el usuario empieza a hablar (barge-in).
    bool    bargeIn = true;

    bool sttIsCloud() const { return sttProvider == QLatin1String("cloud"); }
    bool ttsIsCloud() const { return ttsProvider == QLatin1String("cloud"); }

    QJsonObject toJson() const;
    static VoiceConfig fromJson(const QJsonObject &obj);
};
