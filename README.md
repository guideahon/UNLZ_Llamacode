# LlamaCode

LlamaCode es una app nativa (Qt/QML + C++) para orquestar múltiples backends `llama.cpp` y ejecutar harnesses de agente sobre repos locales.

Principio central:
- La GUI **no** embebe `llama.cpp`.
- La GUI **orquesta binarios externos** (`llama-server.exe`, forks MTP, builds CUDA/Vulkan/CPU).
- La GUI **compone perfiles** reutilizables sobre binarios, modelos y presets.

## Estado actual

**P0 completo y funcionando.** La app arranca, carga QML, navega entre páginas y persiste datos.

| Componente | Estado |
|---|---|
| `BinaryRegistry` + `CapabilityDetector` | ✅ |
| `ModelRootRegistry` + `GGUFScanner` | ✅ |
| `ModelCatalog` (SQLite) | ✅ |
| `ProfileManager` (6 entidades) | ✅ |
| `EffectiveProfileBuilder` | ✅ |
| UI: NavBar + 4 páginas | ✅ |
| Command Preview + Copy | ✅ |
| Start/Stop server (raw QProcess) | ✅ |
| `LlamaProcessManager` dedicado | ⏳ P1 |
| Endpoint health check | ⏳ P1 |
| Port collision detection | ⏳ P1 |

## Objetivo

Launcher serio para `llama-server`, evolucionado a centro de mando de agentes de código.

## Foco diferencial

- **Multi-llama.cpp**: convivir con varias builds/forks sin fricción.
- **Multi-GGUF roots**: indexar varias carpetas/discos de modelos.
- **Multi-perfiles compuestos**: mezclar `Backend + Model + Runtime + Harness + Workspace`.

## Arquitectura

```text
LlamaCode
├── UI Layer (Qt Quick / QML)
│   ├── Main.qml (ApplicationWindow + NavBar)
│   ├── pages/
│   │   ├── BinariesPage.qml
│   │   ├── ModelRootsPage.qml
│   │   ├── ProfilesPage.qml
│   │   └── LaunchPage.qml
│   └── components/
│       ├── LcButton, LcTextField, LcDialog
│       ├── NavBar, PageHeader
│       └── CommandPreview
├── AppController (singleton → QML "App")
├── Backend Manager
│   ├── BinaryRegistry          ← QAbstractListModel + JSON
│   ├── CapabilityDetector      ← llama-server --help parser
│   ├── ProfileManager          ← 6x ProfileListModel<T> + JSON
│   ├── EffectiveProfileBuilder ← merge + validate + commandLine
│   └── LlamaProcessManager     ← [P1] QProcess + live logs
├── Model Manager
│   ├── ModelRootRegistry       ← QAbstractListModel + JSON
│   ├── GGUFScanner             ← async QtConcurrent + inferencia
│   └── ModelCatalog            ← QAbstractListModel + SQLite
└── Storage (AppDataLocation)
    ├── binary_registry.json
    ├── model_roots.json
    ├── model_catalog.db
    └── profiles/{backends,models,runtimes,...}.json
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

Reglas:
- No hardcodear flags por build.
- Detectar capacidades por `--help`, cachear por hash del binario.
- Resolver incompatibilidades en `EffectiveProfileBuilder`, no en runtime.

### Capabilities Matrix

Cada binario mantiene:
- flags soportados (parseados de `--help`)
- aliases de flags (si un fork renombra)
- flags conflictivos
- defaults sugeridos

`EffectiveProfileBuilder.addFlag()` degrada con `warning` si el binario no soporta un flag, o emite `blockingError` si falta algo crítico.

## Diseño Multi-GGUF roots

### Model Root Registry

Entidad `ModelRoot`:
- `id`, `path`, `label`
- `scanMode`: `manual` | `startup` | `watch`
- `enabled`, `priority`, `tags`
- `isOnline` (disco montado)

### Catálogo de modelos (SQLite)

Entidad `CatalogModel`:
- `id`, `rootId`, `absolutePath`, `fileName`
- `sizeBytes`, `mtime`, `familyHint`, `quantHint`
- `isVisionCandidate`, `isDraftCandidate`
- `isAvailable` (false si el archivo desapareció)
- `sha256` (diferido, opt-in)

Reglas:
- No mover ni copiar modelos; solo indexar.
- Rutas caídas → `isAvailable = false`, metadata preservada.
- Deduplicación por SHA256 cuando esté disponible.

### GGUFScanner

- Escaneo async via `QtConcurrent::run`
- Infiere familia (deepseek, llama, mistral, phi, qwen, gemma, ...) por regex sobre nombre
- Infiere quant (`Q4_K_M`, `IQ3_XS`, `BF16`, ...) por regex
- `isDraftCandidate`: contiene "draft"/"small" OR tamaño < 2GB

## Diseño Multi-perfiles compuestos

### Entidades

| Entidad | Qué define |
|---|---|
| `BackendProfile` | host / port / binario / base args |
| `ModelProfile` | modelo principal + mmproj + draft |
| `RuntimePreset` | ctx / batch / threads / gpu-layers / flash-attn / cache |
| `HarnessProfile` | adapter / args / env de harness externo |
| `WorkspaceProfile` | cwd / políticas / permisos de shell |
| `LaunchProfile` | composición de los 5 anteriores + overrides |

### EffectiveProfileBuilder

Produce `EffectiveProfile`:
1. Merge de entidades base
2. Aplicación de `overrides`
3. Validación de compatibilidad binario–flag–modelo
4. Command line final + env final + warnings + blockingErrors

`isValid()` → `blockingErrors.isEmpty()`

## Flujo operativo

1. Registrar binarios `llama-server` con detección de capabilities
2. Agregar roots de modelos GGUF y escanear
3. Componer perfiles (Backend + Model + Runtime)
4. Seleccionar `LaunchProfile` → ver Command Preview
5. Start → logs en vivo → health check
6. Stop limpio

## Stack técnico

- **Qt 6.8.3** (`msvc2022_64`)
- **Qt modules**: Core, Quick, Sql, Concurrent
- **Compilador**: MSVC 2022 (VS BuildTools)
- **CMake 3.21+**, generator: Visual Studio 17 2022
- **QML theme**: Catppuccin Mocha
- **Persistencia**: JSON (registries/profiles) + SQLite (catalog)

## Build

```bat
cd build
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\msvc2022_64"
cmake --build . --config Debug --parallel
```

## Fases

1. **P0** ✅ Launcher multi-binario/multi-modelo base + UI
2. **P1** ⏳ Process manager real + logs + health check + port detection
3. **P2** Config avanzada, UX de perfiles, model catalog filtros
4. **P3** Harness externo (OpenCode, Aider, custom CLI)
5. **P4** Chat integrado streaming
6. **P5** Built-in coding agent con aprobaciones

## Seguridad operativa

- Nada destructivo sin aprobación explícita.
- Escrituras fuera de workspace: bloqueadas por defecto.
- Comandos shell con allowlist/denylist por `WorkspaceProfile`.
