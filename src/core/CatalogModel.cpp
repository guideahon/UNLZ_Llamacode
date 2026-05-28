#include "CatalogModel.h"

QString CatalogModel::sizeLabel() const
{
    if (sizeBytes >= 1024LL * 1024 * 1024)
        return QString::number(sizeBytes / (1024.0 * 1024 * 1024), 'f', 1) + " GB";
    if (sizeBytes >= 1024 * 1024)
        return QString::number(sizeBytes / (1024.0 * 1024), 'f', 1) + " MB";
    return QString::number(sizeBytes / 1024.0, 'f', 1) + " KB";
}
