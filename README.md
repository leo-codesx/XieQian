# 撷签（XieQian）

![Windows Build](https://github.com/IoveXCM520/XieQian/actions/workflows/build.yml/badge.svg?branch=main)

面向教育场景的现代离线抽奖系统，基于 C++20 与 Qt 6 构建，适用于课堂提问、活动抽签与名单管理等场景。

## 项目简介

撷签取“撷取签文”之意。核心抽奖、名单与历史数据管理完全离线运行；选配的 DeepSeek AI 功能用于为中奖者撰写颁奖词与图片名单识别，需要网络连接，密钥可在应用内自行配置。

## 核心特性

- **大转盘抽签**：支持一次抽 1–10 人，等概率、防连抽（NoRepeat）与加权三种策略。
- **概率自洽**：扇区宽度由后端给出的有效权重绘制，加权/动态权重模式下所见概率即真实概率。
- **AI 能力**：为中奖者撰写颁奖词；支持从名单截图识别姓名并导入选项，也能把自然语言描述转换成可绘制的函数表达式。
- **教学工具**：高级计算器（括号、乘方、三角与对数，支持精确分数）与函数图像（直角坐标 / 参数方程 / 隐式方程，描点生长动画）。
- **数据存储**：名单、历史、设置与自定义转盘统一存入单文件 SQLite（`data.db`），所有写入走事务；安装时可指定数据目录，适配带还原卡的机房环境。
- **体验细节**：极简卡片视觉、粒子贺卡、音效与动效开关、快捷键与单实例保护。

## 技术栈

| 层面 | 选型 |
| --- | --- |
| 语言 | C++20 |
| UI | Qt 6.8（Qt Quick / Quick Controls 2） |
| 构建 | CMake 3.20+ |
| 网络与存储 | Qt Network、Qt SQL（SQLite 事务存储） |
| 测试 | Catch2 v3 |
| 持续集成 | GitHub Actions（windows-latest） |
| 安装包 | Inno Setup |

## 构建与运行

云端构建由 GitHub Actions 完成：安装 Qt 6.8.3（MSVC 2022 64 位）与 Catch2，执行 CMake Release 配置与编译，运行 ctest，随后用 windeployqt 收集运行时依赖，最后通过 Inno Setup 生成安装包并上传构建产物。

在本机构建需要 CMake 3.20+、Qt 6.8（含 Multimedia、Network、Sql 模块；安装 qt5compat 可使用 QTextCodec 的 GBK 解码，缺失时自动回退 Windows 代码页）、MSVC 2022 与 Catch2 v3：

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest -C Release --test-dir build --output-on-failure
```

需要 AI 功能时，直接在应用内「设置 → AI 助手配置」填写 API Key、API Base URL 与模型名称即可，配置保存在本地数据库的 Config 表中。应用采用零信任模型：**源码与安装包中不包含任何内置密钥**，图片识别、成绩调权、颁奖词与函数生成全部使用你填写的 Key；未配置时相关入口只会弹出引导弹窗，不会发起任何后台请求。接口地址强制使用 HTTPS（本机地址除外），导出的备份文件默认不包含 Key。

推送 `v*` 格式的 tag 会触发自动发布，GitHub Release 附带安装包 `XieQian-Setup-<version>.exe` 与免安装压缩包 `XieQian-Windows.zip`。

## 开源协议

本项目采用 [MIT License](LICENSE) 发布，Copyright (c) 2026 IoveXCM520。
