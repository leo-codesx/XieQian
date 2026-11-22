#include "storage_recovery.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace xieqian::storage {

Health quickCheck(const QString &databasePath)
{
    Health health;

    const QString connection = QStringLiteral("xq_health_%1")
                                   .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(databasePath);
        database.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=3000"));

        if (!database.open()) {
            health.detail = QStringLiteral("open_failed: %1").arg(database.lastError().text());
        } else {
            QSqlQuery query(database);
            if (!query.exec(QStringLiteral("PRAGMA quick_check"))) {
                health.detail =
                    QStringLiteral("quick_check_failed: %1").arg(query.lastError().text());
            } else if (!query.next()) {
                health.detail = QStringLiteral("quick_check_empty");
            } else {
                const QString value = query.value(0).toString().trimmed();
                if (value.compare(QStringLiteral("ok"), Qt::CaseInsensitive) == 0) {
                    health.ok = true;
                } else {
                    health.detail = QStringLiteral("quick_check: %1").arg(value.left(200));
                }
            }
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);

    return health;
}

Isolation isolateCorrupted(const QString &databasePath, const QString &timestamp)
{
    Isolation result;

    const QFileInfo info(databasePath);
    result.target = info.absoluteDir().filePath(QStringLiteral("data_corrupted_%1.db").arg(timestamp));

    const QStringList suffixes{QString(), QStringLiteral("-wal"), QStringLiteral("-shm")};
    for (const QString &suffix : suffixes) {
        const QString sourcePath = databasePath + suffix;
        if (!QFile::exists(sourcePath)) {
            result.missing << sourcePath;
            continue;
        }

        const QString destination = result.target + suffix;
        if (QFile::rename(sourcePath, destination)) {
            result.moved << destination;
        } else {
            result.failed << sourcePath;
        }
    }

    result.ok = result.failed.isEmpty();
    return result;
}

}  // namespace xieqian::storage
