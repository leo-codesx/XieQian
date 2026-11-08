#include "text_decoder.h"

#include <QByteArray>
#include <QFile>
#include <QStringConverter>
#include <QVector>

#include <cstring>
#include <initializer_list>

#if defined(XIEQIAN_HAS_QTEXTCODEC)
#include <QTextCodec>
#endif

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace xieqian::ui {
namespace {

// 名单文件不可能有几十兆；超过上限直接拒绝，避免把内存打满
constexpr qint64 kMaxTextFileBytes = 64LL * 1024 * 1024;

bool hasPrefix(const QByteArray &data, const char *prefix, int length)
{
    return data.size() >= length && std::memcmp(data.constData(), prefix, static_cast<std::size_t>(length)) == 0;
}

QString decodeUtf16(const QByteArray &data, bool littleEndian)
{
    const QByteArray body = data.mid(2);
    QStringDecoder decoder(littleEndian ? QStringConverter::Utf16LE : QStringConverter::Utf16BE);
    const QString text = decoder(body);
    if (decoder.hasError()) {
        return QString();
    }
    return text;
}

// 没有 BOM 的 UTF-16：ASCII 文本里会有一半字节是 0，
// 偶数位为 0 说明是 BE，奇数位为 0 说明是 LE。
bool looksLikeUtf16(const QByteArray &data, bool *littleEndian)
{
    if (data.size() < 8 || (data.size() % 2) != 0) {
        return false;
    }

    const int sample = static_cast<int>(qMin<qint64>(data.size(), 4096));
    int zerosEven = 0;
    int zerosOdd = 0;
    for (int index = 0; index < sample; ++index) {
        if (data.at(index) != '\0') {
            continue;
        }
        if ((index % 2) == 0) {
            ++zerosEven;
        } else {
            ++zerosOdd;
        }
    }

    const int half = sample / 2;
    if (half == 0) {
        return false;
    }
    if (zerosOdd > half / 3 && zerosEven < zerosOdd / 4) {
        *littleEndian = true;
        return true;
    }
    if (zerosEven > half / 3 && zerosOdd < zerosEven / 4) {
        *littleEndian = false;
        return true;
    }
    return false;
}

// 中文 Windows 上 Excel 导出的 CSV 默认是 GBK。
// 优先用 Qt 的 GB18030 编解码器；没有 Core5Compat 时退回系统代码页 936。
QString decodeGbk(const QByteArray &bytes, QString *encoding)
{
#if defined(XIEQIAN_HAS_QTEXTCODEC)
    if (QTextCodec *codec = QTextCodec::codecForName("GB18030")) {
        *encoding = QStringLiteral("GB18030");
        return codec->toUnicode(bytes);
    }
    if (QTextCodec *codec = QTextCodec::codecForName("GBK")) {
        *encoding = QStringLiteral("GBK");
        return codec->toUnicode(bytes);
    }
#endif

#if defined(Q_OS_WIN)
    if (!bytes.isEmpty()) {
        const int byteCount = static_cast<int>(bytes.size());
        // 先严格解码（遇到非法字节直接失败），失败后再放宽，保证能读出内容
        for (const DWORD flags : {static_cast<DWORD>(MB_ERR_INVALID_CHARS), static_cast<DWORD>(0)}) {
            const int length = MultiByteToWideChar(936, flags, bytes.constData(), byteCount, nullptr, 0);
            if (length <= 0) {
                continue;
            }
            QVector<wchar_t> buffer(length);
            const int written =
                MultiByteToWideChar(936, flags, bytes.constData(), byteCount, buffer.data(), length);
            if (written > 0) {
                *encoding = QStringLiteral("GBK");
                return QString::fromWCharArray(buffer.constData(), written);
            }
        }
    }
#endif

    // 最后的兜底：按 Latin-1 逐字节映射，至少不会丢数据或崩溃
    *encoding = QStringLiteral("ISO-8859-1");
    return QString::fromLatin1(bytes);
}

}  // namespace

DecodedText decodeTextFile(const QString &filePath)
{
    DecodedText result;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("无法打开文件：%1").arg(filePath);
        return result;
    }

    if (file.size() > kMaxTextFileBytes) {
        result.error = QStringLiteral("文件超过 64 MB，请拆分后再导入");
        return result;
    }

    const QByteArray data = file.readAll();
    file.close();

    if (data.isEmpty()) {
        result.ok = true;
        result.encoding = QStringLiteral("UTF-8");
        return result;
    }

    // 1. 带 BOM 的编码
    if (hasPrefix(data, "\xEF\xBB\xBF", 3)) {
        const QByteArray body = data.mid(3);
        QStringDecoder decoder(QStringConverter::Utf8);
        const QString text = decoder(body);
        if (decoder.hasError()) {
            result.error = QStringLiteral("UTF-8 BOM 文件解码失败，文件可能已损坏");
            return result;
        }
        result.text = text;
        result.encoding = QStringLiteral("UTF-8 (BOM)");
        result.ok = true;
        return result;
    }
    if (hasPrefix(data, "\xFF\xFE", 2)) {
        const QString text = decodeUtf16(data, true);
        if (text.isNull()) {
            result.error = QStringLiteral("UTF-16 LE 文件解码失败，文件可能已损坏");
            return result;
        }
        result.text = text;
        result.encoding = QStringLiteral("UTF-16 LE");
        result.ok = true;
        return result;
    }
    if (hasPrefix(data, "\xFE\xFF", 2)) {
        const QString text = decodeUtf16(data, false);
        if (text.isNull()) {
            result.error = QStringLiteral("UTF-16 BE 文件解码失败，文件可能已损坏");
            return result;
        }
        result.text = text;
        result.encoding = QStringLiteral("UTF-16 BE");
        result.ok = true;
        return result;
    }

    // 2. 无 BOM 的 UTF-16 启发式判断
    bool littleEndian = true;
    if (looksLikeUtf16(data, &littleEndian)) {
        QStringDecoder decoder(littleEndian ? QStringConverter::Utf16LE : QStringConverter::Utf16BE);
        const QString text = decoder(data);
        if (!decoder.hasError()) {
            result.text = text;
            result.encoding = littleEndian ? QStringLiteral("UTF-16 LE") : QStringLiteral("UTF-16 BE");
            result.ok = true;
            return result;
        }
    }

    // 3. 合法 UTF-8 直接采用（覆盖纯 ASCII）
    {
        QStringDecoder decoder(QStringConverter::Utf8);
        const QString text = decoder(data);
        if (!decoder.hasError()) {
            result.text = text;
            result.encoding = QStringLiteral("UTF-8");
            result.ok = true;
            return result;
        }
    }

    // 4. 中文 Windows 的 ANSI / GBK 回退
    QString encoding;
    result.text = decodeGbk(data, &encoding);
    result.encoding = encoding;
    result.ok = true;
    return result;
}

}  // namespace xieqian::ui
