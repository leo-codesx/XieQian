# 撷签 XieQian · AI 协作规范

本文件是 V1.2.5 工程指令的硬约束落盘版，供后续会话自动继承。
原始文档：`.aicode/attachments/撷签XieQian-V1.2.5-aicode-DeepSeek工程指令.md`。

## 编号对照

指令文档使用「§0 事实基线 + §1 协作协议（1.1–1.7）+ §2–§9 技术需求」编号；
早期沟通中出现过「§0 契约 + §1.1–1.3」的简写版。两者内容一致，引用时以本文件内容为准，
不要只按编号推断。

## 0. 项目事实基线（R0 已核实，禁止再凭猜测覆盖）

| 项 | 事实 |
|---|---|
| 定位 | Windows 离线抽签工具；抽奖/名单/历史完全离线，AI 选配。任何校验不得让离线功能依赖网络 |
| 语言/UI | C++20 + Qt 6.8（Qt Quick / Quick Controls 2） |
| 目录 | QML 在 `ui/qml/`（不是 `ui/`）；UI C++ 在 `src/ui/`，入口 `src/ui/main.cpp`；core 在 `src/core` + `include/xieqian` |
| QML 注册 | 无 `qt_add_qml_module`；`ui/qml.qrc`（prefix `/ui`）+ `qmldir`（仅 `singleton Theme 1.0`）。新增 QML 必须同轮登记 qrc |
| Theme | `ui/qml/Theme.qml` 已存在，token 名沿用，禁止重命名；旧值改注释保留 |
| 组件 | `IosSwitch` 已存在（自绘）；`PageHeader/GroupedList/ListRow/PrimaryButton` 由 R1 新增 |
| 存储 | SQLite 单文件 `data.db`，已启用 WAL + synchronous=FULL + busy_timeout；schema 版本是 `PRAGMA user_version`（当前 2），无 `schema_version` 表 |
| 缺失 | `quick_check`/损坏恢复/版本化迁移/数据目录回退链尚未实现 |
| 单实例 | 只有 `QLockFile` + 「已在运行」弹窗；`QLocalServer/QLocalSocket` 缺失 |
| AI | DeepSeek，零信任（源码与安装包无内置 Key），强制 HTTPS（本机除外），备份不含 Key |
| AI 网络 | 超时是 `QTimer::singleShot` + `abort()` 30s；无 `QNetworkInformation`、无 `setTransferTimeout`、无脱敏、无可达性拦截 |
| 分数 | 仅 JS（`ui/qml/components/ExpressionParser.js` 的 gcd/makeFraction/decimalToFraction/formatResult），显示为单行 `5/6 ≈ 0.833`，分母上限 100000 |
| 函数图像 | `plotPaths` 已是分段结构、逐段 moveTo；无数学键盘、无缩放平移 |
| 权重 | 唯一公式 `core::dynamicWeight`（`roster.cpp`）：`base * exp(-drawCount/3) * max(0.1, 1-streak*0.2)`；`weightsVersion` 不存在 |
| 版本点 | 5 处：`CMakeLists.txt`、`src/ui/main.cpp`、`installer.iss`、`ui/qml/pages/AboutPage.qml` 两处硬编码 |
| CI | `windows-latest`，`push main`/`pull_request main`/`tags v*`；Qt 6.8.3 + Catch2 v3.16.1 → CMake Release → ctest → windeployqt → Inno Setup |
| 署名 | README 版权与徽章指向 IoveXCM520，remote 是 leo-codesx；R11 前不得擅自改 |

## 1. 协作硬约束

### 1.1 上下文纪律
- 未给出文件内容前，禁止编造代码引用；一次最多索取 3 个文件。
- 判断依据只允许：R0 事实表、当前贴出的文件原文、CI 报错原文。
- 引用不在事实表内的符号，先读文件，不得凭记忆。

### 1.2 输出纪律
- 禁止整文件重写；使用锚点式补丁（PATCH @ 路径 | 锚点 → DELETE/REPLACE_WITH → CHECK）。
- 新文件可整文件输出，单文件 ≤200 行。
- 单轮 ≤300 行 / ≤5 个补丁块。

### 1.3 编译纪律
- CI 是唯一编译器，禁止声称「本地已验证」。
- 每轮交付一个可独立通过 CI 的最小改动集，一个 commit 一个模块。
- 改动必须编译闭合：改签名同轮给调用点，加 QML 同轮给 qrc 登记。

### 1.4 修复循环
- 只贴第一条错误原文；只改导致该错误的一处。
- ctest 失败先复述断言业务语义，禁止为通过而改测试断言。
- 同类错误连续 2 轮未修复 → 停下，重贴相关文件段落并重写该函数。

### 1.5 幻觉自查
- API 名必须能在仓库现有代码找到出处；不写 Qt5 风格（`QRegExp`、旧迭代）。
- 零信任：禁止把 Key 写入源码/日志/弹窗/备份；诊断输出掩码（前 4 后 4）。
- 不造伪需求（不加 ORM、不换存储引擎）；不重复造轮子（先 grep 既有实现）。
- 数据流改动同轮给 C++ 与 QML 两端补丁，并列出信号/NOTIFY/注册点。
- 禁止 rebase / force push / 删历史 / 改已推 tag；commit 尾缀 `[AI-generated]`。

### 1.6 轮次表（一次只投一轮，CI 全绿才进下一轮）

| 轮 | 主题 | commit 前缀 |
|---|---|---|
| R0 | 侦察（只读，不提交） | — |
| R1 | UI Token 与基础控件 | `feat(ui)` |
| R2 | UI 收口 | `refactor(ui)` |
| R3 | 网络强校验 | `fix(net)` |
| R4 | 存储抗崩溃 | `fix(db)` |
| R5 | 单实例激活 | `fix(life)` |
| R6 | 计算器 | `feat(tool-calc)` |
| R7 | 数学键盘 | `feat(tool-graph)` |
| R8 | 分段渲染 | `fix(tool-graph)` |
| R9 | 高级倒计时 | `feat(tool-timer)` |
| R10 | 权重链路死磕 | `fix(weight)` |
| R11 | 版本与发布 | `chore(release)` |

红线：R1–R2 未完成不得进 R3；每轮不许顺手做后面的轮次。

### 1.7 每轮末尾四件套

```text
FILES_TOUCHED: <相对路径清单>
OPEN_QUESTIONS: <未决项>
NEXT_ROUND_READ: <下轮需提供的文件，≤3>
CI_WATCH: <需重点看的测试/编译单元>
```

## 2. 进度

- R0 完成：14 条事实修正已采纳。
- R1 完成：Theme 改值（radiusCard 16、radiusControl 10、fontLargeTitle 32，textPrimary 保持 #000000）、
  新增 PageHeader/GroupedList/ListRow/PrimaryButton、IosSwitch 改弹簧、SettingsPage 与 AddPage 接入。
- 下一步 R2：剩余页面收口与全站硬编码扫描。
