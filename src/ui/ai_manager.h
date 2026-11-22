#pragma once

#include <functional>
#include <memory>

#include <QNetworkInformation>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

class QJsonObject;
class QNetworkAccessManager;
class QNetworkReply;
class QUrl;

enum class AiErrorKind {
    None,
    NoNetwork,
    Dns,
    Connect,
    Timeout,
    Ssl,
    Unauthorized,
    Server,
    Parse,
    Other
};

// 用户只看 userMessage；diagnostic 已脱敏，供「查看诊断」与日志使用
struct AiErrorInfo {
    AiErrorKind kind = AiErrorKind::None;
    QString userMessage;
    QString diagnostic;

    [[nodiscard]] bool failed() const { return kind != AiErrorKind::None; }
};

class AiManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString lastDiagnostic READ lastDiagnostic NOTIFY lastDiagnosticChanged)

public:
    explicit AiManager(QObject *parent = nullptr);
    ~AiManager() override;

    // 全局唯一的网络状态入口：后端不可用（nullptr）时返回 nullptr，调用方降级放行
    [[nodiscard]] static QNetworkInformation *networkInformation();
    [[nodiscard]] bool networkAvailable() const;
    [[nodiscard]] QString lastDiagnostic() const;

    // 颁奖词：使用用户在设置页配置的文本 API Key
    Q_INVOKABLE void generateAwardSpeech(const QString &winnerName);

    // 图片 OCR：与文本任务共用同一份用户配置（零信任，内置无任何密钥）
    Q_INVOKABLE void ocrFromImage(const QString &filePath);

    // 成绩单 OCR：返回 [{name, score}]，用于开发者页的 AI 调权
    Q_INVOKABLE void ocrScoresFromImage(const QString &filePath);

    // 自然语言转函数表达式，结果可直接填进函数图像页
    Q_INVOKABLE void generateFunction(const QString &prompt);

    // 用户配置优先；为空表示未配置，所有 AI 入口都应引导到设置页
    void setApiConfig(const QString &apiKey, const QString &baseUrl, const QString &model);

    [[nodiscard]] bool hasApiKey() const;

signals:
    void awardSpeechReady(const QString &speech);
    void awardSpeechFailed(const QString &message);
    // 文本 AI 未配置用户密钥时发出，由界面弹窗引导去设置页
    void textAiKeyMissing();
    void ocrRosterReady(const QStringList &names);
    void ocrScoresReady(const QVariantList &entries);
    void ocrFailed(const QString &message);
    void ocrProgress(const QString &message);
    void functionGenerated(const QString &expression);
    void functionFailed(const QString &message);
    void lastDiagnosticChanged();
    // 网络/接口类失败时通知上层弹诊断窗；未配置 Key 等本地错误不发
    void diagnosisRequested(const QString &message);

private:
    enum class RequestKind { AwardSpeech, FunctionExpression };

    using ChatHandler = std::function<void(bool ok, const QString &content, const AiErrorInfo &error)>;

    void requestCompletion(const QString &prompt, RequestKind kind);
    void emitFailure(RequestKind kind, const QString &message);
    [[nodiscard]] QString effectiveModel() const;
    [[nodiscard]] QString endpointUrl() const;
    [[nodiscard]] bool ensureApiKey();
    [[nodiscard]] static bool isSecureEndpoint(const QUrl &url);
    [[nodiscard]] static QString missingKeyMessage();
    [[nodiscard]] static QVariantList parseScores(const QString &content);
    [[nodiscard]] static QString cleanExpression(const QString &content);
    [[nodiscard]] static QString cleanSpeech(const QString &content);
    [[nodiscard]] static QStringList parseNameLines(const QString &content);

    void postChat(const QJsonObject &payload, const QString &key, const QString &endpoint,
                  int timeoutMs, ChatHandler handler);
    void postOcrChat(const QString &prompt, int timeoutMs, ChatHandler handler);
    void requestOcrCategory();
    void requestOcrExtraction();
    void requestOcrVerification();
    void failOcr(const QString &message);
    [[nodiscard]] bool prepareOcrImage(const QString &filePath);

    [[nodiscard]] static AiErrorInfo classifyFailure(QNetworkReply *reply, const QUrl &url,
                                                     const QByteArray &body, qint64 elapsedMs,
                                                     int timeoutMs);
    [[nodiscard]] static AiErrorInfo localFailure(AiErrorKind kind, const QString &message);
    [[nodiscard]] static QString sanitizedUrl(const QUrl &url);
    [[nodiscard]] static QString maskedBody(const QByteArray &body);
    void rememberDiagnostic(const AiErrorInfo &info);

    std::unique_ptr<QNetworkAccessManager> network_;
    QNetworkInformation::Reachability reachability_ = QNetworkInformation::Reachability::Unknown;
    QString lastDiagnostic_;
    bool isRequesting_ = false;
    QString apiKey_;
    QString baseUrl_;
    QString model_;

    QString ocrImageUrl_;
    QString ocrCategory_;
    QStringList ocrFirstPass_;
    QStringList ocrSecondPass_;
};
