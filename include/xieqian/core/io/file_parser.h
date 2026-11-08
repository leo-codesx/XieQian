#pragma once

#include <string>

#include "xieqian/core/roster/roster.h"

namespace xieqian::core {

class FileParser
{
public:
    // 按扩展名选择解析方式：
    // - .csv：解析 "姓名,学号(可选),班级(可选),权重(可选)"，空行与表头会被跳过，
    //   权重缺失、无法解析或不是正数时回落为 1.0；
    // - 其它（.txt 等）：把空格、制表符、逗号、换行都当作分隔符，每个片段视为一个姓名。
    // 解析成功后以文件内容替换 roster，失败时保留 roster 原状。
    bool parseFile(const std::string &filePath, Roster &roster);

    // 解析已经完成编码转换的 UTF-8 文本（应用层用 text_decoder 处理 BOM/UTF-16/GBK 后调用）。
    // csvMode=true 走逗号分列，false 走空白分词。失败时 roster 保持原状。
    //
    // 编码探测刻意留在 UI 层（src/ui/text_decoder.cpp）：core 不依赖 Qt 与平台 API，
    // 这样 GBK → Unicode 的转换可以用 QTextCodec（Core5Compat）或 Win32 代码页完成，
    // 而解析规则仍然只有一份、可被 Catch2 直接测试。
    bool parseText(const std::string &text, bool csvMode, Roster &roster);

    // 扩展名是否为 .csv（大小写不敏感）。
    [[nodiscard]] static bool usesCsvFormat(const std::string &filePath);

    [[nodiscard]] const std::string &lastError() const;

private:
    std::string lastError_;
};

}  // namespace xieqian::core
