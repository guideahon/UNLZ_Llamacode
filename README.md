# LlamaCode

LlamaCode es una app nativa (Qt/QML + C++) para orquestar múltiples backends `llama.cpp`, gestionar sesiones de chat, y ejecutar harnesses de agente IA (opencode, aider) sobre repos locales.

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
| Endpoint health check automático | ⏳ P1 |

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
    └── chat/{index.json, *.json} ← sesiones de chat persistidas
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

Pegar un comando de terminal (e.g. `llama-server --model ... --ctx-size 8192 --n-gpu-layers 99`) y LlamaCode extrae y configura automáticamente todos los parámetros reconocidos.

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

## Process Lifecycle

- **Windows Job Object**: todos los subprocesos (llama-server + harness) se asignan al Job Object del proceso principal. Al cerrar LlamaCode (normal o crash), los hijos mueren automáticamente.
- **Env vars de trazabilidad**: `LLAMACODE_MANAGED=1`, `LLAMACODE_ROLE=server|harness-*`, `LLAMACODE_APP_PID=<pid>` en todos los procesos spawneados.
- **PID state file** (`services.json`): al iniciar, detecta orphans de sesiones anteriores y los mata antes de levantar nuevos procesos.
- **Stop asíncrono**: `stopServer()` no bloquea la UI. Envía `terminate()`, expone `serverStopping` property, muestra "Deteniendo..." en botón y estado. Kill forzado tras 5s si el proceso no termina.

## Stack técnico

- **Qt 6.8.3** (`msvc2022_64`)
- **Qt modules**: Core, Quick, Sql, Concurrent, Network
- **Compilador**: MSVC 2022 (VS BuildTools)
- **CMake 3.21+**, generator: Visual Studio 17 2022
- **QML theme**: Catppuccin Mocha
- **Persistencia**: JSON (registries/profiles/chat) + SQLite (catalog) + QSettings

## Build

```bat
cd build
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\msvc2022_64"
cmake --build . --config Debug --parallel
```

## Fases

1. **P0** ✅ Launcher multi-binario/multi-modelo base + UI
2. **P1** ✅ (parcial) Validación, ejecución, logs en vivo
3. **P2** ✅ (parcial) UX de perfiles, importador CLI
4. **P3** ✅ Harness opencode via HTTP API + sesiones + proyectos
5. **P4** ✅ Chat integrado streaming + historial persistente + proyectos
6. **P5** ⏳ Built-in coding agent con aprobaciones

## Seguridad operativa

- Nada destructivo sin aprobación explícita.
- Escrituras fuera de workspace: bloqueadas por defecto.
- Comandos shell con allowlist/denylist por `WorkspaceProfile`.
- Subprocesos tagged con env vars para auditoría y control de ciclo de vida.
