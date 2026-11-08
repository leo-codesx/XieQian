#pragma once

#include <QString>

namespace xieqian::ui {

// 文本文件解码结果：text 统一为 UTF-8 语义的 QString，
// encoding 用于日志与错误提示，ok=false 时 error 说明原因。
struct DecodedText {
    QString text;
    QString encoding;
    bool ok = false;
    QString error;
};

// 读取并解码文本文件：
// 1. 识别并剥离 UTF-8 / UTF-16 LE / UTF-16 BE 的 BOM；
// 2. 字节流是合法 UTF-8 时直接按 UTF-8 解码；
// 3. 否则按中文 Windows 最常见的 Excel 导出编码处理：
//    - 构建环境提供 Qt6::Core5Compat 时使用 QTextCodec("GB18030")；
//    - 否则（Windows）使用 MultiByteToWideChar(936)；
//    - 再退化为 Latin-1，保证不抛异常、不返回空文本。
DecodedText decodeTextFile(const QString &filePath);

}  // namespace xieqian::ui
