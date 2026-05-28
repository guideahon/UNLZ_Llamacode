#pragma once
#include <QString>
#include <QDateTime>

struct CatalogModel {
    QString id;
    QString rootId;
    QString absolutePath;
    QString fileName;
    qint64 sizeBytes = 0;
    QDateTime mtime;
    QString familyHint;
    QString quantHint;
    bool isVisionCandidate = false;
    bool isDraftCandidate = false;
    QString sha256;
    bool isAvailable = true;

    QString sizeLabel() const;
};
