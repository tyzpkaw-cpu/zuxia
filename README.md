# 应物足下输入法

> 千里之行，始于足下
>
> 预发布阶段。版本号见 [Releases](https://github.com/tyzpkaw-cpu/zuxia/releases)

Windows 平台的汉字输入法，实现的是**应物音形汉字键入方案**：全拼承担读音，结构与部件承担字形，二者按固定次序拼接成编码，每一级都可用，打到候选出现即可停手。

```text
全拼  →  加结构码  →  加一个部件码  →  加第二个部件码
qing  →  qingz     →  qingzs        →  qingzsq        （清）
```

本软件与其方案均出自一人之手，当前为预发布状态，供有兴趣者试用与反馈。

- **方案**——为什么这样设计、编码规则、实测数据：[《应物音形汉字键入方案》](docs/应物音形汉字键入方案.md)
- **代码状态**——哪些验过、哪些没验过、哪些是已知欠账：[docs/REVIEW.md](docs/REVIEW.md)

---

## 一、软件架构

### 1.1 总体结构

应物足下输入法由三个独立部分组成，各自在不同的进程与权限边界内运行：

```
┌─────────────────────────────────────────────────────┐
│  宿主程序（Word、浏览器、记事本……）                  │
│                                                     │
│  ┌──────────────────────────────────────────────┐  │
│  │  ZuxiaTSF.dll  （文本服务，加载进宿主进程）   │  │
│  │                                              │  │
│  │  TSF 层          KeyHandler / EditSession    │  │
│  │  引擎层          RimeEngine ──→ librime      │  │
│  │  解码层          ColumnarDecoder             │  │
│  │  界面层          CandidateWindow             │  │
│  │                  CPartsWindow（拆字窗）       │  │
│  └──────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────┘

ZuxiaSettings.exe  （独立进程，不加载进宿主）
ZuxiaSetup.exe     （安装程序）
```

**ZuxiaTSF.dll** 是核心，以 Windows 文本服务框架（TSF / Text Services Framework）的形式注册，由操作系统加载进每一个接受键盘输入的应用程序进程里。它不是独立运行的程序，而是宿主进程的一部分。

**ZuxiaSettings.exe** 是独立的设置程序，读写同一份纯文本设置文件，但运行在自己的进程里。设置程序出问题，碰不到正在编辑的文档。

### 1.2 TSF 层

TSF 层是软件与 Windows 输入框架之间的接口，负责：

- **按键派发**：`OnKeyDown` / `OnTestKeyDown` 决定哪些键由输入法处理、哪些直接放行给应用。字母键、数字选字键、空格、退格、方向键等由输入法接管；Ctrl、Alt 组合键、功能键直接放行。
- **组字生命周期**：`_EnsureComposition` / `_TerminateComposition` 管理 TSF 的组字范围（Composition Range）。组字中，已打的码串以下划线形式显示在光标位置；落字后组字范围关闭，文本进入文档。
- **编辑会话**：所有对文档的读写必须在 TSF 的编辑会话（Edit Session）里进行，以确保线程安全和撤销栈完整性。`CKeyHandlerEditSession` 封装了一次按键的完整处理流程。
- **焦点与多文档管理**：`ThreadMgrEventSink` / `TextEditSink` 跟踪焦点切换，在不同文档之间切换时正确恢复或清空输入状态。

TSF 层是整个软件中**唯一无法自动化测试**的部分——它的行为依赖真实的 Windows 消息循环与宿主程序配合。目前只能真机手动验证。

### 1.3 引擎层（RimeEngine）

引擎层封装了对 [librime](https://github.com/rime/librime) 的调用。librime 是一个开源的输入法引擎，承担码表查询、候选排序、词频学习等工作。

**RimeEngine 做的事：**
- 懒加载 `rime.dll`（从 DLL 同目录查找，不走系统路径）
- 管理 librime 的 Session 生命周期
- 将 Windows 虚拟键码翻译成 librime 的 XKeySym 格式
- 读取 librime 返回的快照（commit、preedit、candidates）并转换为内部数据结构 `EngineSnapshot`
- 处理中/西切换、Caps Lock 直通、回车放弃组字等特殊按键

**关于启动时间：** librime 的初始化（加载词库、编译数据）在第一次切换到输入法时进行，首次可能需要十余秒。后续启动读已编译好的缓存，通常在 1–2 秒内完成。

### 1.4 解码层（ColumnarDecoder）

列式解码器是对 librime 的**补充**，不是替代。它处理 librime 词表覆盖不到的情形——人名、生僻词组、词表外搭配。

**工作原理：** 应物音形方案的编码是「按列」排的（全拼串＋逐位结构串＋逐位部件串），与词典的「按词条」存储方式不同。列式解码器直接按「列」拆开输入码，用每个字自己的码段去查单字表，现场拼出词组候选，不依赖预先收录的词条。

**补位机制：** librime 给出的候选原样排在前面；解码器补充 librime 没有覆盖到的候选，填满一页剩余的位置。两者并存，不互相干扰。

解码器的完整规则见 [docs/列式解码.md](docs/列式解码.md)。

### 1.5 候选窗（CandidateWindow）

候选窗是纯 Win32 GDI 绘制的浮动窗口，`WS_EX_NOACTIVATE` 防止抢焦点，`WS_EX_TOPMOST` 保持浮在宿主上方。

- **竖排 / 横排**：可在设置里切换
- **宽度自适应**：每次更新候选时重新计算内容宽度，窗口宽度贴合内容，不固定
- **编码注释**：每个候选字旁边显示其完整编码，帮助使用者在打字过程中顺带学码

候选窗不使用 IME 的传统 UI，完全自绘，便于精确控制外观与交互。

### 1.6 拆字窗（CPartsWindow）

拆字窗是学习模式的核心界面（需在设置中开启）。候选高亮时，拆字窗实时显示当前高亮字（或词组各字）的结构、部件、部件名称、拼音与对应字母键。

- **竖排模式（默认）**：每字一行，格在左，完整信息在右，信息不截断
- **横排模式**：多字并排，适合屏幕较宽的场景
- 可自由拖动位置，可拖边框调整大小
- 右键或关闭按钮隐藏；不抢焦点

拆字窗从 `data/zuxia.parts.tsv` 懒加载数据（DLL 上级目录的 `data\` 子目录），首次显示时读入，后续缓存在内存中。

### 1.7 设置系统

设置存储在 `%LOCALAPPDATA%\Zuxia\设置.txt`，UTF-8 纯文本，`键 = 值` 格式，每行一项，井号开头为注释。

**两个读写路径：**
- `ZuxiaSettings.exe`：图形界面，实时预览，每次改动写入文件
- 直接编辑文件：记事本打开即可修改，改完约半秒生效

**设置内容包括：** 字体、字号、行高、内边距、最小/最大宽度、候选排列方向、配色（五套预设或自定义 `#RRGGBB`）、任务栏图标字、拆字窗开关与方向。

### 1.8 数据管道

```
data-tools/
  generate_zuxia.py      →  data/zuxia.dict.yaml（单字码表）
                            data/zuxia.parts.tsv（拆字数据）
  generate_codes.py      →  data/zuxia_char_codes.dict.yaml（候选注释用）
  generate_phrases.py    →  data/zuxia.extended.dict.yaml（词组码表）
                            data/zuxia.decoder.tsv（解码器数据）

data-tools/sources/
  component-names.yaml   →  部件名称表（人工维护）
  gf0013-duti.txt        →  GF 0013—2009 独体字表
  gf0014-components.txt  →  GF 0014—2009 国标名称转录本
```

词组码表（18.6 MB）与单字码表（4.4 MB）不进 git，由 CI 现生成现用。安装包里包含完整的生成产物。

---

## 二、持续集成与质量保证

### 2.1 CI 流程

每次推送到 `master` 或打 `v*` 标签时，GitHub Actions 自动执行：

1. **码表验证**（Linux）：生成单字表和词组表，跑码表不变量自查（27 条），跑 Python 解码器自检（15 条），跑 C++ 解码器自检（37 条）
2. **Windows 编译**：x64＋x86 双架构，MSVC，静态 CRT
3. **引擎断言**（真 Windows Runner）：51 条断言覆盖单字查询、词组查询、词表外人名、解码器补位等
4. **载荷门禁**：清单逐条重算，出货数据与 HEAD 逐字节比对，DLL 里必须有诊断串，载荷里不许夹带测试程序
5. **打包并上传**：生成 `ZuxiaSetup-<版本>.exe`，打 `v*` 标签时挂到 Release

### 2.2 测试覆盖情况

| 层 | 覆盖 | 在哪跑 |
|---|---|---|
| 解码算法（C++） | 37 条断言 | Linux＋CI |
| 解码算法（Python 参考实现） | 15 条断言 | CI |
| 码表不变量、版本一致性 | 27 条 | CI |
| 引擎行为（真 librime、真词表） | 51 条断言 | CI，真 Windows |
| 出货载荷（清单重算、数据比对） | 8 组硬断言 | CI，真 Windows |
| **文本服务（TSF）那一层** | **零** | **只能真机手动** |

TSF 层指按键派发、组字生命周期、编辑会话、候选窗这几件事。**那一层没有任何自动化测试碰得到**，是目前最大的风险所在。

---

## 三、怎么打字

按 `Win + 空格` 切到「应物音形足下输入法」。**首次切换会编译词库，可能需要十几秒。**

### 3.1 基本用法

只会拼音也能用——全拼就是第一级编码：

```text
qing      →  请、轻、清、情、晴 …（候选较多）
```

加一位结构码缩小范围：

```text
qingz     →  左右结构的 qing：请、轻、清、情、晴 …
qings     →  上下结构的 qing：亲、青、擎 …
```

再加部件名称的首字母（下面给的都是首选）：

```text
qingzs    →  氵 叫「水」shuǐ  →  清
qingzy    →  讠 叫「言」yán   →  请
qingzx    →  忄 叫「心」xīn   →  情
qingzr    →  日 读 rì         →  晴
```

结构码五个：`z` 左右、`s` 上下、`b` 包围、`p` 品字形、`d` 独体。

### 3.2 词的打法

词与单字同构，只是按列排：全拼串＋逐位结构串＋逐位部件串。

```text
nihao           →  你好
nihaozz         →  你好（两字都是左右结构）
bushouzd        →  部首（部是左右，首是独体）
suyaoszcw       →  苏耀（首选）/ 苏瑶 / 苏侥
```

词表没收的词（人名、生僻搭配）也能打：

```text
yangzhipengzszmsp  →  杨志鹏
```

### 3.3 按键一览

| 键 | 作用 |
|---|---|
| 数字键 `1`–`9` | 选第 N 个候选 |
| `空格` | 选第一个候选 |
| `↑` `↓` | 在候选之间移动 |
| `PageUp` `PageDown` | 上一页 / 下一页候选 |
| `Backspace` `Delete` | 删一位码 |
| `Esc` | 清空当前输入，不落字 |
| `回车` | 放弃这次组字，什么都不落 |
| `Shift` 轻敲 | 中文 / 西文切换 |
| `Ctrl` + `` ` `` | 方案选单 |

---

## 四、设置

两条路，改的是同一处：

- **设置界面**：开始菜单搜「足下输入法设置」，或在语言栏的「中／西」按钮上右键。带字体选择、取色器、五套配色预设，下方有实时预览。
- **直接改文件**：`%LOCALAPPDATA%\Zuxia\设置.txt`，改完约半秒生效。

**学习模式（拆字窗）** 在设置界面的「拆字窗口（学习模式）」处开启。开启后，候选高亮时会实时弹出拆字窗口，显示该字的结构、部件、名称、拼音与字母键，可自由拖动与调整大小。

---

## 五、当前不足与完善方向

### 5.1 已知限制

**无代码签名。** Windows 会弹出 SmartScreen 警告；启用了 Smart App Control 的系统会直接拒绝安装。这是当前最影响可用性的一项欠缺，短期内无解决计划。

**ARM64 原生应用不可用。** 安装器能装，x86／x64 模拟与 Arm64EC 应用里可用，原生 ARM64 应用不行。根本原因是 ZuxiaTSF.dll 没有 ARM64 原生构建，而 ARM64 原生进程不能加载 x64 DLL。

**升级需重启。** 输入法 DLL 会被加载进每一个接受键盘输入的程序，正在运行的程序会一直挂着旧版本，文件被占用时安装会失败。

**TSF 层无自动化测试。** 按键派发、组字生命周期、编辑会话这一层只能真机手动验证，是目前最大的风险所在。

**词码部件段只取第一个部件。** 词组编码的部件位目前只认每个字最靠前的那一个部件，使用者不能用词组里某字的第二个部件来缩小范围。例如「揽月」无法走 览(l)月(y) 路径。

**部件命名表待人工复核。** 国标并入的 480 条名称来自公开转录本，尚未逐条核对国标正本。

**5 个无名部件。** 疋、亅、匸、夊、羋拆到底仍无通行叫法，遇到含这些部件的字时没有可猜的部件码。

### 5.2 下一步完善方向

**词码部件段（优先级高）。** 让词组编码的部件位支持任意部件，而不只是第一个。这是当前编码体验上最明显的不一致：单字可以任意选部件，词组不行。实现难点在于全展开后的体积，需要解码器而不是词典来支撑这一层。

**反查功能。** 想不起某个字的结构码或部件名时，目前没有任何出路。计划实现一个反查入口（例如 `fz:` 前缀），输入汉字返回其完整编码与拆字信息。

**日期与农历输入。** 计划以 `dt:` 前缀触发日期输入，以 `hl:` 前缀触发农历日期。这一层在 TSF 层实现，不依赖 librime 的 Lua 扩展。

**学习模式完善。** 拆字窗当前显示结构与部件信息，后续计划结合用户选字记录，在反复打错某个字时主动提示正确部件路径。

**代码签名。** 长期目标，需要购置商业证书或申请开源项目签名。

**ARM64 原生支持。** 需要在构建系统中增加 ARM64 目标，并解决 librime 的 ARM64 Windows 构建问题。

**部件命名表人工复核。** 逐条核对国标并入的 480 条名称，修正 OCR 可能引入的错误。

---

## 六、安装

### 6.1 取安装包

到 [Releases](https://github.com/tyzpkaw-cpu/zuxia/releases) 下载 `ZuxiaSetup-<版本>.exe`。

安装包由持续集成现编现打，包内附 `BUILD-INFO.txt`（记明编译来源）与 `MANIFEST.sha256`（可用于逐文件核对完整性）。

### 6.2 安装

右键安装包 → **以管理员身份运行** → 点「安装」。

安装完成后需要**注销重新登录**，或在「设置 → 时间和语言 → 语言和区域 → 键盘」里手动添加。

> **升级时请先重启电脑**，否则旧版 DLL 被占用，安装会提示「无法写入文件」。

### 6.3 未签名

安装包没有代码签名，Windows 会弹出 SmartScreen 警告。若系统启用了 Smart App Control，需在 Windows 安全中心暂时调整后再安装。

### 6.4 卸载

控制面板（设置 → 应用）正常卸载，或运行安装目录里的 `ZuxiaUninstall.exe`。

卸载默认**保留** `%LOCALAPPDATA%\Zuxia`（约 15 MB：用户词典、学习记录、设置、日志）。要彻底清除，卸载后手动删除该文件夹。

---

## 七、从源码构建

需要 Visual Studio 2022（含「使用 C++ 的桌面开发」工作负载）、CMake、Python 3.13。

```powershell
# 生成词组码表（619,357 行 18.6 MB，不在 git 里）
python data-tools/generate_zuxia.py
python data-tools/generate_codes.py
python data-tools/generate_phrases.py

# 渲染图标（仅重新克隆后需要跑一次）
pwsh scripts/make-icon.ps1 -Char 足 -Out src/ZuxiaTSF.ico
pwsh scripts/make-icon.ps1 -Char 足 -Out installer/setup/app.ico

# x64 + x86 编译
pwsh scripts/build.ps1 -Arch all -Configuration Release

# 打单文件安装包
pwsh installer/setup/make-setup.ps1 -SkipBuild
```

### 7.1 不装也能验证引擎

```powershell
pwsh tools/build-engine-test.ps1 -Arch x64
copy tools/engine-test.exe dist/Zuxia/x64/
cd dist/Zuxia/x64
.\engine-test.exe
```

必须在 `x64\` 目录里运行——引擎从自己的模块路径找 `rime.dll` 与 `..\data`。

### 7.2 码表工具（跨平台）

```bash
python3 data-tools/generate_zuxia.py      # 生成单字词典
python3 data-tools/generate_phrases.py    # 生成词组码表
python3 data-tools/measure_zuxia.py data/zuxia.dict.yaml  # 命中率
python3 data-tools/audit_zuxia.py         # 码表不变量自查
python3 data-tools/decode_zuxia.py --selftest  # 解码算法自检
```

---

## 八、隐私

两个文件，性质相反，都只留在本机，都不参与任何上传：

| 文件 | 内容 |
|---|---|
| `zuxia.decoder.user.tsv` | **记录你打了什么、选了什么。** 纯文本，随时可查；删掉即等于忘掉全部 |
| `zuxia.log` | **只记事件，不记内容。** 你打了什么字、选了什么候选，一个字都不进这个文件 |

诊断日志里的文件路径会折成 `%ProgramFiles%`、`%LOCALAPPDATA%` 这类环境变量名，不写出 Windows 用户名。

---

## 九、反馈

到本仓库的 [Issues](https://github.com/tyzpkaw-cpu/zuxia/issues) 提，一个问题一条。

涉及真机行为的，请附上 `%LOCALAPPDATA%\Zuxia\zuxia.log`（只记事件，不含你打的字）。

- 软件问题（崩溃、丢字、候选窗异常、安装失败）：直接提 Issue
- 方案问题（规则好不好学、好不好猜）：见[《应物音形汉字键入方案》](docs/应物音形汉字键入方案.md)
- 代码审阅：请先读 [docs/REVIEW.md](docs/REVIEW.md)

---

## 十、许可

- 自研代码：MIT，见 `LICENSE-CODE.txt`
- 词库数据派生自雾凇拼音（rime-ice）：GPL-3.0
- 汉字结构数据来自 Make Me a Hanzi（LGPL-3.0）与 CJKVI-IDS
- 部件名称数据并入 GF 0014—2009《现代常用字部件及部件名称规范》国标名称
- 文本服务框架源自 Microsoft TSF 示例（MIT），内嵌 librime（BSD 3-Clause）

详见 `licenses/` 与 `THIRD_PARTY_NOTICES.md`。
