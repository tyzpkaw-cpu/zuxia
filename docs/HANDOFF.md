# 足下输入法 · 交接手册

> 交接时间：2026-10-01（UTC）｜上一任：Notion AI 会话（额度用尽，整体移交）
> 仓库：https://github.com/tyzpkaw-cpu/zuxia （公开，默认分支 `master`）
> 读者：接手的 AI 助手或工程师。**用户本人完全不懂代码。**
> 本文整份取代 2026-09-29 版。旧版讲的 `decoder-wip` 分支早已并进 0.2.0，照旧版操作会走错路。

## 一、30 秒速览

- **是什么**：Windows 中文输入法。TSF 文本服务 DLL（x64 + x86），内嵌 librime 1.17.0，自写单文件安装器。从「应物输入法」移植，可与应物并存。编码方案叫「应物音形」：全拼 ＋ 结构码 ＋ 部件字母。
- **当前版本 0.3.0**：代码全部在 `master`。交接时最后一个改代码的提交是 `7865481`，CI 全绿：https://github.com/tyzpkaw-cpu/zuxia/actions/runs/36825198996 （安装包 `ZuxiaSetup-0.3.0.exe`，7.95 MB，在这次运行的 artifacts 里）。本手册是其后的纯文档提交。
- **还没发布**：交接时 `v0.3.0` 标签没打，Releases 里只有 `v0.2.0`（Pre-release，指向 `9c042a4`）。发布要用户在网页上点，步骤见 §7.1。**接手第一件事：去 Releases 页看用户发了没有。**
- **0.2.0 真机验过**：用户装过，Explorer / 微信 / Edge WebView / ollama / conhost 全部 `engine-ready: 8183 characters`，无失败事件。
- **0.3.0 没有真机验过**：三个 C++ 修复（§3.3）和 Rime 首次部署耗时，都只过了 CI。
- **接下来做什么**：§8。

## 二、和用户协作（硬性要求）

1. 中文，简洁。用户常只发一句话、一张截图或一段报错；会直接纠错。
2. 用户完全不懂代码。给他的命令必须**整块可粘贴**；他的环境是 Windows 11 自带的 **Windows PowerShell 5.1**（没有 `&&`）。
3. 每次汇报都**如实标注未验证项**；重要结论给证据（命令输出、日志、sha256、码表查询结果）。
4. 用户原话：「给我成熟度高的成品，我不想多看」。做完、验完再交，不要把半成品和一串选项丢给他。确实要他拍板的事，一次问清，并给出推荐项。
5. 用户额度有限，能少一轮就少一轮。
6. 真机问题让用户附 `%LOCALAPPDATA%\Zuxia\zuxia.log`（只记事件，不记打过的字）。
7. 仓库属于用户的 GitHub 账号 `tyzpkaw-cpu`。上一任通过用户授权的 GitHub 连接（MCP）写仓库；换平台后需要用户重新授权。

## 三、编码方案与已拍板的决定（不要擅改；已否决的不要再提）

### 3.1 码怎么构成

- **单字**：全拼 → 结构码 → 部件字母（0.3.0 起最多 **3** 个）。部件字母**顺序不限**；一个部件有多个名称时，每个名称的首字母都认。
- **结构码**：`z` 左右 / `s` 上下 / `b` 包围 / `p` 品字形及兜底 / `d` 独体。以 GF 0013—2009 为准（256 字，生成器强制校验）。插入式字的宿主：`INFIX_HOSTS = {"行":"z", "雔":"z", "衣":"s"}`。
- **部件名称优先级**：人工名称 ＞ GF 0014—2009 国标名称 ＞ 部件自身读音（兜底）。
- **0.3.0 拆分规则**：取 IDS 顶层部件；没有名称的部件再往里拆一层，顶层部件本身仍保留。例：朝 = ⿰龺月；龺 无名，拆出 十(s)、日(r)，龺 本身也保留，字母 g（读音 gān）。所以 `chaozsry` → 朝（唯一），`chaozgy` → 朝潮嘲（3 个），`chaozszy` 是死码（z 对不上朝的任何部件）。
- **词组（列式码）**：全拼串 ＋ 逐位结构串 ＋ 逐位部件串，逐位左对齐。例：`yangzhipengzszmsp` → 杨志鹏，`suyaoszcw` → 苏耀 / 苏瑶 / 苏侥，`suyaoszcwbf` → 苏瑶，`woxiangwen` → 我想问。词表外的词靠列式解码器（`src/Decoder.cpp`）现场拼。原理见 `docs/列式解码.md`（该文停在 2026-09-28，没按 0.3.0 复核）。

### 3.2 用户已拍板

1. 长码的部件顺序不能固定（固定顺序方案已否决，哪怕实测码表更小）。
2. 组字打一半按回车 = 放弃这次组字、什么都不落（0.3.0 已实现）。打英文走中/西切换。
3. 「尧就应该那样拆，不能修」——否决「常用字不展开」之类的优化。
4. 调频：要做，但判据是「码打满没有」，没打满才调频；schema 保持 `enable_user_dict: false`。**未实现。**
5. 解码器不加调频，加回流（已实现：选过的解码结果记进 `%LOCALAPPDATA%\Zuxia\zuxia.decoder.user.tsv`，下次同码前置）。
6. 明确反对通用纠偏。容错只放在规则层（schema 的 `speller/algebra`）。
7. 结构码以 GF 0013—2009 为准。

### 3.3 0.3.0 改了什么

三件事必须同时上线，拆开任何一件都会出问题（只递归、不放宽到三个部件，按字频的重码会从 19.06% 变坏到 23.67%）：

1. 部件名称并入 GF 0014—2009《现代常用字部件及部件名称规范》的 480 条国标名称（追加；国标另有 30 条没有 Unicode 码位，已跳过并记录）；
2. 没有名称的部件再往里拆一层；
3. 一个码最多带三个部件字母（原来两个）。

向后兼容：0.2.0 的 66,505 条单字码一条没丢（`audit_zuxia.py` 现场用 0.2.0 算法重算比对）。

代价（已如实写进 README 和发版说明）：一级、二级部件码首选率退步（89.78%→88.13%，98.16%→96.08%），总击键/字 3.76→3.81（+1.3%）；换来打满唯一 82.45%→85.03%、整字无可猜部件 2365 字→72 字。这是取舍，不是纯改进。

同版修了三个 C++ bug（**只过了 MSVC 编译和 CI 引擎断言，没有真机验证**）：

- `src/KeyEventSink.cpp`：中文模式下 Caps Lock 亮着且没在组字时，字母直通应用，出大写英文（日志标记 `caps-lock-passthrough`）。schema 里的 `ascii_composer.good_old_caps_lock` 对足下从不生效：中/西切换是 TSF 层预留键，Caps Lock 根本到不了 librime。
- `src/RimeEngine.cpp`：组字途中按回车，改为放弃组字、什么都不落。原来是 librime express_editor 的默认行为——把原始码串当英文落进文档，这也是「揽月打一半 ry 落进聊天框」的根因。
- `src/CandidateWindow.cpp`：候选窗宽度下限 220→120，同一次组字只长不缩。**老用户**的 `%LOCALAPPDATA%\Zuxia\设置.txt` 里「最小宽度」仍是 220，要手改成 120（设置文件只在不存在时生成，不会自动改已有的值）。

## 四、仓库结构

| 位置 | 内容 |
|---|---|
| `src/` | TSF 层（`DllMain` `Server` `Register` `TextService` `KeyEventSink` `KeyHandler` `Composition` `EditSession` `EndComposition` `TextEditSink` `ThreadMgrEventSink` `Compartment` `InputMode` `Globals` `Utf` `Diagnostics`）；`RimeEngine.*`：librime 封装 ＋ 解码器接线；`Decoder.*`：列式解码器（唯一能在 Linux 上编的 C++ 文件）；`CandidateWindow.*`：候选窗；`Settings.*` `SettingsApp.cpp`：设置界面 ZuxiaSettings.exe |
| `data/` | git 里只有 `zuxia.schema.yaml`、`default.yaml`。四个大码表是生成物，**不进 git**，CI 和本地现生成（见下表） |
| `data-tools/` | 生成器、审计、解码原型、度量（见下文）；`sources/` 是全部原始数据 |
| `tools/` | `engine-test.cpp`（引擎行为断言，CI 在 Windows 上跑）、`build-engine-test.ps1`、`linux-selftest/`（`windows.h` 桩 ＋ `decoder-selftest.cpp`）、`schema-probe.cpp`、`port_from_yingwu.py` |
| `installer/` | `setup/`：自写单文件安装器与卸载器（`setup.cpp` `uninst.cpp` `make-setup.ps1` `zxcommon.h` `setup.rc` 等）；`RELEASE-NOTES.md`：发版说明，每版重写；`Install-Zuxia.ps1` 等脚本式安装 |
| `scripts/` | `build.ps1` `fetch-librime.ps1` `audit-payload.ps1`（载荷门禁）`verify-install.ps1` `make-sbom.ps1` `sign-file.ps1` `make-icon.ps1` |
| `third_party/librime/` | librime 1.17.0 预编译（含 `rime.dll`），标准发行包，**不带 lua** |
| `.github/workflows/build-installer.yml` | 唯一的 CI，见 §5 |

**生成物（`data/` 下，已 gitignore）**

| 文件 | 规模（0.3.0） | 生成器 |
|---|---|---|
| `zuxia.dict.yaml` 单字码表 | 275,268 行 / 4.4 MB | `data-tools/generate_zuxia.py` |
| `zuxia_char_codes.dict.yaml` 候选注释表 | 完全由单字码表决定 | `data-tools/generate_codes.py` |
| `zuxia.extended.dict.yaml` 词码（schema 的 `dictionary` 指向它，它再 import 单字表） | 619,357 行 / 18.6 MB | `data-tools/generate_phrases.py` |
| `zuxia.decoder.tsv` 解码器数据 | 3.39 MB | `data-tools/generate_phrases.py` |

**其余工具**：`audit_zuxia.py`（码表不变量、版本一致性、安装载荷清单、0.2.0 码向后兼容）、`decode_zuxia.py`（Python 解码原型；改解码逻辑时 Python 和 C++ 两边要同步改，两边都有自检）、`measure_zuxia.py`（编码质量度量）、`sync_structure.py`（对齐应物结构码，需要应物仓库，只能在用户机器上跑）、`build_component_sheet.py`、`simulate_phrase_error.py`。

**原始数据 `data-tools/sources/`**：`hanzi-dictionary.txt`（IDS 拆分，来自 Make Me a Hanzi）、`cjkvi-ids.txt`、`component-names.tsv` / `component-names.yaml`（部件名称）、`gf0014-components.txt`（GF 0014—2009 国标部件名，来自公开转录本）、`gf0013-duti.txt`（GF 0013 独体字）、`structure-overrides.tsv`（结构码人工覆盖）、`8105.dict.yaml` / `base.dict.yaml`（雾凇拼音字表与词表，GPL-3.0）。

**解码器接线（`src/RimeEngine.cpp`）**：`EnsureDecoder()` 用 `std::call_once` 懒加载 `zuxia.decoder.tsv`，日志 `decoder-loaded`；`FillDecodedCandidates()` 把解码候选补在 Rime 候选后面（补位，不是「Rime 没候选才上场」）；`overlay_tail_` 存解码没用上、交回 Rime 的尾巴。

## 五、CI：`.github/workflows/build-installer.yml`

- 触发：push 到 `master`、push `v*` 标签、PR、手动。**没有路径过滤**，只改文档也会全量跑（约 8 分钟）。
- `data` job（ubuntu）：`generate_zuxia.py` → `generate_phrases.py` → `audit_zuxia.py --regen` → `decode_zuxia.py --selftest` → C++ 解码器自检（Linux）→ `measure_zuxia.py`（只进日志）。
- `installer` job（windows）：生成码表 → 读 `VERSION` → 取 librime 1.17.0 → 编译 x64 ＋ x86 → 引擎断言（`tools/engine-test`）→ 打单文件安装包 → 载荷门禁 → 上传安装包与暂存目录 → 发布 Release（仅 `v*` 标签）。
- 发布那一步会比对标签和 `VERSION`：标签必须**正好**是 `v` ＋ VERSION 的内容（现在是 `v0.3.0`），不一致直接失败。
- 断言规模（截至 `7865481`）：码表不变量 27 项（`--regen` 再加 4 项）、C++ 解码器 37 项、Python 解码器 15 项、引擎行为 51 项、载荷门禁 8 组。
- 步骤顺序不能反：audit 要检查生成物；`cmake --install` 要把 `data/` 拷进 dist。

## 六、本地验证

Linux，只需要 python3 和 g++，在仓库根目录跑（全部在 `7865481` 上通过）：

```bash
python3 data-tools/generate_zuxia.py
python3 data-tools/generate_phrases.py
python3 data-tools/generate_codes.py
python3 data-tools/audit_zuxia.py          # 期望 ALL CHECKS PASSED；CI 加 --regen 再验确定性
python3 data-tools/decode_zuxia.py --selftest
mkdir -p /tmp/zx && g++ -std=c++17 -O2 -o /tmp/zx/dsel tools/linux-selftest/decoder-selftest.cpp src/Decoder.cpp -I tools/linux-selftest -I src && /tmp/zx/dsel
python3 data-tools/measure_zuxia.py data/zuxia.dict.yaml --labels 0.3.0
```

Windows（需要 MSVC；先跑上面三个生成器；命令沿用旧手册，本轮没有复核）：

```powershell
pwsh scripts/fetch-librime.ps1
pwsh scripts/build.ps1 -Arch all -Configuration Release
pwsh tools/build-engine-test.ps1 -Arch x64
```

**验收码**（0.3.0 查码表 / 解码器实测）：`chaozsry` 朝（唯一）、`chaozgy` 朝潮嘲、`fuzrfc` 傅俯蝮、`yanzss` 衍滟沇、`haostk` 毫（唯一）、`shuzml` 梳（唯一）、`yuedj` 月（唯一）、`suyaoszcw` 苏耀（首选）、`suyaoszcwbf` 苏瑶、`woxiangwen` 我想问、`yangzhipengzszmsp` 杨志鹏。死码：`chaozszy`、`zuxiasdkq`。写进文档的每一个示例码都必须这样实查，不能凭推理写。

## 七、发版

### 7.1 发布 0.3.0（交接时等用户操作）

1. 打开 https://github.com/tyzpkaw-cpu/zuxia/releases/new
2. Tag 输入框填 `v0.3.0` → 点 `+ Create new tag: v0.3.0 on publish`
3. Target 保持 `master`；标题和说明都不填
4. Release label 选 `Pre-release`
5. 点绿色 `Publish release`，约 8 分钟后 CI 把安装包和校验文件挂到这个 Release 上（0.2.0 的附件是 `ZuxiaSetup-0.2.0.exe`、`.sha256`、`MANIFEST.sha256`、`BUILD-INFO.txt`、`engine-test.log`）

发布后核对：Actions 里 `v0.3.0` 那次运行是绿的；Release 附件里有 `ZuxiaSetup-0.3.0.exe` 和对应的 `.sha256`。

### 7.2 以后每次发版

1. 版本号 7 处同步：`VERSION`、`CMakeLists.txt`、`data/zuxia.schema.yaml`、`installer/setup/zxcommon.h`、`installer/setup/setup.rc`、`src/RimeEngine.cpp`（`distribution_version`）、`installer/RELEASE-NOTES.md`（安装包文件名）；另有三个生成器的 `--version` 默认值。`audit_zuxia.py` 和 `make-setup.ps1` 会强制校验。
2. README、RELEASE-NOTES 里的数字一律用 `measure_zuxia.py` 实测值；示例码逐条查码表或解码器。
3. 推 master → CI 全绿 → 用户按 §7.1 打标签。

### 7.3 给用户的安装话术

1. 先关掉微信、Word、浏览器这些会用输入法的程序（它们会一直挂着旧 DLL，不关就会新旧混用）。
2. 下载 Release 里的 `ZuxiaSetup-0.3.0.exe`。安装包没有代码签名，SmartScreen 会拦：点「更多信息」→「仍要运行」。可以用 `Get-FileHash .\ZuxiaSetup-0.3.0.exe -Algorithm SHA256` 和附件里的 `.sha256` 比对。
3. 装完在任务栏的输入法里切到「足下」。现象和说明对不上时，先注销重登再试。
4. 第一次打字要编译词库，会等一会儿（0.3.0 词库更大，具体多久**没量过**）。
5. 老用户：把 `%LOCALAPPDATA%\Zuxia\设置.txt` 里「最小宽度」从 220 改成 120。
6. 卸载默认保留 `%LOCALAPPDATA%\Zuxia`（约 15 MB：用户词典、回流表、日志、设置）；要彻底清干净，卸载后手动删掉这个文件夹。

## 八、待办（按优先级）

每个任务的背景、证据、要改的文件、验收命令都在 `docs/任务分解.md`，这里只列现状。

1. **发布并真机验收 0.3.0**（用户按 §7.1、§7.3 操作）。验收清单：组字途中按回车不落字；Caps Lock 亮时直出大写英文；候选窗贴合内容；Rime 首次部署等待时间（词码 12.2→18.6 MB，必须实测并写进文档）；§6 的验收码。日志里看 `engine-ready`、`decoder-loaded`、`caps-lock-passthrough`。0.2.0 实测首次按键到 `decoder-loaded` 是 1.42 / 1.81 秒，可作对照。
2. **任务 4 后一半：词码的部件段**。现在每个字只取第一个部件，所以「揽月」只能打 `lanyuezdsj` / `lanyuezdst`，走 览(l)月(y) 那条路没有。约束：Rime 词码全展开不可行（0.2.0 时测过全展开 568 万行 / 150 MB，Rime 编译不动；三个部件只会更大）。可以评估的方向（**未验证，规模未测**）：词码不动，只让解码器的码表认每个字的全部部件字母。
3. **任务 3 残留**（要人工判断，名称要用户拍板）：独体字的部件码仍来自笔画树，「雨」`yudy` / `yudh` 仍排在「与」后面；拆到底仍无名、本身又不是常用字的部件剩 5 种：疋 亅 匸 夊 羋，待定名。
4. **任务 7 拆字窗**（未开始）：可拖动、不抢焦点的独立顶层窗口；米字格只做背景参考线，不要试图把部件画到正确位置（项目没有字形轮廓数据）。不要在 C++ 里重算拆分：让 `generate_zuxia.py` 顺手导出 `data/zuxia.parts.tsv`，窗口只读这张表（读 TSV 可照抄 `src/Decoder.cpp`）。用户的动机：「瘦」的部件码看不清楚。这个窗口同时是编码的验收工具。
5. **任务 8 `dt:` 日期 / `hl:` 农历**（未开始）：librime 不带 lua，只能在 TSF 层 `src/KeyHandler.cpp` 截住前缀、直接出候选。农历必须查表，写明支持范围（比如 1900—2100）和数据来源，不要自己推。两个前缀都要进 `tools/linux-selftest`。
6. **任务 9 学习模式**（未开始）：依赖任务 7。需求只有一句「对打出来的字进行拆解」，先做完任务 7 再找用户确认细节。
7. **调频**（§3.2 第 4 条）：未实现。
8. **文档小欠账**：`docs/应物音形汉字键入方案.md` 第 4 行仍写「方案版本 0.2.0」（正文已有 0.3.0 内容）；`docs/列式解码.md` 没按 0.3.0 复核；用户手里的 docx 版方案和 README 是 0.3.0 之前的版本。
9. **旧账**（来自旧手册，本轮没有复核，动手前先看 `docs/REVIEW.md`）：跟随光标没在 Chrome / Electron / Windows Terminal 上逐一真机细验；`sync_structure.py` 只能在用户机器上跑；无代码签名；ARM64 原生已决定不做；首次部署预编译没做。

## 九、0.3.0 实测数字（`measure_zuxia.py`，按字频加权；括号内为 0.2.0）

- 单字码表 275,268 行 / 4.4 MB（66,505 / 1.0 MB）；8183 字，不同码 181,073，平均每字 33.64 个码（8.13）
- 词条 119,964；词码 619,357 行 / 18.6 MB（421,131 / 12.2 MB）
- 解码器数据 3.39 MB：码 60,052，二元组 161,558（3.0 MB）
- 部件名称：人工 329 条 ＋ 国标并入 480 条；实际用到的部件 646 种，638 种有名称
- 整字一个可猜部件都没有：72 字，占字数 0.88% / 字频 4.72%（2365 字，28.90% / 34.46%）

| 段 | 平均键长 | 首选 | 前三 | 平均候选 | 不同码 |
|---|---:|---:|---:|---:|---:|
| 全拼 | 2.97 | 56.79% | 80.65% | 38.12 | 411 |
| ＋结构 | 3.96 | 74.75% | 93.18% | 12.58 | 1,255 |
| ＋部件1 | 5.03 | 88.13% | 98.33% | 4.79 | 10,615 |
| ＋部件2 | 6.10 | 96.08% | 99.82% | 2.00 | 48,568 |
| ＋部件3 | 7.31 | 98.04% | 99.94% | 1.46 | 120,014 |

0.2.0 对照：＋部件1 4.99 / 89.78% / 98.62% / 4.16 / 8,485；＋部件2 5.99 / 98.16% / 99.96% / 1.43 / 21,522；没有第三级。

- 平均附加键 0.842（0.787）；总击键/字 3.81（3.76）；打满后仍需选字 1.96%（1.84%）
- 打满时唯一的码占比 85.03%（82.45%）；打满仍不唯一的字 2205（字数 26.95% / 字频 26.03%）

## 十、文档阅读顺序

1. 本文
2. `docs/任务分解.md`：「进度」一节 ＋ 未完成任务（3、4、7、8、9）的原文
3. `docs/应物音形汉字键入方案.md`：方案本身——为什么这样设计、编码规则、实测数据。读代码之前先读，免得把有意的取舍当缺陷
4. `README.md`：软件本身——安装、怎么打字、设置、现状与限制、构建
5. `docs/REVIEW.md`：给审核的人——验过什么、没验过什么、已知的坑
6. `docs/列式解码.md`：词组列式码与解码器原理
7. `installer/RELEASE-NOTES.md`：0.3.0 发版说明（给最终用户看的口径）

历史档（0.1.0 / 0.2.0 时期，只备查，结论可能已过期）：`docs/审计发现.md`、`docs/工程排查.md`、`docs/方案升级审计.md`、`docs/待你定夺.md`。

## 十一、工程约束与坑（先读，能省几小时）

**通用**

1. 不要新建或删除 `docs/HANDOFF.md`：两个分支各自新建过一次，add/add 冲突让整个 CI 工作流都不启动。改内容没问题。
2. 改 PR 的 base 分支不会重新触发 CI，必须往 head 分支再推一次。
3. `ZUXIA_COM_GUARD_BEGIN/END` 是函数 try 块：`END` 自带闭合 `}`，函数体末尾不要再补 `}`。
4. Windows runner 控制台是 cp1252，Python print 中文会炸：脚本已把 stdout 改成 utf-8、报告键改成 ASCII，别改回去。
5. `ZuxiaSettings.rc` 自带 RT_MANIFEST，和链接器默认清单会撞（CVT1100 / LNK1123），已用 `/MANIFEST:NO` 解决，别删。
6. 编译选项：`/utf-8 /W4 /EHsc /permissive- /Zc:__cplusplus`（无 `/WX`）、`cxx_std_17`、静态 CRT、`/Brepro`。
7. 解码器用 `char32_t`：字表里有超 BMP 的字，不能用单个 `wchar_t`。
8. 审计的载荷检查只认 `\data\...` 条目。
9. librime 不带 lua：任何 `lua_translator` 方案都走不通，要在 TSF 层做。
10. 改了生成器就要重跑生成器再跑审计；CI 会跑两遍比对确定性。
11. 国标部件表来自公开转录本，上游自述例字经过 OCR，可能有错，**没有人工核对过国标正本**。
12. 不要用 `[skip ci]` 之类的提交信息：用户在网页上打标签时，发布构建也会被跳过。

**如果接着用 Notion AI 的 Linux 沙箱**

13. 沙箱能联网（curl 可用），但没有 GitHub 凭据；写仓库靠 MCP 服务器 `GitHub` 的 `push_files`（能直推 master）。参数文件上限 1 MiB，几 MB 的生成物推不上去——这也是它们不进 git 的原因之一。
14. 沙箱没有 pwsh、没有 MSVC：TSF 层 `.cpp` 在 Linux 上编不过，只有 `src/Decoder.cpp` 能编。Windows 侧只能靠 CI 验。
15. `git reset --hard`、`git checkout HEAD -- .` 会被安全审查拦掉；要取远端版本用 `git show origin/master:<路径> > <路径>`。
16. 旧沙箱里的 `/data/zuxia` 本地 HEAD 停在 `9c042a4`，工作区有约 30 处未提交改动，**不可信**。核对事实一律用 `git show origin/master:<路径>`，或者重新 clone。
17. `/tmp` 在 bash 调用之间会丢，用前先 `mkdir -p`；heredoc 里的 `cd` 可能不生效，Python 脚本里用绝对路径。

## 十二、外包给 DeepSeek（可选）

上一任为省额度，把部分写作活派给 DeepSeek（`deepseek-chat`，temperature 0.2，max_tokens 上限约 8100；调用脚本只在旧沙箱的 `/data/ds.py`，没进仓库）。实测它会漏 import、漏改函数调用、在文档里编造不存在的码——**产出必须机械验收**（跑全套自检、示例码逐条查码表）之后才能用。

API key 不写在任何文档里。旧 key 曾出现在对话记录中，**应由用户吊销**；以后需要就向用户要新 key，存在仓库外（权限 600），绝不进 git。

## 十三、给新平台的启动语（可直接粘贴）

> 这是 Windows 中文输入法项目「足下输入法」，仓库 https://github.com/tyzpkaw-cpu/zuxia （公开，默认分支 master）。请先完整读 `docs/HANDOFF.md`（交接手册），再读 `docs/任务分解.md`。当前版本 0.3.0 代码已在 master、CI 全绿；先去 Releases 页确认 v0.3.0 发了没有。我不懂代码：给我的命令请整块可粘贴（Windows PowerShell 5.1，没有 &&）；没验证过的事请如实标注；做完验完再交给我。四个大码表是生成物、不在 git 里，用 `data-tools/` 下的三个生成器现生成（命令见手册 §6）。