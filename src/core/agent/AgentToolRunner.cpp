#include "AgentToolRunner.h"
#include "McpClient.h"
#include "LlamaAgentBackend.h"   // LlamaAgentBackend::makeDiff (static)
#include "MemoryStore.h"         // memoria por capas (hechos atómicos)
#include "GraphStore.h"          // knowledge graph (entidades + relaciones)
#include "BrowserTeach.h"        // skills de browser grabados (modo teach)
#include "core/mail/MailClient.h" // tools email_send/list/read

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonObject>
#include <QStandardPaths>
#include <QThread>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <cmath>
#include <cstring>

static const QString kMcpPrefix = QStringLiteral("mcp__");

// Carpetas que grep/glob NO recorren (ruido + lentitud). Aproxima a los defaults
// de opencode/aider; no parsea .gitignore completo.
static bool isIgnoredDir(const QString &name)
{
    static const QSet<QString> ig{
        QStringLiteral("node_modules"), QStringLiteral(".git"), QStringLiteral("build"),
        QStringLiteral("build2"), QStringLiteral("dist"), QStringLiteral(".venv"),
        QStringLiteral("venv"), QStringLiteral("__pycache__"), QStringLiteral(".next"),
        QStringLiteral(".turbo"), QStringLiteral("coverage"), QStringLiteral("target"),
        QStringLiteral(".cache"), QStringLiteral(".idea"), QStringLiteral(".vs"),
        QStringLiteral(".gradle"), QStringLiteral("bin"), QStringLiteral("obj")};
    return ig.contains(name);
}

// Recorre rootAbs recursivamente saltando dirs ignorados; junta rutas absolutas
// de archivos hasta maxFiles.
static void collectFiles(const QString &rootAbs, QStringList &out, int maxFiles)
{
    QStringList stack{rootAbs};
    while (!stack.isEmpty() && out.size() < maxFiles) {
        QDir d(stack.takeLast());
        const auto entries = d.entryInfoList(
            QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs, QDir::Name);
        for (const QFileInfo &fi : entries) {
            if (fi.isDir()) {
                if (!isIgnoredDir(fi.fileName())) stack << fi.absoluteFilePath();
            } else {
                out << fi.absoluteFilePath();
                if (out.size() >= maxFiles) break;
            }
        }
    }
}

// Traduce un glob ('*'=segmento, '**'=recursivo, '?'=1 char) a regex anclada.
static QRegularExpression globToRegex(const QString &glob)
{
    // Construido a mano (NO escape: escaparía '/'). Paths rel con '/'.
    QString rx = QStringLiteral("^");
    const int n = glob.size();
    for (int i = 0; i < n; ++i) {
        const QChar c = glob.at(i);
        if (c == QLatin1Char('*')) {
            if (i + 1 < n && glob.at(i + 1) == QLatin1Char('*')) {
                if (i + 2 < n && glob.at(i + 2) == QLatin1Char('/')) { rx += QStringLiteral("(?:.*/)?"); i += 2; }
                else { rx += QStringLiteral(".*"); i += 1; }
            } else { rx += QStringLiteral("[^/]*"); }
        } else if (c == QLatin1Char('?')) {
            rx += QStringLiteral("[^/]");
        } else {
            if (QStringLiteral(".^$+(){}[]|\\").contains(c)) rx += QLatin1Char('\\');
            rx += c;
        }
    }
    rx += QLatin1Char('$');
    return QRegularExpression(rx);
}

// ── Helpers web (compartidos por web_fetch / web_search / deep_research) ──
struct WebHit { QString title, url, snippet; };

// GET sincrónico con timeout (corre en el hilo worker, sin event loop propio del caller).
static QByteArray httpGetSync(const QUrl &url, QString *err, int timeoutMs = 20000)
{
    QNetworkAccessManager nam;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("Mozilla/5.0 LlamaCode/0.1"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = nam.get(req);
    QEventLoop loop;
    QTimer killer; killer.setSingleShot(true);
    QObject::connect(&killer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    killer.start(timeoutMs);
    loop.exec();
    if (reply->isRunning()) { reply->abort(); reply->deleteLater(); if (err) *err = QStringLiteral("timeout"); return {}; }
    if (reply->error() != QNetworkReply::NoError) { if (err) *err = reply->errorString(); reply->deleteLater(); return {}; }
    const QByteArray body = reply->readAll(); reply->deleteLater(); return body;
}

// HTML crudo → texto plano: saca script/style, tags, entidades, colapsa espacios.
static QString cleanHtmlToText(QString text)
{
    text.remove(QRegularExpression(QStringLiteral("(?is)<(script|style)[^>]*>.*?</\\1>")));
    text.remove(QRegularExpression(QStringLiteral("(?s)<[^>]+>")));
    text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    text.replace(QStringLiteral("&amp;"),  QStringLiteral("&"));
    text.replace(QStringLiteral("&lt;"),   QStringLiteral("<"));
    text.replace(QStringLiteral("&gt;"),   QStringLiteral(">"));
    text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    text.replace(QStringLiteral("&#x27;"), QStringLiteral("'"));
    text.replace(QRegularExpression(QStringLiteral("[ \t]+")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("\n[ \t]*(?:\n[ \t]*)+")), QStringLiteral("\n\n"));
    return text.trimmed();
}

// Descarga una URL y devuelve su texto limpiado (cap chars). "" si falla.
static QString fetchUrlText(const QString &url, int cap, QString *err = nullptr)
{
    if (!url.startsWith(QLatin1String("http"))) { if (err) *err = QStringLiteral("url inválida"); return {}; }
    const QByteArray body = httpGetSync(QUrl(url), err);
    if (body.isEmpty()) return {};
    const QString text = cleanHtmlToText(QString::fromUtf8(body));
    return text.left(cap);
}

// Resuelve el redirect /l/?uddg= de DuckDuckGo a la URL destino.
static QString resolveDdgRedirect(QString raw)
{
    if (raw.startsWith(QLatin1String("//"))) raw = QStringLiteral("https:") + raw;
    QUrl ru(raw);
    if (ru.path().endsWith(QLatin1String("/l/")) || ru.path() == QLatin1String("/l")) {
        const QString uddg = QUrlQuery(ru).queryItemValue(QStringLiteral("uddg"), QUrl::FullyDecoded);
        if (!uddg.isEmpty()) return uddg;
    }
    return raw;
}

// Búsqueda web: SearXNG si LLAMACODE_SEARXNG_URL está seteado, si no DuckDuckGo HTML.
static QVector<WebHit> runWebSearch(const QString &query, int count, QString *err = nullptr)
{
    QVector<WebHit> hits;
    const QString searxng = qEnvironmentVariable("LLAMACODE_SEARXNG_URL").trimmed();
    if (!searxng.isEmpty()) {
        QUrl u(searxng.endsWith(QLatin1Char('/')) ? searxng + QStringLiteral("search")
                                                   : searxng + QStringLiteral("/search"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("q"), query);
        q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
        u.setQuery(q);
        const QByteArray body = httpGetSync(u, err);
        if (!body.isEmpty()) {
            const QJsonArray arr = QJsonDocument::fromJson(body).object()
                                       .value(QStringLiteral("results")).toArray();
            for (const QJsonValue &v : arr) {
                const QJsonObject o = v.toObject();
                hits.append({o.value(QStringLiteral("title")).toString(),
                             o.value(QStringLiteral("url")).toString(),
                             o.value(QStringLiteral("content")).toString()});
                if (hits.size() >= count) break;
            }
        }
    }
    if (hits.isEmpty()) {
        QUrl u(QStringLiteral("https://html.duckduckgo.com/html/"));
        QUrlQuery q; q.addQueryItem(QStringLiteral("q"), query); u.setQuery(q);
        const QByteArray body = httpGetSync(u, err);
        if (body.isEmpty()) return hits;
        const QString html = QString::fromUtf8(body);
        QRegularExpression reTitle(
            QStringLiteral("(?is)<a[^>]+class=\"[^\"]*result__a[^\"]*\"[^>]+href=\"([^\"]+)\"[^>]*>(.*?)</a>"));
        QRegularExpression reSnip(
            QStringLiteral("(?is)class=\"[^\"]*result__snippet[^\"]*\"[^>]*>(.*?)</a>"));
        auto snipIt = reSnip.globalMatch(html);
        auto titleIt = reTitle.globalMatch(html);
        while (titleIt.hasNext() && hits.size() < count) {
            const auto tm = titleIt.next();
            WebHit h;
            h.url = resolveDdgRedirect(tm.captured(1));
            h.title = cleanHtmlToText(tm.captured(2));
            if (snipIt.hasNext()) h.snippet = cleanHtmlToText(snipIt.next().captured(1));
            if (!h.url.isEmpty()) hits.append(h);
        }
    }
    return hits;
}

// ── Embeddings + cache de vectores (RAG semántico vía /v1/embeddings) ──

// POST JSON sincrónico. Devuelve el body o {} con *err.
static QByteArray httpPostJson(const QUrl &url, const QByteArray &body, QString *err,
                               int timeoutMs = 60000, const QString &bearer = QString())
{
    QNetworkAccessManager nam;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    if (!bearer.isEmpty())
        req.setRawHeader(QByteArrayLiteral("Authorization"),
                         QByteArrayLiteral("Bearer ") + bearer.toUtf8());
    QNetworkReply *reply = nam.post(req, body);
    QEventLoop loop;
    QTimer killer; killer.setSingleShot(true);
    QObject::connect(&killer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    killer.start(timeoutMs);
    loop.exec();
    if (reply->isRunning()) { reply->abort(); reply->deleteLater(); if (err) *err = QStringLiteral("timeout"); return {}; }
    if (reply->error() != QNetworkReply::NoError) {
        const QByteArray b = reply->readAll(); QString e = reply->errorString();
        if (!b.isEmpty()) e += QStringLiteral(" · ") + QString::fromUtf8(b.left(200));
        reply->deleteLater(); if (err) *err = e; return {};
    }
    const QByteArray out = reply->readAll(); reply->deleteLater(); return out;
}

// Llama /v1/embeddings con un batch de textos → vectores. "" en *err si OK.
static QVector<QVector<float>> embedTexts(const QString &baseUrl, const QStringList &texts,
                                          QString *err)
{
    QVector<QVector<float>> out;
    if (baseUrl.isEmpty()) { if (err) *err = QStringLiteral("sin URL de server"); return out; }
    QJsonArray inputs;
    for (const QString &t : texts) inputs.append(t);
    const QJsonObject payload{
        {QStringLiteral("input"), inputs},
        {QStringLiteral("model"), QStringLiteral("llamacode-embed")}};
    const QByteArray body = httpPostJson(QUrl(baseUrl + QStringLiteral("/v1/embeddings")),
                                         QJsonDocument(payload).toJson(QJsonDocument::Compact), err);
    if (body.isEmpty()) return out;
    const QJsonArray data = QJsonDocument::fromJson(body).object()
                                .value(QStringLiteral("data")).toArray();
    if (data.isEmpty()) { if (err) *err = QStringLiteral("respuesta sin embeddings (¿server sin --embeddings?)"); return out; }
    out.resize(data.size());
    for (const QJsonValue &dv : data) {
        const QJsonObject o = dv.toObject();
        const int idx = o.value(QStringLiteral("index")).toInt();
        const QJsonArray emb = o.value(QStringLiteral("embedding")).toArray();
        QVector<float> vec; vec.reserve(emb.size());
        for (const QJsonValue &ev : emb) vec.append(static_cast<float>(ev.toDouble()));
        if (idx >= 0 && idx < out.size()) out[idx] = vec; else out.append(vec);
    }
    return out;
}

// Conexión SQLite per-thread al cache de vectores. Tabla: key TEXT PK, dim INT, vec BLOB.
static QSqlDatabase embedCacheDb()
{
    const QString conn = QStringLiteral("embed_cache_%1")
                             .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    if (QSqlDatabase::contains(conn)) return QSqlDatabase::database(conn);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    db.setDatabaseName(dir + QStringLiteral("/embeddings_cache.db"));
    if (db.open()) {
        QSqlQuery q(db);
        q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS vecs ("
                              "key TEXT PRIMARY KEY, dim INTEGER, vec BLOB)"));
    }
    return db;
}

static QByteArray vecToBlob(const QVector<float> &v)
{
    return QByteArray(reinterpret_cast<const char *>(v.constData()),
                      int(v.size() * sizeof(float)));
}
static QVector<float> blobToVec(const QByteArray &b)
{
    QVector<float> v(int(b.size() / sizeof(float)));
    memcpy(v.data(), b.constData(), v.size() * sizeof(float));
    return v;
}

static float cosineSim(const QVector<float> &a, const QVector<float> &b)
{
    if (a.size() != b.size() || a.isEmpty()) return 0.f;
    double dot = 0, na = 0, nb = 0;
    for (int i = 0; i < a.size(); ++i) { dot += double(a[i]) * b[i]; na += double(a[i]) * a[i]; nb += double(b[i]) * b[i]; }
    if (na == 0 || nb == 0) return 0.f;
    return float(dot / (std::sqrt(na) * std::sqrt(nb)));
}

// Llama /rerank (llama-server con --reranking, p.ej. Qwen3-Reranker) → score de
// relevancia por doc, alineado al orden de 'docs'. Vector vacío si el endpoint no
// existe o falla (el caller cae a la fusión sin reranker). "" en *err si OK.
static QVector<float> rerankTexts(const QString &baseUrl, const QString &query,
                                  const QStringList &docs, QString *err)
{
    QVector<float> out;
    if (baseUrl.isEmpty() || docs.isEmpty()) {
        if (err) *err = QStringLiteral("sin server/docs"); return out;
    }
    QJsonArray arr;
    for (const QString &d : docs) arr.append(d);
    const QJsonObject payload{
        {QStringLiteral("query"), query},
        {QStringLiteral("documents"), arr},
        {QStringLiteral("model"), QStringLiteral("llamacode-rerank")}};
    const QByteArray body = httpPostJson(QUrl(baseUrl + QStringLiteral("/rerank")),
                                         QJsonDocument(payload).toJson(QJsonDocument::Compact), err);
    if (body.isEmpty()) return out;
    const QJsonArray results = QJsonDocument::fromJson(body).object()
                                   .value(QStringLiteral("results")).toArray();
    if (results.isEmpty()) {
        if (err) *err = QStringLiteral("respuesta sin results (¿server sin --reranking?)");
        return out;
    }
    out.resize(docs.size());
    for (const QJsonValue &rv : results) {
        const QJsonObject o = rv.toObject();
        const int idx = o.value(QStringLiteral("index")).toInt();
        const double score = o.value(QStringLiteral("relevance_score"))
                                 .toDouble(o.value(QStringLiteral("score")).toDouble());
        if (idx >= 0 && idx < out.size()) out[idx] = float(score);
    }
    return out;
}

// Extrae los "módulos" referenciados por un archivo de código (estilo dep-graph
// de archex, sin tree-sitter): #include de C/C++, import/require de JS/TS,
// import/from de Python, import de QML. Devuelve los nombres base (último
// segmento, sin extensión, en minúsculas) para casar contra archivos del repo.
static QSet<QString> extractImportRefs(const QString &text)
{
    QSet<QString> refs;
    static const QRegularExpression reC(
        QStringLiteral("#\\s*include\\s*[\"<]([^\">]+)[\">]"));
    static const QRegularExpression reFrom(
        QStringLiteral("(?:from|import|require)\\s*\\(?\\s*[\"']([^\"']+)[\"']"));
    static const QRegularExpression rePy(
        QStringLiteral("^\\s*(?:from|import)\\s+([\\w.]+)"),
        QRegularExpression::MultilineOption);
    static const QRegularExpression reQml(
        QStringLiteral("^\\s*import\\s+([\\w.]+)"),
        QRegularExpression::MultilineOption);
    static const QRegularExpression reCodeExt(
        QStringLiteral("\\.(h|hpp|hh|hxx|c|cc|cpp|cxx|js|jsx|mjs|ts|tsx|py|qml|java|go|rs|kt)$"),
        QRegularExpression::CaseInsensitiveOption);
    auto add = [&](const QString &raw) {
        QString s = raw;
        s.replace(QLatin1Char('\\'), QLatin1Char('/'));
        s = s.section(QLatin1Char('/'), -1).trimmed();   // basename (ruta) / módulo
        if (reCodeExt.match(s).hasMatch())
            s = s.section(QLatin1Char('.'), 0, -2);       // ruta: dropear extensión
        else if (s.contains(QLatin1Char('.')))
            s = s.section(QLatin1Char('.'), -1);          // módulo a.b.c → último tramo
        s = s.toLower();
        if (s.size() >= 2) refs.insert(s);
    };
    for (const QRegularExpression *re : {&reC, &reFrom, &rePy, &reQml}) {
        auto it = re->globalMatch(text);
        while (it.hasNext()) add(it.next().captured(1));
    }
    return refs;
}

AgentToolRunner::AgentToolRunner(QObject *parent) : QObject(parent) {}
AgentToolRunner::~AgentToolRunner() { shutdown(); }

void AgentToolRunner::setConfined(bool confined) { m_confined = confined; }
void AgentToolRunner::setServerBaseUrl(const QString &url) { m_serverBaseUrl = url; }
void AgentToolRunner::setMailAccounts(const QVariantList &accounts) { m_mailAccounts = accounts; }
void AgentToolRunner::setTeacherConfig(const QString &url, const QString &model, const QString &key)
{
    m_teacherUrl = url.trimmed();
    m_teacherModel = model.trimmed();
    m_teacherKey = key.trimmed();
}
void AgentToolRunner::setMasterCli(const QString &kind, const QString &cliName,
                                   const QString &cliPath, bool applyEdits, int timeoutSec)
{
    m_masterKind = kind.trimmed().isEmpty() ? QStringLiteral("none") : kind.trimmed();
    m_masterCliName = cliName.trimmed();
    m_masterCliPath = cliPath.trimmed();
    m_masterApplyEdits = applyEdits;
    m_masterTimeoutS = timeoutSec > 0 ? timeoutSec : 300;
}

void AgentToolRunner::setMasterChain(const QVariantList &chain)
{
    m_masterChain = chain;
}

// Invoca claude-code / codex en modo no-interactivo, bloqueante. cwd = proyecto.
QString AgentToolRunner::runMasterCli(const QString &cliName, const QString &cliPath,
                                      bool applyEdits, int timeoutSec,
                                      const QString &question, const QString &context,
                                      const QString &cwd, bool *ok)
{
    if (cliPath.isEmpty())
        return QStringLiteral("[ask_teacher: CLI maestro '%1' no encontrado en PATH. "
                              "Instalalo o configurá el maestro en el perfil.]").arg(cliName);

    const int timeout = timeoutSec > 0 ? timeoutSec : 300;
    QString prompt = question;
    if (!context.isEmpty())
        prompt = QStringLiteral("Contexto:\n%1\n\nProblema:\n%2").arg(context, question);
    if (!applyEdits)
        prompt += QStringLiteral("\n\nNO modifiques archivos. Devolvé sólo un plan/solución concreta.");
    else
        prompt += QStringLiteral("\n\nResolvé el problema en el proyecto (podés editar archivos). "
                                 "Al terminar resumí qué cambiaste.");

    QStringList args;
    if (cliName == QLatin1String("claude")) {
        // Claude Code modo print: respuesta a stdout y termina.
        args << QStringLiteral("-p") << prompt;
        if (applyEdits)
            args << QStringLiteral("--permission-mode") << QStringLiteral("acceptEdits");
    } else if (cliName == QLatin1String("codex")) {
        // Codex modo no-interactivo.
        args << QStringLiteral("exec");
        if (applyEdits) args << QStringLiteral("--full-auto");
        args << prompt;
    } else {
        return QStringLiteral("[ask_teacher: CLI maestro desconocido: %1]").arg(cliName);
    }

    QProcess proc;
    if (!cwd.isEmpty()) proc.setWorkingDirectory(cwd);
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(cliPath, args);
    if (!proc.waitForStarted(10000))
        return QStringLiteral("[ask_teacher: no se pudo iniciar %1]").arg(cliName);
    if (!proc.waitForFinished(timeout * 1000)) {
        proc.kill();
        proc.waitForFinished(2000);
        return QStringLiteral("[ask_teacher: el maestro %1 superó el timeout de %2s]")
            .arg(cliName).arg(timeout);
    }
    const QString out = QString::fromUtf8(proc.readAll()).trimmed();
    if (out.isEmpty())
        return QStringLiteral("[ask_teacher: respuesta vacía del maestro %1]").arg(cliName);
    if (ok) *ok = true;
    return QStringLiteral("[Respuesta del maestro %1]\n%2").arg(cliName, out);
}

// Consulta HTTP OpenAI-compat a un maestro. ok=true sólo si hubo respuesta útil.
QString AgentToolRunner::runHttpTeacher(const QString &url, const QString &model,
                                        const QString &key, const QString &question,
                                        const QString &context, bool *ok)
{
    if (url.isEmpty())
        return QStringLiteral("[ask_teacher: endpoint del maestro vacío]");
    const QString mdl = model.isEmpty() ? QStringLiteral("default") : model;
    QString userMsg = question;
    if (!context.isEmpty())
        userMsg = QStringLiteral("Contexto:\n%1\n\nPregunta:\n%2").arg(context, question);
    const QJsonArray msgs{
        QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                    {QStringLiteral("content"), QStringLiteral(
                         "Sos un experto sénior asistiendo a otro agente de código. "
                         "Respondé conciso, correcto y accionable.")}},
        QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                    {QStringLiteral("content"), userMsg}}};
    const QJsonObject payload{
        {QStringLiteral("model"), mdl},
        {QStringLiteral("messages"), msgs},
        {QStringLiteral("stream"), false}};
    const QUrl endpoint(url.endsWith(QLatin1Char('/'))
                            ? url + QStringLiteral("v1/chat/completions")
                            : url + QStringLiteral("/v1/chat/completions"));
    QString err;
    const QByteArray resp = httpPostJson(endpoint,
                                         QJsonDocument(payload).toJson(QJsonDocument::Compact),
                                         &err, 120000, key);
    if (resp.isEmpty())
        return QStringLiteral("[ask_teacher: error consultando al maestro: %1]").arg(err);
    const QJsonObject root = QJsonDocument::fromJson(resp).object();
    const QString answer = root.value(QStringLiteral("choices")).toArray().isEmpty()
        ? QString()
        : root.value(QStringLiteral("choices")).toArray().first().toObject()
              .value(QStringLiteral("message")).toObject()
              .value(QStringLiteral("content")).toString();
    if (answer.isEmpty())
        return QStringLiteral("[ask_teacher: respuesta vacía del maestro]");
    if (ok) *ok = true;
    return QStringLiteral("[Respuesta del modelo maestro]\n") + answer;
}

// Recorre la cadena de fallbacks en orden; corta y devuelve al primer éxito.
// Si todos fallan, devuelve el último error acumulado.
QString AgentToolRunner::runMasterChain(const QString &question, const QString &context,
                                        const QString &cwd, bool *ok)
{
    QString last = QStringLiteral("[ask_teacher: cadena de maestros vacía]");
    int level = 0;
    for (const QVariant &v : m_masterChain) {
        const QVariantMap e = v.toMap();
        const QString type = e.value(QStringLiteral("type")).toString();
        const QString lbl  = e.value(QStringLiteral("label")).toString();
        ++level;
        bool localOk = false;
        QString res;
        if (type == QLatin1String("cli")) {
            res = runMasterCli(e.value(QStringLiteral("cliName")).toString(),
                               e.value(QStringLiteral("cliPath")).toString(),
                               e.value(QStringLiteral("applyEdits"), true).toBool(),
                               e.value(QStringLiteral("timeoutSec"), 300).toInt(),
                               question, context, cwd, &localOk);
        } else { // http (incluye profile ya resuelto a http)
            res = runHttpTeacher(e.value(QStringLiteral("httpUrl")).toString(),
                                 e.value(QStringLiteral("httpModel")).toString(),
                                 e.value(QStringLiteral("httpKey")).toString(),
                                 question, context, &localOk);
        }
        if (localOk) {
            if (ok) *ok = true;
            return res;
        }
        last = QStringLiteral("[fallback %1%2 falló] %3")
                   .arg(level)
                   .arg(lbl.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(lbl))
                   .arg(res);
    }
    return last;
}

void AgentToolRunner::shutdown()
{
    if (m_shellTimer) m_shellTimer->stop();
    if (m_shellProc) {
        m_shellProc->disconnect(this);
        m_shellProc->kill();
        m_shellProc->waitForFinished(2000);
        delete m_shellProc;
        m_shellProc = nullptr;
        m_shellCallId.clear();
    }
    for (McpClient *c : std::as_const(m_mcp)) { c->shutdown(); delete c; }
    m_mcp.clear();
}

void AgentToolRunner::initServers(const QVariantList &cfg, const QString &cwd)
{
    shutdown();
    for (const QVariant &v : cfg) {
        const QVariantMap s = v.toMap();
        if (!s.value(QStringLiteral("enabled"), true).toBool()) continue;
        if (s.value(QStringLiteral("type"), QStringLiteral("local")).toString()
                != QLatin1String("local")) continue;
        const QString name = s.value(QStringLiteral("name")).toString();
        const QString cmd  = s.value(QStringLiteral("command")).toString();
        if (name.isEmpty() || cmd.isEmpty()) continue;

        auto *c = new McpClient(name);   // sin parent: vive en este hilo worker
        connect(c, &McpClient::logAppended, this, &AgentToolRunner::logAppended);
        if (c->start(cmd, cwd)) m_mcp.append(c);
        else delete c;
    }

    QVariantList defs;
    for (McpClient *c : m_mcp) {
        for (const McpClient::ToolDef &t : c->tools()) {
            defs.append(QVariantMap{
                {QStringLiteral("server"), c->serverName()},
                {QStringLiteral("name"), t.name},
                {QStringLiteral("description"), t.description},
                {QStringLiteral("schema"), QVariant::fromValue(
                     QString::fromUtf8(QJsonDocument(t.inputSchema).toJson(QJsonDocument::Compact)))}
            });
        }
    }
    emit serversReady(defs);
}

void AgentToolRunner::executeTool(const QString &callId, const QString &name,
                                  const QString &argsJson, const QString &cwd)
{
    const QJsonObject args = QJsonDocument::fromJson(argsJson.toUtf8()).object();

    // run_shell es ASÍNCRONO: spawnea y vuelve. La salida se streamea por
    // toolOutputChunk y el resultado final llega por toolExecuted al terminar.
    // No bloquea el hilo worker → el proceso es cancelable (cancelShell).
    if (name == QLatin1String("run_shell")) {
        const QString command = args.value(QStringLiteral("command")).toString();
        int timeoutS = args.value(QStringLiteral("timeout_s")).toInt(120);
        if (timeoutS <= 0) timeoutS = 120;
        if (timeoutS > 1800) timeoutS = 1800;
        startShell(callId, command, cwd, timeoutS);
        return;
    }

    QVariantMap out{{QStringLiteral("callId"), callId}, {QStringLiteral("name"), name}};
    bool ok = false;
    QString result;

    if (name.startsWith(kMcpPrefix)) {
        // mcp__<server>__<tool>
        const QString rest = name.mid(kMcpPrefix.size());
        const int sep = rest.indexOf(QStringLiteral("__"));
        McpClient *c = nullptr;
        QString bare;
        if (sep >= 0) {
            const QString server = rest.left(sep);
            bare = rest.mid(sep + 2);
            for (McpClient *cc : m_mcp)
                if (cc->serverName() == server) { c = cc; break; }
        }
        if (!c) result = QStringLiteral("[mcp: server/tool no encontrado: %1]").arg(name);
        else    result = c->callTool(bare, args, &ok);
    } else {
        result = runNative(name, args, cwd, out, &ok);
    }

    out[QStringLiteral("result")] = result;
    out[QStringLiteral("ok")] = ok;
    emit toolExecuted(out);
}

// Ejecución de tools nativas (idéntica a la vieja LlamaAgentBackend::executeTool,
// pero sin tocar UI: el write devuelve metadata para que el main arme diff/snapshot).
QString AgentToolRunner::runNative(const QString &name, const QJsonObject &args,
                                   const QString &cwd, QVariantMap &out, bool *ok)
{
    if (ok) *ok = false;
    const QDir base(cwd);
    auto resolve = [&](const QString &rel) { return QDir::cleanPath(base.absoluteFilePath(rel)); };
    // En modo "Super Agente" (no confinado) se permite cualquier ruta del disco.
    auto inProject = [&](const QString &abs) {
        if (!m_confined) return true;
        const QString root = QDir::cleanPath(base.absolutePath());
        return abs == root || abs.startsWith(root + QStringLiteral("/"))
               || abs.startsWith(root + QStringLiteral("\\"));
    };

    if (name == QLatin1String("read_file")) {
        const QString abs = resolve(args.value(QStringLiteral("path")).toString());
        if (!inProject(abs)) return QStringLiteral("[ruta fuera del proyecto]");
        QFile f(abs);
        if (!f.open(QIODevice::ReadOnly)) return QStringLiteral("[no se pudo abrir: %1]").arg(abs);
        const QByteArray raw = f.read(4 * 1024 * 1024);
        if (ok) *ok = true;

        const int offset = args.value(QStringLiteral("offset")).toInt(0);
        const int limit  = args.value(QStringLiteral("limit")).toInt(0);
        if (offset > 0 || limit > 0) {
            // Lectura parcial por líneas. NO genera huella de dedup (sería por
            // archivo, no por rango → falsos positivos).
            const QStringList lines = QString::fromUtf8(raw).split(QLatin1Char('\n'));
            const int start = qBound(0, offset > 0 ? offset - 1 : 0, lines.size());
            const int count = (limit > 0) ? limit : (lines.size() - start);
            const QStringList slice = lines.mid(start, count);
            return QStringLiteral("[líneas %1-%2 de %3]\n%4")
                       .arg(start + 1).arg(start + slice.size()).arg(lines.size())
                       .arg(slice.join(QLatin1Char('\n')));
        }
        // Archivo completo → huella para read-dedup.
        out[QStringLiteral("readRel")] = base.relativeFilePath(abs);
        out[QStringLiteral("readFp")]  = QString::fromLatin1(
            QCryptographicHash::hash(raw, QCryptographicHash::Md5).toHex());
        return QString::fromUtf8(raw);
    }
    if (name == QLatin1String("list_dir")) {
        const QString abs = resolve(args.value(QStringLiteral("path")).toString());
        if (!inProject(abs)) return QStringLiteral("[ruta fuera del proyecto]");
        QDir d(abs);
        if (!d.exists()) return QStringLiteral("[no existe: %1]").arg(abs);
        const bool recursive = args.value(QStringLiteral("recursive")).toBool();
        if (ok) *ok = true;
        if (!recursive) {
            QStringList outList;
            const auto entries = d.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries, QDir::Name);
            for (const QFileInfo &fi : entries)
                outList << (fi.isDir() ? fi.fileName() + QStringLiteral("/") : fi.fileName());
            return outList.join(QLatin1Char('\n'));
        }
        // Recursivo: rutas relativas al cwd, saltando dirs ignorados, cap 1000.
        QStringList outList;
        QStringList stack{abs};
        while (!stack.isEmpty() && outList.size() < 1000) {
            QDir dd(stack.takeLast());
            const auto entries = dd.entryInfoList(
                QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs, QDir::Name);
            for (const QFileInfo &fi : entries) {
                const QString rel = base.relativeFilePath(fi.absoluteFilePath());
                if (fi.isDir()) {
                    outList << rel + QStringLiteral("/");
                    if (!isIgnoredDir(fi.fileName())) stack << fi.absoluteFilePath();
                } else {
                    outList << rel;
                }
                if (outList.size() >= 1000) break;
            }
        }
        outList.sort();
        return outList.join(QLatin1Char('\n'));
    }
    if (name == QLatin1String("web_fetch")) {
        const QString url = args.value(QStringLiteral("url")).toString();
        if (!url.startsWith(QLatin1String("http")))
            return QStringLiteral("[url inválida: debe empezar con http(s)://]");
        QString err;
        const QString text = fetchUrlText(url, 48 * 1024, &err);
        if (text.isEmpty()) {
            if (!err.isEmpty()) return QStringLiteral("[error al descargar %1: %2]").arg(url, err);
            return QStringLiteral("[respuesta vacía]");
        }
        if (ok) *ok = true;
        return text;
    }
    if (name == QLatin1String("web_search")) {
        const QString query = args.value(QStringLiteral("query")).toString().trimmed();
        if (query.isEmpty()) return QStringLiteral("[query vacía]");
        int count = args.value(QStringLiteral("count")).toInt();
        if (count <= 0) count = 5;
        count = qBound(1, count, 10);
        QString err;
        const QVector<WebHit> hits = runWebSearch(query, count, &err);
        if (hits.isEmpty())
            return QStringLiteral("[sin resultados para: %1%2]").arg(query,
                       err.isEmpty() ? QString() : QStringLiteral(" — ") + err);
        QStringList out;
        for (int i = 0; i < hits.size(); ++i)
            out << QStringLiteral("%1. %2\n   %3\n   %4")
                       .arg(i + 1).arg(hits[i].title, hits[i].url, hits[i].snippet);
        if (ok) *ok = true;
        return out.join(QStringLiteral("\n\n"));
    }
    if (name == QLatin1String("deep_research")) {
        const QString query = args.value(QStringLiteral("query")).toString().trimmed();
        if (query.isEmpty()) return QStringLiteral("[query vacía]");
        int maxPages = args.value(QStringLiteral("max_pages")).toInt();
        if (maxPages <= 0) maxPages = 5;
        maxPages = qBound(1, maxPages, 10);

        // Ángulos: el modelo puede pasar varias sub-consultas; si no, usa la query sola.
        QStringList queries;
        const QJsonArray angles = args.value(QStringLiteral("angles")).toArray();
        for (const QJsonValue &v : angles) {
            const QString a = v.toString().trimmed();
            if (!a.isEmpty()) queries << a;
        }
        if (queries.isEmpty()) queries << query;
        if (queries.size() > 4) queries = queries.mid(0, 4);   // cap

        // 1) Buscar por cada ángulo, juntar URLs únicas (orden de aparición).
        QStringList urls;
        QHash<QString, WebHit> meta;
        QStringList searchLog;
        for (const QString &q : queries) {
            const QVector<WebHit> hits = runWebSearch(q, 4);
            searchLog << QStringLiteral("· \"%1\" → %2 resultados").arg(q).arg(hits.size());
            for (const WebHit &h : hits) {
                if (h.url.isEmpty() || urls.contains(h.url)) continue;
                urls << h.url; meta.insert(h.url, h);
                if (urls.size() >= maxPages) break;
            }
            if (urls.size() >= maxPages) break;
        }
        if (urls.isEmpty())
            return QStringLiteral("[deep_research: sin resultados de búsqueda para: %1]").arg(query);

        // 2) Descargar y limpiar cada página (excerpt acotado por fuente).
        const int perPageCap = 3500;
        QStringList sources, bodies;
        for (int i = 0; i < urls.size(); ++i) {
            const WebHit &h = meta.value(urls[i]);
            sources << QStringLiteral("[%1] %2 — %3").arg(i + 1).arg(h.title, urls[i]);
            const QString text = fetchUrlText(urls[i], perPageCap);
            bodies << QStringLiteral("### [%1] %2\n%3")
                          .arg(i + 1).arg(h.title,
                               text.isEmpty() ? QStringLiteral("(no se pudo descargar)") : text);
        }

        // 3) Devolver dossier crudo; el MODELO sintetiza (es el LLM del loop).
        QString out;
        out += QStringLiteral("# Dossier de investigación: %1\n\n").arg(query);
        out += QStringLiteral("Búsquedas:\n%1\n\n").arg(searchLog.join(QLatin1Char('\n')));
        out += QStringLiteral("## Fuentes\n%1\n\n").arg(sources.join(QLatin1Char('\n')));
        out += QStringLiteral("## Contenido\n%1\n\n").arg(bodies.join(QStringLiteral("\n\n")));
        out += QStringLiteral("---\nSintetizá una respuesta a \"%1\" citando las fuentes por su número [n]. "
                              "Si algo no está cubierto, decilo.").arg(query);
        if (ok) *ok = true;
        return out.left(28 * 1024);
    }
    if (name == QLatin1String("search_docs")) {
        // RAG-lite: ranking por relevancia de keywords sobre chunks (sin embeddings).
        const QString query = args.value(QStringLiteral("query")).toString().trimmed();
        if (query.isEmpty()) return QStringLiteral("[query vacía]");
        int k = args.value(QStringLiteral("k")).toInt();
        if (k <= 0) k = 5;
        k = qBound(1, k, 15);
        const QString sub = args.value(QStringLiteral("path")).toString();
        const QString rootAbs = resolve(sub);
        if (!inProject(rootAbs)) return QStringLiteral("[ruta fuera del proyecto]");

        // Tokens de la consulta (lowercase, >=2 chars, únicos).
        QStringList terms;
        for (const QString &t : query.toLower().split(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}_]+")),
                                                       Qt::SkipEmptyParts))
            if (t.size() >= 2 && !terms.contains(t)) terms << t;
        if (terms.isEmpty()) return QStringLiteral("[query sin términos útiles]");

        struct Chunk { QString file; int line; double score; QString text; };
        QVector<Chunk> top;   // mantenido chico (k)
        auto consider = [&](const Chunk &c) {
            if (c.score <= 0) return;
            if (top.size() < k) { top.append(c); }
            else {
                int wi = 0;
                for (int i = 1; i < top.size(); ++i) if (top[i].score < top[wi].score) wi = i;
                if (c.score > top[wi].score) top[wi] = c;
            }
        };

        QStringList files;
        collectFiles(rootAbs, files, 8000);
        const int chunkLines = 40;
        for (const QString &fp : files) {
            // Sólo archivos de texto de tamaño razonable.
            QFileInfo fi(fp);
            if (fi.size() > 1024 * 1024) continue;
            QFile f(fp);
            if (!f.open(QIODevice::ReadOnly)) continue;
            const QByteArray raw = f.read(1024 * 1024);
            if (raw.contains('\0')) continue;   // binario
            const QStringList lines = QString::fromUtf8(raw).split(QLatin1Char('\n'));
            const QString rel = base.relativeFilePath(fp);
            for (int start = 0; start < lines.size(); start += chunkLines) {
                const QStringList slice = lines.mid(start, chunkLines);
                const QString chunk = slice.join(QLatin1Char('\n'));
                const QString low = chunk.toLower();
                if (low.isEmpty()) continue;
                // Score BM25-ish: por término, tf con saturación; bonus por cobertura.
                double score = 0; int distinct = 0;
                for (const QString &t : terms) {
                    int tf = low.count(t);
                    if (tf > 0) { distinct++; score += tf / (tf + 1.5); }
                }
                if (distinct == 0) continue;
                score *= (1.0 + 0.5 * (distinct - 1));            // recompensa multi-término
                score /= (1.0 + chunk.size() / 4000.0);           // normaliza por largo
                consider({rel, start + 1, score, chunk.trimmed().left(600)});
            }
        }
        if (top.isEmpty()) return QStringLiteral("[sin coincidencias para: %1]").arg(query);
        std::sort(top.begin(), top.end(), [](const Chunk &a, const Chunk &b) { return a.score > b.score; });
        QStringList out;
        for (const Chunk &c : top)
            out << QStringLiteral("%1:%2  (score %3)\n%4")
                       .arg(c.file).arg(c.line).arg(c.score, 0, 'f', 2).arg(c.text);
        if (ok) *ok = true;
        return out.join(QStringLiteral("\n\n──────\n"));
    }
    if (name == QLatin1String("semantic_search")) {
        // RAG semántico real: embeddings vía /v1/embeddings + cache de vectores SQLite.
        const QString query = args.value(QStringLiteral("query")).toString().trimmed();
        if (query.isEmpty()) return QStringLiteral("[query vacía]");
        if (m_serverBaseUrl.isEmpty())
            return QStringLiteral("[semantic_search: no hay server activo]");
        int k = args.value(QStringLiteral("k")).toInt();
        if (k <= 0) k = 5;
        k = qBound(1, k, 15);
        const QString rootAbs = resolve(args.value(QStringLiteral("path")).toString());
        if (!inProject(rootAbs)) return QStringLiteral("[ruta fuera del proyecto]");

        struct Ch { QString rel; int line; QString key; QString text; QVector<float> vec; };
        QVector<Ch> chunks;
        const int chunkLines = 40, maxChunks = 800;
        QStringList files;
        collectFiles(rootAbs, files, 8000);
        bool truncated = false;
        for (const QString &fp : files) {
            if (chunks.size() >= maxChunks) { truncated = true; break; }
            QFileInfo fi(fp);
            if (fi.size() > 1024 * 1024) continue;
            QFile f(fp);
            if (!f.open(QIODevice::ReadOnly)) continue;
            const QByteArray raw = f.read(1024 * 1024);
            if (raw.contains('\0')) continue;
            const QStringList lines = QString::fromUtf8(raw).split(QLatin1Char('\n'));
            const QString rel = base.relativeFilePath(fp);
            for (int start = 0; start < lines.size() && chunks.size() < maxChunks; start += chunkLines) {
                const QString text = lines.mid(start, chunkLines).join(QLatin1Char('\n')).trimmed();
                if (text.size() < 16) continue;   // descartar fragmentos triviales
                const QString key = QString::fromLatin1(
                    QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Md5).toHex());
                chunks.append({rel, start + 1, key, text, {}});
            }
        }
        if (chunks.isEmpty()) return QStringLiteral("[no hay archivos de texto para indexar]");

        // 1) Cargar vectores cacheados; juntar los faltantes.
        QSqlDatabase db = embedCacheDb();
        QHash<QString, QVector<float>> cache;
        if (db.isOpen()) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT vec FROM vecs WHERE key=?"));
            for (const Ch &c : chunks) {
                if (cache.contains(c.key)) continue;
                q.addBindValue(c.key);
                if (q.exec() && q.next()) cache.insert(c.key, blobToVec(q.value(0).toByteArray()));
                q.finish();
            }
        }
        QStringList missKeys, missTexts;
        QSet<QString> seen;
        for (const Ch &c : chunks) {
            if (cache.contains(c.key) || seen.contains(c.key)) continue;
            seen.insert(c.key); missKeys << c.key; missTexts << c.text;
        }

        // 2) Embeber faltantes en batches; guardar en cache.
        int embedded = 0;
        for (int i = 0; i < missTexts.size(); i += 64) {
            const QStringList batch = missTexts.mid(i, 64);
            const QStringList bkeys = missKeys.mid(i, 64);
            QString err;
            const QVector<QVector<float>> vecs = embedTexts(m_serverBaseUrl, batch, &err);
            if (vecs.isEmpty())
                return QStringLiteral("[semantic_search: el server no devolvió embeddings. "
                                      "Levantá un server con --embeddings (o un modelo de embeddings). "
                                      "Detalle: %1]").arg(err);
            if (db.isOpen()) db.transaction();
            for (int j = 0; j < vecs.size() && j < bkeys.size(); ++j) {
                cache.insert(bkeys[j], vecs[j]);
                if (db.isOpen()) {
                    QSqlQuery iq(db);
                    iq.prepare(QStringLiteral("INSERT OR REPLACE INTO vecs(key,dim,vec) VALUES(?,?,?)"));
                    iq.addBindValue(bkeys[j]);
                    iq.addBindValue(vecs[j].size());
                    iq.addBindValue(vecToBlob(vecs[j]));
                    iq.exec();
                }
                ++embedded;
            }
            if (db.isOpen()) db.commit();
        }

        // 3) Embeber la query y rankear por coseno.
        QString qerr;
        const QVector<QVector<float>> qv = embedTexts(m_serverBaseUrl, {query}, &qerr);
        if (qv.isEmpty() || qv[0].isEmpty())
            return QStringLiteral("[semantic_search: no se pudo embeber la query: %1]").arg(qerr);
        const QVector<float> &qvec = qv[0];

        QVector<QPair<float, int>> scored;
        for (int i = 0; i < chunks.size(); ++i) {
            const QVector<float> v = cache.value(chunks[i].key);
            if (v.isEmpty()) continue;
            scored.append({cosineSim(qvec, v), i});
        }
        std::sort(scored.begin(), scored.end(), [](auto &a, auto &b) { return a.first > b.first; });

        QStringList out;
        for (int i = 0; i < scored.size() && out.size() < k; ++i) {
            const Ch &c = chunks[scored[i].second];
            out << QStringLiteral("%1:%2  (sim %3)\n%4")
                       .arg(c.rel).arg(c.line).arg(scored[i].first, 0, 'f', 3)
                       .arg(c.text.left(600));
        }
        if (out.isEmpty()) return QStringLiteral("[sin resultados semánticos]");
        if (ok) *ok = true;
        QString header = QStringLiteral("[%1 chunks · %2 embebidos nuevos%3]\n\n")
                             .arg(chunks.size()).arg(embedded)
                             .arg(truncated ? QStringLiteral(" · TRUNCADO a 800") : QString());
        return header + out.join(QStringLiteral("\n\n──────\n"));
    }
    if (name == QLatin1String("hybrid_search")) {
        // RAG HÍBRIDO: fusiona BM25 (keywords) + vectorial (embeddings) por
        // Reciprocal Rank Fusion, y RE-RANKEA el top con /rerank si el server lo
        // soporta. Es la mejor recuperación disponible: combiná esto antes de
        // razonar sobre el repo. Cae a fusión sin reranker si no hay endpoint.
        const QString query = args.value(QStringLiteral("query")).toString().trimmed();
        if (query.isEmpty()) return QStringLiteral("[query vacía]");
        int k = args.value(QStringLiteral("k")).toInt();
        if (k <= 0) k = 6;
        k = qBound(1, k, 15);
        const QString rootAbs = resolve(args.value(QStringLiteral("path")).toString());
        if (!inProject(rootAbs)) return QStringLiteral("[ruta fuera del proyecto]");

        // Términos de la query para BM25.
        QStringList terms;
        for (const QString &t : query.toLower().split(
                 QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}_]+")), Qt::SkipEmptyParts))
            if (t.size() >= 2 && !terms.contains(t)) terms << t;

        struct Ch { QString rel; int line; QString key; QString text; double bm25; };
        QVector<Ch> chunks;
        const int chunkLines = 40, maxChunks = 800;
        QStringList files;
        collectFiles(rootAbs, files, 8000);
        bool truncated = false;
        for (const QString &fp : files) {
            if (chunks.size() >= maxChunks) { truncated = true; break; }
            QFileInfo fi(fp);
            if (fi.size() > 1024 * 1024) continue;
            QFile f(fp);
            if (!f.open(QIODevice::ReadOnly)) continue;
            const QByteArray raw = f.read(1024 * 1024);
            if (raw.contains('\0')) continue;
            const QStringList lines = QString::fromUtf8(raw).split(QLatin1Char('\n'));
            const QString rel = base.relativeFilePath(fp);
            for (int start = 0; start < lines.size() && chunks.size() < maxChunks; start += chunkLines) {
                const QString text = lines.mid(start, chunkLines).join(QLatin1Char('\n')).trimmed();
                if (text.size() < 16) continue;
                const QString low = text.toLower();
                double bm = 0; int distinct = 0;
                for (const QString &t : terms) {
                    int tf = low.count(t);
                    if (tf > 0) { distinct++; bm += tf / (tf + 1.5); }
                }
                if (distinct > 0) {
                    bm *= (1.0 + 0.5 * (distinct - 1));
                    bm /= (1.0 + text.size() / 4000.0);
                }
                const QString key = QString::fromLatin1(
                    QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Md5).toHex());
                chunks.append({rel, start + 1, key, text, bm});
            }
        }
        if (chunks.isEmpty()) return QStringLiteral("[no hay archivos de texto para indexar]");

        // Ranking BM25.
        QVector<int> byBm25(chunks.size());
        for (int i = 0; i < chunks.size(); ++i) byBm25[i] = i;
        std::sort(byBm25.begin(), byBm25.end(),
                  [&](int a, int b) { return chunks[a].bm25 > chunks[b].bm25; });

        // Ranking vectorial (si hay server con embeddings). Reusa el cache SQLite.
        QVector<int> byVec;
        QString vecErr;
        if (!m_serverBaseUrl.isEmpty()) {
            QSqlDatabase db = embedCacheDb();
            QHash<QString, QVector<float>> cache;
            if (db.isOpen()) {
                QSqlQuery q(db);
                q.prepare(QStringLiteral("SELECT vec FROM vecs WHERE key=?"));
                for (const Ch &c : chunks) {
                    if (cache.contains(c.key)) continue;
                    q.addBindValue(c.key);
                    if (q.exec() && q.next()) cache.insert(c.key, blobToVec(q.value(0).toByteArray()));
                    q.finish();
                }
            }
            QStringList missKeys, missTexts; QSet<QString> seen;
            for (const Ch &c : chunks) {
                if (cache.contains(c.key) || seen.contains(c.key)) continue;
                seen.insert(c.key); missKeys << c.key; missTexts << c.text;
            }
            bool embedOk = true;
            for (int i = 0; i < missTexts.size() && embedOk; i += 64) {
                const QStringList batch = missTexts.mid(i, 64);
                const QStringList bkeys = missKeys.mid(i, 64);
                const QVector<QVector<float>> vecs = embedTexts(m_serverBaseUrl, batch, &vecErr);
                if (vecs.isEmpty()) { embedOk = false; break; }
                if (db.isOpen()) db.transaction();
                for (int j = 0; j < vecs.size() && j < bkeys.size(); ++j) {
                    cache.insert(bkeys[j], vecs[j]);
                    if (db.isOpen()) {
                        QSqlQuery iq(db);
                        iq.prepare(QStringLiteral("INSERT OR REPLACE INTO vecs(key,dim,vec) VALUES(?,?,?)"));
                        iq.addBindValue(bkeys[j]); iq.addBindValue(vecs[j].size());
                        iq.addBindValue(vecToBlob(vecs[j])); iq.exec();
                    }
                }
                if (db.isOpen()) db.commit();
            }
            if (embedOk) {
                QString qerr;
                const QVector<QVector<float>> qv = embedTexts(m_serverBaseUrl, {query}, &qerr);
                if (!qv.isEmpty() && !qv[0].isEmpty()) {
                    QVector<QPair<float, int>> scored;
                    for (int i = 0; i < chunks.size(); ++i) {
                        const QVector<float> v = cache.value(chunks[i].key);
                        if (!v.isEmpty()) scored.append({cosineSim(qv[0], v), i});
                    }
                    std::sort(scored.begin(), scored.end(),
                              [](auto &a, auto &b) { return a.first > b.first; });
                    for (const auto &p : scored) byVec.append(p.second);
                }
            }
        }

        // Reciprocal Rank Fusion (k0=60). Si no hubo vectorial, queda BM25 puro.
        const double k0 = 60.0;
        QHash<int, double> rrf;
        for (int r = 0; r < byBm25.size(); ++r) rrf[byBm25[r]] += 1.0 / (k0 + r + 1);
        for (int r = 0; r < byVec.size(); ++r)  rrf[byVec[r]]  += 1.0 / (k0 + r + 1);
        QVector<int> fused;
        fused.reserve(rrf.size());
        for (auto it = rrf.cbegin(); it != rrf.cend(); ++it) fused.append(it.key());
        std::sort(fused.begin(), fused.end(),
                  [&](int a, int b) { return rrf[a] > rrf[b]; });

        // Re-rank del top (hasta 30) con el reranker del server, si está.
        const int candN = qMin(fused.size(), 30);
        QVector<int> finalOrder = fused;
        QString rerankNote = byVec.isEmpty()
            ? QStringLiteral("BM25 (sin embeddings)") : QStringLiteral("BM25+vector RRF");
        if (candN > 1 && !m_serverBaseUrl.isEmpty()) {
            QStringList docs;
            for (int i = 0; i < candN; ++i) docs << chunks[fused[i]].text;
            QString rerr;
            const QVector<float> scores = rerankTexts(m_serverBaseUrl, query, docs, &rerr);
            if (scores.size() == candN) {
                QVector<int> idx(candN);
                for (int i = 0; i < candN; ++i) idx[i] = i;
                std::sort(idx.begin(), idx.end(),
                          [&](int a, int b) { return scores[a] > scores[b]; });
                finalOrder.clear();
                for (int i = 0; i < candN; ++i) finalOrder.append(fused[idx[i]]);
                for (int i = candN; i < fused.size(); ++i) finalOrder.append(fused[i]);
                rerankNote += QStringLiteral(" + rerank");
            }
        }

        // Empaquetado por presupuesto de tokens (estilo archex): si token_budget>0
        // se llena hasta el presupuesto (≈ chars/4) en vez de un k fijo. Devolver
        // contexto pre-presupuestado evita reventar la ventana del modelo local.
        const int tokenBudget = args.value(QStringLiteral("token_budget")).toInt();
        QStringList outL;
        QStringList outFiles;            // archivos ya incluidos (para el dep-graph)
        int usedTok = 0;
        for (int i = 0; i < finalOrder.size(); ++i) {
            const Ch &c = chunks[finalOrder[i]];
            const QString seg = c.text.left(600);
            const int tok = seg.size() / 4 + 8;
            if (tokenBudget > 0) {
                if (!outL.isEmpty() && usedTok + tok > tokenBudget) break;
                usedTok += tok;
            } else if (outL.size() >= k) {
                break;
            }
            outL << QStringLiteral("%1:%2\n%3").arg(c.rel).arg(c.line).arg(seg);
            if (!outFiles.contains(c.rel)) outFiles << c.rel;
        }

        // Expansión por dep-graph: a partir de los archivos en el resultado, mirar
        // qué módulos importan/incluyen y listar los vecinos del repo que casan por
        // nombre. Es "provenance" barata: el modelo ve qué archivos están a un salto.
        QString graphFooter;
        if (args.value(QStringLiteral("expand_graph")).toBool(true) && !outFiles.isEmpty()) {
            // Índice basename(sin ext)→rel de todos los archivos colectados.
            QHash<QString, QString> byBase;
            for (const QString &fp : files) {
                const QString rel = base.relativeFilePath(fp);
                const QString b = QFileInfo(rel).completeBaseName().toLower();
                if (!b.isEmpty() && !byBase.contains(b)) byBase.insert(b, rel);
            }
            QSet<QString> already(outFiles.cbegin(), outFiles.cend());
            QStringList neighbors;
            for (const QString &rel : outFiles) {
                QFile nf(base.absoluteFilePath(rel));
                if (!nf.open(QIODevice::ReadOnly)) continue;
                const QString full = QString::fromUtf8(nf.read(64 * 1024));
                for (const QString &refb : extractImportRefs(full)) {
                    const QString nrel = byBase.value(refb);
                    if (!nrel.isEmpty() && !already.contains(nrel)
                        && !neighbors.contains(nrel)) {
                        neighbors << nrel;
                        if (neighbors.size() >= 12) break;
                    }
                }
                if (neighbors.size() >= 12) break;
            }
            if (!neighbors.isEmpty())
                graphFooter = QStringLiteral("\n\n══════\nArchivos relacionados (dep-graph): ")
                                  + neighbors.join(QStringLiteral(", "));
        }

        if (ok) *ok = true;
        const QString header = QStringLiteral("[%1 chunks · %2%3%4]\n\n")
            .arg(chunks.size()).arg(rerankNote)
            .arg(truncated ? QStringLiteral(" · TRUNCADO a 800") : QString())
            .arg(tokenBudget > 0 ? QStringLiteral(" · ~%1 tok").arg(usedTok) : QString());
        return header + outL.join(QStringLiteral("\n\n──────\n")) + graphFooter;
    }
    if (name == QLatin1String("verify_claims")) {
        // Anti-alucinación: por cada afirmación, busca evidencia en el proyecto y
        // en la memoria, y la etiqueta. NO reescribe el informe: devuelve un mapa
        // de respaldo para que el modelo redacte con cautela citando la fuente.
        QStringList claims;
        const QJsonValue cv = args.value(QStringLiteral("claims"));
        if (cv.isArray()) {
            for (const QJsonValue &v : cv.toArray())
                if (v.isString() && !v.toString().trimmed().isEmpty())
                    claims << v.toString().trimmed();
        } else {
            for (const QString &ln : cv.toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts))
                if (!ln.trimmed().isEmpty()) claims << ln.trimmed();
        }
        if (claims.isEmpty()) return QStringLiteral("[verify_claims: 'claims' vacío]");
        const QString rootAbs = resolve(args.value(QStringLiteral("path")).toString());
        if (!inProject(rootAbs)) return QStringLiteral("[ruta fuera del proyecto]");

        // Corpus: archivos de texto del proyecto + memoria estructurada.
        QStringList files;
        collectFiles(rootAbs, files, 8000);
        const QString memAll = MemoryStore::recall(cwd, QString(), QString(), 30);

        QStringList report;
        for (const QString &claim : claims) {
            QStringList terms;
            for (const QString &t : claim.toLower().split(
                     QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}_]+")), Qt::SkipEmptyParts))
                if (t.size() >= 3 && !terms.contains(t)) terms << t;
            if (terms.isEmpty()) { report << QStringLiteral("[NO ACREDITADO] %1").arg(claim); continue; }

            // Buscar la mejor cobertura de términos en un único fragmento.
            double best = 0; QString where;
            auto scan = [&](const QString &rel, const QString &text) {
                const QString low = text.toLower();
                int hit = 0;
                for (const QString &t : terms) if (low.contains(t)) ++hit;
                const double cov = double(hit) / terms.size();
                if (cov > best) { best = cov; where = rel; }
            };
            if (!memAll.isEmpty()) scan(QStringLiteral("memoria"), memAll);
            for (const QString &fp : files) {
                if (best >= 0.99) break;
                QFileInfo fi(fp);
                if (fi.size() > 1024 * 1024) continue;
                QFile f(fp);
                if (!f.open(QIODevice::ReadOnly)) continue;
                const QByteArray raw = f.read(1024 * 1024);
                if (raw.contains('\0')) continue;
                scan(base.relativeFilePath(fp), QString::fromUtf8(raw));
            }

            QString tag;
            if (best >= 0.8)      tag = QStringLiteral("[ACREDITADO en %1]").arg(where);
            else if (best >= 0.4) tag = QStringLiteral("[INFERIDO · parcial en %1]").arg(where);
            else                  tag = QStringLiteral("[NO ACREDITADO]");
            report << QStringLiteral("%1 %2  (cobertura %3)")
                          .arg(tag, claim).arg(best, 0, 'f', 2);
        }
        if (ok) *ok = true;
        return QStringLiteral("Verificación de evidencia (etiquetá las afirmaciones del "
                              "informe según esto; lo no acreditado va como hipótesis):\n\n")
               + report.join(QLatin1Char('\n'));
    }
    if (name == QLatin1String("grep")) {
        const QString pattern = args.value(QStringLiteral("pattern")).toString();
        const QString sub = args.value(QStringLiteral("path")).toString();
        const QString rootAbs = resolve(sub);
        if (!inProject(rootAbs)) return QStringLiteral("[ruta fuera del proyecto]");
        const QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
        if (!re.isValid())
            return QStringLiteral("[regex inválida: %1]").arg(re.errorString());
        QStringList files;
        collectFiles(rootAbs, files, 8000);
        QStringList hits;
        for (const QString &fp : files) {
            if (hits.size() >= 100) break;
            QFile f(fp);
            if (!f.open(QIODevice::ReadOnly)) continue;
            const QByteArray raw = f.read(512 * 1024);
            if (raw.contains('\0')) continue;   // binario
            const QStringList lines = QString::fromUtf8(raw).split(QLatin1Char('\n'));
            for (int i = 0; i < lines.size(); ++i) {
                if (re.match(lines[i]).hasMatch()) {
                    hits << QStringLiteral("%1:%2: %3")
                            .arg(base.relativeFilePath(fp)).arg(i + 1).arg(lines[i].trimmed());
                    if (hits.size() >= 100) break;
                }
            }
        }
        if (ok) *ok = true;
        return hits.isEmpty() ? QStringLiteral("[sin coincidencias]") : hits.join(QLatin1Char('\n'));
    }
    if (name == QLatin1String("glob")) {
        const QString pattern = args.value(QStringLiteral("pattern")).toString();
        const QString sub = args.value(QStringLiteral("path")).toString();
        const QString rootAbs = resolve(sub);
        if (!inProject(rootAbs)) return QStringLiteral("[ruta fuera del proyecto]");
        const QRegularExpression re = globToRegex(pattern);
        // Si el patrón no tiene '/', matchea contra el nombre de archivo; si tiene,
        // contra la ruta relativa (con '/').
        const bool matchPath = pattern.contains(QLatin1Char('/'));
        QStringList files;
        collectFiles(rootAbs, files, 20000);
        QStringList matches;
        for (const QString &fp : files) {
            const QString rel = base.relativeFilePath(fp);
            const QString subject = matchPath ? rel : QFileInfo(fp).fileName();
            if (re.match(subject).hasMatch()) {
                matches << rel;
                if (matches.size() >= 300) break;
            }
        }
        matches.sort();
        if (ok) *ok = true;
        return matches.isEmpty() ? QStringLiteral("[sin coincidencias]")
                                 : matches.join(QLatin1Char('\n'));
    }
    if (name == QLatin1String("write_file")) {
        const QString rel = args.value(QStringLiteral("path")).toString();
        const QString abs = resolve(rel);
        if (!inProject(abs)) return QStringLiteral("[ruta fuera del proyecto]");

        QFile prev(abs);
        const bool existed = prev.exists();
        QByteArray oldContent;
        if (existed && prev.open(QIODevice::ReadOnly)) { oldContent = prev.read(4 * 1024 * 1024); prev.close(); }

        QDir().mkpath(QFileInfo(abs).absolutePath());
        QFile f(abs);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return QStringLiteral("[no se pudo escribir: %1]").arg(abs);
        const QByteArray data = args.value(QStringLiteral("content")).toString().toUtf8();
        f.write(data);
        f.close();
        if (ok) *ok = true;

        // Metadata para que el main arme el snapshot (revert) y el mensaje diff.
        out[QStringLiteral("isWrite")]      = true;
        out[QStringLiteral("relPath")]      = base.relativeFilePath(abs);
        out[QStringLiteral("absPath")]      = abs;
        out[QStringLiteral("existed")]      = existed;
        out[QStringLiteral("oldContentB64")] = QString::fromLatin1(oldContent.toBase64());
        out[QStringLiteral("diff")] = LlamaAgentBackend::makeDiff(QString::fromUtf8(oldContent),
                                                              QString::fromUtf8(data));
        return QStringLiteral("[escrito %1 bytes en %2]").arg(data.size()).arg(rel);
    }
    if (name == QLatin1String("edit_file")) {
        const QString rel = args.value(QStringLiteral("path")).toString();
        const QString abs = resolve(rel);
        if (!inProject(abs)) return QStringLiteral("[ruta fuera del proyecto]");
        QFile prev(abs);
        if (!prev.exists())
            return QStringLiteral("[no existe: %1 — usá write_file para crearlo]").arg(rel);
        if (!prev.open(QIODevice::ReadOnly))
            return QStringLiteral("[no se pudo abrir: %1]").arg(rel);
        const QByteArray oldContent = prev.read(8 * 1024 * 1024);
        prev.close();
        const QString oldText = QString::fromUtf8(oldContent);

        const QString oldS = args.value(QStringLiteral("old_string")).toString();
        const QString newS = args.value(QStringLiteral("new_string")).toString();
        const bool replaceAll = args.value(QStringLiteral("replace_all")).toBool();
        if (oldS.isEmpty())
            return QStringLiteral("[old_string vacío: especificá el texto exacto a reemplazar]");

        const int occurrences = oldText.count(oldS);
        if (occurrences == 0)
            return QStringLiteral("[no se encontró old_string en %1. Copiá el texto EXACTO "
                                  "(con indentación) o leé el archivo primero.]").arg(rel);
        if (occurrences > 1 && !replaceAll)
            return QStringLiteral("[old_string aparece %1 veces en %2: agregá más contexto "
                                  "para que sea único, o usá replace_all=true.]")
                       .arg(occurrences).arg(rel);

        QString newText = oldText;
        if (replaceAll) {
            newText.replace(oldS, newS);
        } else {
            const int idx = oldText.indexOf(oldS);
            newText = oldText.left(idx) + newS + oldText.mid(idx + oldS.size());
        }

        QFile f(abs);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return QStringLiteral("[no se pudo escribir: %1]").arg(rel);
        const QByteArray data = newText.toUtf8();
        f.write(data);
        f.close();
        if (ok) *ok = true;

        // Misma metadata que write_file → reusa tarjeta de diff + snapshot/revert.
        out[QStringLiteral("isWrite")]       = true;
        out[QStringLiteral("relPath")]       = base.relativeFilePath(abs);
        out[QStringLiteral("absPath")]       = abs;
        out[QStringLiteral("existed")]       = true;
        out[QStringLiteral("oldContentB64")] = QString::fromLatin1(oldContent.toBase64());
        out[QStringLiteral("diff")] = LlamaAgentBackend::makeDiff(oldText, newText);
        return QStringLiteral("[editado %1 reemplazo(s) en %2]")
                   .arg(replaceAll ? occurrences : 1).arg(rel);
    }
    if (name == QLatin1String("memory")) {
        // Memoria PERSISTENTE por capas. Hechos atómicos con metadata en
        // .llamacode/memory.jsonl (scope/type/confidence + recall por relevancia).
        // Mantiene el viejo memory.md (append-only) para back-compat: 'recall' sin
        // query devuelve ambos.
        const QString action = args.value(QStringLiteral("action")).toString().trimmed().toLower();
        const QString scope = args.value(QStringLiteral("scope")).toString();
        if (action == QLatin1String("save")) {
            const QString content = args.value(QStringLiteral("content")).toString().trimmed();
            if (content.isEmpty()) return QStringLiteral("[memory save: 'content' vacío]");
            const QString type = args.value(QStringLiteral("type")).toString();
            const double conf = args.value(QStringLiteral("confidence")).toDouble();
            const QString source = args.value(QStringLiteral("source")).toString();
            const QString res = MemoryStore::save(cwd, content, scope, type, conf, source);
            // Espejo en memory.md para inspección humana / compatibilidad.
            const QString mdPath = LlamaAgentBackend::memoryFilePath(cwd);
            QDir().mkpath(QFileInfo(mdPath).absolutePath());
            QFile md(mdPath);
            if (md.open(QIODevice::Append | QIODevice::Text)) {
                md.write((QStringLiteral("- (%1) %2\n")
                              .arg(QDateTime::currentDateTime().toString(Qt::ISODate), content)).toUtf8());
                md.close();
            }
            if (ok) *ok = true;
            return res;
        }
        if (action == QLatin1String("forget")) {
            // Olvido: marca stale (default) o borra hechos que matchean query/scope.
            const QString query = args.value(QStringLiteral("query")).toString();
            const QString mode = args.value(QStringLiteral("mode")).toString();
            const QString res = MemoryStore::forget(cwd, query, scope, mode);
            if (ok) *ok = true;
            return res;
        }
        if (action == QLatin1String("prune")) {
            // Poda anti-bloat: evicta hechos de bajo valor / redundantes.
            const int maxKeep = args.value(QStringLiteral("max_keep")).toInt();
            const QString mode = args.value(QStringLiteral("mode")).toString();
            const bool dryRun = args.value(QStringLiteral("dry_run")).toBool(false);
            const QString res = MemoryStore::prune(cwd, scope, maxKeep, mode, dryRun);
            if (ok) *ok = true;
            return res;
        }
        // recall (default): hechos estructurados (rankeados por query/scope si hay).
        const QString query = args.value(QStringLiteral("query")).toString();
        int k = args.value(QStringLiteral("k")).toInt();
        const QString facts = MemoryStore::recall(cwd, query, scope, k);
        if (ok) *ok = true;
        // Sin query ni scope: anexar el memory.md crudo para no perder lo viejo.
        if (query.trimmed().isEmpty() && scope.trimmed().isEmpty()) {
            QFile f(LlamaAgentBackend::memoryFilePath(cwd));
            if (f.open(QIODevice::ReadOnly)) {
                const QByteArray raw = f.read(256 * 1024);
                if (!raw.isEmpty())
                    return facts + QStringLiteral("\n\n── memory.md (legacy) ──\n")
                           + QString::fromUtf8(raw);
            }
        }
        return facts;
    }
    if (name == QLatin1String("graph")) {
        // KNOWLEDGE GRAPH: entidades + relaciones tipadas en .llamacode/graph.jsonl.
        // action='link' (default) conecta subj-[pred]->obj; 'add_entity' crea una
        // entidad; 'query' devuelve el vecindario de una entidad (depth 1|2).
        const QString action = args.value(QStringLiteral("action")).toString().trimmed().toLower();
        if (action == QLatin1String("add_entity")) {
            const QString res = GraphStore::addEntity(
                cwd, args.value(QStringLiteral("name")).toString(),
                args.value(QStringLiteral("etype")).toString());
            if (ok) *ok = true;
            return res;
        }
        if (action == QLatin1String("query")) {
            const QString res = GraphStore::query(
                cwd, args.value(QStringLiteral("name")).toString(),
                args.value(QStringLiteral("depth")).toInt());
            if (ok) *ok = true;
            return res;
        }
        if (action == QLatin1String("decide")) {
            // 'rejected' acepta array de objetos {alt,reason} o de strings sueltos.
            GraphStore::Rejected rejected;
            for (const QJsonValue &v : args.value(QStringLiteral("rejected")).toArray()) {
                if (v.isObject()) {
                    const QJsonObject ro = v.toObject();
                    rejected.append({ro.value(QStringLiteral("alt")).toString(),
                                     ro.value(QStringLiteral("reason")).toString()});
                } else {
                    rejected.append({v.toString(), QString()});
                }
            }
            const QString res = GraphStore::decide(
                cwd, args.value(QStringLiteral("topic")).toString(),
                args.value(QStringLiteral("chosen")).toString(), rejected,
                args.value(QStringLiteral("reason")).toString());
            if (ok) *ok = true;
            return res;
        }
        if (action == QLatin1String("decisions")) {
            const QString res = GraphStore::decisions(
                cwd, args.value(QStringLiteral("topic")).toString());
            if (ok) *ok = true;
            return res;
        }
        // link (default)
        const QString res = GraphStore::link(
            cwd, args.value(QStringLiteral("subj")).toString(),
            args.value(QStringLiteral("pred")).toString(),
            args.value(QStringLiteral("obj")).toString());
        if (ok) *ok = true;
        return res;
    }
    if (name == QLatin1String("ask_teacher")) {
        // Consulta puntual a un modelo MÁS capaz (endpoint OpenAI-compatible aparte).
        // Config por env: LLAMACODE_TEACHER_URL (req), _MODEL, _KEY (opcionales).
        const QString question = args.value(QStringLiteral("question")).toString().trimmed();
        if (question.isEmpty()) return QStringLiteral("[ask_teacher: 'question' vacía]");
        const QString ctxArg = args.value(QStringLiteral("context")).toString();
        // Cadena de fallbacks del perfil tiene prioridad: recorre niveles en orden.
        if (!m_masterChain.isEmpty())
            return runMasterChain(question, ctxArg, cwd, ok);
        // Maestro CLI legacy (claude-code / codex) si el perfil lo configuró.
        if (m_masterKind == QLatin1String("cli"))
            return runMasterCli(m_masterCliName, m_masterCliPath, m_masterApplyEdits,
                                m_masterTimeoutS, question, ctxArg, cwd, ok);
        // Config de UI (setTeacherConfig) tiene prioridad; si está vacía, env vars.
        const QString teacher = !m_teacherUrl.isEmpty()
            ? m_teacherUrl : qEnvironmentVariable("LLAMACODE_TEACHER_URL").trimmed();
        if (teacher.isEmpty())
            return QStringLiteral("[ask_teacher: configurá el endpoint del modelo maestro en "
                                  "Ajustes (o la env LLAMACODE_TEACHER_URL). Endpoint "
                                  "OpenAI-compatible, ej. https://api.openai.com o http://localhost:8081]");
        const QString model = !m_teacherModel.isEmpty() ? m_teacherModel
            : qEnvironmentVariable("LLAMACODE_TEACHER_MODEL", QStringLiteral("default"));
        const QString key = !m_teacherKey.isEmpty()
            ? m_teacherKey : qEnvironmentVariable("LLAMACODE_TEACHER_KEY").trimmed();
        const QString context = args.value(QStringLiteral("context")).toString();

        QString userMsg = question;
        if (!context.isEmpty())
            userMsg = QStringLiteral("Contexto:\n%1\n\nPregunta:\n%2").arg(context, question);
        const QJsonArray msgs{
            QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                        {QStringLiteral("content"), QStringLiteral(
                             "Sos un experto sénior asistiendo a otro agente de código. "
                             "Respondé conciso, correcto y accionable.")}},
            QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                        {QStringLiteral("content"), userMsg}}};
        const QJsonObject payload{
            {QStringLiteral("model"), model},
            {QStringLiteral("messages"), msgs},
            {QStringLiteral("stream"), false}};
        const QUrl url(teacher.endsWith(QLatin1Char('/'))
                           ? teacher + QStringLiteral("v1/chat/completions")
                           : teacher + QStringLiteral("/v1/chat/completions"));
        QString err;
        const QByteArray resp = httpPostJson(url, QJsonDocument(payload).toJson(QJsonDocument::Compact),
                                             &err, 120000, key);
        if (resp.isEmpty())
            return QStringLiteral("[ask_teacher: error consultando al maestro: %1]").arg(err);
        const QJsonObject root = QJsonDocument::fromJson(resp).object();
        const QString answer = root.value(QStringLiteral("choices")).toArray().isEmpty()
            ? QString()
            : root.value(QStringLiteral("choices")).toArray().first().toObject()
                  .value(QStringLiteral("message")).toObject()
                  .value(QStringLiteral("content")).toString();
        if (answer.isEmpty())
            return QStringLiteral("[ask_teacher: respuesta vacía del maestro]");
        if (ok) *ok = true;
        return QStringLiteral("[Respuesta del modelo maestro]\n") + answer;
    }
    // ── Browser teach: listar/reproducir skills grabados ──────────────────
    if (name == QLatin1String("browser_skill_list")) {
        const QStringList skills = BrowserTeach::listSkills();
        if (ok) *ok = true;
        if (skills.isEmpty())
            return QStringLiteral("[sin skills de browser grabados. El usuario los graba "
                                  "en Ajustes → Automatización de browser.]");
        return QStringLiteral("Skills de browser disponibles:\n- ") + skills.join(QStringLiteral("\n- "));
    }
    if (name == QLatin1String("browser_skill_replay")) {
        const QString skill = args.value(QStringLiteral("name")).toString();
        if (skill.trimmed().isEmpty())
            return QStringLiteral("[browser_skill_replay: falta 'name']");
        if (!BrowserTeach::hasSkill(skill))
            return QStringLiteral("[browser_skill_replay: skill no encontrado: %1. Usá "
                                  "browser_skill_list para ver los disponibles.]").arg(skill);
        const QStringList pa = BrowserTeach::replayProgramArgs(skill);
        if (pa.size() < 2)
            return QStringLiteral("[browser_skill_replay: skill inválido: %1]").arg(skill);
        QProcess proc;
        proc.setWorkingDirectory(BrowserTeach::skillsDir());   // resuelve 'playwright' local
        proc.setProcessChannelMode(QProcess::MergedChannels);
        proc.start(pa.first(), pa.mid(1));
        if (!proc.waitForStarted(8000))
            return QStringLiteral("[browser_skill_replay: no se pudo iniciar node. ¿Node instalado?]");
        if (!proc.waitForFinished(180000)) {
            proc.kill(); proc.waitForFinished(2000);
            return QStringLiteral("[browser_skill_replay: timeout (180s) reproduciendo %1]").arg(skill);
        }
        const QString out = QString::fromUtf8(proc.readAll()).trimmed();
        const bool good = proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
        if (ok) *ok = good;
        return QStringLiteral("[browser_skill_replay %1 · exit=%2]\n%3")
                   .arg(skill).arg(proc.exitCode()).arg(out.left(8000));
    }
    if (name == QLatin1String("email_accounts")) {
        if (ok) *ok = true;
        if (m_mailAccounts.isEmpty())
            return QStringLiteral("[sin cuentas de correo configuradas. El usuario las "
                                  "agrega en Configuración → Correo.]");
        QStringList names;
        for (const QVariant &v : m_mailAccounts) {
            const QVariantMap a = v.toMap();
            names << QStringLiteral("%1 <%2>").arg(a.value(QStringLiteral("name")).toString(),
                                                   a.value(QStringLiteral("email")).toString());
        }
        return QStringLiteral("Cuentas de correo:\n- ") + names.join(QStringLiteral("\n- "));
    }

    if (name == QLatin1String("email_send") || name == QLatin1String("email_list")
        || name == QLatin1String("email_read")) {
        if (m_mailAccounts.isEmpty())
            return QStringLiteral("[%1: no hay cuentas de correo. Configuralas en "
                                  "Configuración → Correo.]").arg(name);
        // Resolver cuenta: por 'account' o la primera (default).
        const QString want = args.value(QStringLiteral("account")).toString().trimmed();
        QVariantMap acctMap = m_mailAccounts.first().toMap();
        if (!want.isEmpty()) {
            bool found = false;
            for (const QVariant &v : m_mailAccounts) {
                const QVariantMap a = v.toMap();
                if (a.value(QStringLiteral("name")).toString() == want
                    || a.value(QStringLiteral("email")).toString() == want) {
                    acctMap = a; found = true; break;
                }
            }
            if (!found)
                return QStringLiteral("[%1: cuenta '%2' no encontrada. Usá email_accounts.]")
                           .arg(name, want);
        }
        MailClient::Account acct;
        acct.name        = acctMap.value(QStringLiteral("name")).toString();
        acct.email       = acctMap.value(QStringLiteral("email")).toString();
        acct.displayName = acctMap.value(QStringLiteral("displayName")).toString();
        acct.provider    = acctMap.value(QStringLiteral("provider")).toString();
        acct.smtpHost    = acctMap.value(QStringLiteral("smtpHost")).toString();
        acct.smtpPort    = acctMap.value(QStringLiteral("smtpPort")).toInt();
        acct.smtpSecurity= acctMap.value(QStringLiteral("smtpSecurity")).toString();
        acct.recvProto   = acctMap.value(QStringLiteral("recvProto")).toString();
        acct.recvHost    = acctMap.value(QStringLiteral("recvHost")).toString();
        acct.recvPort    = acctMap.value(QStringLiteral("recvPort")).toInt();
        acct.recvSsl     = acctMap.value(QStringLiteral("recvSsl"), true).toBool();
        acct.user        = acctMap.value(QStringLiteral("user")).toString();
        acct.password    = acctMap.value(QStringLiteral("password")).toString();

        if (name == QLatin1String("email_send")) {
            const QString to = args.value(QStringLiteral("to")).toString().trimmed();
            const QString subject = args.value(QStringLiteral("subject")).toString();
            const QString body = args.value(QStringLiteral("body")).toString();
            const QString cc = args.value(QStringLiteral("cc")).toString();
            if (to.isEmpty()) return QStringLiteral("[email_send: falta 'to']");
            QString err;
            if (MailClient::sendSmtp(acct, to, cc, subject, body, &err)) {
                if (ok) *ok = true;
                return QStringLiteral("[email enviado a %1 · asunto: %2]").arg(to, subject);
            }
            return QStringLiteral("[email_send falló: %1]").arg(err);
        }
        if (name == QLatin1String("email_list")) {
            const QString folder = args.value(QStringLiteral("folder")).toString();
            int limit = args.value(QStringLiteral("limit")).toInt(10);
            const bool unread = args.value(QStringLiteral("unread_only")).toBool();
            QString err;
            const QVariantList msgs = MailClient::fetchInbox(acct, folder, limit, unread, &err);
            if (!err.isEmpty()) return QStringLiteral("[email_list falló: %1]").arg(err);
            if (ok) *ok = true;
            if (msgs.isEmpty()) return QStringLiteral("[sin correos]");
            QString s;
            for (const QVariant &v : msgs) {
                const QVariantMap m = v.toMap();
                s += QStringLiteral("uid:%1 | %2 | de: %3 | %4\n")
                         .arg(m.value(QStringLiteral("uid")).toString(),
                              m.value(QStringLiteral("date")).toString(),
                              m.value(QStringLiteral("from")).toString(),
                              m.value(QStringLiteral("subject")).toString());
            }
            return s.trimmed();
        }
        // email_read
        const QString uid = args.value(QStringLiteral("uid")).toString().trimmed();
        if (uid.isEmpty()) return QStringLiteral("[email_read: falta 'uid' (ver email_list)]");
        const QString folder = args.value(QStringLiteral("folder")).toString();
        QString err;
        const QString body = MailClient::readMessage(acct, folder, uid, &err);
        if (!err.isEmpty()) return QStringLiteral("[email_read falló: %1]").arg(err);
        if (ok) *ok = true;
        return body.left(20000);
    }

    // run_shell se maneja async en executeTool/startShell (no llega acá).
    return QStringLiteral("[tool desconocida: %1]").arg(name);
}

// ───────────────────────────── run_shell async ───────────────────────────
void AgentToolRunner::startShell(const QString &callId, const QString &command,
                                 const QString &cwd, int timeoutS)
{
    // Solo un shell a la vez (el loop ReAct es secuencial). Si quedó uno vivo,
    // matarlo sin emitir (no debería pasar).
    if (m_shellProc) { m_shellProc->kill(); m_shellProc->deleteLater(); m_shellProc = nullptr; }

    m_shellCallId = callId;
    m_shellOut.clear();
    m_shellTimeoutS = timeoutS;
    m_shellClock.start();

    emit toolStarted(QVariantMap{
        {QStringLiteral("callId"), callId},
        {QStringLiteral("name"), QStringLiteral("run_shell")},
        {QStringLiteral("kind"), QStringLiteral("shell")},
        {QStringLiteral("command"), command}});

    m_shellProc = new QProcess(this);   // vive en el hilo worker
    m_shellProc->setWorkingDirectory(cwd);
    m_shellProc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_shellProc, &QProcess::readyRead, this, &AgentToolRunner::onShellReadyRead);
    connect(m_shellProc, &QProcess::finished, this, &AgentToolRunner::onShellFinished);

#ifdef Q_OS_WIN
    // cmd con setNativeArguments: no re-citar args (rompe comillas anidadas).
    m_shellProc->setProgram(QStringLiteral("cmd"));
    m_shellProc->setNativeArguments(QStringLiteral("/c ") + command);
    m_shellProc->start();
#else
    m_shellProc->start(QStringLiteral("sh"), {QStringLiteral("-c"), command});
#endif
    if (!m_shellProc->waitForStarted(5000)) {
        m_shellProc->deleteLater();
        m_shellProc = nullptr;
        const QString cid = m_shellCallId; m_shellCallId.clear();
        emit toolExecuted(QVariantMap{
            {QStringLiteral("callId"), cid}, {QStringLiteral("name"), QStringLiteral("run_shell")},
            {QStringLiteral("result"), QStringLiteral("[no se pudo iniciar el comando]")},
            {QStringLiteral("ok"), false}});
        return;
    }

    if (!m_shellTimer) {
        m_shellTimer = new QTimer(this);
        m_shellTimer->setSingleShot(true);
        connect(m_shellTimer, &QTimer::timeout, this, &AgentToolRunner::onShellTimeout);
    }
    m_shellTimer->start(timeoutS * 1000);
}

void AgentToolRunner::onShellReadyRead()
{
    if (!m_shellProc) return;
    const QByteArray chunk = m_shellProc->readAll();
    if (chunk.isEmpty()) return;
    m_shellOut.append(chunk);
    // Cap de memoria de la salida acumulada (la tarjeta igual recorta).
    if (m_shellOut.size() > 2 * 1024 * 1024)
        m_shellOut = m_shellOut.right(2 * 1024 * 1024);
    emit toolOutputChunk(m_shellCallId, QString::fromUtf8(chunk));
}

void AgentToolRunner::onShellFinished()
{
    finishShell(false, false);
}

void AgentToolRunner::onShellTimeout()
{
    if (!m_shellProc) return;
    m_shellProc->kill();           // dispara finished() → finishShell vía bandera
    finishShell(true, false);
}

void AgentToolRunner::cancelShell()
{
    if (!m_shellProc) return;
    m_shellProc->kill();
    finishShell(false, true);
}

void AgentToolRunner::finishShell(bool timedOut, bool cancelled)
{
    if (!m_shellProc) return;
    if (m_shellTimer) m_shellTimer->stop();

    // Drenar lo que quede y desconectar para no re-entrar (kill emite finished()).
    m_shellOut.append(m_shellProc->readAll());
    m_shellProc->disconnect(this);
    if (m_shellProc->state() != QProcess::NotRunning)
        m_shellProc->waitForFinished(2000);
    const int exitCode = m_shellProc->exitCode();
    m_shellProc->deleteLater();
    m_shellProc = nullptr;

    const QString cid = m_shellCallId;
    m_shellCallId.clear();
    const QString outStr = QString::fromUtf8(m_shellOut).left(64 * 1024);

    QString result;
    bool ok = true;
    if (cancelled) { result = QStringLiteral("[cancelado por el usuario]\n") + outStr; ok = false; }
    else if (timedOut) { result = QStringLiteral("[timeout tras %1s — proceso terminado]\n%2")
                                       .arg(m_shellTimeoutS).arg(outStr); ok = false; }
    else result = QStringLiteral("exit=%1\n%2").arg(exitCode).arg(outStr);

    emit toolExecuted(QVariantMap{
        {QStringLiteral("callId"), cid},
        {QStringLiteral("name"), QStringLiteral("run_shell")},
        {QStringLiteral("result"), result},
        {QStringLiteral("ok"), ok}});
}
