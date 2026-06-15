#pragma once
#include <QByteArray>
#include <cstdint>

// Helpers de audio puros (sin Qt Multimedia ni red) → testeables sin hardware.
// Trabajamos con PCM 16-bit signed, mono, a una sample rate dada (16 kHz para STT).
namespace AudioCodec {

// Envuelve PCM16 mono crudo en un contenedor WAV (RIFF) con su header de 44 bytes.
QByteArray pcm16ToWav(const QByteArray &pcm, int sampleRate, int channels = 1);

// Extrae el PCM crudo de un WAV (salta el header RIFF buscando el chunk "data").
// Si no parece WAV, devuelve la entrada tal cual.
QByteArray wavExtractPcm(const QByteArray &wav);

// RMS normalizado [0..1] de un bloque PCM16. 0 si está vacío.
double rms(const int16_t *samples, int count);

// Conveniencia: RMS sobre un QByteArray interpretado como PCM16.
double rmsBytes(const QByteArray &pcm);

} // namespace AudioCodec
