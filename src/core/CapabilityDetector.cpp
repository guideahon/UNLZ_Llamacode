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

    // Match lines with flags: leading whitespace, then --flag or -f
    static const QRegularExpression re(
        R"(^\s{1,8}(-{1,2}[\w][\w-]*(?:,\s*-{1,2}[\w][\w-]*)*))",
        QRegularExpression::MultilineOption
    );

    auto it = re.globalMatch(helpOutput);
    while (it.hasNext()) {
        const auto match = it.next();
        // May be "  --long-flag, -s" — split on comma
        const QStringList parts = match.captured(1).split(QRegularExpression(R"(,\s*)"));
        QString canonical;
        for (const QString &part : parts) {
            const QString flag = part.trimmed();
            if (flag.startsWith("--")) {
                canonical = flag;
                break;
            }
        }
        if (canonical.isEmpty() && !parts.isEmpty())
            canonical = parts.first().trimmed();

        if (!cap.flags.contains(canonical))
            cap.flags.append(canonical);

        // Build alias map: short flag -> canonical
        for (const QString &part : parts) {
            const QString flag = part.trimmed();
            if (flag != canonical && !flag.isEmpty())
                cap.flagAliases[flag] = canonical;
        }
    }

    return cap;
}
