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

}  // namespace MemoryStore
