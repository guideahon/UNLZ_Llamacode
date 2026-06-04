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

- [x] Separar entidades: `BackendProfile`, `ModelProfile`, `RuntimePreset`, `HarnessProfile`, `WorkspaceProfile`, `LaunchProfile`
- [x] Implementar `EffectiveProfileBuilder`
- [x] Resolver merge + overrides
- [x] Salida: `effectiveArgs`, `effectiveEnv`, `warnings`, `blockingErrors`
- [x] Command Preview exacto y copiable (`CommandPreview.qml`)

## P0 - UI base ✅

- [x] App Qt Quick arranca sin errores (`WIN32_EXECUTABLE`)
- [x] NavBar + páginas (Binaries, Model Roots, Profiles, Launch, Chat, Agente)
- [x] Tema Catppuccin Mocha
- [x] `LcButton`, `LcTextField`, `LcDialog`, `NavBar`, `PageHeader`, `CommandPreview`

## P1 - Validación y compatibilidad ✅

- [x] Matriz flag/capability por binario (`EffectiveProfileBuilder.addFlag`)
- [x] Reglas de degradación (warning) vs bloqueo (error)
- [x] Validar modelo/mmproj/draft antes de start
- [x] Validar colisión de puerto antes de start (server: `QTcpServer::listen` probe en `startServer`; opencode: pre-kill puerto 4096)
- [x] Diagnóstico temprano: verificar que el binario existe y es ejecutable (`QFileInfo` exists/isFile/isExecutable en `startServer`)

## P1 - Ejecución y observabilidad (parcial)

- [ ] `LlamaProcessManager` dedicado (extraer de `AppController`)
- [x] Logs en vivo stdout/stderr (en AppController + AgentPage Vista terminal)
- [x] Filtros de log por nivel (`serverLogByLevel(level)` invokable: all/error/warn/stderr/stdout/lifecycle/health/diag; falta combo en UI)
- [x] Detecciones automáticas por regex en log (`detectServerLogPatterns`: OOM, port busy, modelo cargado, arg inválido, load fail, context-shift → señal `serverDiagnostic(level,msg)`)
- [x] Botón copiar comando

## P1 - Process lifecycle ✅

- [x] Windows Job Object: hijos mueren al cerrar LlamaCode
- [x] Env vars de trazabilidad: `LLAMACODE_MANAGED`, `LLAMACODE_ROLE`, `LLAMACODE_APP_PID`
- [x] PID state file (`services.json`): detecta y mata orphans al iniciar
- [x] Pre-kill de puerto 4096 al levantar opencode
- [x] Stop asíncrono: `serverStopping` property + botón "Deteniendo..." + kill fallback 5s (UI no se congela)

## P1 - Endpoint health

- [x] `GET /health` polling post-start (`startHealthPolling`, intervalo 2s, set `serverReady` al 200)
- [ ] `POST /v1/chat/completions` test prompt mínimo (existe smoke-test en benchmark, no en arranque)
- [ ] Medir latencia first-token
- [x] UI de estado: iniciando / listo / error (`serverReady`/`serverStopping`/`serverError`)

## P2 - UX de perfiles (parcial)

- [x] Duplicar perfil
- [x] Renombrar perfil
- [x] Eliminar perfil
- [x] **Importar perfil desde argumentos CLI** (parsea --host, --port, --model, --ctx-size, --batch-size, --ubatch-size, --threads, --n-gpu-layers, --flash-attn, --no-mmap, --mlock, --parallel, --cache-type-k)
- [ ] Plantillas de perfil
- [ ] Etiquetas y búsqueda por perfil
- [ ] Favoritos y último usado
- [ ] Export/Import de perfiles completos (JSON)
- [ ] Historial de cambios por perfil

## P2 - Model Catalog avanzado

- [ ] Dedupe por hash SHA256 (diferido, opt-in)
- [ ] Filtros por familia/quant/tamaño/root (UI)
- [ ] Marcar compatibilidad vision/draft manualmente
- [ ] Asociación rápida modelo → perfil

## P3 - Harness opencode ✅

- [x] Integración HTTP API nativa (POST `/session`, `/session/{id}/prompt_async`, GET `/event` SSE)
- [x] Eliminado conflicto de DB SQLite (no más subproceso `opencode run`)
- [x] Vista Agente: chat bubbles con streaming en tiempo real (`message.part.delta` SSE)
- [x] Vista terminal: log raw para debug
- [x] Indicador "⏳ Procesando..." + cursor `▌` durante streaming
- [x] Panel lateral de sesiones con agrupación por proyecto (directorio)
- [x] Resume automático de última sesión (QSettings `opencode/lastSessionId`)
- [x] Creación de nueva sesión desde UI
- [x] Switch entre sesiones con carga de historial (`GET /session/{id}/message`)
- [x] Actualización de título de sesión en tiempo real vía SSE `session.updated`
- [x] Limpieza de sesión/SSE al detener agente
- [ ] `AiderCliAdapter`
- [ ] Templates args/env por harness
- [ ] Adjuntar archivos al mensaje (phase 2 de agente)

## P4 - Chat integrado ✅

- [x] Chat streaming directo a `llama-server` vía `/v1/chat/completions` SSE
- [x] Estado gestionado en `AppController` (no en QML): `chatMessages`, `chatGenerating`, `sendChatMessage`, `stopChatGeneration`
- [x] Sesiones persistidas en JSON (`AppLocalData/LlamaCode/chat/{id}.json`)
- [x] Índice de sesiones (`index.json`) con título, fecha, projectId
- [x] Auto-título desde primer mensaje del usuario
- [x] Panel lateral de chats agrupado por proyecto (launch profile)
- [x] Switch entre chats con carga de historial
- [x] Nueva sesión desde UI
- [x] Stop de generación con guardado
- [x] Indicador "⏳ Procesando..." + cursor `▌`
- [ ] Sampling configurable por sesión (temp, top-p, etc.) — requiere plumbing en `RawChatBackend` (persistir en session JSON + pasar al payload)
- [x] Export conversación (Markdown/JSON) (`exportChatSession(id,format)` + items en menú contextual de ChatPage)
- [x] Búsqueda en historial (`searchChatHistory(query)` invokable: matchea título + contenido, devuelve snippet; falta panel de búsqueda en UI)

## P5 - Built-in agent nativo ✅ (`LlamaAgentBackend`, loop ReAct)

- [x] `read_file` (offset/limit, dedup por huella)
- [x] `write_file` (aprobación + diff + snapshot/revert)
- [x] `edit_file` (reemplazo exacto, aprobación)
- [x] `list_dir` (recursivo opcional)
- [x] `grep` (regex) + `glob`
- [x] `run_shell` (async, output en vivo, timeout, cancelación)
- [x] `web_fetch`
- [x] `task` (subagents paralelos en git worktrees)
- [x] MCP stdio (tools `mcp__server__tool`)
- [x] Bloqueos de seguridad por workspace (confinamiento cwd, permisos por patrón, plan mode)
- [x] Checkpoint/rollback de conversación
- [x] Imágenes al agente (detección de visión por `--mmproj`)

## P6 - Benchmarking

- [ ] `BenchmarkRunner`: lanza perfiles en secuencia vía `AppController`, parámetros fijos (temp 0, seed fijo)
- [ ] `BenchmarkSession`: entidad con métricas por perfil (RAM, VRAM, t/s prompt eval, t/s gen, tiempo total, scores)
- [ ] Modo **Corta**: 5 prompts × 256 tokens, score 0–2, ~30 s
- [ ] Modo **Completa**: 15 prompts × 512 tokens, score 0–5, 1–5 min
- [ ] Editor de prompts por categoría (razonamiento, código, pericial, extracción, contexto largo)
- [ ] Scoring manual post-corrida desde UI (o automático vía juez LLM)
- [ ] Persistencia en JSON (`benchmarks/{timestamp}.json`)
- [ ] `BenchmarkPage.qml`: tabla comparativa con columnas ordenables y filtros
- [ ] Exportar resultados a CSV
- [ ] Selección multi-perfil para comparar en una misma corrida
- [ ] Calidad relativa normalizada contra perfil baseline

## Calidad

Target de tests: `cmake -B build_tests -DBUILD_TESTS=ON` → `LlamaCodeTests` (Qt Test). `tests/test_core.cpp`. 17/17 pasan.

- [ ] Tests `BinaryRegistry`
- [ ] Tests `ModelRootRegistry`
- [x] Tests `EffectiveProfileBuilder` (host/port, drop flag no soportado, modelo faltante = blocking)
- [x] Tests `GGUFScanner` (inferencia familia/quant/vision/draft)
- [ ] Tests `AppController` chat session CRUD

## Pendientes deferidos (jun-2026) — backend/infra listo, falta lo anotado

- [ ] **LlamaProcessManager dedicado** — extraer ciclo de vida de proceso de `AppController` a clase propia. Refactor arquitectónico grande, alto riesgo, bajo ROI ahora. No empezado.
- [ ] **Sampling por sesión (chat)** — temp/top-p/top-k por sesión. Plumbing: persistir en session JSON + pasar al payload de `RawChatBackend::runCompletion`. UI: panel en ChatPage.
- [ ] **Panel UI de búsqueda en historial** — invokable `searchChatHistory(query)` ya existe; falta campo de búsqueda + lista de resultados (snippet→switchChatSession) en ChatPage.
- [ ] **Combo UI de filtro de log por nivel** — invokable `serverLogByLevel(level)` ya existe; falta selector (all/error/warn/stderr/stdout/lifecycle/health/diag) en la vista de log del server.
- [ ] **Banner UI de `serverDiagnostic`** — señal emitida (OOM/port-busy/load-fail/…); falta mostrarla como aviso no-bloqueante en la UI.
- [ ] **Verificación GUI e2e de subagents con LLM vivo** — requiere server+modelo corriendo. Plumbing git/worktree/merge/abort ya validado; falta corrida real con el modelo manejando `task`.
- [ ] **Tests `BinaryRegistry`** — infra Qt Test ya montada (`BUILD_TESTS=ON`); agregar casos.
- [ ] **Tests `ModelRootRegistry`** — idem.
- [ ] **Tests `AppController` chat session CRUD** — idem (new/switch/delete/move/rename + persistencia index.json).

## Definition of Done MVP real

- [ ] 3+ binarios registrados y seleccionables
- [ ] 3+ roots GGUF escaneadas y navegables
- [ ] 10+ perfiles compuestos persistidos y recargables
- [x] Start/Stop con comando reproducible
- [x] Command Preview exacto y copiable
- [x] Chat streaming funcionando con historial
- [x] Harness opencode con sesiones y proyectos
- [ ] Test API exitoso desde perfil activo (health check automático)
- [x] Reapertura de app sin pérdida de estado (chat + sesiones opencode)
- [x] Subprocesos orphan limpiados al reiniciar
