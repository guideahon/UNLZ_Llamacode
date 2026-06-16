<p align="center">
  <img src="https://github.com/JonatanBogadoUNLZ/PPS-Jonatan-Bogado/blob/9952aac097aca83a1aadfc26679fc7ec57369d82/LOGO%20AZUL%20HORIZONTAL%20-%20fondo%20transparente.png?raw=true" alt="Universidad Nacional de Lomas de Zamora — Facultad de Ingeniería" width="520">
</p>

<h1 align="center">UNLZ_Llamacode</h1>

<p align="center">
  <strong>Universidad Nacional de Lomas de Zamora — Facultad de Ingeniería</strong><br>
  Proyecto institucional · Práctica Profesional Supervisada / Investigación aplicada
</p>

<p align="center">
  🇦🇷 <strong>Español</strong> (este documento) ·
  🇬🇧 <a href="README.en.md">English</a>
</p>

---

> **Proyecto institucional de la Universidad Nacional de Lomas de Zamora (UNLZ), Facultad de Ingeniería.**
>
> UNLZ_Llamacode es una estación de trabajo de IA local: una app nativa de escritorio
> (Qt/QML + C++) que, en hardware propio y sin depender de la nube, abarca **chat
> integrado** con historial persistente, **harness de agente de código** sobre
> repositorios locales, **lanzamiento del servidor** de modelos `llama.cpp`
> (multi-binario / multi-GGUF roots / perfiles compuestos), **backends cloud con
> secretos cifrados**, **modo Charla** (voz-a-voz STT/TTS), **memoria/RAG y
> verificación de afirmaciones**, **maestro/supervisor (escalado)**, **cuentas de
> correo**, **automatización de browser (Playwright)**, **adjuntos/documentos +
> visión**, **Tasks** (macros semánticas + scheduler cron) y **watchdog + medidor
> de VRAM en vivo**.
>
> Se desarrolla como base de trabajo académico y de investigación, pensado para
> docencia, experimentación con LLMs locales y trabajo de becarios/tesistas de la
> Facultad.

## Índice

- [Instalación ultra-rápida](#instalación-ultra-rápida-banco-de-pruebas-aislado)
- [Qué es](#qué-es) · [Estado actual](#estado-actual) · [Objetivo](#objetivo) · [Foco diferencial](#foco-diferencial)
- [Arquitectura](#arquitectura)
- [Diseño Multi-llama.cpp](#diseño-multi-llamacpp) · [Multi-GGUF roots](#diseño-multi-gguf-roots) · [Multi-perfiles](#diseño-multi-perfiles-compuestos)
- [Cookbook de modelos (hardware-fit)](#cookbook-de-modelos-recomendaciones-hardware-fit)
- [Chat integrado](#chat-integrado) · [Harness de Agente](#harness-de-agente-opencode) · [Lanzamiento del servidor](#lanzamiento-del-servidor-launchpage)
- [Backends cloud + secretos](#backends-cloud--secretos-cifrados) · [Modo Charla (voz)](#modo-charla-voz-a-voz) · [Memoria/RAG](#memoria-rag-y-verificación) · [Maestro/supervisor](#maestro--supervisor-escalado)
- [Correo](#cuentas-de-correo) · [Browser (Playwright)](#automatización-de-browser-playwright) · [Adjuntos/visión](#adjuntos-documentos--visión) · [Watchdog + VRAM](#robustez-del-server-watchdog--vram) · [Otras capacidades](#otras-capacidades)
- [Process Lifecycle](#process-lifecycle) · [Stack técnico](#stack-técnico) · [Build](#build) · [Estructura del repo](#estructura-del-repo)
- [Fases](#fases) · [Tasks (macros + scheduler)](#tasks-macros-configurables--scheduler-cron) · [Benchmarking](#benchmarking) · [Auto-tuning](#auto-tuning-de-parámetros) · [Seguridad operativa](#seguridad-operativa)
- [Agradecimientos](#agradecimientos)

## Instalación ultra-rápida (banco de pruebas aislado)

Un solo comando: instala todas las dependencias, clona el repo en una carpeta
aislada, compila y arranca. No requiere clonar a mano ni preparar el entorno.

**Windows** (PowerShell):

```powershell
irm https://raw.githubusercontent.com/guideahon/UNLZ_Llamacode/main/scripts/bootstrap.ps1 | iex
```

**Linux** (bash):

```bash
curl -fsSL https://raw.githubusercontent.com/guideahon/UNLZ_Llamacode/main/scripts/bootstrap.sh | bash
```

Instala automáticamente:

- **git, CMake, Ninja, Python** y el toolchain C++ — MSVC v143 (Build Tools
  2022) en Windows / `g++` + `build-essential` en Linux.
- **Qt 6.8.3** vía `aqtinstall` en ambas plataformas (Windows `msvc2022_64`,
  Linux `gcc_64`). En Linux se usa aqt — **no** los paquetes Qt de la distro —
  porque el código requiere Qt ≥ 6.5 (`QQmlApplicationEngine::loadFromModule`) y
  varias LTS traen Qt viejo (Ubuntu 24.04 = 6.4.2). De la distro sólo salen el
  toolchain y las libs de sistema contra las que Qt enlaza (GL, xcb, glib,
  fontconfig…).

Clona en `%USERPROFILE%\LlamaCode` / `~/LlamaCode` y al terminar lanza la app
(salvo `LC_NORUN=1`).

Variables opcionales (setear antes de correr):

| Var | Default | Qué hace |
|---|---|---|
| `LC_DIR` | `~/LlamaCode` | carpeta de instalación aislada |
| `LC_BRANCH` | `main` | rama a clonar |
| `LC_CONFIG` | `Release` | `Release` o `Debug` |
| `LC_QTVER` | `6.8.3` | versión de Qt (sólo Linux) |
| `LC_QTROOT` | `~/Qt` | raíz de instalación de Qt (sólo Linux) |
| `LC_NORUN` | (vacío) | `1` = no lanzar al terminar |

Ejemplo con overrides (Linux):

```bash
LC_DIR=/opt/llamacode LC_CONFIG=Debug LC_NORUN=1 \
  bash -c "$(curl -fsSL https://raw.githubusercontent.com/guideahon/UNLZ_Llamacode/main/scripts/bootstrap.sh)"
```

Requisitos mínimos previos: **Windows** necesita `winget` (App Installer de la
Microsoft Store). **Linux** soporta apt / dnf / pacman / zypper y pide `sudo`
para los paquetes de sistema. Validado en contenedor Ubuntu 24.04 limpio
(toolchain + aqt Qt 6.8.3 + build).

---

## Qué es

UNLZ_Llamacode es una app nativa (Qt/QML + C++) para orquestar múltiples backends `llama.cpp`, gestionar sesiones de chat, y ejecutar harnesses de agente IA (opencode, aider) sobre repos locales.

Principio central:
- La GUI **no** embebe `llama.cpp`.
- La GUI **orquesta binarios externos** (`llama-server.exe`, forks MTP, builds CUDA/Vulkan/CPU).
- La GUI **compone perfiles** reutilizables sobre binarios, modelos y presets.
- La GUI **integra harnesses de agente** (opencode) vía HTTP API nativa.

## Estado actual

**P0–P4 completo y funcionando.**

| Componente | Estado |
|---|---|
| `BinaryRegistry` + `CapabilityDetector` | ✅ |
| `ModelRootRegistry` + `GGUFScanner` | ✅ |
| `ModelCatalog` (SQLite) | ✅ |
| `ProfileManager` (6 entidades) | ✅ |
| `EffectiveProfileBuilder` | ✅ |
| Importador de perfiles desde args CLI | ✅ |
| Start/Stop server (QProcess + Job Object, async stop) | ✅ |
| Chat streaming integrado (P4) | ✅ |
| Historial de chats con proyectos | ✅ |
| Harness opencode via HTTP API (P3) | ✅ |
| Vista Agente (chat bubbles) + Vista terminal | ✅ |
| Historial de sesiones opencode con proyectos | ✅ |
| Process lifecycle (Job Object + PID file) | ✅ |
| `LlamaProcessManager` dedicado | ⏳ P1 refactor |
| Endpoint health check automático | ✅ (polling /health post-start) |
| Pre-check colisión de puerto al iniciar server | ✅ |
| Agente nativo (LlamaAgentBackend, ReAct + tools + MCP) | ✅ P5 |

## Objetivo

Launcher serio para `llama-server`, evolucionado a centro de mando de agentes de código con chat integrado e historial persistente.

## Foco diferencial

- **Multi-llama.cpp**: convivir con varias builds/forks sin fricción.
- **Multi-GGUF roots**: indexar varias carpetas/discos de modelos.
- **Multi-perfiles compuestos**: mezclar `Backend + Model + Runtime + Harness + Workspace`.
- **Chat persistente**: historial de conversaciones agrupado por proyecto/perfil.
- **Agente integrado**: opencode via HTTP API sin subproceso por mensaje, con sesiones y proyectos.

## Arquitectura

```text
LlamaCode
├── UI Layer (Qt Quick / QML)
│   ├── Main.qml (ApplicationWindow + NavBar)
│   ├── pages/
│   │   ├── BinariesPage.qml
│   │   ├── ModelRootsPage.qml
│   │   ├── ProfilesPage.qml      ← import desde args CLI
│   │   ├── LaunchPage.qml
│   │   ├── ChatPage.qml          ← chat streaming + historial + proyectos
│   │   └── AgentPage.qml         ← Vista Agente + Vista terminal + sesiones
│   └── components/
│       ├── LcButton, LcTextField, LcDialog
│       ├── NavBar, PageHeader
│       └── CommandPreview
├── AppController (singleton → QML "App")
│   ├── Chat session management   ← JSON local, agrupado por launchProfile
│   ├── Agent session management  ← opencode HTTP API + SSE
│   └── Process lifecycle         ← Job Object + PID state file + orphan kill
├── Backend Manager
│   ├── BinaryRegistry
│   ├── CapabilityDetector
│   ├── ProfileManager            ← 6x ProfileListModel<T> + JSON
│   └── EffectiveProfileBuilder
├── Model Manager
│   ├── ModelRootRegistry
│   ├── GGUFScanner
│   └── ModelCatalog (SQLite)
└── Storage (AppLocalDataLocation)
    ├── binary_registry.json
    ├── model_roots.json
    ├── model_catalog.db
    ├── profiles/{backends,models,runtimes,...}.json
    ├── services.json             ← PID state para orphan detection
    ├── chat/{index.json, *.json} ← sesiones de chat persistidas
    ├── tasks/tasks.json          ← Tasks (macros) + su programación cron
    └── benchmarks/               ← caché del benchmark de calidad + resultados de corridas
```

## Diseño Multi-llama.cpp

### Binary Registry

Entidad `LlamaBinary`:
- `id`, `name`, `path`, `flavor` (`official`, `mtp-fork`, `custom`)
- `backend` (`cuda`, `vulkan`, `cpu`, `metal`)
- `versionHint` (texto libre)
- `supportedFlags`, `conflictingFlags`, `flagAliases`
- `envDefaults`, `workingDirectory`, `binaryHash` (SHA256 primer 1MB)
- `pathValid` (validado en runtime)

### Capabilities Matrix

Cada binario mantiene flags soportados, aliases y conflictos. `EffectiveProfileBuilder.addFlag()` degrada con `warning` o emite `blockingError` según criticidad.

## Diseño Multi-GGUF roots

### Model Root Registry

Entidad `ModelRoot`: `id`, `path`, `label`, `scanMode` (manual/startup/watch), `enabled`, `priority`, `tags`, `isOnline`.

### Catálogo de modelos (SQLite)

Entidad `CatalogModel`: `id`, `rootId`, `absolutePath`, `fileName`, `sizeBytes`, `mtime`, `familyHint`, `quantHint`, `isVisionCandidate`, `isDraftCandidate`, `isAvailable`, `sha256`.

### GGUFScanner

- Escaneo async via `QtConcurrent::run`
- Infiere familia (deepseek, llama, mistral, phi, qwen, gemma...) por regex sobre nombre
- Infiere quant (`Q4_K_M`, `IQ3_XS`, `BF16`...) por regex
- `isDraftCandidate`: contiene "draft"/"small" OR tamaño < 2GB

## Cookbook de modelos (recomendaciones hardware-fit)

`ModelRootsPage` recomienda qué modelos descargar según el hardware detectado (RAM / VRAM / GPU vía `nvidia-smi`), usando el catálogo `assets/hwfit/hf_models.json` (~900 modelos, basado en el cookbook de Odysseus).

### Scoring

Cada modelo recibe un score `0–100` que combina, ponderado al caso de uso *general* (calidad 0.45 / velocidad 0.30 / fit 0.15 / contexto 0.10):

- **Calidad** — preferentemente un **benchmark real** (Artificial Analysis *Intelligence Index*, remapeado a 0–100); si no hay match, heurística por params + familia + bonus de arquitectura (qwen3.6 +9, qwen3.5 +8, qwen3-next +6, …) con penalización por tier de quant. Modelos coder se penalizan en el scan general para no dominar.
- **Velocidad** — t/s estimados según ancho de banda de la GPU y params activos (MoE-aware). En `partial_offload` la velocidad es un blend armónico GPU/CPU según la fracción residente en VRAM.
- **Fit** — ratio memoria requerida vs. presupuesto.
- **Contexto** — target moderno: 32k=100, 16k=85, 8k=70, 4k=50 (no se premia el stub de 4k).

Desempate por versión (Qwen3.6 > Qwen3.5).

### Estimación de memoria (`estimateCatalogMemoryGb`)

Footprint = **pesos + KV cache + overhead**, dimensionado a un contexto realista, no a 4k:

- **Pesos** — params **totales** (MoE guarda todos los expertos en memoria, sólo rutea un subset por token) × bytes-por-param del quant.
- **KV cache** — escala con el modelo **completo** (todas las capas cachean K/V sin importar el ruteo MoE) y el **contexto real de sizing**, no un stub de 4k. Constante `1.5e-5 GB/token/B` ≈ KV fp16 de un modelo era-GQA (Llama3-8B @32k ≈ 4 GB).
- **Overhead** — compute graph de llama.cpp + buffers MTP/draft (`0.7 GB + 5%` de los pesos).
- **Contexto de sizing** (`sizingContext`) — target 32k, capeado por el ctx máx del modelo, piso 8k. Evita subestimar el costo de KV con defaults chicos.

### Modos de ejecución (run mode / fit)

Calculado contra VRAM (`nvidia-smi`) y RAM del sistema (90% utilizable como headroom):

| Modo | Condición | Notas |
|---|---|---|
| `gpu` | entra en VRAM | todo en GPU |
| `partial_offload` | no entra en VRAM, sí en VRAM+RAM | spill VRAM+RAM (llama.cpp `-ngl` parcial); `gpuFraction = vram/required` |
| `cpu_only` | sin GPU, entra en RAM | todo en RAM |
| `no_fit` | no entra en VRAM+RAM | — |

### Benchmark de calidad (Artificial Analysis)

- **Tabla bundled** `assets/benchmarks/aa_intelligence.json` — piso offline, sin dependencias de red.
- **Refresco semanal**: si la caché (`AppLocalData/LlamaCode/benchmarks/`) tiene >7 días, hace un fetch en background y la sobrescribe; ante cualquier fallo de red/JSON, queda la bundled.
- **Matching**: `benchmarkKey()` normaliza el nombre del catálogo (saca provider, quant/formato, GGUF, `-4bit`, `instruct`/`it`/`base`…) para mapear contra la tabla.

## Diseño Multi-perfiles compuestos

| Entidad | Qué define |
|---|---|
| `BackendProfile` | host / port / binario / base args |
| `ModelProfile` | modelo principal + mmproj + draft |
| `RuntimePreset` | ctx / batch / threads / gpu-layers / flash-attn / cache |
| `HarnessProfile` | adapter / args / env de harness externo |
| `WorkspaceProfile` | cwd / políticas / permisos de shell |
| `LaunchProfile` | composición de los 5 anteriores + overrides |

### Importador de perfiles desde CLI

Pegar un comando de terminal (e.g. `llama-server --model ... --ctx-size 8192 --n-gpu-layers 99`) y UNLZ_Llamacode extrae y configura automáticamente todos los parámetros reconocidos.

## Chat integrado

- **Chat streaming** directo al `llama-server` vía `/v1/chat/completions` SSE
- **Sesiones persistidas** en JSON local (`AppLocalData/LlamaCode/chat/`)
- **Agrupadas por proyecto** (launch profile activo al crear la sesión)
- **Indicador "⏳ Procesando..."** mientras espera, cursor `▌` durante generación
- **Stop de generación** con guardado de lo recibido

## Harness de Agente (opencode)

- **Integración HTTP nativa**: comunica con opencode server vía REST + SSE, sin subproceso `opencode run` (elimina conflicto de DB SQLite en Windows)
- **Vista Agente**: chat bubbles con streaming en tiempo real
- **Vista terminal**: log raw para debug
- **Sesiones opencode**: historial persistido en opencode DB, agrupado por directorio/proyecto
- **Resume automático**: retoma la última sesión al reiniciar el agente
- **Títulos auto-generados**: actualización en tiempo real vía `session.updated` SSE

## Backends cloud + secretos cifrados

Aunque el foco es 100% local, cada perfil puede apuntar a un **endpoint OpenAI-compat
externo** (OpenAI, OpenRouter, Groq, DeepSeek, etc.) en vez de a un `llama-server`
propio. `BackendProfile.kind = "cloud"` no lanza proceso ni binario: el chat/agente
pegan directo al `cloudBaseUrl` con el modelo configurado.

- **SecretStore**: las API keys **nunca** se serializan en los JSON del repo. El
  perfil guarda una **referencia** (`cloudKeyRef`) y el valor se resuelve en runtime
  vía variable de entorno o store cifrado en disco — **QtKeychain** (Secret Service /
  WinCred / macOS Keychain) y, si no está disponible, fallback **DPAPI** en Windows.
- Aplica igual a los maestros HTTP, cuentas de correo y proveedores de voz.

## Modo Charla (voz-a-voz)

Hablar con la IA y escuchar la respuesta, manos libres. Sección **🎙 Charla** en la
NavBar (reusa el backend de chat: sesiones e historial incluidos).

- **STT y TTS** van por endpoints **OpenAI-compat** (`/v1/audio/transcriptions`,
  `/v1/audio/speech`). Una sola ruta de código: **local** (whisper.cpp server,
  openedai-speech, piper-http en localhost, sin key) o **cloud** (URL remota +
  keyRef). Configurable por separado para STT y TTS.
- **Captura** PCM16 mono 16 kHz (`QAudioSource`) con **VAD por energía RMS** (fin de
  turno por silencio configurable), **selección de micrófono** y **medidor de nivel**
  en vivo. Botón *Probar micrófono* para validar entrada sin servidor.
- **Barge-in**: interrumpir el TTS al detectar voz nueva. Máquina de estados
  `escuchando → transcribiendo → pensando → hablando` con auto-escucha opcional.

## Memoria, RAG y verificación

El agente nativo no solo lee archivos: mantiene memoria y conocimiento estructurado.

- **MemoryStore por capas**: hechos durables extraídos de las conversaciones
  (consolidación en background al dejar una sesión) + memoria por proyecto en archivo.
- **GraphStore**: grafo de entidades/relaciones para conocimiento estructurado.
- **Tools**: `hybrid_search` (búsqueda híbrida léxica+semántica), `verify_claims`
  (chequeo de afirmaciones), memoria por capas. RAG sobre el material del proyecto.

## Maestro / supervisor (escalado)

Cuando el modelo local se traba, el agente puede **escalar** el sub-problema a un
modelo o CLI más capaz. Config por `LaunchProfile` (o fallback global).

- **Cadena de fallbacks** ordenada: tipo `profile` (otro perfil del propio
  LlamaCode), `http` (endpoint OpenAI-compat con keyRef) o `cli` (`claude-code` /
  `codex` detectados en el sistema).
- Escalado **manual** (botón), **auto** (tras N fallos de la misma firma de tool) o
  **ambos**, con anti-recursión por firma. Tool `ask_teacher` para el agente.

## Cuentas de correo

Cliente minimalista SMTP (enviar) + IMAP/POP3 (recibir) sobre sockets, con tools
`email_*` para el agente. Presets por proveedor (Gmail/Outlook/custom). El password
va a SecretStore (`mail/<name>`), nunca al JSON. `email_send` pide aprobación salvo
que se active *auto-send* (enviar correo es acción externa irreversible).

## Automatización de browser (Playwright)

Toggle global + override por perfil (`browserAutomation` inherit/on/off) que inyecta
el **MCP de Playwright** en el set de tools del agente. **Modo teach**: el usuario
graba acciones con Playwright codegen y se guardan como **skills reproducibles** que
las Tasks pueden reejecutar.

## Adjuntos (documentos + visión)

`DocumentExtractor` convierte adjuntos **pdf/office → markdown** vía sidecar
**markitdown** (con caché por md5), para inyectarlos al contexto del chat/agente. Con
un modelo de visión (server lanzado con `--mmproj`) también acepta **imágenes**.

## Robustez del server (watchdog + VRAM)

- **Watchdog**: auto-restart de `llama-server` ante crash (con tope de reintentos);
  `serverState` = `stopped|running|restarting|failed`.
- **Medidor de VRAM/stats en vivo**: poll async de `nvidia-smi` mientras el server
  corre (`serverStats`), para ver el consumo real.
- **Diagnóstico del log**: detecta por regex OOM, colisión de puerto, modelo cargado,
  etc., y los emite como eventos con nivel.
- **Colisión de puerto recuperable**: si el puerto del perfil está ocupado, la UI
  ofrece un puerto libre, actualiza el `BackendProfile` y relanza usando esa misma
  fuente de configuración.

## Otras capacidades

- **Router mode (hot-swap)**: un único `llama-server` con varios modelos cargados vía
  preset `.ini`; el chat/agente conmutan por el campo `model` del request.
- **GPU power limit**: fija el límite de potencia (W) por GPU vía `nvidia-smi`
  (en Windows se relanza elevado), global o por perfil.
- **Deep Research**: investigación multi-página con reportes persistidos.
- **Integrations**: registro unificado de **MCP Tool Servers** + **API services**
  (endpoint + key), con test de conexión.
- **ControlApi / headless**: toda feature es controlable por API local (target
  traversal), con variantes sin diálogo para automatización.
- **EvalSuite**: evaluación reproducible de modelos (importable como benchmark custom).
- **Mermaid**: render de diagramas en el chat (sidecar mermaid-cli).
- **Multi-idioma**: UI en español, inglés, chino, francés, italiano y alemán.
- **Export/Import/Wipe** de datos de usuario por categorías.

## Lanzamiento del servidor (`LaunchPage`)

- **Vista previa del comando** con botón *Copiar*.
- **Iniciar servidor + agente** — levanta `llama-server` y el harness de agente.
- **Iniciar solo servidor** — solo `llama-server`, sin agente.
- **Endpoint OpenAI** — con el server corriendo muestra `http://<host>:<port>/v1` (read-only, seleccionable) + botón *Copiar*, para apuntar agentes externos (opencode, aider, etc.) al backend local.

## Process Lifecycle

- **Windows Job Object**: todos los subprocesos (llama-server + harness) se asignan al Job Object del proceso principal. Al cerrar UNLZ_Llamacode (normal o crash), los hijos mueren automáticamente.
- **Env vars de trazabilidad**: `LLAMACODE_MANAGED=1`, `LLAMACODE_ROLE=server|harness-*`, `LLAMACODE_APP_PID=<pid>` en todos los procesos spawneados.
- **PID state file** (`services.json`): al iniciar, detecta orphans de sesiones anteriores y los mata antes de levantar nuevos procesos.
- **Stop asíncrono**: `stopServer()` no bloquea la UI. Envía `terminate()`, expone `serverStopping` property, muestra "Deteniendo..." en botón y estado. Kill forzado tras 5s si el proceso no termina.

## Stack técnico

- **Qt 6.8.3** (`msvc2022_64`)
- **Qt modules**: Core, Quick, Sql, Concurrent, Network, Widgets, Multimedia
- **Secretos**: QtKeychain (Secret Service / WinCred / Keychain) con fallback DPAPI
- **Compilador**: MSVC 2022 (VS BuildTools)
- **CMake 3.21+**, generator: Visual Studio 17 2022 (multi-config)
- **QML theme**: Catppuccin Mocha
- **Persistencia**: JSON (registries/profiles/chat) + SQLite (catalog) + QSettings

## Build

### Rápido (recomendado)

`build.bat` mata procesos colgados, configura, compila, despliega el runtime Qt (`windeployqt`) y regenera los accesos directos. Acepta config:

```bat
build.bat            REM Debug + Release (default)
build.bat Debug      REM solo Debug
build.bat Release    REM solo Release
```

Salidas:

| Config | Binario | Acceso directo | Icono |
|--------|---------|----------------|-------|
| Release | `build\Release\LlamaCode.exe` (optimizado, `NDEBUG`) | `LlamaCode.lnk` | `assets\app_icon.ico` (llama normal) |
| Debug | `build\Debug\LlamaCode.exe` (símbolos + asserts) | `LlamaCode-Debug.lnk` | `assets\debug_icon.ico` (llama **roja**) |

El icono rojo del Debug va embebido en el `.exe` (taskbar/explorer) vía `app_icon.rc` + `#ifdef LC_DEBUG_ICON` (CMake define `/dLC_DEBUG_ICON` solo en config Debug), y también en el `.lnk`.

> Tras tocar código siempre recompilar — el QML va embebido en el binario vía `qt_add_qml_module`.

### Manual

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\msvc2022_64"
cmake --build build --config Release --parallel
```

### Primera instalación

`install.bat` / `setup.bat` instalan Python deps + Qt 6.8.3 vía `aqtinstall` antes del primer build.

## Estructura del repo

```text
LlamaCode/
├── CMakeLists.txt          ← raíz CMake
├── app_icon.rc             ← recurso de icono (Debug/Release condicional)
├── build.bat / install.bat / setup.bat
├── update-shortcut.ps1     ← genera los .lnk (parametrizable por config/icono)
├── LlamaCode.lnk / LlamaCode-Debug.lnk
├── src/                    ← C++ (AppController, backends de agente, core)
├── qml/                    ← UI (Main.qml, pages/, components/)
├── assets/
│   ├── app_icon.ico / debug_icon.ico / app_icon.png
│   ├── hwfit/hf_models.json          ← catálogo de modelos (cookbook)
│   └── benchmarks/aa_intelligence.json ← scores de calidad (offline)
├── docs/                   ← documentación (agent.md, TODO.md, plan_harness.md, tuner.md, ...)
├── logs/                   ← logs de runtime/install (gitignored)
├── tests/ + build_tests/   ← suite Qt Test
└── build/                  ← artefactos (Debug/ + Release/, gitignored)
```

## Fases

1. **P0** ✅ Launcher multi-binario/multi-modelo base + UI
2. **P1** ✅ (parcial) Validación, ejecución, logs en vivo
3. **P2** ✅ (parcial) UX de perfiles, importador CLI
4. **P3** ✅ Harness opencode via HTTP API + sesiones + proyectos
5. **P4** ✅ Chat integrado streaming + historial persistente + proyectos
6. **P5** ✅ Built-in coding agent nativo (`LlamaAgentBackend`): loop ReAct contra `llama-server`, tools (read/write/edit/grep/glob/list_dir/run_shell/web_fetch/task), MCP stdio, aprobaciones, plan mode, checkpoint/rollback, subagents paralelos en git worktrees, permisos por patrón, @-mentions, imágenes (visión)
7. **P6** ✅ Tasks (macros semánticas configurables) + scheduler cron in-app, con auto ciclo de vida del agente
8. **P7** ✅ Backends cloud + secretos cifrados, modo Charla (voz-a-voz), correo, browser (Playwright/teach), memoria/RAG, maestro/supervisor, watchdog + VRAM, router hot-swap, headless ControlApi

## Tasks (macros configurables + scheduler cron)

Sección **Tasks** (en la NavBar, arriba de Benchmark): macros que el usuario
configura, guarda y ejecuta. **No son macros tontas** — no graban coordenadas
crudas estilo TinyTask, sino que delegan en el agente IA: cada Task guarda un
**objetivo en lenguaje natural** + **pasos de referencia**, y en la ejecución el
agente re-deriva las acciones con sus tools (browser MCP, shell, mail, etc.) y
**se adapta** si un botón, elemento o archivo cambió de lugar o de nombre.

### Modelo de datos (`TaskStore`)

- `id`, `name`, `description` (el objetivo), `profileId` (perfil de agente opcional).
- `steps[]`: cada paso `{kind, intent, ref}` con `kind` ∈
  `instruction|browser|shell|mail|desktop`. Los pasos `browser` graban un skill
  reproducible vía Playwright codegen (reusa el modo *teach* del browser).
- `scheduleEnabled` / `scheduleCron`, `lastRunAt` / `lastRunStatus`.
- Persistencia JSON en `AppLocalData/LlamaCode/tasks/tasks.json`.
- `composePrompt()` arma el prompt-objetivo con la consigna explícita de que los
  pasos son **guía, no guion literal** (replay adaptativo).

### Ejecución (manual o programada)

`runTask()` unifica el botón ▶ y el scheduler con auto ciclo de vida del agente:

- Si el **agente ya corre**, lo usa tal cual (no lo apaga).
- Si **no hay agente**, auto-inicia servidor + agente (perfil de la Task o el
  activo), ejecuta al quedar listo y **lo apaga** al terminar el turno.
- Sin perfil asignable → marca `lastRun = "error"`.

El cierre del ciclo se apoya en la señal `IAgentBackend::turnFinished` (emitida al
completar el turno), que marca `lastRun = "ok"` y apaga el agente auto-iniciado.

### Scheduler cron (`CronSchedule` + `TaskScheduler`)

- Parser cron puro de 5 campos `min hora díaMes mes díaSem`: `*`, listas `a,b`,
  rangos `a-b`, pasos `*/n` y `a-b/n`, día de semana `0`/`7` = domingo, semántica
  OR de díaMes/díaSem cuando ambos están restringidos.
- `TaskScheduler` evalúa por minuto (timer in-app, de-dup por minuto) y dispara
  `runTask` en cada Task vencida. Toggle global persistido; corre mientras la app
  esté abierta.
- Ejemplos: `0 9 * * *` (9:00 diario) · `*/15 9-17 * * 1-5` (cada 15 min, 9–17h,
  lun–vie) · `0 0 1 * *` (día 1 de cada mes).

## Benchmarking

Módulo para comparar quants y perfiles de forma sistemática: mide RAM, VRAM, velocidad y calidad relativa con resultados persistidos en tabla.

### Flujo de uso

1. Seleccionar uno o más `LaunchProfile` para comparar.
2. Elegir modo de prueba: **Corta** (~30 s) o **Completa** (1–5 min).
3. Ejecutar: UNLZ_Llamacode lanza cada perfil en secuencia, corre los prompts, registra métricas.
4. Ver resultados en tabla comparativa; exportar o guardar para comparaciones futuras.

### Modos de prueba

| Modo | Prompts | `n_predict` | Score | Tiempo estimado |
|------|---------|-------------|-------|-----------------|
| **Corta** | 5 fijos | 256 | 0–2 por prompt (máx 10) | ~30 s |
| **Completa** | 15 configurables | 512 | 0–5 por prompt (máx 75) | 1–5 min |

Parámetros fijos en toda corrida: `temp 0`, `top_p 1`, `top_k 0`, seed fijo, `ctx` según perfil.

### Categorías de prompts (modo Completo)

```text
3 × razonamiento lógico
3 × código / debug
3 × redacción técnica / pericial
3 × extracción de datos estructurada
3 × contexto largo (1 000–4 000 tokens de entrada)
```

Los prompts son editables y persistidos; el usuario puede reemplazarlos con casos reales (logs llama.cpp, pericias, SQL, Airflow, expedientes judiciales, etc.).

### Scoring

```text
Modo Corta:  0 = falla  /  1 = aceptable  /  2 = buena
Modo Completa:
  5 = igual o mejor que baseline
  4 = leve pérdida, usable
  3 = correcto pero menos preciso
  2 = error importante
  1 = falla grave
  0 = no siguió la consigna
```

Calidad relativa normalizada contra el perfil baseline (el de mayor score):

```
calidad_relativa = score_perfil / score_baseline × 100
```

### Métricas registradas por corrida

```text
perfil / modelo / quant
RAM usada (MB)
VRAM usada (MB)
tokens/s — prompt eval
tokens/s — generation
tiempo total (s)
score corto / score completo
errores graves (count)
```

### Persistencia y vista

- Resultados en JSON (`AppLocalData/LlamaCode/benchmarks/{timestamp}.json`).
- Vista tabla en `BenchmarkPage.qml`: columnas ordenables, filtro por perfil/quant/fecha.
- Exportar a CSV desde la UI.

### Tabla de ejemplo

| Quant | Score | Δ baseline | t/s gen | RAM | VRAM |
|-------|-------|------------|---------|-----|------|
| Q8_0 | 92/100 | base | 20 | 2 GB | 28 GB |
| Q6_K | 90/100 | −2.2% | 25 | 2 GB | 22 GB |
| Q5_K_M | 86/100 | −6.5% | 30 | 2 GB | 18 GB |
| Q4_K_M | 80/100 | −13.0% | 38 | 2 GB | 14 GB |
| IQ4_XS | 77/100 | −16.3% | 42 | 2 GB | 12 GB |
| Q3_K_M | 65/100 | −29.3% | 55 | 2 GB | 9 GB |

## Auto-tuning de parámetros

Búsqueda automática de los flags de `llama-server` (`ngl`, `batch`, `ubatch`, `flash-attn`, `cache-type-k/v`) que maximizan **tok/s** sin degradar la **calidad**. Optimizador TPE-lite (Parzen discreto) con **gate de calidad**: a diferencia de *llama-launcher v1.3*, tunear el quant de KV cache solo por velocidad no colapsa al quant más bajo, porque la pérdida penaliza fuerte caer bajo el umbral.

- Corre `N` trials en un puerto scratch (lanza/mide/mata el server por candidato, en un `QThread` aparte para no congelar la UI).
- Mide throughput de `timings.predicted_per_second` (`/completion`) y califica la salida con substrings estilo EvalSuite.
- Al terminar **clona** el perfil en uno nuevo `-tuned` con la mejor config en `extraArgs`; el original queda intacto.
- UI: `ProfilesPage` → **Auto-tune** / **Cancelar tune** + estado en vivo.

Detalle completo en [`docs/tuner.md`](docs/tuner.md).

## Seguridad operativa

- Nada destructivo sin aprobación explícita.
- Escrituras fuera de workspace: bloqueadas por defecto.
- Comandos shell con allowlist/denylist por `WorkspaceProfile`.
- Subprocesos tagged con env vars para auditoría y control de ciclo de vida.

## Agradecimientos

Código, datos y diseño tomados de otros proyectos:

| Proyecto | Uso en UNLZ_Llamacode | Repo / Fuente |
|---|---|---|
| **llama.cpp** | Binarios orquestados (`llama-server`), API OpenAI-compat, formato GGUF | https://github.com/ggml-org/llama.cpp |
| **opencode** | Harness de agente externo (HTTP API + SSE); formato de config MCP `mcp{}` | https://github.com/sst/opencode |
| **aider** | Harness de agente externo soportado | https://github.com/Aider-AI/aider |
| **markitdown** | Sidecar de extracción de documentos (pdf/office → markdown) en `DocumentExtractor` | https://github.com/microsoft/markitdown |
| **Odysseus cookbook** | Base del catálogo hardware-fit `assets/hwfit/hf_models.json` (~900 modelos) | https://github.com/TheBlokeAI/odysseus-cookbook |
| **Artificial Analysis** | Scores de calidad bundled `assets/benchmarks/aa_intelligence.json` (Intelligence Index) | https://artificialanalysis.ai |
| **Playwright (MCP)** | Automatización de browser + codegen (modo teach) | https://github.com/microsoft/playwright-mcp |
| **API de audio OpenAI** | Contrato `/v1/audio/transcriptions` y `/v1/audio/speech` del modo Charla (whisper.cpp, openedai-speech, piper) | https://platform.openai.com/docs/api-reference/audio |
| **QtKeychain** | Cifrado de secretos respaldado por el SO | https://github.com/frankosterfeld/qtkeychain |
| **Catppuccin (Mocha)** | Paleta del theme QML | https://github.com/catppuccin/catppuccin |
| **archex** | Ideas de pipeline de code-context en `hybrid_search`: empaquetado por presupuesto de tokens + expansión por dep-graph (vecinos vía imports/includes) | https://github.com/Mathews-Tom/archex |

> Al sumar código/datos de otro repo, agregar la fila correspondiente acá.
