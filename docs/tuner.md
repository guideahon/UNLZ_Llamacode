# Auto-tuning de parámetros de inferencia

Búsqueda automática de los flags de `llama-server` que maximizan **velocidad
(tok/s)** SIN degradar la **calidad** del modelo. Inspirado en *llama-launcher
v1.3* (Bayesian/TPE), pero corrige su fallo conocido: tunear el quant de KV
cache solo por velocidad colapsa siempre al quant más bajo y degrada el modelo.
Acá un **gate de calidad** lo impide.

```
LaunchProfile (origen)
   ↓ EffectiveProfile (binario + args + env)
AutoTuner (TPE-lite)  →  TunerEngine  →  llama-server (puerto scratch)
   ↑ loss = -tok/s + penalización si calidad < gate      ↓
   └──────────── TrialResult (tok/s, calidad) ───────────┘
   ↓ mejor config
Nuevo LaunchProfile "-tuned"  (el original queda intacto)
```

---

## Componentes

| Archivo | Rol |
|---------|-----|
| `src/core/tuner/AutoTuner.{h,cpp}` | Optimizador TPE-lite (C++ puro, sin Qt). Parzen discreto por parámetro + gate de calidad. Evaluación inyectada por callback. |
| `src/core/tuner/TunerEngine.{h,cpp}` | Integración real: compone argv, lanza `llama-server`, espera `/health`, mide tok/s vía `/completion`, califica con substrings y valida PPL con `llama-perplexity` si está disponible. |
| `src/core/tuner/TunerWorker.{h,cpp}` | Corre el tuning en un `QThread` (cada trial carga modelo → no congela la UI). Señales `trial`/`finished`, cancelación cooperativa. |
| `AppController` | `startAutoTune` / `cancelAutoTune` + props `autoTuneRunning/Progress/Status` + señales `autoTuneTrial/Finished`. |
| `qml/pages/ProfilesPage.qml` | Botones **Auto-tune** / **Cancelar tune** + línea de estado. |

---

## Optimizador (AutoTuner, TPE-lite)

TPE = Tree-structured Parzen Estimator. Versión discreta y compacta:

1. **Startup** (`startupTrials`): muestreo aleatorio para sembrar el historial.
2. Cada iteración posterior: parte el historial en **bueno** (mejor fracción
   `gamma` por loss) y **malo**. Por cada parámetro modela `l(x)` (de los buenos)
   y `g(x)` (de los malos) como distribuciones discretas con suavizado de Laplace.
3. Muestrea `eiCandidates` candidatos de `l(x)` y elige el de mayor `l(x)/g(x)`
   (Expected Improvement aproximado).
4. Corta al llegar a `maxTrials` o si `shouldStop()` (cancelación).

### Función de loss (el gate)

```
loss = -throughput
if quality < qualityGate:
    loss += 1e6 * (qualityGate - quality)   # penalización dominante
if trial.failed:  # server no arrancó / OOM / timeout
    loss = 1e9
```

La penalización (`1e6`) supera cualquier ganancia realista de tok/s → ninguna
velocidad compensa romper la calidad. Por eso el quant KV bajo no gana si el
modelo se degrada por debajo del umbral.

---

## Espacio de búsqueda (default)

Definido en `AppController::buildTuneParams()`:

| Parámetro | Flag | Opciones | Quality-risk |
|-----------|------|----------|:---:|
| ngl | `-ngl` | 0 / 20 / 40 / 99 | |
| batch | `-b` | 256 / 512 / 1024 / 2048 | |
| ubatch | `-ub` | 128 / 256 / 512 | |
| flash-attn | `--flash-attn` (switch) | off / on | |
| cache-type-k | `--cache-type-k` | f16 / q8_0 / q4_0 | ✓ |
| cache-type-v | `--cache-type-v` | f16 / q8_0 / q4_0 | ✓ |

Combos inválidos para el binario (p.ej. quant de V cache sin flash-attn) hacen
fallar ese trial → quedan penalizados automáticamente. El optimizador los evita.

### Modo CPU-only

El botón **Tune CPU** fuerza `-ngl 0` y usa un espacio más apropiado para equipos
sin GPU: `threads`, `batch`, `ubatch` y cache K/V. No explora flash-attn ni MTP,
porque en CPU el beneficio suele estar en prompt processing y balance de hilos.

### Gate PPL

Si junto a `llama-server` existe `llama-perplexity` y hay un corpus local
disponible, el tuner mide primero la PPL baseline y valida los trials que cambian
knobs de riesgo de calidad (`cache-type-k/v`). Un trial sólo conserva su calidad
completa si su PPL queda dentro del umbral configurado (default 3%). Si falla PPL,
el trial se marca inviable. Si no hay binario/corpus, el tuner cae al gate liviano
por substrings para no bloquear el flujo.

---

## Medición de un trial (TunerEngine)

1. **Compone argv**: `baseArgs` (del EffectiveProfile, sin host/port ni los
   flags afinados) + flags del candidato + `--host`/`--port`.
2. **Lanza** `llama-server` en un **puerto scratch** (`18099`), con el **entorno
   del perfil** (`effectiveEnv`: PATH/CUDA para cargar las DLLs del backend GPU)
   y working dir = carpeta del binario. *(Sin el env correcto el server no usa
   GPU o crashea — todos los trials darían 0 tok/s.)*
3. **Espera** `/health` 200 (hasta `readyTimeoutMs`); aborta apenas el proceso
   muere.
4. **Mide**: POST `/completion` (`stream:false`), throughput de
   `timings.predicted_per_second` (fallback `predicted_n / predicted_ms`).
5. **Califica**: fracción de substrings de aceptación presentes en `content`
   (estilo EvalSuite), en `[0,1]`.
6. **Mata** el server para liberar RAM/VRAM.
7. Si corresponde, corre `llama-perplexity` sobre el candidato y combina el score
   con el gate PPL.

---

## Resultado: perfil nuevo, no sobrescribe

Al terminar, `onAutoTuneFinished` **clona** el LaunchProfile origen
(backend/model/runtime/harness/workspace) en uno nuevo con sufijo `-tuned` y
alias `"Auto-tuned: <origen>"`, y aplica la mejor config en `extraArgs`
(reemplazando flags previos de los mismos parámetros). El perfil original queda
intacto.

---

## Uso (UI)

1. `ProfilesPage` → elegir LaunchProfile → **Auto-tune** o **Tune CPU** (el
   server principal debe estar detenido).
2. Corre `maxTrials` (default 24); la línea de estado muestra
   `Trial i/N — X tok/s, calidad Q [flags]`.
3. **Cancelar tune** corta tras el trial en curso.
4. Al terminar aparece el perfil `-tuned` en el dropdown.

Parámetros (`startAutoTune(launchProfileId, maxTrials, qualityGate, nPredict)`):
default `24, 0.6, 256`.

---

## Tests

Sin modelo real (CI-friendly):

- `tools/tuner_selftest.cpp` — core puro. Objetivo sintético donde el quant KV
  bajo es más rápido pero peor: verifica que **con gate** no colapsa a q4_0 y
  **sin gate** sí (contraste vs llama-launcher).
- `tools/tuner_engine_selftest.cpp` — primitivas (`composeArgs`/`parseThroughput`/
  `scoreQuality`/`tunedArgs`) + medición HTTP real contra un mock `QTcpServer`
  que imita `llama-server`.

Compilar y correr ambos:

```powershell
tools\build_tuner_tests.ps1   # MSVC 2022 + Qt 6.8.3; engine necesita /Zc:__cplusplus
```

---

## Pendiente

- Prueba end-to-end con modelo real (hasta ahora validado con mock HTTP).
- Afinar espacio de búsqueda / `nPredict` / prompt de medición según hardware.
- Exponer en UI la tabla de trials (hoy solo la última línea de estado).
- Elegir corpus PPL y umbral desde UI avanzada.
