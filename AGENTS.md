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
| 组件 | `IosSwitch`；R1 新增 `PageHeader/GroupedList/ListRow/PrimaryButton`；R7/R9/R3.1 新增 `MathKeypad/CountdownTab/DiagnosisDialog` |
| 存储 | SQLite 单文件 `data.db`，WAL + synchronous=FULL + busy_timeout；schema 用 `PRAGMA user_version`（当前 2）；启动后台 `quick_check`（独立连接，主线程最多等 200ms）；损坏时 `wal_checkpoint(TRUNCATE)` + 三件套同时间戳改名 `data_corrupted_<ts>.db`；`user_version` 事务化迁移；数据目录回退链 DataPath → AppData → Home → Temp |
| 已知限制 | 阶跃函数（floor/ceil/round）跳变阈值法漏断；极陡连续曲线放大后可能误断；`e` 条件退格依赖字符判断；`ToolSegment` 260 宽度容纳 3 标签偏紧 |
| 单实例 | `QLockFile` + `QLocalServer/QLocalSocket` 激活通道；次实例唤醒主窗口后静默退出；陈旧锁仅在持有者进程已死时接管；Windows 用 `SW_RESTORE` + `SetForegroundWindow` 兜底 |
| AI | DeepSeek，零信任（源码与安装包无内置 Key），强制 HTTPS（本机除外），备份不含 Key |
| AI 网络 | `QNetworkInformation` 可达性拦截（Online/Unknown 放行，Disconnected/Local/Site 拦截；后端不可用降级放行）、`setTransferTimeout`、错误分类（NET_DNS/NET_CONNECT/NET_TIMEOUT/NET_SSL/API_*）与脱敏诊断（`lastDiagnostic`）；`isRequesting_` 防重入；失败发 `diagnosisRequested` 弹诊断窗 |
| 分数 | 仅 JS（`ExpressionParser.js` 的 gcd/makeFraction/decimalToFraction/formatResult），主行分数 + 副行 6 位近似，分母上限 100000 |
| 函数图像 | `plotPaths` 为分段数组，跳变阈值切段；缩放/拖拽/双击复位；`MathKeypad` 数学键盘；显式模式选择只影响变量键与提示，不改变文本解析 |
| 权重 | 唯一公式 `core::dynamicWeight`（`roster.cpp`）：`base * exp(-drawCount/3) * max(0.1, 1-streak*0.2)`；`weightsVersion` 不存在 |
| 版本点 | 3 处硬编码：`CMakeLists.txt`、`src/ui/main.cpp`、`installer.iss`；AboutPage 读 `Qt.application.version` 运行时版本 |
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

- R0–R11 全部完成（含 R3.1）：设计 token、网络强校验、存储恢复、单实例、计算器、数学键盘、分段渲染/缩放、倒计时、权重与发布准备。
- R12（V1.2.6）已完成：动态权重取证日志与 O(1) 权重索引、抽奖全零兜底、随机种子时间熵、
  名单解析异步化（QtConcurrent）、单实例唤醒排队激活、渲染路径去后端调用、舞台投影改 1px 描边。
- 当前版本 1.2.6；README 署名已指向 leo-codesx（`205f7ad`），原发布阻塞项已清除。
- 发布后核对：`v1.2.6` tag 触发的 `XieQian-Setup-1.2.6.exe` 与 `XieQian-Windows.zip`。
