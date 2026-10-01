# 给审核的人

这份文件的唯一目的是让你少走弯路。它会明说哪些东西验过、哪些没验过、哪些是已知的坑——重复报一个我们自己已经写在欠账清单里的问题，对谁都没有价值。

上一轮全量外部复审的基线是 `03d17ec`，提出的 20 条已全部处理完（修、钉死、或判为误报并写明理由），清单在下面「上一轮复审已处理」一节。**那一节里的条目请不要重复报。**

## 这是什么

足下输入法：Windows 上的一个 TSF 文本服务，用 librime 做引擎，输入方案是自造的「列式码」。

列式码一句话讲完：一个词的码是**三段拼接、逐位左对齐**——

```
全拼串 + 逐位结构串 + 逐位部件串
suyao    sz           cw          -> 苏瑶
```

苏=`su`+`s`(上下)+`c`(草字头)，瑶=`yao`+`z`(左右)+`w`(王)。单字是它的退化情形（n=1），所以打字的人只记一套规则。

代价是：**Rime 自带的 `table_translator` 切不动它**。结构位和部件位与它们描述的那个字并不相邻，必须按「列」而不是按「行」去配。`src/Decoder.cpp` 就是那个按列配的解码器，它在 Rime 之外单独跑，把候选补进 Rime 留下的空位。

## 从哪读起

按这个顺序，不要按目录顺序：

0. **[`docs/应物音形汉字键入方案.md`](应物音形汉字键入方案.md)** — 方案本身：为什么这样设计、编码规则、实测数据。读代码之前先读它，省得把有意为之的取舍当缺陷报
1. **`docs/HANDOFF.md`** — 怎么跑起来、目录里什么是什么
2. **`data-tools/decode_zuxia.py`** — 解码算法的可读参考实现，15 条自检。C++ 那份与它逐行对应，先读 Python 省一半力气
3. **`src/Decoder.cpp`** — 生产实现。风险最集中的文件之一
4. **`src/RimeEngine.cpp` 的 `FillDecodedCandidates` 与 `ProcessKey`** — 解码器怎么与 Rime 共用一页候选。这里的下标算错就会「按 3 选到第 4 个」
5. **`src/KeyEventSink.cpp`** — TSF 按键路径。另一个风险集中的文件
6. **`data-tools/generate_zuxia.py`** — 码表是怎么从汉字数据长出来的

## 验过什么，没验过什么

**这一点请务必看清，它决定你该往哪使劲。**

| 层 | 自动化覆盖 | 在哪跑 |
|---|---|---|
| 解码算法（C++） | 37 条断言 | Linux + CI |
| 解码算法（Python 参考） | 15 条断言 | CI |
| 码表不变量、版本一致性 | `data-tools/audit_zuxia.py`，27 条（`--regen` 再加 4 条） | CI |
| 引擎行为（真 librime、真词表） | `tools/engine-test.cpp`，51 条断言 | CI，真 Windows |
| 出货载荷（自证提交、清单逐条重算、数据逐字节比对、DLL 诊断串） | `scripts/audit-payload.ps1`，8 组硬断言 | CI，真 Windows |
| **TSF 那一层** | **零** | **只能真机手动** |

也就是说：`src/KeyEventSink.cpp`、`src/KeyHandler.cpp`、`src/TextService.cpp`、`src/InputMode.cpp`、`src/CandidateWindow.cpp` 这几个文件里的任何改动，**都没有任何自动化测试碰得到**。这一个月里最难查的几个缺陷全出在这一层。你在这几个文件里看出来的任何问题，价值都高于别处。

**0.3.0 的三个修复全部落在这一层**，只过了 CI 的 MSVC 编译，一条真机验证都没有：

- `src/KeyEventSink.cpp` — Caps Lock 亮着且没在组字时让字母直通应用（日志标记 `caps-lock-passthrough`）
- `src/RimeEngine.cpp` — 组字途中按回车改成放弃这次组字、什么都不落（原来会把原始码串当英文落进文档）
- `src/CandidateWindow.cpp` — `min_width` 下限 220→120，并且同一次组字里只长不缩

还有两件：

- **开发机是 Linux，本地编译不了 MSVC。** 所有 Windows 侧的编译与运行都在 GitHub Actions 上。本地只有一个用桩头文件做的语法检查（`tools/linux-selftest/windows.h`）
- **安装包没有代码签名。** Windows 会弹 SmartScreen

## 上一轮复审已处理，不用重复报

基线 `03d17ec`，20 条。按处理方式分三类。

**已修（代码有改动）**

| 问题 | 落点 |
|---|---|
| 落字失败仍然终止组字，用户的码字消失 | `KeyHandler.cpp` 改走 `_CancelComposition`，留下码字并记 `commit-failed` |
| 组字终止路径二次释放，并吃掉刚喂回引擎的尾巴键 | `EndComposition.cpp` 先置空成员再 Release；`Composition.cpp` 的 `OnCompositionTerminated` 认领来源 |
| `_EndComposition` 用异步会话且丢弃结果 | 先 `TF_ES_SYNC`，失败才退异步；记 `end-composition-failed` |
| 插入点移出组字范围时静默擦除 | `TextEditSink.cpp` 记 `composition-dropped`（丢弃本身是有意为之，组字范围里是拉丁码串） |
| 切西文不收 TSF 组字 | `InputMode.cpp` 在切走前经 `GetFocus()` 拿 context 收尾 |
| 死码上的回流记录盖住兜底候选 | `Decoder.cpp` 的 `Decode()` 先算精确解，空则直接走最长前缀兜底 |
| 设置文件里 `#RRGGBB` 被当行内注释截掉，五项配色永不生效 | `Settings.cpp` 重写 `ApplyLine`，只把「空白 + #」当注释起点 |
| 设置写盘不校验，崩在中途会留半个文件 | 写临时文件 + `FlushFileBuffers` + `MoveFileExW` 原子替换 |
| 诊断日志自己会抛异常 | `Diagnostics.*` 全部入口 `noexcept` + try/catch 兜死 |
| 共享解码器无锁 | `Decode` / `RecordChoice` 各上 `std::mutex` |
| 回流表半写行会污染下一次追加 | 记账循环补写、失败时补 `\n` 封断、读侧丢掉没有换行收尾的末行 |
| 小键盘数字键不能选字 | `KeyEventSink.cpp` 与 `RimeEngine::VirtualKeyToRimeKey` 都认 `VK_NUMPAD0..9` |
| COM 工厂不判空指针 | `Server.cpp` 补 `E_INVALIDARG` / `E_UNEXPECTED` |
| `LoadLibraryW` 有不带搜索约束的裸兜底 | 删掉兜底；`ModuleDirectory` 失败即拒绝初始化 |
| 发布流程可以出一个来源不明的包 | 新增 `scripts/audit-payload.ps1`；包内带 `BUILD-INFO.txt` 自证 commit 与工作区洁净；标签必须等于 `VERSION`；不再 `--clobber` 覆盖 Release 附件 |
| 测试程序 `engine-test.exe` 被打进了安装包 | CI 跑完即删，门禁再断言一次 |
| 版本号一致性漏查两处 | `audit_zuxia.py` 加 `setup.rc` 与 `RELEASE-NOTES.md`，判据改严格相等 |

顺着同一轮复审的线索又修了这几处（原报告只提到其中一部分）：

| 问题 | 落点 |
|---|---|
| `ProcessKey` 或 `_ApplyRimeSnapshot` 抛异常时跳过收尾，`engine_took_key_` 已置位而函数仍报 `S_OK` | `KeyHandler.cpp` 两处各自接住，走同一套清理；新增 `process-key-threw` 日志 |
| 设置窗口固定 520×736，1366×768 上「确定／取消」掉到屏幕外面 | 按 `SPI_GETWORKAREA` 压高度，装不下就挂 `WS_VSCROLL`，补 `WM_SIZE`/`WM_VSCROLL`/`WM_MOUSEWHEEL`，窗口自己居中 |
| 「打开设置文件」之后按取消，会用启动快照把记事本里的手改整份盖掉 | 交出文件的那一刻把基线对齐到已落盘的值并撤掉回写标志 |
| bigram 权重用 `atof` 不查 `isfinite`，`nan` 会让排序比较器失去严格弱序 | `Decoder.cpp` 非有限值归零 |
| 日志写完整路径，里面带 Windows 用户名 | `RimeEngine.cpp` 新增 `RedactPath`，把已知用户目录前缀折回 `%LOCALAPPDATA%` 这类名字 |
| `build.ps1` 不清暂存目录，上一次构建的陈旧文件会混进安装包 | 打包前重建 `dist\Zuxia` |
| 卸载说明指向一个从不存在的 `Uninstall.cmd`，也没说会留下 15 MB 用户目录 | `installer/RELEASE-NOTES.md` 改写 |

**钉死现行行为，不算缺陷**

- 组字中按回车，把原始码串当文本落进文档。这与微软拼音、搜狗一致，是惯例。`tools/engine-test.cpp` 已加断言钉住，要改先改产品决策
- 组字中按 Esc 清空且不落字；翻页键在没有下一页时仍被吞掉

**判为误报，但顺手做了防御性改动**

- 关于 AZERTY／QWERTZ 会打错字的具体机制，我认为原报告说错了：Windows 的布局 DLL 会把虚拟键码跟着字母一起换，`VK_A` 在 AZERTY 上就是那个物理 Q 键。改成先问 `AsciiForKey` 是为了一致性，**不是**一条已证实的用户可见缺陷

## 已知欠账，不用重复报

按我们自己的优先级排：

1. **反查** — 想不起某个字的结构位时没有出路。Rime 有 `reverse_lookup` 现成机制，没接
2. **部件别名** — 出＝山山，而现在的码只认 `c`=屮、`q`=凵，常见拆法对不上
3. **解码器没有回归门禁** — `data-tools/measure_zuxia.py` 能算首选命中率这类数字，但它不在 CI 里，改算法不会有人拦
4. **TSF 层零自动化覆盖**（见上）
5. **没有一键诊断脚本** — 用户出问题时没法自己收集信息
6. **运行时不校验数据文件** — 打包时会逐条重算 `MANIFEST.sha256`，但装完之后没人再核对
7. **解码器里有拍出来的魔数** — `kBeam=400`、`kBigramWeight=1.0`、`kMaxSegmentations=64`，没有可复现的调参脚本。已知后果：`kMaxSegmentations` 的截断发生在可用性过滤之前，极端重复纯拼串（如 `xian` 连打九次）会让候选从 9 条变成 0 条
8. **与「应物」码位有 207 字分歧** — 占输入频率 13.69%。结构码已改按 GF 0013—2009 判定，应物那边还没跟；`data-tools/sources/structure-overrides.tsv` 一直是空的
9. **调频未实现** — 方案已定（判据是「码打满没有」，未打满才调频），没写
10. **无代码签名；ARM64 无构建**
11. **界面与引擎两套取值范围** — 设置界面允许的参数范围与引擎实际接受的范围不是同一份定义
12. **日志里还有宿主进程名与 PID** — 路径已折成 `%LOCALAPPDATA%` 这类环境变量名，进程名和 PID 保留（要靠它们定位是哪个应用出问题）。输入内容一个字都不记

## 已经定下来的设计取舍

这几条不是没想过，是想过之后决定这么做的。要推翻请带论据，但别当成疏漏报上来：

- **不做通用纠偏／模糊音。** 容错应该做在规则层（`speller/algebra`），不在算法层。理由是可解释性：列式码的每一位都对应汉字的一个可见事实（读音、结构、部件），打错了用户能自己看出错在哪一位；算法层纠偏会把这条性质抹掉，出错时只能猜。
  这条**不**建立在「码打满就唯一」之上——那个说法是错的。实测满码唯一率只有 85.03%，有 26.95% 的字（占输入频率 26.03%）打满仍需选字。数字用 `python3 data-tools/measure_zuxia.py data/zuxia.dict.yaml` 复现
- **解码器不做按次数的调频，只做回流。** 选一次就生效，只按「最近用过」排序，最多前置 3 条。错选一次的代价是下次选对，一步翻回来；按次数排的话得再选好多次才追得上
- **结构码以国标为准。** 判定依据是 GF 0013—2009《现代常用独体字规范》（扫描件逐字抄录，存在 `data-tools/sources/gf0013-duti.txt`，256 字）。以前那份硬编码 46 字的独体字表判错了 214 个国标独体字，占全部输入频率的 18.94%
- **诊断日志只记事件，绝不记内容。** 用户打了什么、选了什么，一个字都不进 `zuxia.log`。加任何新的写入点之前请先读 `src/Diagnostics.h` 顶上那段约定
- **回流表记录用户输入，只留在本机。** 在 `%LOCALAPPDATA%\Zuxia\zuxia.decoder.user.tsv`，纯文本，用户随时能用记事本打开，删掉就等于忘掉全部

## 怎么自己跑一遍

不需要 Windows，下面这些在 Linux/macOS 上就能跑：

```bash
python3 data-tools/generate_zuxia.py            # 单字码表（0.3.0 起不进 git）
python3 data-tools/generate_codes.py            # 候选注释表（同上）
python3 data-tools/generate_phrases.py          # 词组码表与解码器数据（不进 git）
python3 data-tools/audit_zuxia.py               # 码表与源码的不变量，27 条
python3 data-tools/decode_zuxia.py --selftest   # 解码算法参考实现，15 条
g++ -std=c++17 -O2 -o /tmp/dsel \
  tools/linux-selftest/decoder-selftest.cpp src/Decoder.cpp \
  -I tools/linux-selftest -I src && /tmp/dsel   # C++ 解码器，37 条
python3 data-tools/measure_zuxia.py data/zuxia.dict.yaml  # 码表的命中率与击键数
```

想看某串码解出什么，`decode_zuxia.py` 直接带码跑即可。

Windows 侧的编译和引擎断言看 `.github/workflows/build-installer.yml`——想自己编一份安装包，fork 之后跑一次 workflow 就有。

## 最想听到什么

按价值从高到低：

1. **TSF 那一层的任何问题**（生命周期、线程、编辑会话、`*eaten` 语义、语言栏）——那里没有测试兜着
2. **能让人丢字或者丢输入的路径**。这类缺陷已经修掉八条了，不敢说没有第九条
3. **列式码这套规则本身**：好不好学、好不好猜、跟已有方案比值不值
4. 崩溃、内存、资源泄漏
5. 代码可读性、命名、注释里说错的地方（注释说错话比没注释更坏——这一个月里就有一条过时注释差点把修好的东西带回去）

## 报到哪里

这个仓库的 Issues。一个问题一条，带上你看的 commit。涉及真机行为的，请附 `%LOCALAPPDATA%\Zuxia\zuxia.log`（它只有事件，没有你打的字）。
