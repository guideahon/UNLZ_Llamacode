#pragma once
#include "ModelRoot.h"
#include "CatalogModel.h"
#include <QList>
#include <QObject>

class GGUFScanner : public QObject
{
    Q_OBJECT
public:
    explicit GGUFScanner(QObject *parent = nullptr);

    // Synchronous scan — call from thread pool
    QList<CatalogModel> scan(const ModelRoot &root);

signals:
    void progress(const QString &rootId, int found);

private:
    static QString inferFamily(const QString &fileName);
    static QString inferQuant(const QString &fileName);
    static bool isVisionCandidate(const QString &fileName);
    static bool isDraftCandidate(const QString &fileName, qint64 sizeBytes);
};
