#pragma once
#include <QString>
#include <QStringList>

// Memoria PERSISTENTE por capas para el agente. Hechos atómicos con metadata,
// guardados como JSONL en <cwd>/.llamacode/memory.jsonl (un objeto por línea).
// Convive con el viejo memory.md (append-only) que sigue manejando la tool
// 'memory' para back-compat; este store es la capa estructurada (scope/type/
// confidence + recall por relevancia).
//
// Capas (scope):
//   - "session"  : contexto de la sesión actual (volátil semánticamente).
//   - "project"  : reglas, arquitectura, decisiones, paths, bugs del repo.
//   - "personal" : preferencias/estilo del usuario (transversal a proyectos).
// type: preference | decision | fact | bug | other.
namespace MemoryStore {

// Ruta del JSONL estructurado para un cwd dado.
QString jsonlPath(const QString &cwd);

// Guarda un hecho atómico. 'source' = PROVENANCE (de dónde salió el hecho:
// nombre de sesión/tarea, archivo, "user", etc.); opcional. Cada hecho recibe
// un 'id' corto estable. Devuelve un mensaje de estado para la tool.
QString save(const QString &cwd, const QString &content, const QString &scope,
             const QString &type, double confidence, const QString &source);

// Recupera hechos NO obsoletos. Si query != "", rankea por solapamiento de
// keywords + sesgo por confianza y recencia; si scope != "", filtra por capa.
// Devuelve top-k formateado (markdown) con id y provenance.
QString recall(const QString &cwd, const QString &query, const QString &scope, int k);

// OLVIDO: marca como obsoletos (o borra) los hechos que matchean 'query'
// (keywords sobre content) y/o 'scope'. mode='stale' (default, conserva
// historial) o 'delete' (reescribe el JSONL sin ellos). Devuelve estado.
QString forget(const QString &cwd, const QString &query, const QString &scope,
               const QString &mode);

// PODA anti-bloat (penaliza el peso, no sólo premia guardar). Inspirado en el
// gate MDL de "Self-Revising Discovery Systems": un hecho se conserva sólo si su
// VALOR (confianza · recencia · tipo) compensa su COSTO (largo en chars) y no es
// redundante con otro mejor. Marca stale (default) o borra (mode='delete') los
// hechos de menor valor por encima de 'maxKeep' y los casi-duplicados. Con
// dryRun=true sólo reporta sin tocar nada. scope opcional acota la capa.
QString prune(const QString &cwd, const QString &scope, int maxKeep,
              const QString &mode, bool dryRun);

}  // namespace MemoryStore
