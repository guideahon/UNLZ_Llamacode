# Agent harness TODO

Gaps de paridad vs opencode/pi/aider. Estado:

- [x] **Plan mode** — `m_approvalMode=="plan"`. buildToolSchemas filtra a read-only (read_file/list_dir/grep/glob/web_fetch); processPendingCalls bloquea writes; system prompt de plan; combo en AgentPage ("Plan (solo lectura)").
- [ ] **Subagents** — agentes paralelos aislados (worktree). Tool `task`. GRANDE — pendiente.
- [ ] **Imágenes al agente** — adjuntar imágenes al mensaje del agente (modelo es VL). Reusar `imageDataUri` del chat + botón de adjuntar en AgentPage + content multimodal en LlamaAgentBackend::sendMessage. Pendiente (necesita UI).
- [x] **Checkpoint/rollback de conversación** — checkpoint por turno (`pushCheckpoint` en sendMessage); `rollbackToMessage(msgIndex)` trunca msgs/ctx + revierte edits posteriores; botón "↩ Rebobinar" en burbujas de usuario.
- [x] **list_dir recursivo** — arg `recursive` (salta dirs ignorados, cap 1000).
- [ ] **Permisos por patrón** — allow/deny por glob de ruta. Pendiente (necesita storage + UI de config).
- [ ] **@-mentions** — autocompletar `@archivo` en el input. Pendiente (necesita popup de autocomplete).
- [x] **Web fetch** — tool `web_fetch(url)` → HTML limpiado a texto (timeout 20s, cap 48KB).

## Hechos en esta tanda
plan mode, checkpoint/rollback, list_dir recursivo, web_fetch. Build Debug OK.

## Pendientes (orden sugerido por esfuerzo)
1. Imágenes al agente (medio — reusa infra de chat).
2. Permisos por patrón (medio — config + chequeo en approval/worker).
3. @-mentions (medio-alto — UI autocomplete).
4. Subagents (alto — arquitectura nueva: pool de backends + worktrees + tool task).
