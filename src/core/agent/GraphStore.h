#pragma once
#include <QString>
#include <QVector>
#include <QPair>

// KNOWLEDGE GRAPH liviano para el agente. Entidades + relaciones tipadas,
// persistidas como JSONL en <cwd>/.llamacode/graph.jsonl (un objeto por línea).
// Complementa la memoria atómica de [[MemoryStore]]: ahí guardamos hechos sueltos,
// acá guardamos CÓMO se conectan (entidad -[predicado]-> entidad).
//
// Cada línea es uno de tres tipos:
//   {"kind":"entity","id":..,"name":..,"etype":..,"ts":..}
//   {"kind":"relation","id":..,"subj":<entId>,"pred":..,"obj":<entId>,"ts":..}
//   {"kind":"decision","id":..,"topic":..,"chosen":..,"reason":..,
//        "rejected":[{"alt":..,"reason":..}],"ts":..}
// Entidades se identifican por nombre NORMALIZADO (lowercase/trim) → id estable.
// etype: file|module|decision|bug|person|concept|other.
//
// El log de DECISIONES (decide/decisions) preserva las alternativas RECHAZADAS
// como artefactos de primera clase (con su motivo), no las descarta: deja un
// audit trail de qué se consideró y por qué se descartó. Idea de
// "Self-Revising Discovery Systems" (rejected alternatives como artefactos).
namespace GraphStore {

// Una alternativa rechazada: (texto de la alternativa, motivo del descarte).
using Rejected = QVector<QPair<QString, QString>>;

QString jsonlPath(const QString &cwd);

// Crea/asegura una entidad por nombre. Devuelve estado (incluye su id).
QString addEntity(const QString &cwd, const QString &name, const QString &etype);

// Crea una relación subj -[pred]-> obj (auto-crea las entidades por nombre).
QString link(const QString &cwd, const QString &subj, const QString &pred,
             const QString &obj);

// Consulta el vecindario de una entidad por nombre. depth=1 (default) o 2
// (graph expansion: incluye vecinos de vecinos). Devuelve markdown.
QString query(const QString &cwd, const QString &name, int depth);

// Registra una decisión: tema, opción elegida, motivo y las alternativas
// rechazadas (cada una con su propio motivo). Se conservan TODAS: el valor está
// en saber qué se descartó y por qué, para no re-evaluarlo más tarde.
QString decide(const QString &cwd, const QString &topic, const QString &chosen,
               const Rejected &rejected, const QString &reason);

// Devuelve el log de decisiones en markdown. topic vacío = todas; si no,
// filtra por substring del tema (case-insensitive). Incluye las rechazadas.
QString decisions(const QString &cwd, const QString &topic);

}  // namespace GraphStore
