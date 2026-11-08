#include "backend_bridge.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <utility>

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QSettings>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTimer>
#include <QVariantMap>

#include "text_decoder.h"
#include "xieqian/core/io/file_parser.h"

namespace {

constexpr int kMinDrawCount = 1;
constexpr int kMaxDrawCount = 10;
// 与开发者页输入框的上下限一致
constexpr double kMinWeight = 0.1;
constexpr double kMaxWeight = 1000.0;
// 成绩单调权时，没出现在成绩单上的人统一落到最低档
constexpr double kLowestScoreWeight = 0.5;
constexpr int kSaveDebounceMs = 220;
constexpr auto kConnectionName = "xieqian_state";

// Config 表的键名
constexpr auto kCfgAiKey = "ai_api_key";
constexpr auto kCfgAiBaseUrl = "ai_base_url";
constexpr auto kCfgAiModel = "ai_model";
constexpr auto kCfgSound = "sound_enabled";
constexpr auto kCfgMotion = "motion_enabled";
constexpr auto kCfgDynamic = "dynamic_weights";
constexpr auto kCfgDrawCount = "draw_count";
constexpr auto kCfgDrawStrategy = "draw_strategy";
constexpr auto kCfgActiveWheel = "active_wheel";
constexpr auto kCfgLastWheel = "last_selected_wheel";
constexpr auto kCfgCustomWheels = "custom_wheels";
constexpr auto kCfgFirstRunSeen = "first_run_seen";

std::mt19937_64 seededGenerator()
{
    std::random_device device;
    std::seed_seq seed{device(), device(), device(), device()};
    return std::mt19937_64(seed);
}

bool hasStateArrays(const QJsonObject &root)
{
    return root.value(QStringLiteral("roster")).isArray() &&
           root.value(QStringLiteral("history")).isArray();
}

QString boolText(bool value)
{
    return value ? QStringLiteral("1") : QStringLiteral("0");
}

QString configText(const QHash<QString, QString> &config, const char *key, const QString &fallback)
{
    const QString value = config.value(QString::fromLatin1(key));
    return value.isEmpty() ? fallback : value;
}

bool configFlag(const QHash<QString, QString> &config, const char *key, bool fallback)
{
    const QString value = config.value(QString::fromLatin1(key));
    if (value.isEmpty()) {
        return fallback;
    }
    return value == QStringLiteral("1") || value == QStringLiteral("true");
}

}  // namespace

BackendBridge::BackendBridge(QObject *parent)
    : QObject(parent),
      roster_(std::make_shared<xieqian::core::Roster>()),
      drawEngine_(std::make_unique<xieqian::core::DrawEngine>(seededGenerator())),
      aiManager_(std::make_unique<AiManager>())
{
    if (openDatabase()) {
        // 老版本把状态写在 data.json 里，首次升级时自动迁移进 SQLite。
        // 迁移失败时保留内存中的旧数据，不能再用空库把它覆盖掉。
        if (migrateLegacyJson()) {
            (void)readState();
        }
    } else {
        qWarning() << "database unavailable, running in memory only:" << storageError_;
    }

    aiManager_->setApiConfig(aiApiKey_, aiBaseUrl_, aiModel_);
    drawEngine_->setDynamicWeights(dynamicWeights_);

    saveTimer_ = new QTimer(this);
    saveTimer_->setSingleShot(true);
    saveTimer_->setInterval(kSaveDebounceMs);
    connect(saveTimer_, &QTimer::timeout, this, [this]() { writeState(); });

    if (QCoreApplication *app = QCoreApplication::instance()) {
        connect(app, &QCoreApplication::aboutToQuit, this, &BackendBridge::flushState);
    }
}

BackendBridge::~BackendBridge()
{
    flushState();
    closeDatabase();
}

// ─────────────────────────────────────────────────────────────
// SQLite 持久层
// ─────────────────────────────────────────────────────────────

bool BackendBridge::openDatabase()
{
    const QString path = dataFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    const QString connection = QString::fromLatin1(kConnectionName);
    if (QSqlDatabase::contains(connection)) {
        QSqlDatabase::removeDatabase(connection);
    }

    database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    database_.setDatabaseName(path);
    database_.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));

    if (!database_.open()) {
        storageReady_ = false;
        raiseStorageError(QStringLiteral("无法打开数据库 %1：%2").arg(path, database_.lastError().text()));
        emit storageReadyChanged();
        return false;
    }

    QSqlQuery pragma(database_);
    pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragma.exec(QStringLiteral("PRAGMA synchronous=FULL"));
    pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    pragma.exec(QStringLiteral("PRAGMA busy_timeout=5000"));

    if (!ensureSchema()) {
        database_.close();
        storageReady_ = false;
        emit storageReadyChanged();
        return false;
    }

    storageReady_ = true;
    storageError_.clear();
    emit storageReadyChanged();
    return true;
}

void BackendBridge::closeDatabase()
{
    if (!database_.isValid()) {
        return;
    }

    const QString connection = database_.connectionName();
    if (database_.isOpen()) {
        database_.close();
    }
    database_ = QSqlDatabase();
    if (!connection.isEmpty()) {
        QSqlDatabase::removeDatabase(connection);
    }
}

bool BackendBridge::ensureSchema()
{
    const QStringList statements{
        QStringLiteral("CREATE TABLE IF NOT EXISTS roster ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "name TEXT NOT NULL,"
                       "student_id TEXT NOT NULL DEFAULT '',"
                       "group_name TEXT NOT NULL DEFAULT '',"
                       "enabled INTEGER NOT NULL DEFAULT 1,"
                       "weight REAL NOT NULL DEFAULT 1.0,"
                       "draw_count INTEGER NOT NULL DEFAULT 0,"
                       "streak INTEGER NOT NULL DEFAULT 0,"
                       "position INTEGER NOT NULL DEFAULT 0)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS history ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "name TEXT NOT NULL,"
                       "created_at TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS config ("
                       "\"key\" TEXT PRIMARY KEY,"
                       "\"value\" TEXT NOT NULL)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_roster_position ON roster(position)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_history_id ON history(id)"),
    };

    QSqlQuery query(database_);
    for (const QString &statement : statements) {
        if (!query.exec(statement)) {
            raiseStorageError(QStringLiteral("初始化数据库表失败：%1").arg(query.lastError().text()));
            return false;
        }
    }

    // schema 版本号，方便后续迁移；当前为 2（JSON → SQLite 后的第一版）
    query.exec(QStringLiteral("PRAGMA user_version = 2"));
    return true;
}

bool BackendBridge::readState()
{
    if (!storageReady_) {
        return false;
    }

    roster_->clear();
    {
        QSqlQuery query(database_);
        query.prepare(QStringLiteral(
            "SELECT name, student_id, group_name, enabled, weight, draw_count, streak "
            "FROM roster ORDER BY position ASC, id ASC"));
        if (query.exec()) {
            while (query.next()) {
                xieqian::core::Participant participant;
                participant.name = query.value(0).toString().toStdString();
                participant.studentId = query.value(1).toString().toStdString();
                participant.group = query.value(2).toString().toStdString();
                participant.enabled = query.value(3).toInt() != 0;
                participant.weight = query.value(4).toDouble();
                participant.drawCount = query.value(5).toInt();
                participant.streak = query.value(6).toInt();
                if (!participant.name.empty()) {
                    roster_->add(std::move(participant));
                }
            }
        } else {
            raiseStorageError(QStringLiteral("读取名单失败：%1").arg(query.lastError().text()));
        }
    }

    history_.clear();
    pendingHistory_.clear();
    historyRewritePending_ = false;
    {
        QSqlQuery query(database_);
        query.prepare(QStringLiteral("SELECT name, created_at FROM history ORDER BY id ASC"));
        if (query.exec()) {
            while (query.next()) {
                HistoryEntry entry;
                entry.name = query.value(0).toString();
                entry.time = query.value(1).toString();
                if (!entry.name.isEmpty()) {
                    history_.push_back(entry);
                }
            }
        } else {
            raiseStorageError(QStringLiteral("读取历史失败：%1").arg(query.lastError().text()));
        }
    }

    QHash<QString, QString> config;
    {
        QSqlQuery query(database_);
        if (query.exec(QStringLiteral("SELECT \"key\", \"value\" FROM config"))) {
            while (query.next()) {
                config.insert(query.value(0).toString(), query.value(1).toString());
            }
        }
    }

    aiApiKey_ = configText(config, kCfgAiKey, QString());
    aiBaseUrl_ = configText(config, kCfgAiBaseUrl, QString());
    aiModel_ = configText(config, kCfgAiModel, QString());
    soundEnabled_ = configFlag(config, kCfgSound, true);
    motionEnabled_ = configFlag(config, kCfgMotion, true);
    dynamicWeights_ = configFlag(config, kCfgDynamic, false);
    firstRunSeen_ = configFlag(config, kCfgFirstRunSeen, false);
    drawCount_ = std::clamp(configText(config, kCfgDrawCount, QStringLiteral("1")).toInt(),
                            kMinDrawCount, kMaxDrawCount);
    drawStrategy_ = static_cast<xieqian::core::DrawStrategy>(
        std::clamp(configText(config, kCfgDrawStrategy, QStringLiteral("0")).toInt(), 0, 2));

    activeWheelName_ = configText(config, kCfgActiveWheel, QString());
    lastSelectedWheel_ = configText(config, kCfgLastWheel, QString());

    customWheels_.clear();
    const QJsonDocument wheelsDocument =
        QJsonDocument::fromJson(configText(config, kCfgCustomWheels, QStringLiteral("[]")).toUtf8());
    if (wheelsDocument.isArray()) {
        for (const QJsonValue &value : wheelsDocument.array()) {
            const QJsonObject entry = value.toObject();
            const QString name = entry.value(QStringLiteral("name")).toString().trimmed();
            if (name.isEmpty()) {
                continue;
            }

            QStringList options;
            for (const QJsonValue &option : entry.value(QStringLiteral("options")).toArray()) {
                const QString text = option.toString().trimmed();
                if (!text.isEmpty() && !options.contains(text)) {
                    options.append(text);
                }
            }
            if (options.size() >= 2) {
                customWheels_.push_back(CustomWheel{name, options});
            }
        }
    }

    const auto active = std::find_if(customWheels_.begin(), customWheels_.end(),
                                     [this](const CustomWheel &wheel) {
                                         return wheel.name == activeWheelName_;
                                     });
    if (active == customWheels_.end()) {
        activeWheelName_ = customWheels_.empty() ? QString() : customWheels_.back().name;
    }

    // 动态权重只对加权策略有意义，历史配置里若不一致则以策略为准
    if (drawStrategy_ != xieqian::core::DrawStrategy::Weighted) {
        dynamicWeights_ = false;
    }

    drawEngine_->clearDrawnHistory();
    drawEngine_->setDynamicWeights(dynamicWeights_);
    aiManager_->setApiConfig(aiApiKey_, aiBaseUrl_, aiModel_);
    return true;
}

bool BackendBridge::writeState()
{
    if (!storageReady_ || !database_.isOpen()) {
        return false;
    }

    const bool rewriteHistory = historyRewritePending_;
    const QVector<HistoryEntry> pending = pendingHistory_;

    if (!database_.transaction()) {
        raiseStorageError(QStringLiteral("开启事务失败：%1").arg(database_.lastError().text()));
        return false;
    }

    bool ok = true;
    QString failure;

    {
        QSqlQuery query(database_);

        // 名单全量重写：人数少、顺序敏感，一次性覆盖最不容易产生半截状态
        if (ok && !query.exec(QStringLiteral("DELETE FROM roster"))) {
            ok = false;
            failure = query.lastError().text();
        }
        if (ok) {
            query.prepare(QStringLiteral(
                "INSERT INTO roster(name, student_id, group_name, enabled, weight, draw_count, streak, position) "
                "VALUES(?, ?, ?, ?, ?, ?, ?, ?)"));
            for (std::size_t index = 0; index < roster_->size(); ++index) {
                const xieqian::core::Participant &participant = roster_->at(index);
                query.bindValue(0, QString::fromStdString(participant.name));
                query.bindValue(1, QString::fromStdString(participant.studentId));
                query.bindValue(2, QString::fromStdString(participant.group));
                query.bindValue(3, participant.enabled ? 1 : 0);
                query.bindValue(4, participant.weight);
                query.bindValue(5, participant.drawCount);
                query.bindValue(6, participant.streak);
                query.bindValue(7, static_cast<int>(index));
                if (!query.exec()) {
                    ok = false;
                    failure = query.lastError().text();
                    break;
                }
            }
        }

        // 历史：导入/清空时全量重写，平时只追加未落盘的行
        if (ok && rewriteHistory) {
            if (!query.exec(QStringLiteral("DELETE FROM history"))) {
                ok = false;
                failure = query.lastError().text();
            } else {
                query.prepare(QStringLiteral("INSERT INTO history(name, created_at) VALUES(?, ?)"));
                for (const HistoryEntry &entry : history_) {
                    query.bindValue(0, entry.name);
                    query.bindValue(1, entry.time);
                    if (!query.exec()) {
                        ok = false;
                        failure = query.lastError().text();
                        break;
                    }
                }
            }
        } else if (ok && !pending.isEmpty()) {
            query.prepare(QStringLiteral("INSERT INTO history(name, created_at) VALUES(?, ?)"));
            for (const HistoryEntry &entry : pending) {
                query.bindValue(0, entry.name);
                query.bindValue(1, entry.time);
                if (!query.exec()) {
                    ok = false;
                    failure = query.lastError().text();
                    break;
                }
            }
        }

        // 配置全量重写：键值少，保证删除的转盘不会留下幽灵配置
        if (ok) {
            if (!query.exec(QStringLiteral("DELETE FROM config"))) {
                ok = false;
                failure = query.lastError().text();
            } else {
                query.prepare(QStringLiteral("INSERT INTO config(\"key\", \"value\") VALUES(?, ?)"));
                const auto put = [&query, &ok, &failure](const char *key, const QString &value) {
                    query.bindValue(0, QString::fromLatin1(key));
                    query.bindValue(1, value);
                    if (!query.exec()) {
                        ok = false;
                        failure = query.lastError().text();
                    }
                };

                QJsonArray wheelArray;
                for (const CustomWheel &wheel : customWheels_) {
                    QJsonObject entry;
                    entry.insert(QStringLiteral("name"), wheel.name);
                    entry.insert(QStringLiteral("options"), QJsonArray::fromStringList(wheel.options));
                    wheelArray.append(entry);
                }
                const QString wheelsJson =
                    QString::fromUtf8(QJsonDocument(wheelArray).toJson(QJsonDocument::Compact));

                put(kCfgAiKey, aiApiKey_);
                put(kCfgAiBaseUrl, aiBaseUrl_);
                put(kCfgAiModel, aiModel_);
                put(kCfgSound, boolText(soundEnabled_));
                put(kCfgMotion, boolText(motionEnabled_));
                put(kCfgDynamic, boolText(dynamicWeights_));
                put(kCfgDrawCount, QString::number(drawCount_));
                put(kCfgDrawStrategy, QString::number(static_cast<int>(drawStrategy_)));
                put(kCfgActiveWheel, activeWheelName_);
                put(kCfgLastWheel, lastSelectedWheel_);
                put(kCfgCustomWheels, wheelsJson);
                put(kCfgFirstRunSeen, boolText(firstRunSeen_));
            }
        }
    }

    if (!ok) {
        database_.rollback();
        raiseStorageError(QStringLiteral("保存数据失败：%1").arg(failure));
        return false;
    }

    if (!database_.commit()) {
        const QString message = database_.lastError().text();
        database_.rollback();
        raiseStorageError(QStringLiteral("提交事务失败：%1").arg(message));
        return false;
    }

    historyRewritePending_ = false;
    pendingHistory_.clear();
    dirty_ = false;
    return true;
}

bool BackendBridge::migrateLegacyJson()
{
    const QString legacyPath = legacyJsonPath();
    QFile legacyFile(legacyPath);
    if (!legacyFile.exists()) {
        return true;
    }

    qint64 existingRows = 0;
    {
        QSqlQuery query(database_);
        if (query.exec(QStringLiteral("SELECT COUNT(*) FROM roster")) && query.next()) {
            existingRows = query.value(0).toLongLong();
        }
    }
    if (existingRows > 0) {
        return true;
    }

    if (!legacyFile.open(QIODevice::ReadOnly)) {
        qWarning() << "cannot read legacy json:" << legacyPath;
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(legacyFile.readAll());
    legacyFile.close();

    if (!document.isObject() || !hasStateArrays(document.object())) {
        qWarning() << "legacy json is not a valid state file:" << legacyPath;
        return true;
    }

    applyData(document.object());
    if (!writeState()) {
        qWarning() << "legacy migration failed; keeping migrated data in memory only";
        return false;
    }

    const QString migratedPath = legacyPath + QStringLiteral(".migrated");
    QFile::remove(migratedPath);
    if (!QFile::rename(legacyPath, migratedPath)) {
        qWarning() << "cannot rename migrated json:" << legacyPath;
    }
    return true;
}

void BackendBridge::saveState()
{
    dirty_ = true;
    if (saveTimer_ != nullptr) {
        saveTimer_->start();
    } else {
        writeState();
    }
}

void BackendBridge::flushState()
{
    if (saveTimer_ != nullptr) {
        saveTimer_->stop();
    }
    if (dirty_) {
        writeState();
    }
}

void BackendBridge::raiseStorageError(const QString &message)
{
    storageError_ = message;
    qWarning().noquote() << message;
}

QString BackendBridge::dataFilePath()
{
    QSettings settings(QStringLiteral("HKEY_CURRENT_USER\\Software\\XieQian"), QSettings::NativeFormat);
    const QString customPath = settings.value(QStringLiteral("DataPath")).toString().trimmed();

    if (!customPath.isEmpty()) {
        if (!QDir().mkpath(customPath)) {
            qWarning() << "cannot create data directory:" << customPath;
        }
        return QDir(customPath).filePath(QStringLiteral("data.db"));
    }

    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("data.db"));
}

QString BackendBridge::legacyJsonPath()
{
    return QFileInfo(dataFilePath()).absoluteDir().filePath(QStringLiteral("data.json"));
}

// ─────────────────────────────────────────────────────────────
// 抽签
// ─────────────────────────────────────────────────────────────

QString BackendBridge::drawWinner()
{
    const std::vector<xieqian::core::Participant> pool = roster_->enabledParticipants();
    const std::vector<xieqian::core::Participant> winners = drawEngine_->draw(pool, 1, strategyFor());
    if (winners.empty()) {
        return QStringLiteral("无可用名单");
    }

    recordWinners(winners);
    const QString name = QString::fromStdString(winners.front().name);
    recordDraw(name);
    emit winnerDrawn(name);
    // 让转盘在结果展示前就刷新扇区（动态权重衰减、防连抽的已中签标记）
    emit rosterUpdated();
    return name;
}

QVariantList BackendBridge::drawMultipleWinners()
{
    QVariantList winners;
    QStringList names;

    const std::vector<xieqian::core::Participant> pool = roster_->enabledParticipants();
    const std::vector<xieqian::core::Participant> picked =
        drawEngine_->draw(pool, drawCount_, strategyFor());
    recordWinners(picked);
    for (const xieqian::core::Participant &participant : picked) {
        const QString name = QString::fromStdString(participant.name);
        recordDraw(name);
        names.append(name);
        winners.append(name);
    }

    if (!names.isEmpty()) {
        emit winnersDrawn(names);
    }
    emit rosterUpdated();
    return winners;
}

void BackendBridge::updateDrawSettings(int count, int strategy)
{
    if (strategy < static_cast<int>(xieqian::core::DrawStrategy::Random) ||
        strategy > static_cast<int>(xieqian::core::DrawStrategy::Weighted)) {
        return;
    }

    const int nextCount = std::clamp(count, kMinDrawCount, kMaxDrawCount);
    const auto nextStrategy = static_cast<xieqian::core::DrawStrategy>(strategy);

    bool changed = false;
    if (drawCount_ != nextCount) {
        drawCount_ = nextCount;
        changed = true;
    }
    if (drawStrategy_ != nextStrategy) {
        drawStrategy_ = nextStrategy;
        changed = true;
    }

    // 动态权重只对加权策略有意义：切到其它策略时同步关闭，避免开关与概率表里不一
    if (drawStrategy_ != xieqian::core::DrawStrategy::Weighted && dynamicWeights_) {
        dynamicWeights_ = false;
        drawEngine_->setDynamicWeights(false);
        emit dynamicWeightsChanged();
        changed = true;
    }

    if (!changed) {
        return;
    }

    saveState();
    emit drawSettingsChanged();
}

xieqian::core::DrawStrategy BackendBridge::strategyFor() const
{
    // 用户选什么就用什么，不再因为“名单里有权重”而静默改判
    return drawStrategy_;
}

double BackendBridge::effectiveWeight(const xieqian::core::Participant &participant) const
{
    // 加权模式：扇区宽度 = 真实权重（动态模式下含衰减惩罚）
    if (drawStrategy_ == xieqian::core::DrawStrategy::Weighted) {
        if (dynamicWeights_) {
            return xieqian::core::dynamicWeight(participant);
        }
        return std::max(0.0, participant.weight);
    }

    // 防连抽：已中签者本轮概率为 0，扇区收成 0 宽度；
    // 当所有人都抽过时引擎会退回全员随机，此时全 0 权重在 WheelItem 里同样回落为等分。
    if (drawStrategy_ == xieqian::core::DrawStrategy::NoRepeat) {
        return drawEngine_->isDrawn(participant) ? 0.0 : 1.0;
    }

    // 等概率：所有人都一样宽
    return 1.0;
}

void BackendBridge::recordWinners(const std::vector<xieqian::core::Participant> &winners)
{
    if (winners.empty() || !dynamicWeights_) {
        return;
    }

    std::vector<std::string> ids;
    ids.reserve(winners.size());
    for (const xieqian::core::Participant &winner : winners) {
        ids.push_back(xieqian::core::participantId(winner));
    }

    roster_->recordWins(ids);
    saveState();
}

void BackendBridge::recordDraw(const QString &name)
{
    const QString time = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    const HistoryEntry entry{name, time};
    history_.push_back(entry);
    pendingHistory_.push_back(entry);
    saveState();
    emit historyUpdated();
}

void BackendBridge::appendHistory(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    recordDraw(trimmed);
}

QVariantList BackendBridge::getHistory()
{
    QVariantList history;
    for (auto entry = history_.rbegin(); entry != history_.rend(); ++entry) {
        QVariantMap record;
        record.insert(QStringLiteral("name"), entry->name);
        record.insert(QStringLiteral("time"), entry->time);
        history.append(record);
    }
    return history;
}

void BackendBridge::clearHistory()
{
    if (history_.empty() && pendingHistory_.isEmpty()) {
        return;
    }

    history_.clear();
    pendingHistory_.clear();
    historyRewritePending_ = true;
    saveState();
    emit historyUpdated();
}

void BackendBridge::clearAllData()
{
    // 只抹掉名单与历史；AI 配置、自定义转盘与偏好设置属于用户资产，必须保留
    roster_->clear();
    history_.clear();
    pendingHistory_.clear();
    historyRewritePending_ = true;
    drawEngine_->clearDrawnHistory();

    saveState();
    emit rosterUpdated();
    emit historyUpdated();
}

void BackendBridge::resetDrawStats()
{
    drawEngine_->clearDrawnHistory();
    roster_->resetDrawStats();
    saveState();
    emit rosterUpdated();
}

// ─────────────────────────────────────────────────────────────
// 名单
// ─────────────────────────────────────────────────────────────

QVariantList BackendBridge::readRosterFromFile(const QString &filePath)
{
    QVariantList entries;
    lastError_.clear();

    if (filePath.isEmpty()) {
        lastError_ = QStringLiteral("没有选择文件");
        return entries;
    }

    const xieqian::ui::DecodedText decoded = xieqian::ui::decodeTextFile(filePath);
    if (!decoded.ok) {
        lastError_ = decoded.error;
        return entries;
    }

    xieqian::core::Roster parsed;
    xieqian::core::FileParser parser;
    const bool csvMode = xieqian::core::FileParser::usesCsvFormat(
        QFile::encodeName(filePath).toStdString());
    if (!parser.parseText(decoded.text.toStdString(), csvMode, parsed)) {
        lastError_ = QString::fromStdString(parser.lastError());
        return entries;
    }

    for (std::size_t index = 0; index < parsed.size(); ++index) {
        const xieqian::core::Participant &participant = parsed.at(index);
        const QString name = QString::fromStdString(participant.name).trimmed();
        if (name.isEmpty()) {
            continue;
        }

        QVariantMap entry;
        entry.insert(QStringLiteral("name"), name);
        entry.insert(QStringLiteral("studentId"), QString::fromStdString(participant.studentId).trimmed());
        entry.insert(QStringLiteral("group"), QString::fromStdString(participant.group).trimmed());
        entry.insert(QStringLiteral("weight"), participant.weight);
        entries.append(entry);
    }

    return entries;
}

int BackendBridge::appendEntriesToRoster(const QVariantList &entries)
{
    int added = 0;
    for (const QVariant &value : entries) {
        const QVariantMap entry = value.toMap();
        const QString name = entry.value(QStringLiteral("name")).toString().trimmed();
        const QString studentId = entry.value(QStringLiteral("studentId")).toString().trimmed();
        const QString group = entry.value(QStringLiteral("group")).toString().trimmed();
        const double rawWeight = entry.value(QStringLiteral("weight")).toDouble();
        if (name.isEmpty()) {
            continue;
        }

        // 同名同（或缺失）学号视为同一人；同名不同学号允许并存，避免同名同学被误判
        bool exists = false;
        for (std::size_t index = 0; index < roster_->size(); ++index) {
            const xieqian::core::Participant &participant = roster_->at(index);
            if (QString::fromStdString(participant.name) != name) {
                continue;
            }
            const QString existingId = QString::fromStdString(participant.studentId);
            if (studentId.isEmpty() || existingId.isEmpty() || existingId == studentId) {
                exists = true;
                break;
            }
        }
        if (exists) {
            continue;
        }

        xieqian::core::Participant participant;
        participant.name = name.toStdString();
        participant.studentId = studentId.toStdString();
        participant.group = group.toStdString();
        if (std::isfinite(rawWeight) && rawWeight > 0.0) {
            participant.weight = std::clamp(rawWeight, kMinWeight, kMaxWeight);
        }
        roster_->add(std::move(participant));
        ++added;
    }

    if (added > 0) {
        saveState();
        emit rosterUpdated();
    }
    return added;
}

QVariantList BackendBridge::getParticipantsWithStatus()
{
    QVariantList participants;
    for (std::size_t index = 0; index < roster_->size(); ++index) {
        const xieqian::core::Participant &participant = roster_->at(index);

        QVariantMap entry;
        entry.insert(QStringLiteral("index"), static_cast<int>(index));
        entry.insert(QStringLiteral("id"),
                     QString::fromStdString(xieqian::core::participantId(participant)));
        entry.insert(QStringLiteral("name"), QString::fromStdString(participant.name));
        entry.insert(QStringLiteral("studentId"), QString::fromStdString(participant.studentId));
        entry.insert(QStringLiteral("group"), QString::fromStdString(participant.group));
        entry.insert(QStringLiteral("enabled"), participant.enabled);
        entry.insert(QStringLiteral("weight"), participant.weight);
        // 转盘使用的权重：非加权策略下恒为 1，加权策略下计入衰减，确保扇区宽度等于真实概率
        entry.insert(QStringLiteral("effectiveWeight"), effectiveWeight(participant));
        entry.insert(QStringLiteral("drawCount"), participant.drawCount);
        entry.insert(QStringLiteral("streak"), participant.streak);
        entry.insert(QStringLiteral("isDrawn"), drawEngine_->isDrawn(participant));
        participants.append(entry);
    }
    return participants;
}

void BackendBridge::toggleParticipantStatus(int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= roster_->size()) {
        return;
    }

    const auto position = static_cast<std::size_t>(index);
    roster_->setEnabled(position, !roster_->at(position).enabled);
    saveState();
    emit rosterUpdated();
}

void BackendBridge::updateParticipantWeight(const QString &id, double weight)
{
    if (id.isEmpty() || !std::isfinite(weight) || weight <= 0.0) {
        return;
    }

    // 与界面上的输入限制保持一致，越界值直接夹紧而不是静默丢弃
    const double clamped = std::clamp(weight, kMinWeight, kMaxWeight);
    const std::string key = id.toStdString();
    for (std::size_t index = 0; index < roster_->size(); ++index) {
        if (xieqian::core::participantId(roster_->at(index)) == key) {
            roster_->setWeight(index, clamped);
            saveState();
            emit rosterUpdated();
            return;
        }
    }
}

bool BackendBridge::removeParticipant(const QString &id)
{
    if (id.isEmpty()) {
        return false;
    }

    const std::string key = id.toStdString();
    for (std::size_t index = 0; index < roster_->size(); ++index) {
        if (xieqian::core::participantId(roster_->at(index)) == key) {
            roster_->removeAt(index);
            saveState();
            emit rosterUpdated();
            return true;
        }
    }
    return false;
}

int BackendBridge::applyScoreWeights(const QVariantList &entries)
{
    struct ScoreEntry {
        QString name;
        double score = 0.0;
    };

    std::vector<ScoreEntry> scored;
    for (const QVariant &value : entries) {
        const QVariantMap entry = value.toMap();
        const QString name = entry.value(QStringLiteral("name")).toString().trimmed();
        const double score = entry.value(QStringLiteral("score")).toDouble();
        if (name.isEmpty() || !std::isfinite(score)) {
            continue;
        }
        scored.push_back(ScoreEntry{name, score});
    }

    if (scored.empty()) {
        return 0;
    }

    std::sort(scored.begin(), scored.end(), [](const ScoreEntry &left, const ScoreEntry &right) {
        return left.score > right.score;
    });

    // 按名次线性映射：最高分 5.0，最低分 0.5
    std::vector<std::pair<QString, double>> weights;
    weights.reserve(scored.size());
    for (std::size_t rank = 0; rank < scored.size(); ++rank) {
        const double ratio = scored.size() > 1
                                 ? static_cast<double>(rank) /
                                       static_cast<double>(scored.size() - 1)
                                 : 0.0;
        weights.emplace_back(scored[rank].name, 5.0 - 4.5 * ratio);
    }

    int updated = 0;
    // 全员重算：成绩单上的人按名次拿权重，没出现在成绩单上的人压到最低档，
    // 否则旧权重会留在池子里把刚刚算好的比例打破。
    for (std::size_t index = 0; index < roster_->size(); ++index) {
        const QString name = QString::fromStdString(roster_->at(index).name);
        const auto match = std::find_if(weights.begin(), weights.end(),
                                        [&name](const std::pair<QString, double> &entry) {
                                            return entry.first == name;
                                        });
        if (match != weights.end()) {
            roster_->setWeight(index, match->second);
            ++updated;
        } else {
            roster_->setWeight(index, kLowestScoreWeight);
        }
    }

    if (updated == 0) {
        return 0;
    }

    saveState();
    emit rosterUpdated();
    return updated;
}

// ─────────────────────────────────────────────────────────────
// 自定义转盘
// ─────────────────────────────────────────────────────────────

bool BackendBridge::saveCustomWheel(const QString &name, const QVariantList &options)
{
    const QString wheelName = name.trimmed();
    if (wheelName.isEmpty()) {
        return false;
    }

    QStringList wheelOptions;
    for (const QVariant &value : options) {
        const QString option = value.toString().trimmed();
        if (!option.isEmpty() && !wheelOptions.contains(option)) {
            wheelOptions.append(option);
        }
    }

    if (wheelOptions.size() < 2) {
        return false;
    }

    const auto existing = std::find_if(customWheels_.begin(), customWheels_.end(),
                                       [&wheelName](const CustomWheel &wheel) {
                                           return wheel.name == wheelName;
                                       });
    if (existing != customWheels_.end()) {
        existing->options = wheelOptions;
    } else {
        customWheels_.push_back(CustomWheel{wheelName, wheelOptions});
    }

    activeWheelName_ = wheelName;
    saveState();
    emit customWheelsChanged();
    return true;
}

bool BackendBridge::removeCustomWheel(const QString &name)
{
    const QString wheelName = name.trimmed();
    if (wheelName.isEmpty()) {
        return false;
    }

    const auto wheel = std::find_if(customWheels_.begin(), customWheels_.end(),
                                    [&wheelName](const CustomWheel &entry) {
                                        return entry.name == wheelName;
                                    });
    if (wheel == customWheels_.end()) {
        return false;
    }

    customWheels_.erase(wheel);
    if (activeWheelName_ == wheelName) {
        activeWheelName_ = customWheels_.empty() ? QString() : customWheels_.back().name;
    }

    saveState();
    emit customWheelsChanged();
    return true;
}

QVariantList BackendBridge::customWheels() const
{
    QVariantList wheels;
    for (const CustomWheel &wheel : customWheels_) {
        QVariantList options;
        for (const QString &option : wheel.options) {
            options.append(option);
        }

        QVariantMap entry;
        entry.insert(QStringLiteral("name"), wheel.name);
        entry.insert(QStringLiteral("options"), options);
        wheels.append(entry);
    }
    return wheels;
}

QString BackendBridge::lastSelectedWheel() const
{
    return lastSelectedWheel_;
}

void BackendBridge::setLastSelectedWheel(const QString &name)
{
    const QString key = name.trimmed();
    if (lastSelectedWheel_ == key) {
        return;
    }

    lastSelectedWheel_ = key;
    saveState();
}

// ─────────────────────────────────────────────────────────────
// 设置与 AI 配置
// ─────────────────────────────────────────────────────────────

int BackendBridge::rosterCount() const
{
    return static_cast<int>(roster_->size());
}

int BackendBridge::drawCount() const
{
    return drawCount_;
}

int BackendBridge::drawStrategy() const
{
    return static_cast<int>(drawStrategy_);
}

bool BackendBridge::soundEnabled() const
{
    return soundEnabled_;
}

void BackendBridge::setSoundEnabled(bool enabled)
{
    if (soundEnabled_ == enabled) {
        return;
    }

    soundEnabled_ = enabled;
    saveState();
    qInfo() << "sound enabled:" << enabled;
    emit soundEnabledChanged();
}

bool BackendBridge::motionEnabled() const
{
    return motionEnabled_;
}

void BackendBridge::setMotionEnabled(bool enabled)
{
    if (motionEnabled_ == enabled) {
        return;
    }

    motionEnabled_ = enabled;
    saveState();
    qInfo() << "motion enabled:" << enabled;
    emit motionEnabledChanged();
}

AiManager *BackendBridge::aiManager() const
{
    return aiManager_.get();
}

bool BackendBridge::dynamicWeights() const
{
    return dynamicWeights_;
}

void BackendBridge::setDynamicWeights(bool enabled)
{
    if (dynamicWeights_ == enabled) {
        return;
    }

    dynamicWeights_ = enabled;
    drawEngine_->setDynamicWeights(enabled);

    // 动态权重只对加权策略生效；打开时显式切换到加权，让用户在设置里看得见
    if (enabled && drawStrategy_ != xieqian::core::DrawStrategy::Weighted) {
        drawStrategy_ = xieqian::core::DrawStrategy::Weighted;
        emit drawSettingsChanged();
    }

    saveState();
    emit dynamicWeightsChanged();
}

void BackendBridge::saveAIConfig(const QString &apiKey, const QString &baseUrl, const QString &model)
{
    aiApiKey_ = apiKey.trimmed();
    aiBaseUrl_ = baseUrl.trimmed();
    aiModel_ = model.trimmed();
    aiManager_->setApiConfig(aiApiKey_, aiBaseUrl_, aiModel_);
    saveState();
    emit aiConfigChanged();
}

QVariantMap BackendBridge::getAIConfig()
{
    QVariantMap config;
    config.insert(QStringLiteral("apiKey"), aiApiKey_);
    config.insert(QStringLiteral("baseUrl"), aiBaseUrl_);
    config.insert(QStringLiteral("model"), aiModel_);
    return config;
}

bool BackendBridge::hasAIConfig() const
{
    return aiManager_ != nullptr && aiManager_->hasApiKey();
}

bool BackendBridge::isFirstRun() const
{
    return !firstRunSeen_;
}

void BackendBridge::markFirstRunSeen()
{
    if (firstRunSeen_) {
        return;
    }
    firstRunSeen_ = true;
    saveState();
}

bool BackendBridge::storageReady() const
{
    return storageReady_;
}

QString BackendBridge::storageError() const
{
    return storageError_;
}

QString BackendBridge::lastError() const
{
    return lastError_;
}

// ─────────────────────────────────────────────────────────────
// 文件对话框与导入导出
// ─────────────────────────────────────────────────────────────

QString BackendBridge::chooseRosterFilePath()
{
    return QFileDialog::getOpenFileName(
        nullptr, QStringLiteral("选择名单文件"), QString(),
        QStringLiteral("名单文件 (*.csv *.txt);;CSV 文件 (*.csv);;文本文件 (*.txt)"));
}

QString BackendBridge::chooseRosterImagePath()
{
    return QFileDialog::getOpenFileName(nullptr, QStringLiteral("选择名单图片"), QString(),
                                        QStringLiteral("图片文件 (*.png *.jpg *.jpeg *.webp *.bmp)"));
}

QString BackendBridge::chooseBackupPath()
{
    return QFileDialog::getSaveFileName(nullptr, QStringLiteral("备份数据"),
                                        QStringLiteral("xieqian-backup.json"),
                                        QStringLiteral("JSON Files (*.json)"));
}

QString BackendBridge::chooseRestorePath()
{
    return QFileDialog::getOpenFileName(nullptr, QStringLiteral("恢复数据"), QString(),
                                        QStringLiteral("JSON Files (*.json)"));
}

bool BackendBridge::exportData(const QString &filePath)
{
    lastError_.clear();
    if (filePath.isEmpty()) {
        lastError_ = QStringLiteral("没有选择备份路径");
        return false;
    }

    QString path = filePath;
    if (!path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".json");
    }

    // QSaveFile 先写临时文件再原子替换，导出中途失败不会破坏已有备份
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        lastError_ = QStringLiteral("无法写入备份文件：%1").arg(path);
        return false;
    }

    const QByteArray payload = QJsonDocument(stateToJson()).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        lastError_ = QStringLiteral("写入备份文件失败：%1").arg(path);
        file.cancelWriting();
        return false;
    }

    if (!file.commit()) {
        lastError_ = QStringLiteral("保存备份文件失败：%1").arg(path);
        return false;
    }

    return true;
}

bool BackendBridge::importData(const QString &filePath)
{
    lastError_.clear();
    if (filePath.isEmpty()) {
        lastError_ = QStringLiteral("没有选择备份文件");
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        lastError_ = QStringLiteral("无法读取备份文件：%1").arg(filePath);
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (document.isNull() || !document.isObject() || !hasStateArrays(document.object())) {
        lastError_ = QStringLiteral("这不是有效的撷签备份文件");
        return false;
    }

    applyData(document.object());
    const bool saved = writeState();
    if (!saved) {
        lastError_ = storageError_.isEmpty()
                         ? QStringLiteral("写入数据库失败")
                         : storageError_;
    }

    emit rosterUpdated();
    emit historyUpdated();
    emit customWheelsChanged();
    emit aiConfigChanged();
    emit soundEnabledChanged();
    emit motionEnabledChanged();
    emit drawSettingsChanged();
    emit dynamicWeightsChanged();
    return saved;
}

QJsonObject BackendBridge::stateToJson() const
{
    QJsonArray roster;
    for (std::size_t index = 0; index < roster_->size(); ++index) {
        const xieqian::core::Participant &participant = roster_->at(index);

        QJsonObject entry;
        entry.insert(QStringLiteral("name"), QString::fromStdString(participant.name));
        entry.insert(QStringLiteral("studentId"), QString::fromStdString(participant.studentId));
        entry.insert(QStringLiteral("group"), QString::fromStdString(participant.group));
        entry.insert(QStringLiteral("enabled"), participant.enabled);
        entry.insert(QStringLiteral("weight"), participant.weight);
        entry.insert(QStringLiteral("drawCount"), participant.drawCount);
        entry.insert(QStringLiteral("streak"), participant.streak);
        roster.append(entry);
    }

    QJsonArray history;
    for (const HistoryEntry &record : history_) {
        QJsonObject entry;
        entry.insert(QStringLiteral("name"), record.name);
        entry.insert(QStringLiteral("time"), record.time);
        history.append(entry);
    }

    QJsonArray customWheels;
    for (const CustomWheel &wheel : customWheels_) {
        QJsonObject entry;
        entry.insert(QStringLiteral("name"), wheel.name);
        entry.insert(QStringLiteral("options"), QJsonArray::fromStringList(wheel.options));
        customWheels.append(entry);
    }

    // 零信任：备份文件绝不携带 API Key，避免“发个备份就把密钥发出去”
    QJsonObject aiConfig;
    aiConfig.insert(QStringLiteral("apiKey"), QString());
    aiConfig.insert(QStringLiteral("baseUrl"), aiBaseUrl_);
    aiConfig.insert(QStringLiteral("model"), aiModel_);

    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 2);
    root.insert(QStringLiteral("roster"), roster);
    root.insert(QStringLiteral("history"), history);
    root.insert(QStringLiteral("customWheels"), customWheels);
    root.insert(QStringLiteral("activeWheel"), activeWheelName_);
    root.insert(QStringLiteral("lastSelectedWheel"), lastSelectedWheel_);
    root.insert(QStringLiteral("aiConfig"), aiConfig);
    root.insert(QStringLiteral("soundEnabled"), soundEnabled_);
    root.insert(QStringLiteral("motionEnabled"), motionEnabled_);
    root.insert(QStringLiteral("dynamicWeights"), dynamicWeights_);
    root.insert(QStringLiteral("drawCount"), drawCount_);
    root.insert(QStringLiteral("drawStrategy"), static_cast<int>(drawStrategy_));
    return root;
}

void BackendBridge::applyData(const QJsonObject &root)
{
    roster_->clear();
    for (const QJsonValue &value : root.value(QStringLiteral("roster")).toArray()) {
        const QJsonObject entry = value.toObject();

        xieqian::core::Participant participant;
        participant.name = entry.value(QStringLiteral("name")).toString().toStdString();
        participant.studentId = entry.value(QStringLiteral("studentId")).toString().toStdString();
        participant.group = entry.value(QStringLiteral("group")).toString().toStdString();
        participant.enabled = entry.value(QStringLiteral("enabled")).toBool(true);
        participant.weight = entry.value(QStringLiteral("weight")).toDouble(1.0);
        participant.drawCount = entry.value(QStringLiteral("drawCount")).toInt(0);
        participant.streak = entry.value(QStringLiteral("streak")).toInt(0);

        if (!participant.name.empty()) {
            roster_->add(std::move(participant));
        }
    }

    history_.clear();
    pendingHistory_.clear();
    for (const QJsonValue &value : root.value(QStringLiteral("history")).toArray()) {
        const QJsonObject entry = value.toObject();
        const QString name = entry.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) {
            continue;
        }

        history_.push_back(HistoryEntry{name, entry.value(QStringLiteral("time")).toString()});
    }
    historyRewritePending_ = true;

    customWheels_.clear();
    for (const QJsonValue &value : root.value(QStringLiteral("customWheels")).toArray()) {
        const QJsonObject entry = value.toObject();
        const QString name = entry.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty()) {
            continue;
        }

        QStringList options;
        for (const QJsonValue &option : entry.value(QStringLiteral("options")).toArray()) {
            const QString text = option.toString().trimmed();
            if (!text.isEmpty() && !options.contains(text)) {
                options.append(text);
            }
        }

        if (options.size() >= 2) {
            customWheels_.push_back(CustomWheel{name, options});
        }
    }

    activeWheelName_ = root.value(QStringLiteral("activeWheel")).toString().trimmed();
    const auto active = std::find_if(customWheels_.begin(), customWheels_.end(),
                                     [this](const CustomWheel &wheel) {
                                         return wheel.name == activeWheelName_;
                                     });
    if (active == customWheels_.end()) {
        activeWheelName_ = customWheels_.empty() ? QString() : customWheels_.back().name;
    }

    const QJsonObject aiConfig = root.value(QStringLiteral("aiConfig")).toObject();
    QString importedKey = aiConfig.value(QStringLiteral("apiKey")).toString().trimmed();
    QString importedBase = aiConfig.value(QStringLiteral("baseUrl")).toString().trimmed();
    QString importedModel = aiConfig.value(QStringLiteral("model")).toString().trimmed();

    // 1.0.7.9 及更早把 AI 配置平铺在根节点，迁移旧备份时仍能读回
    if (importedKey.isEmpty() && importedBase.isEmpty()) {
        importedKey = root.value(QStringLiteral("aiApiKey")).toString().trimmed();
        importedBase = root.value(QStringLiteral("aiBaseUrl")).toString().trimmed();
    }

    // 备份默认不含 Key；导入时若为空则保留用户当前配置
    if (!importedKey.isEmpty()) {
        aiApiKey_ = importedKey;
    }
    if (!importedBase.isEmpty()) {
        aiBaseUrl_ = importedBase;
    }
    if (!importedModel.isEmpty()) {
        aiModel_ = importedModel;
    }

    lastSelectedWheel_ = root.value(QStringLiteral("lastSelectedWheel")).toString().trimmed();

    soundEnabled_ = root.value(QStringLiteral("soundEnabled")).toBool(true);
    motionEnabled_ = root.value(QStringLiteral("motionEnabled")).toBool(true);
    dynamicWeights_ = root.value(QStringLiteral("dynamicWeights")).toBool(false);

    drawCount_ = std::clamp(root.value(QStringLiteral("drawCount")).toInt(1),
                            kMinDrawCount, kMaxDrawCount);
    const int rawStrategy = root.value(QStringLiteral("drawStrategy")).toInt(0);
    drawStrategy_ = static_cast<xieqian::core::DrawStrategy>(std::clamp(rawStrategy, 0, 2));
    if (drawStrategy_ != xieqian::core::DrawStrategy::Weighted) {
        dynamicWeights_ = false;
    }

    drawEngine_->setDynamicWeights(dynamicWeights_);
    drawEngine_->clearDrawnHistory();
    aiManager_->setApiConfig(aiApiKey_, aiBaseUrl_, aiModel_);
}
