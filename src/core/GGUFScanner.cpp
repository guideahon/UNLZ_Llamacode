#include "GGUFScanner.h"
#include <QDirIterator>
#include <QFileInfo>
#include <QUuid>
#include <QRegularExpression>

GGUFScanner::GGUFScanner(QObject *parent) : QObject(parent) {}

QList<CatalogModel> GGUFScanner::scan(const ModelRoot &root)
{
    QList<CatalogModel> results;

    QDirIterator it(root.path, {"*.gguf", "*.GGUF"},
                    QDir::Files, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        const QString filePath = it.next();
        const QFileInfo info(filePath);

        CatalogModel m;
        m.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m.rootId = root.id;
        m.absolutePath = filePath;
        m.fileName = info.fileName();
        m.sizeBytes = info.size();
        m.mtime = info.lastModified();
        m.familyHint = inferFamily(info.fileName());
        m.quantHint = inferQuant(info.fileName());
        m.isVisionCandidate = isVisionCandidate(info.fileName());
        m.isDraftCandidate = isDraftCandidate(info.fileName(), info.size());
        m.isAvailable = true;

        results.append(m);
        emit progress(root.id, results.size());
    }

    return results;
}

QString GGUFScanner::inferFamily(const QString &fileName)
{
    const QString lower = fileName.toLower();
    const QStringList families = {
        "deepseek", "codellama", "starcoder", "mixtral", "mistral",
        "vicuna", "wizard", "llama", "falcon", "gemma", "qwen",
        "solar", "orca", "yi", "phi"
    };
    for (const QString &f : families)
        if (lower.contains(f)) return f;
    return "unknown";
}

QString GGUFScanner::inferQuant(const QString &fileName)
{
    static const QRegularExpression re(
        R"((IQ[1-4]_[A-Z_]+|Q[1-8]_K_[SML]|Q[1-8]_[0-9]|BF16|F16|F32))",
        QRegularExpression::CaseInsensitiveOption
    );
    const auto match = re.match(fileName);
    return match.hasMatch() ? match.captured(1).toUpper() : "unknown";
}

bool GGUFScanner::isVisionCandidate(const QString &fileName)
{
    const QString lower = fileName.toLower();
    return lower.contains("mmproj") || lower.contains("vision")
           || lower.contains("clip") || lower.contains("llava");
}

bool GGUFScanner::isDraftCandidate(const QString &fileName, qint64 sizeBytes)
{
    const QString lower = fileName.toLower();
    if (lower.contains("draft") || lower.contains("small")) return true;
    // Heuristic: models under 2GB are draft candidates
    return sizeBytes > 0 && sizeBytes < 2LL * 1024 * 1024 * 1024;
}
