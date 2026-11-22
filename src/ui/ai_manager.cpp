#include "ai_manager.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkInformation>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QVariantMap>

namespace {

constexpr int kRequestTimeoutMs = 30000;
// 图片上传比纯文本慢得多，OCR 每一步单独放宽超时
constexpr int kOcrTimeoutMs = 60000;
// 单张图片上限，避免 base64 之后把请求体和内存撑爆
constexpr qint64 kMaxImageBytes = 12LL * 1024 * 1024;

// 文本任务与 OCR 都只使用用户在设置页配置的接口，应用内不存在任何内置密钥。
constexpr const char *kDefaultBaseUrl = "https://api.deepseek.com";
constexpr const char *kDefaultModel = "deepseek-flash";

QString categoryPrompt()
{
    return QStringLiteral("这是一张表格或照片。请分析图片中包含哪些类别的数据（如：姓名、年龄、"
                          "爱好、学号）。请只回答你认为最适合用于抽奖的一类数据的名称"
                          "（例如回答：姓名）。");
}

QString extractionPrompt(const QString &category, bool reordered)
{
    if (reordered) {
        return QStringLiteral("请再次提取图片中所有的【%1】。要求：每行一个，不要包含其他多余文字，"
                              "也不要添加编号。")
            .arg(category);
    }

    return QStringLiteral("请提取图片中所有的【%1】。要求：每行一个，不要包含其他多余文字。")
        .arg(category);
}

QString verificationPrompt(const QString &category)
{
    return QStringLiteral("请再次仔细识别图片，提取所有的【%1】，核对总共有多少个。"
                          "每行一个，不要包含其他多余文字。")
        .arg(category);
}

QString scoresPrompt()
{
    return QStringLiteral("请识别这张成绩单或成绩表格图片，提取所有学生的姓名与对应分数。"
                          "严格按 JSON 数组返回：[{\"name\":\"张三\",\"score\":95}]，"
                          "分数只写数字，不要带单位，不要包含其他任何文字");
}

QString functionPrompt(const QString &description)
{
    return QStringLiteral("你是数学绘图助手。用户想画：%1。请返回可以直接绘制的函数表达式，"
                          "严格遵守："
                          "1. 只输出表达式本身，不要解释，不要 Markdown 代码块；"
                          "2. 直角坐标写成 y = ...（变量用 x）；"
                          "3. 参数方程写成 x = ... , y = ...（变量用 t）；"
                          "4. 隐式方程写成 F(x, y) = G(x, y)；"
                          "5. 只使用 x y t 这几个变量，支持 sin cos tan log ln sqrt abs exp 与 ^ 乘方。")
        .arg(description);
}

// 模型常把类别写成“类别：姓名”“姓名（Name）”或带引号，取第一个有效词作为类别
QString cleanCategory(const QString &content)
{
    QString text = content.trimmed();
    if (text.isEmpty()) {
        return QString();
    }

    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (!lines.isEmpty()) {
        text = lines.first().trimmed();
    }

    const int colon = text.indexOf(QChar(u'：'));
    const int asciiColon = text.indexOf(QLatin1Char(':'));
    const int split = colon >= 0 ? colon : asciiColon;
    if (split >= 0) {
        text = text.mid(split + 1).trimmed();
    }

    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch == QChar(u'（') || ch == QLatin1Char('(') || ch == QLatin1Char('[')
            || ch == QChar(u'，') || ch == QChar(u'。') || ch == QLatin1Char('.')
            || ch == QChar(u'；') || ch == QLatin1Char(';')) {
            text = text.left(i).trimmed();
            break;
        }
    }

    const QString wrappers = QStringLiteral("\"'“”‘’`「」《》【】*# ");
    while (!text.isEmpty() && wrappers.contains(text.front())) {
        text.remove(0, 1);
    }
    while (!text.isEmpty() && wrappers.contains(text.back())) {
        text.chop(1);
    }

    // 模型偶尔把答案写成一整句话，命中已知类别时直接采用该词
    static const QStringList knownCategories{QStringLiteral("姓名"), QStringLiteral("名字"),
                                             QStringLiteral("学号"), QStringLiteral("年龄"),
                                             QStringLiteral("爱好"), QStringLiteral("班级"),
                                             QStringLiteral("编号"), QStringLiteral("昵称")};
    for (const QString &category : knownCategories) {
        if (text.contains(category)) {
            return category;
        }
    }

    return text.left(12).trimmed();
}

// 投票：两份一致直接采用；三份都不同时取元素最多的那份（并列取第一份）
QStringList pickBestResult(const QStringList &first, const QStringList &second,
                           const QStringList &third)
{
    if (!first.isEmpty() && first == second) {
        return first;
    }
    if (!first.isEmpty() && first == third) {
        return first;
    }
    if (!second.isEmpty() && second == third) {
        return second;
    }

    const QList<QStringList> candidates{first, second, third};
    QStringList best;
    for (const QStringList &candidate : candidates) {
        if (candidate.size() > best.size()) {
            best = candidate;
        }
    }
    return best;
}

QString responseErrorMessage(const QByteArray &body)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject()) {
        return QString();
    }

    const QJsonObject root = document.object();
    QString message = root.value(QStringLiteral("error"))
                          .toObject()
                          .value(QStringLiteral("message"))
                          .toString()
                          .trimmed();
    if (message.isEmpty()) {
        message = root.value(QStringLiteral("message")).toString().trimmed();
    }
    if (message.size() > 160) {
        message = message.left(160) + QStringLiteral("…");
    }
    return message;
}

QString reachabilityName(QNetworkInformation::Reachability value)
{
    switch (value) {
    case QNetworkInformation::Reachability::Online:
        return QStringLiteral("Online");
    case QNetworkInformation::Reachability::Disconnected:
        return QStringLiteral("Disconnected");
    case QNetworkInformation::Reachability::Local:
        return QStringLiteral("Local");
    case QNetworkInformation::Reachability::Site:
        return QStringLiteral("Site");
    case QNetworkInformation::Reachability::Unknown:
        break;
    }
    return QStringLiteral("Unknown");
}

QString errorKindName(AiErrorKind kind)
{
    switch (kind) {
    case AiErrorKind::None:
        return QStringLiteral("NONE");
    case AiErrorKind::NoNetwork:
        return QStringLiteral("NET_NO_NETWORK");
    case AiErrorKind::Dns:
        return QStringLiteral("NET_DNS");
    case AiErrorKind::Connect:
        return QStringLiteral("NET_CONNECT");
    case AiErrorKind::Timeout:
        return QStringLiteral("NET_TIMEOUT");
    case AiErrorKind::Ssl:
        return QStringLiteral("NET_SSL");
    case AiErrorKind::Unauthorized:
        return QStringLiteral("API_UNAUTHORIZED");
    case AiErrorKind::Server:
        return QStringLiteral("API_SERVER");
    case AiErrorKind::Parse:
        return QStringLiteral("API_PARSE");
    case AiErrorKind::Other:
        break;
    }
    return QStringLiteral("API_OTHER");
}

QString timeoutMessage(int timeoutMs)
{
    return QStringLiteral("请求超时（%1 秒无响应），请稍后重试").arg(qMax(1, timeoutMs / 1000));
}

}  // namespace

AiManager::AiManager(QObject *parent)
    : QObject(parent), network_(std::make_unique<QNetworkAccessManager>())
{
    if (QNetworkInformation *info = networkInformation()) {
        reachability_ = info->reachability();
        connect(info, &QNetworkInformation::reachabilityChanged, this,
                [this](QNetworkInformation::Reachability value) { reachability_ = value; });
    } else {
        qWarning().noquote() << "[NET] reachability backend unavailable, requests are not blocked";
    }
}

AiManager::~AiManager() = default;

QNetworkInformation *AiManager::networkInformation()
{
    static QNetworkInformation *instance = [] {
        if (!QNetworkInformation::loadBackendByFeatures(QNetworkInformation::Feature::Reachability)) {
            qWarning().noquote() << "[NET] loadBackendByFeatures(Reachability) failed";
        }
        return QNetworkInformation::instance();
    }();
    return instance;
}

bool AiManager::networkAvailable() const
{
    // 后端不可用时不阻断：交给请求自身报错，避免把 Unknown 当成离线误杀
    if (networkInformation() == nullptr) {
        return true;
    }

    switch (reachability_) {
    case QNetworkInformation::Reachability::Online:
    case QNetworkInformation::Reachability::Unknown:
        // Unknown 的语义是“无法判定”，乐观放行；真实失败由 NET_DNS/NET_CONNECT 兜底
        return true;
    case QNetworkInformation::Reachability::Disconnected:
    case QNetworkInformation::Reachability::Local:
    case QNetworkInformation::Reachability::Site:
        return false;
    }
    return true;
}

QString AiManager::lastDiagnostic() const
{
    return lastDiagnostic_;
}

QString AiManager::sanitizedUrl(const QUrl &url)
{
    // 只保留 scheme://host/path，query 与 userinfo 一律丢弃
    QUrl clean = url;
    clean.setQuery(QString());
    clean.setFragment(QString());
    clean.setUserInfo(QString());
    const QString text =
        clean.toString(QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo);
    return text.isEmpty() ? QStringLiteral("(invalid-url)") : text;
}

QString AiManager::maskedBody(const QByteArray &body)
{
    if (body.isEmpty()) {
        return QString();
    }

    QString text = QString::fromUtf8(body.left(512));
    static const QRegularExpression keyPattern(QStringLiteral("sk-[A-Za-z0-9_-]{4,}"));
    text.replace(keyPattern, QStringLiteral("sk-***"));
    return text.trimmed();
}

AiErrorInfo AiManager::localFailure(AiErrorKind kind, const QString &message)
{
    AiErrorInfo info;
    info.kind = kind;
    info.userMessage = message;
    info.diagnostic = QStringLiteral("kind=%1").arg(errorKindName(kind));
    return info;
}

void AiManager::rememberDiagnostic(const AiErrorInfo &info)
{
    lastDiagnostic_ = info.diagnostic;
    emit lastDiagnosticChanged();
}

AiErrorInfo AiManager::classifyFailure(QNetworkReply *reply, const QUrl &url,
                                       const QByteArray &body, qint64 elapsedMs, int timeoutMs)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString serverDetail = responseErrorMessage(body);

    AiErrorKind kind = AiErrorKind::Other;
    QString message;

    switch (reply->error()) {
    case QNetworkReply::HostNotFoundError:
        kind = AiErrorKind::Dns;
        message = QStringLiteral("域名解析失败，请检查网络或稍后重试");
        break;
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::ProxyConnectionRefusedError:
    case QNetworkReply::ProxyConnectionClosedError:
    case QNetworkReply::ProxyNotFoundError:
    case QNetworkReply::ProxyTimeoutError:
    case QNetworkReply::ProxyAuthenticationRequiredError:
        kind = AiErrorKind::Connect;
        message = QStringLiteral("无法连接服务器，请稍后重试");
        break;
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError:
        kind = AiErrorKind::Timeout;
        message = timeoutMessage(timeoutMs);
        break;
    case QNetworkReply::SslHandshakeFailedError:
        kind = AiErrorKind::Ssl;
        message = QStringLiteral("安全连接失败，请检查系统时间或证书");
        break;
    default:
        break;
    }

    if (kind == AiErrorKind::Other) {
        if (status == 401 || status == 403) {
            kind = AiErrorKind::Unauthorized;
            message = QStringLiteral("AI 密钥无效或已过期，请到「设置 → AI 助手配置」更新");
        } else if (status == 402) {
            message = QStringLiteral("账户余额不足或未开通该模型");
        } else if (status == 404) {
            message = QStringLiteral("接口地址不存在（404），请检查 Base URL 是否正确");
        } else if (status == 429) {
            message = QStringLiteral("请求过于频繁，请稍后重试");
        } else if (status >= 500) {
            kind = AiErrorKind::Server;
            message = QStringLiteral("服务暂时不可用");
        } else if (!serverDetail.isEmpty()) {
            message = QStringLiteral("AI 接口返回错误：") + serverDetail;
        } else {
            message = QStringLiteral("无法连接服务器，请稍后重试");
        }
    }

    AiErrorInfo info;
    info.kind = kind;
    info.userMessage = message;

    QStringList detail;
    detail << QStringLiteral("kind=%1").arg(errorKindName(kind));
    detail << QStringLiteral("url=%1").arg(sanitizedUrl(url));
    detail << QStringLiteral("http=%1").arg(status);
    detail << QStringLiteral("elapsed=%1ms").arg(elapsedMs);
    detail << QStringLiteral("timeout=%1ms").arg(timeoutMs);
    detail << QStringLiteral("error=%1").arg(reply->errorString());
    if (!serverDetail.isEmpty()) {
        detail << QStringLiteral("server=%1").arg(serverDetail);
    }
    const QString bodyText = maskedBody(body);
    if (!bodyText.isEmpty()) {
        detail << QStringLiteral("body=%1").arg(bodyText);
    }
    info.diagnostic = detail.join(QLatin1Char('\n'));
    return info;
}

void AiManager::setApiConfig(const QString &apiKey, const QString &baseUrl, const QString &model)
{
    apiKey_ = apiKey.trimmed();
    baseUrl_ = baseUrl.trimmed();
    model_ = model.trimmed();
}

bool AiManager::hasApiKey() const
{
    return !apiKey_.isEmpty();
}

QString AiManager::missingKeyMessage()
{
    return QStringLiteral("请前往设置配置 API Key");
}

QString AiManager::effectiveModel() const
{
    return model_.isEmpty() ? QString::fromLatin1(kDefaultModel) : model_;
}

QString AiManager::endpointUrl() const
{
    QString base = baseUrl_.trimmed();
    if (base.isEmpty()) {
        base = QString::fromLatin1(kDefaultBaseUrl);
    }

    if (!base.contains(QStringLiteral("://"))) {
        base.prepend(QStringLiteral("https://"));
    }

    while (base.endsWith(QLatin1Char('/'))) {
        base.chop(1);
    }

    // 去掉协议之外的重复斜杠，避免拼出 https://host//v1/...
    static const QRegularExpression duplicateSlashes(QStringLiteral("(?<!:)/{2,}"));
    base.replace(duplicateSlashes, QStringLiteral("/"));

    // 允许用户填写 https://api.deepseek.com、.../v1，或直接填完整地址
    if (base.endsWith(QStringLiteral("/chat/completions"), Qt::CaseInsensitive)) {
        return base;
    }

    // 大多数兼容 OpenAI 的服务都需要 /v1；已经带版本号（如 /v1、/compatible-mode/v1）时不重复追加
    static const QRegularExpression versionSegment(
        QStringLiteral("(?:^|/)v\\d+(?:beta|alpha)?(?:/|$)"),
        QRegularExpression::CaseInsensitiveOption);
    if (!versionSegment.match(QUrl(base).path()).hasMatch()) {
        base += QStringLiteral("/v1");
    }

    return base + QStringLiteral("/chat/completions");
}

bool AiManager::isSecureEndpoint(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    if (scheme == QStringLiteral("https")) {
        return true;
    }
    if (scheme != QStringLiteral("http")) {
        return false;
    }

    // 本机回环地址允许 http，方便 Ollama 之类的本地推理服务
    const QString host = url.host().toLower();
    return host == QStringLiteral("localhost") || host == QStringLiteral("127.0.0.1")
           || host == QStringLiteral("::1");
}

bool AiManager::ensureApiKey()
{
    if (!apiKey_.isEmpty()) {
        return true;
    }
    emit textAiKeyMissing();
    return false;
}

void AiManager::postChat(const QJsonObject &payload, const QString &key, const QString &endpoint,
                         int timeoutMs, ChatHandler handler)
{
    const QUrl requestUrl(endpoint);

    // 单例请求闸门：避免连点造成并发计费；QML 调用与 finished 回调都在主线程
    if (isRequesting_) {
        AiErrorInfo info = localFailure(AiErrorKind::Other, QStringLiteral("请求进行中，请稍候"));
        handler(false, QString(), info);
        return;
    }

    // 所有 AI 请求都经这里：断网时直接拦下，不发任何请求
    if (!networkAvailable()) {
        AiErrorInfo info = localFailure(AiErrorKind::NoNetwork,
                                        QStringLiteral("当前无网络连接，请检查网络后重试"));
        info.diagnostic = QStringLiteral("kind=NET_NO_NETWORK\nreachability=%1")
                              .arg(reachabilityName(reachability_));
        qWarning().noquote() << "[NET] blocked | reason=no_network | reachability="
                             << reachabilityName(reachability_);
        rememberDiagnostic(info);
        emit diagnosisRequested(info.userMessage);
        handler(false, QString(), info);
        return;
    }

    QNetworkRequest request{requestUrl};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", "Bearer " + key.toUtf8());
    // 传输超时交给 Qt 自己收敛，超时表现为 Timeout/OperationCanceled，不再 QTimer + abort 双轨
    request.setTransferTimeout(timeoutMs);

    QElapsedTimer elapsed;
    elapsed.start();

    isRequesting_ = true;
    QNetworkReply *reply =
        network_->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, handler = std::move(handler), requestUrl, timeoutMs, elapsed]() {
                const QByteArray body = reply->readAll();
                reply->deleteLater();
                isRequesting_ = false;

                if (reply->error() != QNetworkReply::NoError) {
                    const AiErrorInfo info =
                        classifyFailure(reply, requestUrl, body, elapsed.elapsed(), timeoutMs);
                    qWarning().noquote() << "[NET] request failed | kind="
                                         << errorKindName(info.kind)
                                         << "| url=" << sanitizedUrl(requestUrl)
                                         << "| elapsed=" << elapsed.elapsed() << "ms"
                                         << "| reachability=" << reachabilityName(reachability_);
                    // 分类与可达性矛盾时单独打点，便于排查 Windows NLM 行为
                    if ((info.kind == AiErrorKind::Dns || info.kind == AiErrorKind::Connect)
                        && reachability_ == QNetworkInformation::Reachability::Online) {
                        qWarning().noquote() << "[NET] mismatch | classified="
                                             << errorKindName(info.kind)
                                             << "| reachability=Online";
                    }
                    rememberDiagnostic(info);
                    emit diagnosisRequested(info.userMessage);
                    handler(false, QString(), info);
                    return;
                }

                const QJsonDocument document = QJsonDocument::fromJson(body);
                const QJsonArray choices =
                    document.object().value(QStringLiteral("choices")).toArray();
                if (choices.isEmpty()) {
                    AiErrorInfo info = localFailure(
                        AiErrorKind::Parse,
                        document.isObject()
                            ? QStringLiteral("AI 返回内容为空")
                            : QStringLiteral("AI 返回数据异常，请检查 Base URL 是否指向兼容 OpenAI 的接口"));
                    info.diagnostic = QStringLiteral("kind=API_PARSE\nhttp=200\nbody=%1")
                                          .arg(maskedBody(body));
                    qWarning().noquote() << "[NET] parse failed | url=" << sanitizedUrl(requestUrl);
                    rememberDiagnostic(info);
                    emit diagnosisRequested(info.userMessage);
                    handler(false, QString(), info);
                    return;
                }

                const QString content = choices.first()
                                            .toObject()
                                            .value(QStringLiteral("message"))
                                            .toObject()
                                            .value(QStringLiteral("content"))
                                            .toString()
                                            .trimmed();
                if (content.isEmpty()) {
                    AiErrorInfo info = localFailure(AiErrorKind::Parse,
                                                    QStringLiteral("AI 返回内容为空"));
                    info.diagnostic = QStringLiteral("kind=API_PARSE\nhttp=200\nbody=%1")
                                          .arg(maskedBody(body));
                    rememberDiagnostic(info);
                    emit diagnosisRequested(info.userMessage);
                    handler(false, QString(), info);
                    return;
                }

                handler(true, content, AiErrorInfo{});
            });
}

void AiManager::generateAwardSpeech(const QString &winnerName)
{
    const QString winner = winnerName.trimmed();
    if (winner.isEmpty()) {
        emit awardSpeechFailed(QStringLiteral("中奖者姓名为空"));
        return;
    }

    requestCompletion(
        QStringLiteral("请为抽奖中奖者【%1】写一句 20 字以内的幽默/励志颁奖词").arg(winner),
        RequestKind::AwardSpeech);
}

void AiManager::generateFunction(const QString &prompt)
{
    const QString description = prompt.trimmed();
    if (description.isEmpty()) {
        emit functionFailed(QStringLiteral("先描述一下想要画的图形"));
        return;
    }

    requestCompletion(functionPrompt(description), RequestKind::FunctionExpression);
}

void AiManager::requestCompletion(const QString &prompt, RequestKind kind)
{
    if (!ensureApiKey()) {
        // 文本任务绝不发任何后台请求，也不静默失败：通知界面引导用户去设置页
        emitFailure(kind, missingKeyMessage());
        return;
    }

    const QString endpoint = endpointUrl();
    const QUrl url(endpoint);
    if (!url.isValid() || url.host().isEmpty()) {
        emitFailure(kind, QStringLiteral("AI 接口地址无效，请在设置页检查"));
        return;
    }
    if (!isSecureEndpoint(url)) {
        emitFailure(kind, QStringLiteral("AI 接口地址必须使用 https（本机地址除外）"));
        return;
    }

    QJsonObject message;
    message.insert(QStringLiteral("role"), QStringLiteral("user"));
    message.insert(QStringLiteral("content"), prompt);

    QJsonArray messages;
    messages.append(message);

    QJsonObject payload;
    payload.insert(QStringLiteral("model"), effectiveModel());
    payload.insert(QStringLiteral("messages"), messages);
    payload.insert(QStringLiteral("stream"), false);

    postChat(payload, apiKey_, endpoint, kRequestTimeoutMs,
             [this, kind](bool ok, const QString &content, const AiErrorInfo &error) {
                 if (!ok) {
                     emitFailure(kind, error.userMessage);
                     return;
                 }

                 if (kind == RequestKind::FunctionExpression) {
                     const QString expression = cleanExpression(content);
                     if (expression.isEmpty()) {
                         emit functionFailed(QStringLiteral("AI 返回的表达式无法解析"));
                         return;
                     }
                     emit functionGenerated(expression);
                     return;
                 }

                 const QString speech = cleanSpeech(content);
                 if (speech.isEmpty()) {
                     emitFailure(kind, QStringLiteral("AI 返回内容为空"));
                     return;
                 }
                 emit awardSpeechReady(speech);
             });
}

void AiManager::postOcrChat(const QString &prompt, int timeoutMs, ChatHandler handler)
{
    if (!ensureApiKey()) {
        AiErrorInfo info = localFailure(AiErrorKind::Other, missingKeyMessage());
        rememberDiagnostic(info);
        handler(false, QString(), info);
        return;
    }

    const QString endpoint = endpointUrl();
    const QUrl url(endpoint);
    if (!url.isValid() || url.host().isEmpty()) {
        AiErrorInfo info = localFailure(AiErrorKind::Other,
                                        QStringLiteral("AI 接口地址无效，请在设置页检查"));
        rememberDiagnostic(info);
        handler(false, QString(), info);
        return;
    }
    if (!isSecureEndpoint(url)) {
        AiErrorInfo info = localFailure(
            AiErrorKind::Other, QStringLiteral("AI 接口地址必须使用 https（本机地址除外）"));
        rememberDiagnostic(info);
        handler(false, QString(), info);
        return;
    }

    QJsonObject imageUrl;
    imageUrl.insert(QStringLiteral("url"), ocrImageUrl_);
    // high 等价于原图，保留表格里的小字
    imageUrl.insert(QStringLiteral("detail"), QStringLiteral("high"));

    QJsonObject imagePart;
    imagePart.insert(QStringLiteral("type"), QStringLiteral("image_url"));
    imagePart.insert(QStringLiteral("image_url"), imageUrl);

    QJsonObject textPart;
    textPart.insert(QStringLiteral("type"), QStringLiteral("text"));
    textPart.insert(QStringLiteral("text"), prompt);

    QJsonArray content;
    content.append(imagePart);
    content.append(textPart);

    QJsonObject message;
    message.insert(QStringLiteral("role"), QStringLiteral("user"));
    message.insert(QStringLiteral("content"), content);

    QJsonArray messages;
    messages.append(message);

    QJsonObject payload;
    payload.insert(QStringLiteral("model"), effectiveModel());
    payload.insert(QStringLiteral("messages"), messages);
    payload.insert(QStringLiteral("stream"), false);

    postChat(payload, apiKey_, endpoint, timeoutMs, std::move(handler));
}

bool AiManager::prepareOcrImage(const QString &filePath)
{
    ocrImageUrl_.clear();

    if (filePath.isEmpty()) {
        qWarning() << "empty image path for OCR";
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "cannot open image for OCR:" << filePath;
        return false;
    }

    if (file.size() > kMaxImageBytes) {
        qWarning() << "image too large for OCR:" << filePath;
        return false;
    }

    const QByteArray raw = file.readAll();
    file.close();
    if (raw.isEmpty()) {
        qWarning() << "empty image for OCR:" << filePath;
        return false;
    }

    const QString suffix = QFileInfo(filePath).suffix().toLower();
    QString mime = QStringLiteral("image/jpeg");
    if (suffix == QStringLiteral("png")) {
        mime = QStringLiteral("image/png");
    } else if (suffix == QStringLiteral("webp")) {
        mime = QStringLiteral("image/webp");
    } else if (suffix == QStringLiteral("bmp")) {
        mime = QStringLiteral("image/bmp");
    } else if (suffix == QStringLiteral("gif")) {
        mime = QStringLiteral("image/gif");
    } else if (suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg")) {
        mime = QStringLiteral("image/jpeg");
    }

    ocrImageUrl_ = QStringLiteral("data:%1;base64,%2")
                       .arg(mime, QString::fromLatin1(raw.toBase64()));
    return true;
}

void AiManager::ocrFromImage(const QString &filePath)
{
    ocrCategory_.clear();
    ocrFirstPass_.clear();
    ocrSecondPass_.clear();

    if (!ensureApiKey()) {
        failOcr(missingKeyMessage());
        return;
    }

    if (!prepareOcrImage(filePath)) {
        failOcr(QStringLiteral("图片识别失败，请重试或手动导入（图片需小于 12 MB）"));
        return;
    }

    emit ocrProgress(QStringLiteral("AI 正在分析数据结构..."));
    requestOcrCategory();
}

void AiManager::ocrScoresFromImage(const QString &filePath)
{
    if (!ensureApiKey()) {
        failOcr(missingKeyMessage());
        return;
    }

    if (!prepareOcrImage(filePath)) {
        failOcr(QStringLiteral("图片识别失败，请重试或手动导入（图片需小于 12 MB）"));
        return;
    }

    emit ocrProgress(QStringLiteral("AI 正在读取成绩单..."));
    postOcrChat(scoresPrompt(), kOcrTimeoutMs,
                [this](bool ok, const QString &content, const AiErrorInfo &error) {
                    if (!ok) {
                        failOcr(error.userMessage);
                        return;
                    }

                    const QVariantList entries = parseScores(content);
                    if (entries.isEmpty()) {
                        failOcr(QStringLiteral("没能从成绩单里识别出姓名与分数"));
                        return;
                    }

                    emit ocrScoresReady(entries);
                });
}

void AiManager::requestOcrCategory()
{
    postOcrChat(categoryPrompt(), kOcrTimeoutMs,
                [this](bool ok, const QString &content, const AiErrorInfo &error) {
                    if (!ok) {
                        failOcr(error.userMessage);
                        return;
                    }

                    ocrCategory_ = cleanCategory(content);
                    if (ocrCategory_.isEmpty()) {
                        ocrCategory_ = QStringLiteral("姓名");
                    }

                    emit ocrProgress(QStringLiteral("正在提取名单数据..."));
                    requestOcrExtraction();
                });
}

void AiManager::requestOcrExtraction()
{
    postOcrChat(extractionPrompt(ocrCategory_, false), kOcrTimeoutMs,
                [this](bool ok, const QString &content, const AiErrorInfo &error) {
                    if (!ok) {
                        failOcr(error.userMessage);
                        return;
                    }

                    ocrFirstPass_ = parseNameLines(content);
                    if (ocrFirstPass_.isEmpty()) {
                        failOcr(QStringLiteral("图片识别失败，请重试或手动导入"));
                        return;
                    }

                    emit ocrProgress(QStringLiteral("正在二次校验提取结果..."));
                    postOcrChat(extractionPrompt(ocrCategory_, true), kOcrTimeoutMs,
                                [this](bool verified, const QString &secondContent,
                                       const AiErrorInfo &) {
                                    // 第二遍失败时直接采用第一遍的结果，不再要求用户重来
                                    if (!verified) {
                                        emit ocrRosterReady(ocrFirstPass_);
                                        return;
                                    }

                                    ocrSecondPass_ = parseNameLines(secondContent);
                                    if (ocrSecondPass_ == ocrFirstPass_) {
                                        emit ocrRosterReady(ocrFirstPass_);
                                        return;
                                    }

                                    requestOcrVerification();
                                });
                });
}

void AiManager::requestOcrVerification()
{
    postOcrChat(verificationPrompt(ocrCategory_), kOcrTimeoutMs,
                [this](bool ok, const QString &content, const AiErrorInfo &) {
                    QStringList third;
                    if (ok) {
                        third = parseNameLines(content);
                    }

                    const QStringList best = pickBestResult(ocrFirstPass_, ocrSecondPass_, third);
                    if (best.isEmpty()) {
                        failOcr(QStringLiteral("图片识别失败，请重试或手动导入"));
                        return;
                    }

                    emit ocrRosterReady(best);
                });
}

void AiManager::failOcr(const QString &message)
{
    qWarning() << "OCR failed:" << message;
    emit ocrFailed(message);
}

QStringList AiManager::parseNameLines(const QString &content)
{
    // 校验轮次的返回值里常带“共 42 个”这类汇总行，不能当成人名；
    // 纯数字不算汇总，避免把“学号”类别整列误删
    static const QRegularExpression summaryPattern(
        QStringLiteral("^(?:共|共有|总共|合计|一共|总计)\\s*\\d+\\s*(?:个|人|名|条|行)?"
                       "(?:姓名|名字)?[。.]?$"));
    static const QRegularExpression countPattern(
        QStringLiteral("^\\d+\\s*(?:个|人|名|条|行)(?:姓名|名字)?[。.]?$"));

    QStringList names;
    const QStringList lines = content.split(QLatin1Char('\n'), Qt::SkipEmptyParts);

    for (QString line : lines) {
        line = line.trimmed();

        if (summaryPattern.match(line).hasMatch() || countPattern.match(line).hasMatch()) {
            continue;
        }

        // 模型常把结果写成 “1. 张三” 或 “- 张三”，去掉列表前缀再进名单
        int prefix = 0;
        while (prefix < line.size() && line.at(prefix).isDigit()) {
            ++prefix;
        }
        if (prefix > 0 && prefix < line.size()) {
            const QChar separator = line.at(prefix);
            if (separator == QLatin1Char('.') || separator == QChar(u'、')
                || separator == QLatin1Char(')') || separator == QLatin1Char(' ')) {
                line = line.mid(prefix + 1).trimmed();
            }
        }
        while (!line.isEmpty()
               && (line.front() == QLatin1Char('-') || line.front() == QLatin1Char('*')
                   || line.front() == QChar(u'·'))) {
            line.remove(0, 1);
            line = line.trimmed();
        }

        if (!line.isEmpty() && !names.contains(line)) {
            names.append(line);
        }
    }

    return names;
}

QVariantList AiManager::parseScores(const QString &content)
{
    const int start = content.indexOf(QLatin1Char('['));
    const int end = content.lastIndexOf(QLatin1Char(']'));
    if (start < 0 || end <= start) {
        return {};
    }

    const QJsonDocument document =
        QJsonDocument::fromJson(content.mid(start, end - start + 1).toUtf8());
    if (!document.isArray()) {
        return {};
    }

    QVariantList entries;
    for (const QJsonValue &value : document.array()) {
        const QJsonObject entry = value.toObject();
        const QString name = entry.value(QStringLiteral("name")).toString().trimmed();

        // 模型有时把分数写成 "95 分" 或字符串，统一取第一个数字
        QString rawScore = entry.value(QStringLiteral("score")).toVariant().toString();
        static const QRegularExpression numberPattern(QStringLiteral("-?\\d+(?:\\.\\d+)?"));
        const QRegularExpressionMatch match = numberPattern.match(rawScore);
        if (name.isEmpty() || !match.hasMatch()) {
            continue;
        }

        bool ok = false;
        const double score = match.captured(0).toDouble(&ok);
        if (!ok) {
            continue;
        }

        QVariantMap participant;
        participant.insert(QStringLiteral("name"), name);
        participant.insert(QStringLiteral("score"), score);
        entries.append(participant);
    }
    return entries;
}

QString AiManager::cleanExpression(const QString &content)
{
    QStringList lines;
    for (QString line : content.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QStringLiteral("```"))) {
            continue;
        }

        // “表达式：y = x^2”这类前缀不属于表达式本身
        const int separator = line.indexOf(QChar(u'：'));
        if (separator > 0 && separator <= 6) {
            line = line.mid(separator + 1).trimmed();
        }
        if (!line.isEmpty()) {
            lines.append(line);
        }
    }

    if (lines.isEmpty()) {
        return QString();
    }

    // 参数方程常被写成两行，合并成一行交给 QML 解析
    return lines.join(QStringLiteral(", "));
}

void AiManager::emitFailure(RequestKind kind, const QString &message)
{
    if (kind == RequestKind::FunctionExpression) {
        emit functionFailed(message);
    } else {
        emit awardSpeechFailed(message);
    }
}

QString AiManager::cleanSpeech(const QString &content)
{
    QString speech = content.trimmed();
    const QString quotes = QStringLiteral("\"'“”‘’");

    while (!speech.isEmpty() && quotes.contains(speech.front())) {
        speech.remove(0, 1);
    }
    while (!speech.isEmpty() && quotes.contains(speech.back())) {
        speech.chop(1);
    }
    return speech.trimmed();
}
