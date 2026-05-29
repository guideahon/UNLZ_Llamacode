#include "CapabilityDetector.h"
#include <QProcess>
#include <QRegularExpression>

DetectedCapabilities CapabilityDetector::detect(const QString &binaryPath, int timeoutMs)
{
    QProcess proc;
    proc.setProgram(binaryPath);
    proc.setArguments({"--help"});
    proc.start();

    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        DetectedCapabilities cap;
        cap.error = QStringLiteral("Timeout or failed to start: %1").arg(proc.errorString());
        return cap;
    }

    const QString output = QString::fromUtf8(proc.readAllStandardOutput())
                         + QString::fromUtf8(proc.readAllStandardError());
    return parse(output);
}

DetectedCapabilities CapabilityDetector::parse(const QString &helpOutput)
{
    DetectedCapabilities cap;
    cap.success = true;

    // Strip ANSI escape codes
    static const QRegularExpression ansiRe(R"(\x1B\[[0-9;]*[A-Za-z])");
    QString clean = helpOutput;
    clean.remove(ansiRe);

    // Match flag lines: 0–16 spaces indent, then --flag or -f (but not 17+ spaces = description continuation)
    static const QRegularExpression re(
        R"(^\s{0,16}(-{1,2}[\w][\w-]*(?:,\s*-{1,2}[\w][\w-]*)*))",
        QRegularExpression::MultilineOption
    );

    auto it = re.globalMatch(clean);
    while (it.hasNext()) {
        const auto match = it.next();
        const QStringList parts = match.captured(1).split(QRegularExpression(R"(,\s*)"));
        QString canonical;
        for (const QString &part : parts) {
            const QString flag = part.trimmed();
            if (flag.startsWith("--")) { canonical = flag; break; }
        }
        if (canonical.isEmpty() && !parts.isEmpty())
            canonical = parts.first().trimmed();

        if (!cap.flags.contains(canonical))
            cap.flags.append(canonical);

        for (const QString &part : parts) {
            const QString flag = part.trimmed();
            if (flag != canonical && !flag.isEmpty())
                cap.flagAliases[flag] = canonical;
        }
    }

    // Normalize known renames: add legacy aliases so LlamaCode internals keep working
    // --draft-model was renamed to --spec-draft-model in newer forks
    if (cap.flags.contains("--spec-draft-model") && !cap.flags.contains("--draft-model"))
        cap.flagAliases["--draft-model"] = "--spec-draft-model";

    return cap;
}
