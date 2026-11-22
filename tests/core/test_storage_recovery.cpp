#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QTemporaryDir>
#include <QUuid>

#include "storage_recovery.h"

namespace {

// QSqlDatabase 加载驱动插件需要 QCoreApplication 实例；Catch2 的 main 不创建它
QCoreApplication &testApplication()
{
    static int argc = 1;
    static char appName[] = "xieqian-tests";
    static char *argv[] = {appName, nullptr};
    static QCoreApplication app(argc, argv);
    return app;
}

bool sqlDriverReady()
{
    (void)testApplication();
    return QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"));
}

QString uniqueConnection(const QString &prefix)
{
    return QStringLiteral("%1_%2").arg(prefix, QUuid::createUuid().toString(QUuid::WithoutBraces));
}

bool writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const qint64 written = file.write(bytes);
    file.close();
    return written == bytes.size();
}

bool createDatabase(const QString &path)
{
    const QString connection = uniqueConnection(QStringLiteral("xq_test_create"));
    bool ok = false;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(path);
        if (database.open()) {
            QSqlQuery query(database);
            ok = query.exec(QStringLiteral("CREATE TABLE sample(id INTEGER PRIMARY KEY)"));
            if (ok) {
                ok = query.exec(QStringLiteral("PRAGMA user_version = 2"));
            }
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return ok;
}

int readUserVersion(const QString &path)
{
    const QString connection = uniqueConnection(QStringLiteral("xq_test_version"));
    int version = -1;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(path);
        if (database.open()) {
            QSqlQuery query(database);
            if (query.exec(QStringLiteral("PRAGMA user_version")) && query.next()) {
                version = query.value(0).toInt();
            }
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return version;
}

}  // namespace

TEST_CASE("quickCheck accepts a usable sqlite database", "[storage]")
{
    if (!sqlDriverReady()) {
        SKIP("QSQLITE driver is not available in this environment");
    }

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("data.db"));
    REQUIRE(createDatabase(path));

    const xieqian::storage::Health health = xieqian::storage::quickCheck(path);
    REQUIRE(health.ok);
}

TEST_CASE("quickCheck rejects a file that is not a database", "[storage]")
{
    if (!sqlDriverReady()) {
        SKIP("QSQLITE driver is not available in this environment");
    }

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("data.db"));
    REQUIRE(writeBytes(path, QByteArray("this is not a sqlite database")));

    const xieqian::storage::Health health = xieqian::storage::quickCheck(path);
    REQUIRE_FALSE(health.ok);
    REQUIRE_FALSE(health.detail.isEmpty());
}

TEST_CASE("isolateCorrupted renames database and sidecars with one timestamp", "[storage]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("data.db"));
    REQUIRE(writeBytes(path, QByteArray("corrupt-main")));
    REQUIRE(writeBytes(path + QStringLiteral("-wal"), QByteArray("corrupt-wal")));
    REQUIRE(writeBytes(path + QStringLiteral("-shm"), QByteArray("corrupt-shm")));

    const xieqian::storage::Isolation result =
        xieqian::storage::isolateCorrupted(path, QStringLiteral("20261122_120000"));

    REQUIRE(result.ok);
    REQUIRE(result.moved.size() == 3);
    REQUIRE(result.failed.isEmpty());
    REQUIRE_FALSE(QFile::exists(path));
    REQUIRE(QFile::exists(result.target));
    REQUIRE(QFile::exists(result.target + QStringLiteral("-wal")));
    REQUIRE(QFile::exists(result.target + QStringLiteral("-shm")));
    REQUIRE(result.target.endsWith(QStringLiteral("data_corrupted_20261122_120000.db")));
}

TEST_CASE("isolateCorrupted skips missing sidecars", "[storage]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("data.db"));
    REQUIRE(writeBytes(path, QByteArray("corrupt-main-only")));

    const xieqian::storage::Isolation result =
        xieqian::storage::isolateCorrupted(path, QStringLiteral("20261122_130000"));

    REQUIRE(result.ok);
    REQUIRE(result.moved.size() == 1);
    REQUIRE(result.missing.size() == 2);
    REQUIRE(QFile::exists(result.target));
    REQUIRE_FALSE(QFile::exists(path));
}

TEST_CASE("recovery leaves the original path free for a fresh schema", "[storage]")
{
    if (!sqlDriverReady()) {
        SKIP("QSQLITE driver is not available in this environment");
    }

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("data.db"));
    REQUIRE(writeBytes(path, QByteArray("corrupt")));

    const xieqian::storage::Isolation isolation =
        xieqian::storage::isolateCorrupted(path, QStringLiteral("20261122_140000"));
    REQUIRE(isolation.ok);
    REQUIRE_FALSE(QFile::exists(path));

    // 隔离后原路径可重新建库，schema 版本仍应为 2
    REQUIRE(createDatabase(path));
    REQUIRE(readUserVersion(path) == 2);
    REQUIRE(QFile::exists(isolation.target));
}
