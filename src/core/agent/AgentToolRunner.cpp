#include "AgentToolRunner.h"
#include "McpClient.h"
#include "LlamaAgentBackend.h"   // LlamaAgentBackend::makeDiff (static)

#include <QCryptographicHash>
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
    QString rx = QRegularExpression::escape(glob);
    rx.replace(QStringLiteral("\\*\\*"), QStringLiteral("\x01"));   // marcador temporal
    rx.replace(QStringLiteral("\\*"), QStringLiteral("[^/]*"));
    rx.replace(QStringLiteral("\x01"), QStringLiteral(".*"));
    rx.replace(QStringLiteral("\\?"), QStringLiteral("[^/]"));
    return QRegularExpression(QStringLiteral("^") + rx + QStringLiteral("$"));
}

AgentToolRunner::AgentToolRunner(QObject *parent) : QObject(parent) {}
AgentToolRunner::~AgentToolRunner() { shutdown(); }

void AgentToolRunner::setConfined(bool confined) { m_confined = confined; }

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
        QNetworkAccessManager nam;
        QNetworkRequest req((QUrl(url)));
        req.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("LlamaCode/0.1"));
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply *reply = nam.get(req);
        QEventLoop loop;
        QTimer killer; killer.setSingleShot(true);
        QObject::connect(&killer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        killer.start(20000);   // 20s
        loop.exec();
        if (reply->isRunning()) { reply->abort(); reply->deleteLater();
            return QStringLiteral("[timeout al descargar %1]").arg(url); }
        if (reply->error() != QNetworkReply::NoError) {
            const QString e = reply->errorString(); reply->deleteLater();
            return QStringLiteral("[error de red: %1]").arg(e);
        }
        QByteArray body = reply->readAll();
        reply->deleteLater();
        QString text = QString::fromUtf8(body);
        // Limpieza HTML cruda → texto: sacar script/style, tags, colapsar espacios.
        text.remove(QRegularExpression(QStringLiteral("(?is)<(script|style)[^>]*>.*?</\\1>")));
        text.remove(QRegularExpression(QStringLiteral("(?s)<[^>]+>")));
        text.replace(QRegularExpression(QStringLiteral("&nbsp;")), QStringLiteral(" "));
        text.replace(QRegularExpression(QStringLiteral("&amp;")), QStringLiteral("&"));
        text.replace(QRegularExpression(QStringLiteral("&lt;")), QStringLiteral("<"));
        text.replace(QRegularExpression(QStringLiteral("&gt;")), QStringLiteral(">"));
        text.replace(QRegularExpression(QStringLiteral("[ \t]+")), QStringLiteral(" "));
        text.replace(QRegularExpression(QStringLiteral("\n[ \t]*(?:\n[ \t]*)+")), QStringLiteral("\n\n"));
        text = text.trimmed();
        if (ok) *ok = true;
        return text.isEmpty() ? QStringLiteral("[respuesta vacía]") : text.left(48 * 1024);
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
