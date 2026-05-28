# TODO - LlamaCode

## P0 - Núcleo multi-llama.cpp ✅

- [x] Crear `BinaryRegistry` (CRUD)
- [x] Entidad `LlamaBinary`
- [x] Registrar múltiples `llama-server.exe`
- [x] Detectar capacidades por `--help` (async, timeout 15s)
- [x] Persistir capabilities cacheadas por hash SHA256 del binario
- [x] UI para elegir binario activo por perfil (`BinariesPage.qml`)
- [x] Estado de binario inválido/no encontrado (`pathValid`)

## P0 - Núcleo multi-GGUF roots ✅

- [x] Crear `ModelRootRegistry` (CRUD)
- [x] Agregar múltiples carpetas de modelos
- [x] Prioridades y tags por root (`main`, `vision`, `draft`)
- [x] Escaneo manual + al inicio (`scanMode`: manual/startup/watch)
- [x] `GGUFScanner` async via `QtConcurrent` + inferencia familia/quant
- [x] Persistir catálogo en SQLite (`ModelCatalog`)
- [x] Estado root offline/unavailable sin perder historial (`isAvailable`)

## P0 - Perfiles compuestos ✅

- [x] Separar entidades:
  - [x] `BackendProfile`
  - [x] `ModelProfile`
  - [x] `RuntimePreset`
  - [x] `HarnessProfile`
  - [x] `WorkspaceProfile`
  - [x] `LaunchProfile`
- [x] Implementar `EffectiveProfileBuilder`
- [x] Resolver merge + overrides
- [x] Salida: `effectiveArgs`, `effectiveEnv`, `warnings`, `blockingErrors`
- [x] Command Preview exacto y copiable (`CommandPreview.qml`)

## P0 - UI base ✅

- [x] App Qt Quick arranca sin errores (`WIN32_EXECUTABLE`)
- [x] NavBar + 4 páginas (Binaries, Model Roots, Profiles, Launch)
- [x] Tema Catppuccin Mocha
- [x] `LcButton`, `LcTextField`, `LcDialog`, `PageHeader`, `CommandPreview`
- [x] `LaunchPage` con selector de perfil, preview de comando, log de servidor

## P1 - Validación y compatibilidad

- [x] Matriz flag/capability por binario (en `EffectiveProfileBuilder.addFlag`)
- [x] Reglas de degradación (warning) vs bloqueo (error)
- [x] Validar modelo/mmproj/draft antes de start
- [ ] Validar colisión de puerto antes de start
- [ ] Diagnóstico temprano: verificar que el binario existe y es ejecutable

## P1 - Ejecución y observabilidad

- [ ] `LlamaProcessManager` dedicado (extraer de `AppController`)
- [ ] Logs en vivo stdout/stderr con buffer circular (actualmente raw en AppController)
- [ ] Filtros de log por nivel (error/warn/info)
- [ ] Detecciones automáticas por regex en log:
  - [ ] modelo no encontrado
  - [ ] OOM / CUDA out of memory
  - [ ] fallback a RAM detectado
  - [ ] puerto ocupado (`Address already in use`)
  - [ ] modelo cargado con éxito (tiempo de carga)
- [x] Botón copiar comando (`CommandPreview.qml` → `App.copyToClipboard`)

## P1 - Endpoint health

- [ ] `GET /health` polling post-start
- [ ] `POST /v1/chat/completions` test prompt mínimo
- [ ] Medir latencia first-token
- [ ] UI de estado: iniciando / listo / error

## P2 - UX de perfiles masivos

- [ ] Clonar perfil
- [ ] Plantillas de perfil
- [ ] Etiquetas y búsqueda por perfil
- [ ] Favoritos y último usado
- [ ] Import/Export de perfiles (JSON)
- [ ] Historial de cambios por perfil

## P2 - Model Catalog avanzado

- [ ] Dedupe por hash SHA256 (diferido, opt-in)
- [ ] Filtros por familia/quant/tamaño/root (UI)
- [ ] Marcar compatibilidad vision/draft manualmente
- [ ] Asociación rápida modelo → perfil

## P3 - Harness externo

- [ ] Interfaz `IHarnessAdapter`
- [ ] `CustomCliAdapter`
- [ ] `OpenCodeCliAdapter`
- [ ] `AiderCliAdapter`
- [ ] Templates args/env por harness
- [ ] Vista de logs de harness separada

## P4 - Chat integrado

- [ ] Chat streaming local (SSE)
- [ ] Historial en SQLite
- [ ] Sampling configurable por sesión
- [ ] Export conversación (Markdown/JSON)

## P5 - Built-in agent mínimo

- [ ] `read_file`
- [ ] `write_file` (aprobación)
- [ ] `list_files`
- [ ] `search_text`
- [ ] `apply_patch` (aprobación)
- [ ] `run_command` (aprobación)
- [ ] Bloqueos de seguridad por workspace

## Calidad

- [ ] Tests `BinaryRegistry`
- [ ] Tests `ModelRootRegistry`
- [ ] Tests `EffectiveProfileBuilder`
- [ ] Tests `GGUFScanner` (inferencia familia/quant)
- [ ] Integración `LlamaProcessManager`

## Definition of Done MVP real

- [ ] 3+ binarios registrados y seleccionables
- [ ] 3+ roots GGUF escaneadas y navegables
- [ ] 10+ perfiles compuestos persistidos y recargables
- [ ] Start/Stop con comando reproducible
- [x] Command Preview exacto y copiable
- [ ] Test API exitoso desde perfil activo
- [ ] Reapertura de app sin pérdida de estado
