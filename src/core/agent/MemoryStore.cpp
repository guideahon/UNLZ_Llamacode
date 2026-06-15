#include "MemoryStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace {

QString normScope(const QString &s)
{
    const QString v = s.trimmed().toLower();
    if (v == QLatin1String("session") || v == QLatin1String("project")
        || v == QLatin1String("personal"))
        return v;
    return QStringLiteral("project");   // default: lo más útil para coding
}

QString normType(const QString &t)
{
    const QString v = t.trimmed().toLower();
    static const QSet<QString> ok{QStringLiteral("preference"), QStringLiteral("decision"),
                                  QStringLiteral("fact"), QStringLiteral("bug"),
                                  QStringLiteral("other")};
    return ok.contains(v) ? v : QStringLiteral("fact");
}

// Tokens útiles (lowercase, >=2 chars, únicos) para scoring de keywords.
QStringList terms(const QString &s)
{
    QStringList out;
    const auto parts = s.toLower().split(
        QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}_]+")), Qt::SkipEmptyParts);
    for (const QString &t : parts)
        if (t.size() >= 2 && !out.contains(t)) out << t;
    return out;
}

// id corto y estable a partir de content+ts (suficiente para 'forget'/dedupe).
QString makeId(const QString &content, const QString &ts)
{
    const QByteArray h = QCryptographicHash::hash(
        (content + QLatin1Char('|') + ts).toUtf8(), QCryptographicHash::Sha1);
    return QString::fromLatin1(h.toHex().left(8));
}

}  // namespace

namespace MemoryStore {

QString jsonlPath(const QString &cwd)
{
    return QDir::cleanPath(cwd + QStringLiteral("/.llamacode/memory.jsonl"));
}

QString save(const QString &cwd, const QString &content, const QString &scope,
             const QString &type, double confidence, const QString &source)
{
    const QString text = content.trimmed();
    if (text.isEmpty()) return QStringLiteral("[memory save: 'content' vacío]");

    const QString path = jsonlPath(cwd);
    QDir().mkpath(QFileInfo(path).absolutePath());

    const QString ts = QDateTime::currentDateTime().toString(Qt::ISODate);
    const QString id = makeId(text, ts);
    QJsonObject fact{
        {QStringLiteral("id"), id},
        {QStringLiteral("content"), text},
        {QStringLiteral("scope"), normScope(scope)},
        {QStringLiteral("type"), normType(type)},
        {QStringLiteral("confidence"), qBound(0.0, confidence <= 0.0 ? 0.8 : confidence, 1.0)},
        {QStringLiteral("source"), source.trimmed().isEmpty() ? QStringLiteral("agent")
                                                              : source.trimmed()},
        {QStringLiteral("ts"), ts}};

    QFile f(path);
    if (!f.open(QIODevice::Append | QIODevice::Text))
        return QStringLiteral("[no se pudo escribir la memoria: %1]").arg(path);
    f.write(QJsonDocument(fact).toJson(QJsonDocument::Compact));
    f.write("\n");
    f.close();
    return QStringLiteral("[memoria guardada · id=%1 scope=%2 type=%3]")
        .arg(id, fact.value(QStringLiteral("scope")).toString(),
             fact.value(QStringLiteral("type")).toString());
}

QString recall(const QString &cwd, const QString &query, const QString &scope, int k)
{
    if (k <= 0) k = 8;
    k = qBound(1, k, 30);
    const QString path = jsonlPath(cwd);

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("[memoria estructurada vacía]");

    const QString scopeFilter = scope.trimmed().toLower();
    const QStringList qterms = terms(query);

    struct Row { QJsonObject obj; double score; };
    QVector<Row> rows;
    while (!f.atEnd()) {
        const QByteArray line = f.readLine().trimmed();
        if (line.isEmpty()) continue;
        const QJsonObject o = QJsonDocument::fromJson(line).object();
        if (o.isEmpty()) continue;
        if (o.value(QStringLiteral("stale")).toBool(false)) continue;   // olvidado
        if (!scopeFilter.isEmpty()
            && o.value(QStringLiteral("scope")).toString() != scopeFilter)
            continue;

        // Score: solapamiento de keywords + sesgo por confianza y recencia.
        double score = 0.0;
        if (!qterms.isEmpty()) {
            const QString hay = o.value(QStringLiteral("content")).toString().toLower();
            int hits = 0;
            for (const QString &t : qterms)
                if (hay.contains(t)) ++hits;
            if (hits == 0) continue;                    // sin query-match → descartar
            score = double(hits) / qterms.size();
        }
        score += 0.05 * o.value(QStringLiteral("confidence")).toDouble(0.8);
        // Recencia: decae con la antigüedad (media vida ~30 días).
        const QDateTime ts = QDateTime::fromString(
            o.value(QStringLiteral("ts")).toString(), Qt::ISODate);
        if (ts.isValid()) {
            const double days = ts.daysTo(QDateTime::currentDateTime());
            score += 0.10 * std::pow(0.5, qMax(0.0, days) / 30.0);
        }
        rows.append({o, score});
    }
    f.close();

    if (rows.isEmpty())
        return query.isEmpty() ? QStringLiteral("[memoria estructurada vacía]")
                               : QStringLiteral("[sin hechos para: %1]").arg(query);

    std::stable_sort(rows.begin(), rows.end(),
                     [](const Row &a, const Row &b) { return a.score > b.score; });

    QStringList out;
    for (int i = 0; i < rows.size() && i < k; ++i) {
        const QJsonObject &o = rows[i].obj;
        const QString src = o.value(QStringLiteral("source")).toString();
        const QString id = o.value(QStringLiteral("id")).toString();
        QString line = QStringLiteral("- [%1/%2] %3")
                   .arg(o.value(QStringLiteral("scope")).toString(),
                        o.value(QStringLiteral("type")).toString(),
                        o.value(QStringLiteral("content")).toString());
        // Provenance compacta para trazabilidad (id + fuente).
        QStringList meta;
        if (!id.isEmpty()) meta << QStringLiteral("id=%1").arg(id);
        if (!src.isEmpty()) meta << QStringLiteral("src=%1").arg(src);
        if (!meta.isEmpty()) line += QStringLiteral("  (%1)").arg(meta.join(QLatin1String(", ")));
        out << line;
    }
    return out.join(QLatin1Char('\n'));
}

QString forget(const QString &cwd, const QString &query, const QString &scope,
               const QString &mode)
{
    const QString path = jsonlPath(cwd);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("[memoria estructurada vacía]");

    const QString scopeFilter = scope.trimmed().toLower();
    const QStringList qterms = terms(query);
    if (qterms.isEmpty() && scopeFilter.isEmpty())
        return QStringLiteral("[forget: pasá 'query' y/o 'scope' para no borrar todo]");

    const bool del = mode.trimmed().toLower() == QLatin1String("delete");

    QVector<QJsonObject> kept;     // lo que sobrevive (para mode=delete)
    QVector<QJsonObject> all;      // todo (para mode=stale, reescribe con flag)
    int matched = 0;
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        if (l.isEmpty()) continue;
        QJsonObject o = QJsonDocument::fromJson(l).object();
        if (o.isEmpty()) continue;

        bool hit = true;
        if (!scopeFilter.isEmpty()
            && o.value(QStringLiteral("scope")).toString() != scopeFilter)
            hit = false;
        if (hit && !qterms.isEmpty()) {
            const QString hay = o.value(QStringLiteral("content")).toString().toLower();
            hit = std::any_of(qterms.cbegin(), qterms.cend(),
                              [&](const QString &t) { return hay.contains(t); });
        }
        if (hit && !o.value(QStringLiteral("stale")).toBool(false)) ++matched;
        if (hit) {
            if (del) continue;                              // delete: lo dropeamos
            o.insert(QStringLiteral("stale"), true);        // stale: lo marcamos
        }
        kept.append(o);
        all.append(o);
    }
    f.close();

    if (matched == 0) return QStringLiteral("[forget: nada coincide]");

    const QVector<QJsonObject> &outRows = del ? kept : all;
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return QStringLiteral("[forget: no se pudo reescribir %1]").arg(path);
    for (const QJsonObject &o : outRows) {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        f.write("\n");
    }
    f.close();
    return QStringLiteral("[olvidados %1 hecho(s) · modo=%2]")
        .arg(matched).arg(del ? QStringLiteral("delete") : QStringLiteral("stale"));
}

namespace {

// Peso por tipo: las decisiones/preferencias valen más que un 'fact' suelto.
double typeWeight(const QString &t)
{
    if (t == QLatin1String("decision") || t == QLatin1String("preference")) return 1.0;
    if (t == QLatin1String("bug")) return 0.9;
    if (t == QLatin1String("fact")) return 0.7;
    return 0.5;   // other
}

// VALOR de un hecho en [0..~1]: tipo · confianza · recencia. Sube con utilidad.
double factValue(const QJsonObject &o)
{
    const double conf = o.value(QStringLiteral("confidence")).toDouble(0.8);
    double rec = 0.5;
    const QDateTime ts = QDateTime::fromString(
        o.value(QStringLiteral("ts")).toString(), Qt::ISODate);
    if (ts.isValid()) {
        const double days = ts.daysTo(QDateTime::currentDateTime());
        rec = std::pow(0.5, qMax(0.0, days) / 30.0);   // media vida 30 días
    }
    return typeWeight(o.value(QStringLiteral("type")).toString())
           * (0.4 + 0.6 * conf) * (0.4 + 0.6 * rec);
}

// COSTO MDL: largo del content normalizado a un "presupuesto" de ~240 chars.
// Texto corto ~0; un párrafo largo penaliza fuerte (paga sus bits).
double lenCost(const QJsonObject &o)
{
    const int len = o.value(QStringLiteral("content")).toString().size();
    return len / 240.0;
}

// Jaccard de keywords entre dos hechos (para detectar casi-duplicados).
double jaccard(const QSet<QString> &a, const QSet<QString> &b)
{
    if (a.isEmpty() || b.isEmpty()) return 0.0;
    int inter = 0;
    for (const QString &t : a) if (b.contains(t)) ++inter;
    const int uni = a.size() + b.size() - inter;
    return uni > 0 ? double(inter) / uni : 0.0;
}

}  // namespace

QString prune(const QString &cwd, const QString &scope, int maxKeep,
              const QString &mode, bool dryRun)
{
    if (maxKeep <= 0) maxKeep = 50;
    maxKeep = qBound(1, maxKeep, 1000);

    const QString path = jsonlPath(cwd);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("[memoria estructurada vacía]");

    const QString scopeFilter = scope.trimmed().toLower();

    struct Row {
        QJsonObject obj;
        QSet<QString> kw;
        double net = 0.0;       // valor - costo - redundancia
        bool inScope = false;   // candidato a poda
        QString why;            // motivo si se evicta
        bool evict = false;
    };
    QVector<Row> rows;
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        if (l.isEmpty()) continue;
        QJsonObject o = QJsonDocument::fromJson(l).object();
        if (o.isEmpty()) continue;
        Row r;
        r.obj = o;
        const bool stale = o.value(QStringLiteral("stale")).toBool(false);
        r.inScope = !stale
            && (scopeFilter.isEmpty()
                || o.value(QStringLiteral("scope")).toString() == scopeFilter);
        if (r.inScope) {
            const auto ts = terms(o.value(QStringLiteral("content")).toString());
            r.kw = QSet<QString>(ts.cbegin(), ts.cend());
            r.net = factValue(o) - 0.15 * lenCost(o);
        }
        rows.append(r);
    }
    f.close();

    // Redundancia: entre casi-duplicados (jaccard alto) penalizamos al de menor
    // valor → conserva el que "comprime mejor" la misma info.
    for (int i = 0; i < rows.size(); ++i) {
        if (!rows[i].inScope) continue;
        for (int j = i + 1; j < rows.size(); ++j) {
            if (!rows[j].inScope) continue;
            if (jaccard(rows[i].kw, rows[j].kw) < 0.6) continue;
            Row &lo = rows[i].net <= rows[j].net ? rows[i] : rows[j];
            lo.evict = true;
            lo.why = QStringLiteral("redundante");
        }
    }

    // Orden por valor neto desc; lo que cae bajo el presupuesto maxKeep se evicta.
    QVector<int> idx;
    for (int i = 0; i < rows.size(); ++i) if (rows[i].inScope) idx.append(i);
    std::stable_sort(idx.begin(), idx.end(),
                     [&](int a, int b) { return rows[a].net > rows[b].net; });
    int kept = 0;
    for (int rank = 0; rank < idx.size(); ++rank) {
        Row &r = rows[idx[rank]];
        if (r.evict) continue;                    // ya marcado por redundancia
        if (kept < maxKeep) { ++kept; continue; }
        r.evict = true;
        r.why = QStringLiteral("excede presupuesto");
    }

    int evicted = 0;
    QStringList sample;
    for (const Row &r : rows) if (r.evict) {
        ++evicted;
        if (sample.size() < 5)
            sample << QStringLiteral("  ✗ %1 (%2)")
                .arg(r.obj.value(QStringLiteral("content")).toString().left(60), r.why);
    }

    if (evicted == 0)
        return QStringLiteral("[prune: nada que podar · %1 hecho(s) dentro del presupuesto]")
            .arg(kept);

    const QString report = QStringLiteral("[prune%1: %2 evicto(s), %3 conservado(s)]\n")
        .arg(dryRun ? QStringLiteral(" dry-run") : QString()).arg(evicted).arg(kept)
        + sample.join(QLatin1Char('\n'));
    if (dryRun) return report;

    // Aplicar: borrar o marcar stale los evictos, reescribiendo el JSONL.
    const bool del = mode.trimmed().toLower() == QLatin1String("delete");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return QStringLiteral("[prune: no se pudo reescribir %1]").arg(path);
    for (Row &r : rows) {
        if (r.evict) {
            if (del) continue;
            r.obj.insert(QStringLiteral("stale"), true);
        }
        f.write(QJsonDocument(r.obj).toJson(QJsonDocument::Compact));
        f.write("\n");
    }
    f.close();
    return report + QStringLiteral("\n· modo=%1")
        .arg(del ? QStringLiteral("delete") : QStringLiteral("stale"));
}

}  // namespace MemoryStore
