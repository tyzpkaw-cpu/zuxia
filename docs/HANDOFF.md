# 足下输入法 · 交接手册

> 交接时间：2026-09-29（UTC）｜来源：Notion AI 会话交接
> 仓库：https://github.com/tyzpkaw-cpu/zuxia （公开）
> 读者：接手工作的 AI 助手或工程师。用户本人不懂代码——向他汇报请用「可整块粘贴」的命令，并如实标注未验证项。
>
> 最近更新：2026-09-29 —— 第四节收尾清单的 1／2／4／5 项已做掉（含一处审计误报的修正），
> 只剩「Windows／MSVC 真编译」这一关，已改由 CI 的 `pull_request` 触发器来跑。

## 一、30 秒速览
- **足下输入法**：Windows 中文输入法（TSF 文本服务，x64 + x86，内嵌 librime 1.17.0，自写单文件安装器），从「应物输入法」（衡码）移植，可与应物并存。
- **master 已完成**：任务栏「足」图标、词组码表（约 12 万词）、独立设置界面（ZuxiaSettings.exe）。
- **进行中**：列式解码器（用户点名要 `yangzhipengzszmsp` 这种打法）。代码已写好、Linux 侧全部验证过，在分支 **`decoder-wip`** 上，**未合并**（已开 PR，让 CI 去跑 Windows 编译）。收尾清单见第四节。
- 用户机器：Windows 11；仓库克隆在 `C:\Users\拭有舞月厢\Desktop\df\yingwu-zuxia`；用 Windows PowerShell 5.1（**没有 `&&`**，命令要整块粘贴、逐行执行）。
- （若沿用 Notion AI 沙箱）Linux、无外网；仓库副本 `/data/proj/yingwu-zuxia`；只有 g++，没有 MSVC/mingw。

## 二、打法设计（已定案，勿随意改）
**单字**：全拼 → 结构码（z 左右 / s 上下 / b 包围 / p 品字形及兜底 / d 独体）→ 任意部件1 → 任意部件2。顺序不限，一个部件多个名称都认。

**词组（列式码）**：全拼串 ＋ 逐位结构串 ＋ 逐位部件串（逐位左对齐）。
- 苏瑶 = suyao + sz + cw → `suyaoszcw`
- 你好 = nihao + zz + rn → `nihaozzrn`
- 杨志鹏 = yangzhipeng + zsz + msp → `yangzhipengzszmsp`

**补位规则**（解码器 `c` 表的关键，生成器里实现）：
- 单部件字：第二位重写第一位；
- 无部件字：部件位写拼音首字母（例：`rup`、`rupr` → 入）。

详述见仓库 `docs/列式解码.md`；Python 原型 `data-tools/decode_zuxia.py`（带 11 条自检，CI 里跑）。

## 三、仓库结构与关键文件
- `src/`：`RimeEngine.h/.cpp`（引擎封装 + 解码器接线）、`Decoder.h/.cpp`（新，列式解码器）、`KeyHandler.cpp`、`TextService.cpp`、`SettingsApp.cpp`、`InputMode.cpp`、`CandidateWindow.cpp` 等。
- `data/`：`zuxia.dict.yaml`（单字表，**勿动**）、`zuxia.schema.yaml`、`zuxia_char_codes.dict.yaml`；生成物（gitignore）：`zuxia.extended.dict.yaml`（词组码表，12.2 MB）、`zuxia.decoder.tsv`（解码数据，3.1 MB）——由 `python data-tools/generate_phrases.py` 现生成。
- `data-tools/`：`generate_phrases.py`（词组 + 解码数据生成器）、`audit_zuxia.py`、`decode_zuxia.py`、`measure_zuxia.py`、`generate_zuxia.py`。
- `tools/`：`engine-test.cpp`、`build-engine-test.ps1`、`linux-selftest/`（新，Decoder 的 Linux 自检）。
- `installer/`：`setup/setup.cpp`、`setup/make-setup.ps1`、`Install-Zuxia.ps1`。
- `.github/workflows/build-installer.yml`：CI（触发分支 master）。
- `docs/`：本项目文档（含 `列式解码.md`）。

## 四、进行中的工作：分支 `decoder-wip`（重点）
分支基于 master `75ae9e0`。相对 master 的改动共 **18 个文件**（两个生成物本身不在 git 里；`docs/HANDOFF.md` 是把 master 那一版接过来再更新的，所以 PR 的 diff 里不会出现「删掉手册」）：

### 4.1 改了什么
1. `data-tools/generate_phrases.py`：新增 `write_decoder_data()`，产出 `data/zuxia.decoder.tsv`（3,097,914 字节）。四段格式（每行首列是段标）：`s` 合法音节 / `w` 字+权重 / `c` 码→字（含两条补位规则）/ `b` 二元组（前 20 万词统计，161,558 条）。
2. `src/Decoder.h` + `src/Decoder.cpp`（新）：`ColumnarDecoder`，`Load / Ready / Decode`。DP 切音节 + 定向搜索（beam）；打分 = Σlog(字频) + Σ二元组，按字数归一。用 `char32_t`（8,105 字里有 205 个超 BMP，不能用单个 wchar_t）。
3. `src/RimeEngine.h` + `src/RimeEngine.cpp` 接线：解码器懒加载；**仅当 Rime 一个候选都给不出、且输入 ≥3 键时**出解码候选（最多 9 个）；数字键 1–9 / 空格在 `ProcessKey` 里被截获落字（`overlay_` 机制）；`Clear()` 清 overlay。
4. `tools/linux-selftest/`（新）：`windows.h` 桩 + `decoder-selftest.cpp`，在 Linux 上直接编 `Decoder.cpp` 跑 11 条断言。
5. `data-tools/audit_zuxia.py`：安装载荷检查的数据文件集合从 `*.yaml` 扩到 `*.yaml + *.tsv`。
6. 配套清单：`CMakeLists.txt`（源文件 +Decoder.cpp；install 加 `PATTERN "*.tsv"`；缺 decoder.tsv 的 FATAL_ERROR 守卫）、`tools/build-engine-test.ps1`（+src\Decoder.cpp）、`.gitignore`（+/data/zuxia.decoder.tsv）、`installer/setup/setup.cpp` 与 `installer/Install-Zuxia.ps1`（载荷清单 +zuxia.decoder.tsv）、`.github/workflows/build-installer.yml`（ubuntu job 加「C++ 解码器自检（Linux）」一步）、`scripts/verify-install.ps1`（安装后核对的数据文件清单 +extended/+decoder）。
7. `tools/engine-test.cpp`（2026-09-29 补）：加 2 条解码器断言 —— `yangzhipengzszmsp`→杨志鹏、`suyaoszcw`→苏瑶，外加一个 `OffersText()` 辅助函数。**不能用现成的 `Offers()`**：它是借「注释找不找得到」判断在不在，而解码器给的候选不经过 `AnnotateCode`，注释是空的。
8. `data-tools/audit_zuxia.py`（2026-09-29 补）：版本号残留检查改成 `git grep -l -F`。原来按正则找 `0.1.0`，`.` 通配任意字符，把新 `Decoder.cpp` 里的 `0x10000` 误报成「残留的旧版本号」—— 是误报，不是真有残留。
9. `README.md`（2026-09-29 补）：§2.8 删掉「词表外只有全拼档打得出」「解码器还没进 C++」的旧表述；第七节状态表两行改写；「下一步」第 3 条改成「把 decoder-wip 合进 master」。

### 4.2 已验证 / 未验证（别混淆）
**已验证（沙箱里跑过，可复现）**：
- C++ 自检 **11/11** 通过（命令见 4.4；例：`suyaoszcw→苏瑶/蔌瑶/苏珧/…`、`yangzhipengzszmsp→杨志鹏/杨志砰/…`、`rup→入`、`suyaoxx→（空）`）；
- 生成器确定性：重跑 `generate_phrases.py` 后 `zuxia.decoder.tsv` 与 `zuxia.extended.dict.yaml` 逐字节不变（sha256 一致）；
- `audit_zuxia.py --regen`：ALL CHECKS PASSED（含安装载荷清单 5 个文件）；
- `decode_zuxia.py --selftest`：11/11（Python 原型）；
- 接口核对：`get_input` / `clear_composition` 等都在 librime 1.17 头文件里；`EngineSnapshot` / `Candidate` 字段、字符字面量逐一核对；
- RimeEngine.cpp 近似语法检查：**新增代码区域零错误**；剩余报错全部来自 MSVC 与 gcc 的环境差异（Linux 下 `std::filesystem::path::c_str()` 是窄字符、MSVC secure CRT 模板重载）与沙箱桩头缺声明——不是接线问题。

**2026-09-29 在新沙箱里逐条复现过（Linux、有外网、只有 g++ 11.5）**：
- C++ 自检 **11/11**；
- 生成器：119,964 词 / 421,131 行 / 12,167,874 字节，解码器 3,097,914 字节 —— 与上面的数字逐个吻合；重跑两次 sha256 一致（`e99c3b3a…` extended / `c622eadc…` decoder）；
- `decode_zuxia.py --selftest` 11/11；
- `audit_zuxia.py --regen`：先报 1 项失败（就是 4.1 第 8 条那个误报），修掉后 **ALL CHECKS PASSED**；
- `engine-test.cpp` 新增断言的语法：拿仓库自带的 rime 头 ＋ `tools/linux-selftest/windows.h` 做 `-fsyntax-only` 前后对照，**新增代码零新增错误**（前后都只剩 `sprintf_s`、`SetConsoleOutputCP` 两个 MSVC 专有 API 报错，是桩头缺声明）；
- `Decoder.cpp` 的平台相关调用只有一个 `CreateFileW`，没有任何会被 MSVC 拒掉的弃用 CRT 函数；`<mutex>` 等头文件都显式包含了（gcc 会传递包含、MSVC 不会，这是最常见的一类「Linux 过了 Windows 炸」）。

**未验证（接手后第一件事）**：
- **Windows / MSVC 下从未编译过**（沙箱只有 g++）——近似检查和静态审查都不能替代真编译。**已开 PR 交给 CI 跑**：工作流 `on:` 里有 `pull_request`，Windows job 会真编译 ＋ 跑 engine-test ＋ 打包；以 Actions 的结果为准；
- 新加的那 2 条 engine-test 解码断言**只过了语法检查，从未真跑**。它们依赖「Rime 对这整串码一个候选都给不出」这个前提 —— 这个前提是真是假，只有 engine-test 在 Windows 上跑起来才知道；
- 从未真机安装验收过（所有版本都没有）。

### 4.3 下一步清单（按顺序）
1. ~~检出分支~~ —— **已做**。
2. ~~Linux 复跑解码器自检~~ —— **已做，11/11**（2026-09-29）。
3. **Windows 编译（关键，仍未过 —— 现在唯一的真拦路虎）**。两条路，哪条先绿都算：
   - **CI（推荐，不用碰用户的机器）**：PR 已开（`decoder-wip` → `master`）。Windows job 会 setup-python → generate → fetch-librime → `build.ps1` → `engine-test` → make-setup，产物在 Actions 的 Artifacts 里。
   - 本机：`pwsh scripts/fetch-librime.ps1`，然后 `pwsh scripts/build.ps1 -Arch all -Configuration Release`；有编译错误优先修 `src/RimeEngine.cpp` 的接线。再 `pwsh tools/build-engine-test.ps1 -Arch x64`。
4. ~~engine-test 加断言~~ —— **已做**（2 条，见 4.1 第 7 条）。注意 engine-test 要从 `dist\Zuxia\x64` 这类有 `..\data` 的目录跑；`data/zuxia.decoder.tsv` 须先由生成器产出。**断言本身还没真跑过。**
5. ~~更新文档~~ —— **已做**（README §2.8、第七节状态表两行、「下一步」第 3 条）。
6. 跑 CI 全套：ubuntu job（generate → audit → decode → C++ 自检 → measure）与 Windows job（generate → build → engine-test → 安装包）都要绿，再合并。
7. 让用户按第七节安装验收（新包在 CI Artifacts，**别用 release/ 里的旧包**）。

### 4.4 可复现的验证命令（Linux / 沙箱）
```bash
# 1) 解码器 C++ 自检（期望最后一行：11/11 checks passed）
g++ -std=c++17 -O2 -Itools/linux-selftest -Isrc \
    -o /tmp/dselftest tools/linux-selftest/decoder-selftest.cpp src/Decoder.cpp
cd <仓库根> && /tmp/dselftest

# 2) 生成 + 审计 + Python 自检
python3 data-tools/generate_phrases.py         # 期望：119,964 词 / 421,131 行 / 12,167,874 字节；解码器 3,097,914 字节
python3 data-tools/audit_zuxia.py --regen      # 期望：ALL CHECKS PASSED
python3 data-tools/decode_zuxia.py --selftest  # 期望：11 项通过
```
（沙箱里两个生成物都已就位；它们是 gitignore 的生成物，不在仓库里。）

### 4.5 接线细节备查（编译或行为不对时先看这里）
- `EnsureDecoder()`：`std::call_once` 懒加载 `data/zuxia.decoder.tsv`（相对模块目录的 `..\data`），日志 key `decoder-loaded` / `decoder-unavailable`。
- `FillDecodedCandidates()`：Rime 候选为空、preedit 非空、`get_input()` 长度 ≥3 才解码；最多 9 个，label "1".."9"。
- `ProcessKey()`：`overlay_` 非空且无修饰键时，`1`–`9` 选第 N 个、空格选第 1 个，直接提交并 `clear_composition`；其余按键照常交给 Rime。
- `Snapshot()` 目前没有调用方（全仓库查过），不用管。

## 五、关键工程约束与坑（能省几小时，务必先读）
1. **推送方式**：沙箱无外网。若沿用 Notion AI 环境：用 MCP 服务器 `GitHub`（注意大小写）的 `push_files`；参数文件 ≤1 MiB——`data/zuxia.dict.yaml`（995,683 字节）转义后超限，**所以词组表用「反向 import」**：`zuxia.extended.dict.yaml` 里写 `import_tables: [zuxia]`，schema 指向 `zuxia.extended`；单字表一个字节都不用动。
2. **两个生成物不入库**：`data/zuxia.extended.dict.yaml`、`data/zuxia.decoder.tsv`——gitignore + CI/本地现生成（`python data-tools/generate_phrases.py`）。
3. **CI 顺序**：ubuntu：generate → audit → decode → C++ 自检 → measure；Windows：setup-python → generate → 读版本 → fetch-librime → build.ps1 → engine-test → make-setup → 核对清单。顺序不能反（audit 要检查生成器产物；`cmake --install` 要把 data 拷进 dist）。
4. **Windows 控制台 cp1252**：Python 在 Windows runner 上 print 中文会炸——脚本已把 stdout 改 utf-8、报告键改 ASCII，别改回去。
5. **MSVC 清单坑**：`ZuxiaSettings.rc` 自带 RT_MANIFEST 与链接器默认清单会撞（CVT1100/LNK1123），已用 `/MANIFEST:NO` 解决，别删。
6. 编译选项：`/utf-8 /W4 /EHsc /permissive- /Zc:__cplusplus`（无 /WX）、`cxx_std_17`、静态 CRT、`/Brepro`。
7. 审计的载荷检查只认 `\data\...` 条目（正则），`\x64\...` 之类不触发。
8. PowerShell 5.1 没有 `&&`；给用户的命令必须整块可粘贴、逐行执行。
9. `enable_sentence: true`：词表外的词靠连打成句（只对全拼档有效）。
10. 词组的部件位只取每字第一个部件（全展开 568 万行 150 MB，Rime 编译不动）；解码器同理，但 `c` 表含两条补位规则。

## 六、验收与测试
- **验收码**：`pengzy`鹏、`pengzs`/`pengzk`彭、`bazk`跋、`chuansb`穿、`qingzs`清、`yingbg`应、`nihaozzrn`你好、`yangzhipengzszmsp`杨志鹏、`suyaoszcw`苏瑶。
- **engine-test**：`pwsh tools/build-engine-test.ps1 -Arch x64`，从 `dist/Zuxia/x64` 跑 `tools/engine-test.exe`。
- **CI**：仓库 Actions（触发分支 master）。

## 七、给用户的安装话术（每次发版都要说）
1. 卸载旧版，并删除整个 `%LOCALAPPDATA%\Zuxia`；
2. 装新包（CI Artifacts 里的 `ZuxiaSetup-*.exe`；**不要用 release/ 里的旧包**，那是旧码位）；
3. **注销并重新登录**（或重启）；
4. 第一次打字要编译词库（几秒；词库从 1 MB 变成约 13 MB，另有解码器数据 3 MB，会更久一点）。
- 设置文件：`%LOCALAPPDATA%\Zuxia\设置.txt`（只在不存在时生成，不自动补新项；打开设置界面点「确定」可整份重写补齐）。
- 设置界面：开始菜单「足下输入法设置」或语言栏右键。

## 八、用户沟通要点
- 极简风格，常只发一句报错/截图/链接；**完全不懂代码**，命令要「可整块粘贴」；
- 要求可查证、口径一致、**如实标注未验证项**（每次汇报都标）；
- 会直接纠错；重要结论给证据（命令输出、sha256、日志）；
- 用中文沟通。

## 九、已知缺陷 / 未做事项
- 跟随光标：已实现未真机验证（三级回退；Chrome/Electron/Windows Terminal 常掉到第 2、3 级）；
- `sync_structure.py` 对齐应物结构码：需应物仓库，只能在用户机器上跑；
- `ZUXIA_COM_GUARD_*` 缺陷：约 17 个入口点无异常防护（建议应物上游先修）；
- 代码签名（无）、ARM64 原生（已决定不做）、档 C 皮肤系统、首次部署预编译（未做）；
- 真机安装打字验收：从未做过。

## 十、给新平台的启动语（可直接粘贴）
「这是 Windows 输入法项目『足下输入法』，仓库 https://github.com/tyzpkaw-cpu/zuxia（公开，默认分支 master）。请先读 `docs/HANDOFF.md`（交接手册），再检出分支 `decoder-wip` 继续『列式解码器』的收尾——手册第四节有完整清单和可复现的验证命令。用户不懂代码，命令请给整块可粘贴的形式；未验证的事如实标注。注意：两个大文件（zuxia.extended.dict.yaml、zuxia.decoder.tsv）是生成物、不在 git 里，跑 `python data-tools/generate_phrases.py` 生成。」
