#pragma once

#include <QString>
#include <QStringList>

namespace xieqian::storage {

struct Health {
    bool ok = false;
    QString detail;
};

struct Isolation {
    bool ok = false;
    QString target;
    QStringList moved;
    QStringList failed;
    QStringList missing;
};

// 在调用线程内自建独立 QSQLITE 连接执行 PRAGMA quick_check。
// 禁止跨线程复用连接：调用方必须在同一线程创建并销毁。
[[nodiscard]] Health quickCheck(const QString &databasePath);

// 把 data.db / data.db-wal / data.db-shm 以同一时间戳整体改名到
// data_corrupted_<timestamp>.db（及同后缀伴生文件）；缺件跳过并记录。
[[nodiscard]] Isolation isolateCorrupted(const QString &databasePath, const QString &timestamp);

}  // namespace xieqian::storage
