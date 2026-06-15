#include <QtTest>
#include <QJsonObject>
#include <QJsonDocument>
#include "core/voice/VoiceTypes.h"
#include "core/voice/AudioCodec.h"
#include "core/voice/SttEngine.h"
#include "core/voice/TtsEngine.h"
#include "core/voice/VoiceController.h"

// Tests del modo Charla. Solo funciones puras (sin micrófono/red/playback):
// round-trip de config, codec WAV/RMS, builders de request STT/TTS, lógica VAD.
class TestVoice : public QObject
{
    Q_OBJECT
private slots:
    void configRoundTrip();
    void wavHeaderAndExtract();
    void rmsLevels();
    void sttMultipart();
    void sttParseTranscript();
    void ttsSpeechBody();
    void vadTurnEnded();
};

void TestVoice::configRoundTrip()
{
    VoiceConfig c;
    c.enabled = true;
    c.sttProvider = "cloud";
    c.sttBaseUrl = "https://api.openai.com";
    c.sttModel = "whisper-1";
    c.sttKeyRef = "voice/openai";
    c.sttLanguage = "es";
    c.ttsProvider = "local";
    c.ttsVoice = "nova";
    c.ttsFormat = "mp3";
    c.vadSilenceMs = 1200;
    c.bargeIn = false;

    const VoiceConfig r = VoiceConfig::fromJson(c.toJson());
    QCOMPARE(r.enabled, true);
    QCOMPARE(r.sttProvider, QString("cloud"));
    QVERIFY(r.sttIsCloud());
    QCOMPARE(r.sttKeyRef, QString("voice/openai"));
    QCOMPARE(r.sttLanguage, QString("es"));
    QCOMPARE(r.ttsVoice, QString("nova"));
    QCOMPARE(r.ttsFormat, QString("mp3"));
    QCOMPARE(r.vadSilenceMs, 1200);
    QCOMPARE(r.bargeIn, false);
    QVERIFY(!r.ttsIsCloud());
}

void TestVoice::wavHeaderAndExtract()
{
    QByteArray pcm;
    for (int i = 0; i < 100; ++i) {
        qint16 s = qint16(i * 100);
        pcm.append(char(s & 0xFF));
        pcm.append(char((s >> 8) & 0xFF));
    }
    const QByteArray wav = AudioCodec::pcm16ToWav(pcm, 16000);
    QVERIFY(wav.startsWith("RIFF"));
    QCOMPARE(wav.mid(8, 4), QByteArray("WAVE"));
    QCOMPARE(wav.size(), 44 + pcm.size());      // header de 44 bytes + datos
    // Round-trip: extraer el PCM debe devolver lo original.
    QCOMPARE(AudioCodec::wavExtractPcm(wav), pcm);
    // Entrada no-WAV: se devuelve tal cual.
    QCOMPARE(AudioCodec::wavExtractPcm(pcm), pcm);
}

void TestVoice::rmsLevels()
{
    QByteArray silence(800, '\0');
    QCOMPARE(AudioCodec::rmsBytes(silence), 0.0);

    QByteArray loud;
    for (int i = 0; i < 400; ++i) {
        qint16 s = (i % 2) ? 30000 : -30000;
        loud.append(char(s & 0xFF));
        loud.append(char((s >> 8) & 0xFF));
    }
    QVERIFY(AudioCodec::rmsBytes(loud) > 0.85);
}

void TestVoice::sttMultipart()
{
    const QByteArray wav = AudioCodec::pcm16ToWav(QByteArray(20, '\1'), 16000);
    QByteArray body = SttEngine::buildMultipart("BND", wav, "whisper-1", "es");
    QVERIFY(body.contains("name=\"file\""));
    QVERIFY(body.contains("name=\"model\""));
    QVERIFY(body.contains("whisper-1"));
    QVERIFY(body.contains("name=\"language\""));
    QVERIFY(body.contains("--BND--"));

    // language="auto" no debe emitir el campo language.
    QByteArray body2 = SttEngine::buildMultipart("BND", wav, "whisper-1", "auto");
    QVERIFY(!body2.contains("name=\"language\""));
}

void TestVoice::sttParseTranscript()
{
    QCOMPARE(SttEngine::parseTranscript("{\"text\":\"  hola mundo \"}"), QString("hola mundo"));
    QCOMPARE(SttEngine::parseTranscript("{\"error\":{\"message\":\"x\"}}"), QString());
    QCOMPARE(SttEngine::parseTranscript("garbage"), QString());
}

void TestVoice::ttsSpeechBody()
{
    const QByteArray body = TtsEngine::buildSpeechBody("tts-1", "alloy", "hola", "wav");
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    QCOMPARE(o.value("model").toString(), QString("tts-1"));
    QCOMPARE(o.value("voice").toString(), QString("alloy"));
    QCOMPARE(o.value("input").toString(), QString("hola"));
    QCOMPARE(o.value("response_format").toString(), QString("wav"));
}

void TestVoice::vadTurnEnded()
{
    // Sin voz (peak bajo): nunca termina aunque haya silencio.
    QVERIFY(!VoiceController::turnEnded(0.01, 0.03, 2000, 800));
    // Hubo voz pero el silencio aún no llega al umbral.
    QVERIFY(!VoiceController::turnEnded(0.10, 0.03, 500, 800));
    // Hubo voz y silencio suficiente → fin de turno.
    QVERIFY(VoiceController::turnEnded(0.10, 0.03, 900, 800));
}

QTEST_MAIN(TestVoice)
#include "test_voice.moc"
