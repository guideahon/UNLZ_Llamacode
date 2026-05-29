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
- [ ] Validar colisión de puerto antes de start (detectado y matado al levantar agente; falta en server)
- [ ] Diagnóstico temprano: verificar que el binario existe y es ejecutable

## P1 - Ejecución y observabilidad (parcial)

- [ ] `LlamaProcessManager` dedicado (extraer de `AppController`)
- [x] Logs en vivo stdout/stderr (en AppController + AgentPage Vista terminal)
- [ ] Filtros de log por nivel
- [ ] Detecciones automáticas por regex en log (OOM, port busy, modelo cargado, etc.)
- [x] Botón copiar comando

## P1 - Process lifecycle ✅

- [x] Windows Job Object: hijos mueren al cerrar LlamaCode
- [x] Env vars de trazabilidad: `LLAMACODE_MANAGED`, `LLAMACODE_ROLE`, `LLAMACODE_APP_PID`
- [x] PID state file (`services.json`): detecta y mata orphans al iniciar
- [x] Pre-kill de puerto 4096 al levantar opencode
- [x] Stop asíncrono: `serverStopping` property + botón "Deteniendo..." + kill fallback 5s (UI no se congela)

## P1 - Endpoint health

- [ ] `GET /health` polling post-start
- [ ] `POST /v1/chat/completions` test prompt mínimo
- [ ] Medir latencia first-token
- [ ] UI de estado: iniciando / listo / error

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
- [ ] Sampling configurable por sesión (temp, top-p, etc.)
- [ ] Export conversación (Markdown/JSON)
- [ ] Búsqueda en historial

## P5 - Built-in agent mínimo

- [ ] `read_file`
- [ ] `write_file` (aprobación)
- [ ] `list_files`
- [ ] `search_text`
- [ ] `apply_patch` (aprobación)
- [ ] `run_command` (aprobación)
- [ ] Bloqueos de seguridad por workspace

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

- [ ] Tests `BinaryRegistry`
- [ ] Tests `ModelRootRegistry`
- [ ] Tests `EffectiveProfileBuilder`
- [ ] Tests `GGUFScanner` (inferencia familia/quant)
- [ ] Tests `AppController` chat session CRUD

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
