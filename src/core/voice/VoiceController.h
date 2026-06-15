#pragma once
#include "VoiceTypes.h"
#include "SttEngine.h"
#include "TtsEngine.h"
#include <QObject>
#include <QByteArray>
#include <QPointer>

class QAudioSource;
class QIODevice;
class QMediaPlayer;
class QAudioOutput;
class QBuffer;

// Orquestador del modo "Charla" (voz-a-voz):
//   Listening → (VAD detecta fin de habla) → Transcribing (STT) →
//   emite transcriptReady(text) [AppController lo manda al backend de chat] →
//   Thinking (mientras el LLM genera) → speak(reply) → Speaking (TTS+playback) →
//   (autoListen) vuelve a Listening.
// Captura en PCM16 mono 16 kHz. VAD por energía RMS (sin libs externas).
class VoiceController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ stateStr NOTIFY stateChanged)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(double level READ level NOTIFY levelChanged)        // RMS live [0..1] para meter
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    enum State { Idle, Listening, Transcribing, Thinking, Speaking, Error };
    Q_ENUM(State)

    explicit VoiceController(QObject *parent = nullptr);
    ~VoiceController() override;

    void setConfig(const VoiceConfig &cfg, const QString &sttKey, const QString &ttsKey);

    QString stateStr() const;
    bool active() const { return m_state != Idle && m_state != Error; }
    double level() const { return m_level; }
    QString lastError() const { return m_lastError; }

    State state() const { return m_state; }

    // ── Función pura de VAD (testeable) ──
    // Decide si el turno terminó: hubo voz (peak>=activation) y luego silencio
    // continuo >= silenceMs. silenceAccumMs es el silencio acumulado hasta ahora.
    static bool turnEnded(double peakLevel, double activationLevel,
                          int silenceAccumMs, int silenceMs);

public slots:
    void start();          // arranca la sesión de charla (entra en Listening)
    void stop();           // corta todo y vuelve a Idle
    void startListening(); // fuerza escucha (corta TTS si suena)
    void speak(const QString &text);   // habla un texto (lo invoca AppController al cerrar turno)
    void notifyThinking();             // el LLM empezó a generar

signals:
    void stateChanged();
    void levelChanged();
    void errorChanged();
    // Texto reconocido del usuario; AppController lo envía al backend de chat.
    void transcriptReady(const QString &text);

private slots:
    void onAudioReady();
    void onSttDone(const QString &text);
    void onSttFailed(const QString &err);
    void onTtsAudio(const QByteArray &audio, const QString &format);
    void onTtsFailed(const QString &err);

private:
    void setState(State s);
    void fail(const QString &err);
    void beginCapture();
    void endCapture(bool transcribe);
    void playAudio(const QByteArray &audio, const QString &format);
    void teardownPlayback();

    VoiceConfig m_cfg;
    State m_state = Idle;
    double m_level = 0.0;
    QString m_lastError;

    SttEngine m_stt;
    TtsEngine m_tts;

    QAudioSource *m_source = nullptr;
    QPointer<QIODevice> m_input;     // device de QAudioSource (no es dueño)
    QByteArray m_capture;            // PCM16 acumulado del turno
    int   m_sampleRate = 16000;
    int   m_silenceMs = 0;           // silencio continuo acumulado (ms)
    double m_peak = 0.0;             // máximo RMS visto en el turno (¿hubo voz?)
    bool  m_monitorOnly = false;     // true durante Speaking (barge-in): no acumula

    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOut = nullptr;
    QBuffer *m_playBuf = nullptr;
};
