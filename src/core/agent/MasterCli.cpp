#include "MasterCli.h"
#include <QProcess>
#include <QStandardPaths>
#include <QFileInfo>

QString MasterCli::label(const QString &name)
{
    if (name == QLatin1String("claude")) return QStringLiteral("Claude Code");
    if (name == QLatin1String("codex"))  return QStringLiteral("Codex CLI");
    return name;
}

QString MasterCli::installCommand(const QString &name)
{
    if (name == QLatin1String("claude"))
        return QStringLiteral("npm install -g @anthropic-ai/claude-code");
    if (name == QLatin1String("codex"))
        return QStringLiteral("npm install -g @openai/codex");
    return QString();
}

QString MasterCli::resolvePath(const QString &name)
{
    return status(name).value(QStringLiteral("path")).toString();
}

QVariantMap MasterCli::status(const QString &name, bool force)
{
    if (!force && m_cache.contains(name))
        return m_cache.value(name);

    QVariantMap out;
    out[QStringLiteral("name")]           = name;
    out[QStringLiteral("label")]          = label(name);
    out[QStringLiteral("installCommand")] = installCommand(name);
    out[QStringLiteral("installed")]      = false;
    out[QStringLiteral("version")]        = QString();
    out[QStringLiteral("path")]           = QString();

    // Resolver binario en PATH (Windows agrega .cmd/.exe automáticamente).
    QString exe = QStandardPaths::findExecutable(name);
    if (exe.isEmpty()) {
        m_cache.insert(name, out);
        return out;
    }

    // Probar `--version` con timeout corto; capturar primera línea no vacía.
    QProcess p;
    p.start(exe, {QStringLiteral("--version")});
    QString version;
    if (p.waitForStarted(3000) && p.waitForFinished(8000)) {
        const QString raw = QString::fromUtf8(p.readAllStandardOutput())
                          + QString::fromUtf8(p.readAllStandardError());
        for (const QString &line : raw.split(QLatin1Char('\n'))) {
            const QString t = line.trimmed();
            if (!t.isEmpty()) { version = t; break; }
        }
    }

    out[QStringLiteral("installed")] = true;
    out[QStringLiteral("path")]      = exe;
    out[QStringLiteral("version")]   = version.isEmpty()
        ? QStringLiteral("instalado") : version;

    m_cache.insert(name, out);
    return out;
}
