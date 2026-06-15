#include "VoiceController.h"
#include "AudioCodec.h"

#include <QAudioSource>
#include <QAudioFormat>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QIODevice>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QBuffer>
#include <QUrl>

VoiceController::VoiceController(QObject *parent) : QObject(parent)
{
    connect(&m_stt, &SttEngine::transcribed, this, &VoiceController::onSttDone);
    connect(&m_stt, &SttEngine::failed,      this, &VoiceController::onSttFailed);
    connect(&m_tts, &TtsEngine::audioReady,  this, &VoiceController::onTtsAudio);
    connect(&m_tts, &TtsEngine::failed,       this, &VoiceController::onTtsFailed);
}

VoiceController::~VoiceController()
{
    endCapture(false);
    teardownPlayback();
}

void VoiceController::setConfig(const VoiceConfig &cfg, const QString &sttKey, const QString &ttsKey)
{
    m_cfg = cfg;
    m_stt.setConfig(cfg, sttKey);
    m_tts.setConfig(cfg, ttsKey);
}

void VoiceController::setInputDevice(const QString &id)
{
    m_deviceId = id;
}

QVariantList VoiceController::inputDevices()
{
    QVariantList out;
    const QAudioDevice def = QMediaDevices::defaultAudioInput();
    const auto devs = QMediaDevices::audioInputs();
    for (const QAudioDevice &d : devs) {
        QVariantMap m;
        m["id"]   = QString::fromUtf8(d.id());
        m["name"] = d.description();
        m["isDefault"] = (d.id() == def.id());
        out.append(m);
    }
    return out;
}

QString VoiceController::stateStr() const
{
    switch (m_state) {
    case Idle:         return QStringLiteral("idle");
    case Listening:    return QStringLiteral("listening");
    case Transcribing: return QStringLiteral("transcribing");
    case Thinking:     return QStringLiteral("thinking");
    case Speaking:     return QStringLiteral("speaking");
    case Error:        return QStringLiteral("error");
    }
    return QStringLiteral("idle");
}

void VoiceController::setState(State s)
{
    if (m_state == s) return;
    m_state = s;
    emit stateChanged();
}

void VoiceController::fail(const QString &err)
{
    m_lastError = err;
    emit errorChanged();
    endCapture(false);
    teardownPlayback();
    setState(Error);
}

bool VoiceController::turnEnded(double peakLevel, double activationLevel,
                                int silenceAccumMs, int silenceMs)
{
    return peakLevel >= activationLevel && silenceAccumMs >= silenceMs;
}

// ── Ciclo de vida ──────────────────────────────────────────────────────────

void VoiceController::start()
{
    m_lastError.clear();
    startListening();
}

void VoiceController::stop()
{
    endCapture(false);
    teardownPlayback();
    m_stt.cancel();
    m_tts.cancel();
    m_testMode = false;
    setState(Idle);
}

void VoiceController::startListening()
{
    teardownPlayback();            // barge-in: cortar cualquier TTS sonando
    m_tts.cancel();
    m_testMode = false;
    beginCapture();
    if (m_state != Error) setState(Listening);
}

void VoiceController::micTest()
{
    m_lastError.clear();
    emit errorChanged();
    teardownPlayback();
    beginCapture();
    m_testMode = true;
    if (m_state != Error) setState(Listening);
}

void VoiceController::stopMicTest()
{
    if (m_testMode) stop();
}

// ── Captura de micrófono + VAD ───────────────────────────────────────────────

void VoiceController::beginCapture()
{
    endCapture(false);
    m_capture.clear();
    m_silenceMs = 0;
    m_peak = 0.0;
    m_monitorOnly = false;

    QAudioFormat fmt;
    fmt.setSampleRate(m_sampleRate);
    fmt.setChannelCount(1);
    fmt.setSampleFormat(QAudioFormat::Int16);

    QAudioDevice dev = QMediaDevices::defaultAudioInput();
    if (!m_deviceId.isEmpty()) {
        const auto devs = QMediaDevices::audioInputs();
        for (const QAudioDevice &d : devs)
            if (QString::fromUtf8(d.id()) == m_deviceId) { dev = d; break; }
    }
    if (dev.isNull()) { fail(QStringLiteral("no hay micrófono disponible")); return; }
    // Pedimos siempre 16k/mono/Int16 (lo que espera STT); el backend resamplea.
    m_source = new QAudioSource(dev, fmt, this);
    connect(m_source, &QAudioSource::stateChanged, this, [this](QAudio::State st) {
        if (st == QAudio::StoppedState && m_source && m_source->error() != QAudio::NoError)
            fail(QStringLiteral("micrófono: error de captura"));
    });
    m_input = m_source->start();
    if (!m_input) { fail(QStringLiteral("no se pudo abrir el micrófono")); return; }
    connect(m_input, &QIODevice::readyRead, this, &VoiceController::onAudioReady);
}

void VoiceController::endCapture(bool transcribe)
{
    if (m_input) { disconnect(m_input, nullptr, this, nullptr); m_input = nullptr; }
    if (m_source) {
        m_source->stop();
        m_source->deleteLater();
        m_source = nullptr;
    }
    m_level = 0.0;
    emit levelChanged();

    if (transcribe && !m_capture.isEmpty()) {
        setState(Transcribing);
        m_stt.transcribe(m_capture, m_sampleRate);
    }
    m_capture.clear();
}

void VoiceController::onAudioReady()
{
    if (!m_input) return;
    const QByteArray chunk = m_input->readAll();
    if (chunk.isEmpty()) return;

    const double lvl = AudioCodec::rmsBytes(chunk);
    m_level = lvl;
    emit levelChanged();

    const int chunkMs = int((chunk.size() / 2) * 1000.0 / m_sampleRate);

    // Modo prueba de micrófono: solo nivel, sin VAD ni STT.
    if (m_testMode) return;

    // Modo monitor (durante Speaking): solo detectar barge-in, no acumular.
    if (m_monitorOnly) {
        if (m_cfg.bargeIn && lvl >= m_cfg.vadActivationLevel * 1.6)
            startListening();
        return;
    }

    m_capture += chunk;
    if (lvl >= m_cfg.vadThreshold) {
        if (lvl > m_peak) m_peak = lvl;
        m_silenceMs = 0;
    } else if (m_peak >= m_cfg.vadActivationLevel) {
        m_silenceMs += chunkMs;
    }

    if (turnEnded(m_peak, m_cfg.vadActivationLevel, m_silenceMs, m_cfg.vadSilenceMs))
        endCapture(true);
}

// ── STT → texto ──────────────────────────────────────────────────────────────

void VoiceController::onSttDone(const QString &text)
{
    if (text.trimmed().isEmpty()) {     // nada inteligible → reintentar escucha
        if (m_cfg.autoListen) startListening();
        else setState(Idle);
        return;
    }
    setState(Thinking);
    emit transcriptReady(text);
}

void VoiceController::onSttFailed(const QString &err)
{
    fail(QStringLiteral("STT: ") + err);
}

void VoiceController::notifyThinking()
{
    if (m_state != Idle && m_state != Error)
        setState(Thinking);
}

// ── TTS → audio → playback ───────────────────────────────────────────────────

void VoiceController::speak(const QString &text)
{
    if (m_state == Idle || m_state == Error) return;  // charla no activa
    if (text.trimmed().isEmpty()) {
        if (m_cfg.autoListen) startListening();
        return;
    }
    setState(Speaking);
    m_tts.synthesize(text);
}

void VoiceController::onTtsAudio(const QByteArray &audio, const QString &format)
{
    playAudio(audio, format);
}

void VoiceController::onTtsFailed(const QString &err)
{
    fail(QStringLiteral("TTS: ") + err);
}

void VoiceController::playAudio(const QByteArray &audio, const QString &format)
{
    teardownPlayback();

    m_playBuf = new QBuffer(this);
    m_playBuf->setData(audio);
    m_playBuf->open(QIODevice::ReadOnly);

    m_audioOut = new QAudioOutput(this);
    m_player = new QMediaPlayer(this);
    m_player->setAudioOutput(m_audioOut);
    m_player->setSourceDevice(m_playBuf, QUrl(QStringLiteral("voice.") + format));

    connect(m_player, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus st) {
        if (st == QMediaPlayer::EndOfMedia) {
            teardownPlayback();
            if (m_state == Speaking) {
                if (m_cfg.autoListen) startListening();
                else setState(Idle);
            }
        }
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString &es) { fail(QStringLiteral("playback: ") + es); });

    // Barge-in: monitorear el micrófono mientras hablamos.
    if (m_cfg.bargeIn) {
        beginCapture();
        m_monitorOnly = true;
        if (m_state != Error) setState(Speaking);
    }
    m_player->play();
}

void VoiceController::teardownPlayback()
{
    if (m_player) { m_player->stop(); m_player->deleteLater(); m_player = nullptr; }
    if (m_audioOut) { m_audioOut->deleteLater(); m_audioOut = nullptr; }
    if (m_playBuf) { m_playBuf->close(); m_playBuf->deleteLater(); m_playBuf = nullptr; }
}
