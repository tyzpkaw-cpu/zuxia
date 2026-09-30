# 给审核的人

这份文件的唯一目的是让你少走弯路。它会明说哪些东西验过、哪些没验过、哪些是已知的坑——重复报一个我们自己已经写在欠账清单里的问题，对谁都没有价值。

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
| 解码算法（C++） | 32 条断言 | Linux + CI |
| 解码算法（Python 参考） | 15 条断言 | CI |
| 码表不变量、版本一致性、安装载荷清单 | `data-tools/audit_zuxia.py`，约 30 条 | CI |
| 引擎行为（真 librime、真词表） | `tools/engine-test.cpp`，约 40 条断言 | CI，真 Windows |
| **TSF 那一层** | **零** | **只能真机手动** |

也就是说：`src/KeyEventSink.cpp`、`src/KeyHandler.cpp`、`src/TextService.cpp`、`src/InputMode.cpp`、`src/CandidateWindow.cpp` 这几个文件里的任何改动，**都没有任何自动化测试碰得到**。这一个月里最难查的几个缺陷全出在这一层。你在这几个文件里看出来的任何问题，价值都高于别处。

还有两件：

- **开发机是 Linux，本地编译不了 MSVC。** 所有 Windows 侧的编译与运行都在 GitHub Actions 上。本地只有一个用桩头文件做的语法检查（`tools/linux-selftest/windows.h`）
- **安装包没有代码签名。** Windows 会弹 SmartScreen

## 已知欠账，不用重复报

按我们自己的优先级排：

1. **反查** — 想不起某个字的结构位时没有出路。Rime 有 `reverse_lookup` 现成机制，没接
2. **部件别名** — 出＝山山，而现在的码只认 `c`=屮、`q`=凵，常见拆法对不上
3. **解码器没有评测基线** — 改算法时只有 32 条自检，没有「首选命中率」这类回归数字
4. **TSF 层零自动化覆盖**（见上）
5. **没有一键诊断脚本** — 用户出问题时没法自己收集信息
6. **运行时不校验数据文件** — 安装包里有 `MANIFEST.sha256`，装完之后没人再核对
7. **解码器里有拍出来的魔数** — `kBeam=400`、`kBigramWeight=1.0`、`kMaxSegmentations=64`，没有可复现的调参脚本
8. **与「应物」码位有 207 字分歧** — 占输入频率 13.69%。结构码已改按 GF 0013—2009 判定，应物那边还没跟；`data-tools/sources/structure-overrides.tsv` 一直是空的
9. **调频未实现** — 方案已定（判据是「码打满没有」，未打满才调频），没写
10. **无代码签名；ARM64 无构建**

## 已经定下来的设计取舍

这几条不是没想过，是想过之后决定这么做的。要推翻请带论据，但别当成疏漏报上来：

- **不做通用纠偏／模糊音。** 容错应该做在规则层（`speller/algebra`），不在算法层。算法层纠偏会让「码打满就唯一」这条性质失效，而那是列式码全部价值所在
- **解码器不做按次数的调频，只做回流。** 选一次就生效，只按「最近用过」排序，最多前置 3 条。错选一次的代价是下次选对，一步翻回来；按次数排的话得再选好多次才追得上
- **结构码以国标为准。** 判定依据是 GF 0013—2009《现代常用独体字规范》（扫描件逐字抄录，存在 `data-tools/sources/gf0013-duti.txt`，256 字）。以前那份硬编码 46 字的独体字表判错了 214 个国标独体字，占全部输入频率的 18.94%
- **诊断日志只记事件，绝不记内容。** 用户打了什么、选了什么，一个字都不进 `zuxia.log`。加任何新的写入点之前请先读 `src/Diagnostics.h` 顶上那段约定
- **回流表记录用户输入，只留在本机。** 在 `%LOCALAPPDATA%\Zuxia\zuxia.decoder.user.tsv`，纯文本，用户随时能用记事本打开，删掉就等于忘掉全部

## 怎么自己跑一遍

不需要 Windows，下面这些在 Linux/macOS 上就能跑：

```bash
python3 data-tools/generate_phrases.py          # 生成词组码表（不进 git）
python3 data-tools/audit_zuxia.py               # 码表与源码的不变量
python3 data-tools/decode_zuxia.py --selftest   # 解码算法参考实现，15 条
g++ -std=c++17 -O2 -o /tmp/dsel \
  tools/linux-selftest/decoder-selftest.cpp src/Decoder.cpp \
  -I tools/linux-selftest -I src && /tmp/dsel   # C++ 解码器，32 条
```

想看某串码解出什么，`decode_zuxia.py` 直接带码跑即可。

Windows 侧的编译和引擎断言看 `.github/workflows/build-installer.yml`——想自己编一份安装包，fork 之后跑一次 workflow 就有。

## 最想听到什么

按价值从高到低：

1. **TSF 那一层的任何问题**（生命周期、线程、编辑会话、`*eaten` 语义、语言栏）——那里没有测试兜着
2. **能让人丢字或者丢输入的路径**。已经修掉三条这类缺陷了，不敢说没有第四条
3. **列式码这套规则本身**：好不好学、好不好猜、跟已有方案比值不值
4. 崩溃、内存、资源泄漏
5. 代码可读性、命名、注释里说错的地方（注释说错话比没注释更坏——这一个月里就有一条过时注释差点把修好的东西带回去）

## 报到哪里

这个仓库的 Issues。一个问题一条，带上你看的 commit。涉及真机行为的，请附 `%LOCALAPPDATA%\Zuxia\zuxia.log`（它只有事件，没有你打的字）。
