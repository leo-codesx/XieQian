#pragma once

#include <memory>
#include <vector>

#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include "ai_manager.h"
#include "xieqian/core/draw/draw_engine.h"
#include "xieqian/core/roster/roster.h"

class QJsonObject;
class QTimer;

class BackendBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int rosterCount READ rosterCount NOTIFY rosterUpdated)
    Q_PROPERTY(int drawCount READ drawCount NOTIFY drawSettingsChanged)
    Q_PROPERTY(int drawStrategy READ drawStrategy NOTIFY drawSettingsChanged)
    Q_PROPERTY(bool soundEnabled READ soundEnabled WRITE setSoundEnabled NOTIFY soundEnabledChanged)
    Q_PROPERTY(bool motionEnabled READ motionEnabled WRITE setMotionEnabled NOTIFY motionEnabledChanged)
    Q_PROPERTY(AiManager *aiManager READ aiManager CONSTANT)
    Q_PROPERTY(QVariantList customWheels READ customWheels NOTIFY customWheelsChanged)
    Q_PROPERTY(bool dynamicWeights READ dynamicWeights WRITE setDynamicWeights NOTIFY dynamicWeightsChanged)
    Q_PROPERTY(bool storageReady READ storageReady NOTIFY storageReadyChanged)
    Q_PROPERTY(QString storageError READ storageError NOTIFY storageReadyChanged)

public:
    explicit BackendBridge(QObject *parent = nullptr);
    ~BackendBridge() override;

    Q_INVOKABLE QString drawWinner();
    Q_INVOKABLE QVariantList drawMultipleWinners();
    Q_INVOKABLE void updateDrawSettings(int count, int strategy);
    Q_INVOKABLE QString chooseRosterFilePath();
    // 读取名单文件并返回 [{name, studentId, group}]；编码自动识别（UTF-8/UTF-16/GBK）
    Q_INVOKABLE QVariantList readRosterFromFile(const QString &filePath);
    Q_INVOKABLE int appendEntriesToRoster(const QVariantList &entries);
    Q_INVOKABLE int applyScoreWeights(const QVariantList &entries);
    Q_INVOKABLE void resetDrawStats();
    Q_INVOKABLE void appendHistory(const QString &name);
    Q_INVOKABLE QVariantList getParticipantsWithStatus();
    Q_INVOKABLE void toggleParticipantStatus(int index);
    Q_INVOKABLE void updateParticipantWeight(const QString &id, double weight);
    Q_INVOKABLE bool removeParticipant(const QString &id);
    Q_INVOKABLE bool saveCustomWheel(const QString &name, const QVariantList &options);
    Q_INVOKABLE bool removeCustomWheel(const QString &name);
    Q_INVOKABLE QString lastSelectedWheel() const;
    Q_INVOKABLE void setLastSelectedWheel(const QString &name);
    Q_INVOKABLE QVariantList getHistory();
    Q_INVOKABLE void clearHistory();
    // 只清除名单与历史；AI 配置、自定义转盘与偏好设置一律保留
    Q_INVOKABLE void clearAllData();
    Q_INVOKABLE void saveAIConfig(const QString &apiKey, const QString &baseUrl, const QString &model);
    Q_INVOKABLE QVariantMap getAIConfig();
    Q_INVOKABLE bool hasAIConfig() const;
    Q_INVOKABLE bool isFirstRun() const;
    Q_INVOKABLE void markFirstRunSeen();
    Q_INVOKABLE QString chooseRosterImagePath();
    Q_INVOKABLE QString chooseBackupPath();
    Q_INVOKABLE QString chooseRestorePath();
    // 返回是否真正写盘成功；UI 必须据此提示，不能无条件报“成功”
    Q_INVOKABLE bool exportData(const QString &filePath);
    Q_INVOKABLE bool importData(const QString &filePath);
    Q_INVOKABLE QString lastError() const;

    int rosterCount() const;

    int drawCount() const;
    int drawStrategy() const;

    bool soundEnabled() const;
    void setSoundEnabled(bool enabled);

    bool motionEnabled() const;
    void setMotionEnabled(bool enabled);

    AiManager *aiManager() const;

    QVariantList customWheels() const;

    bool dynamicWeights() const;
    void setDynamicWeights(bool enabled);

    bool storageReady() const;
    QString storageError() const;

signals:
    void rosterUpdated();
    void historyUpdated();
    void drawSettingsChanged();
    void winnerDrawn(const QString &name);
    void winnersDrawn(const QStringList &names);
    void soundEnabledChanged();
    void motionEnabledChanged();
    void customWheelsChanged();
    void dynamicWeightsChanged();
    void aiConfigChanged();
    void storageReadyChanged();

private:
    struct HistoryEntry {
        QString name;
        QString time;
    };

    struct CustomWheel {
        QString name;
        QStringList options;
    };

    void recordDraw(const std::vector<xieqian::core::Participant> &winners);
    [[nodiscard]] xieqian::core::DrawStrategy strategyFor() const;
    [[nodiscard]] double effectiveWeight(const xieqian::core::Participant &participant) const;
    [[nodiscard]] std::vector<double> effectiveWeights(const std::vector<xieqian::core::Participant> &pool) const;
    void applyData(const QJsonObject &root);
    [[nodiscard]] QJsonObject stateToJson() const;
    // 合并短时间内的多次改动后落盘；退出与导入时走同步 flush
    void saveState();
    void flushState();
    [[nodiscard]] bool openDatabase();
    void closeDatabase();
    [[nodiscard]] bool ensureSchema();
    [[nodiscard]] bool readState();
    [[nodiscard]] bool writeState();
    // 返回 false 表示发现了旧 JSON 但迁移写盘失败（此时内存中已载入旧数据，不再被空库覆盖）
    [[nodiscard]] bool migrateLegacyJson();
    [[nodiscard]] static QString dataFilePath();
    [[nodiscard]] static QString legacyJsonPath();
    void raiseStorageError(const QString &message);

    std::shared_ptr<xieqian::core::Roster> roster_;
    std::unique_ptr<xieqian::core::DrawEngine> drawEngine_;
    std::unique_ptr<AiManager> aiManager_;
    QSqlDatabase database_;
    QTimer *saveTimer_ = nullptr;
    std::vector<HistoryEntry> history_;
    std::vector<CustomWheel> customWheels_;
    QVector<HistoryEntry> pendingHistory_;
    QString activeWheelName_;
    QString lastSelectedWheel_;
    QString aiApiKey_;
    QString aiBaseUrl_;
    QString aiModel_;
    QString storageError_;
    QString lastError_;
    xieqian::core::DrawStrategy drawStrategy_ = xieqian::core::DrawStrategy::Random;
    int drawCount_ = 1;
    bool soundEnabled_ = true;
    bool motionEnabled_ = true;
    bool dynamicWeights_ = false;
    bool firstRunSeen_ = false;
    bool storageReady_ = false;
    bool dirty_ = false;
    bool historyRewritePending_ = false;
};
