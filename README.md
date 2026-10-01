# 应物足下输入法

> 千里之行，始于足下
> 预发布阶段。版本号见 [Releases](https://github.com/tyzpkaw-cpu/zuxia/releases)

Windows 平台的汉字输入法。实现的是**应物音形汉字键入方案**：全拼承担读音，结构与部件承担字形。

```text
全拼  →  加结构码  →  加一个部件码  →  加第二个部件码
qing  →  qingz     →  qingzs        →  qingzsq        （清）
```

每一级都是可用的编码，打到候选出现即可停手。

- **方案本身**——为什么这样设计、编码规则、实测数据：[《应物音形汉字键入方案》](docs/应物音形汉字键入方案.md)
- **代码审阅**——哪些验过、哪些没验过、哪些是已知欠账：[docs/REVIEW.md](docs/REVIEW.md)

本输入法与其方案均出自一人之手，目前仍是草案。下文凡属未经验证的事项，均已标明。

---

## 一、安装

### 1.1 取安装包

到 [Releases](https://github.com/tyzpkaw-cpu/zuxia/releases) 下载 `ZuxiaSetup-<版本>.exe`。

安装包由持续集成现编现打，包内附 `BUILD-INFO.txt`，记明它由哪一次提交编出、当时的代码仓库是否干净。随包另有 `MANIFEST.sha256`，可用于逐文件核对完整性。

### 1.2 安装

右键安装包 → **以管理员身份运行** → 点「安装」。

安装完成后**需要注销重新登录**，或在「设置 → 时间和语言 → 语言和区域 → 键盘」里手动添加，输入法才会出现在 `Win + 空格` 的列表里。

> **升级时请先重启电脑。** 输入法的 DLL 会被加载进每一个接受键盘输入的程序里，正在运行的程序会一直挂着旧版本。文件被占用时安装会失败并提示「无法写入文件」。重启后立即安装即可。

### 1.3 未签名

**安装包没有代码签名。** Windows 会弹出 SmartScreen 警告；启用了 Smart App Control 的系统会直接拒绝安装。这是当前最影响可用性的一项欠缺。

### 1.4 卸载

控制面板（设置 → 应用 → 已安装的应用）里正常卸载，或直接运行安装目录里的 `ZuxiaUninstall.exe`。

卸载默认**保留** `%LOCALAPPDATA%\Zuxia`（约 15 MB：用户词典、学习记录、设置、日志），重装后这些还在。要彻底清除，卸载后手动删除该文件夹。

---

## 二、怎么打字

按 `Win + 空格` 切到「应物音形足下输入法」。**首次切换会编译词库，可能需要十几秒。**

### 2.1 最小上手

只会拼音也能用——全拼就是第一级编码：

```text
qing      →  请、轻、清、情、晴 …（候选较多）
```

想让候选少一些，加一位结构码：

```text
qingz     →  左右结构的 qing       请、轻、清、情、晴 …
qings     →  上下结构的 qing       亲、青、擎 …
```

再加部件名称的首字母（下面给的都是首选）：

```text
qingzs    →  氵 叫「水」      →  清
qingzy    →  讠 叫「言」      →  请
qingzx    →  忄 叫「心」      →  情
qingzr    →  日 读 rì         →  晴
```

部件不止两个的字，任意挑两个即可，顺序也不论：

```text
轻 = 车(c) + 又(y) + 工(g)
qingzcy   qingzgy   qingzyg   …  六种组合都是「轻」
```

结构码五个：`z` 左右、`s` 上下、`b` 包围、`p` 品字形、`d` 独体。

```text
yid       →  独体的 yi         一、义、乙、亦、夷、已、衣
yidt      →  第一部件是「亠」  衣、亦
wub       →  包围的 wu         武
jiand     →  独体的 jian       见
```

### 2.2 词

词与单字同构，只是按列排：全拼串 ＋ 逐位结构串 ＋ 逐位部件串。

```text
nihao     →  你好
nihaozz   →  你好（两字都是左右结构）
bushouzd  →  部首（部是左右，首是独体）
```

词表没收的词（人名、生僻搭配）也能打——解码器会按列现场拼：

```text
suyaoszcw          →  苏瑶
yangzhipengzszmsp  →  杨志鹏
```

### 2.3 按键

| 键 | 作用 |
|---|---|
| 数字键 `1`–`9`（主键盘或小键盘） | 选第 N 个候选 |
| `空格` | 选第一个候选 |
| `↑` `↓` | 在候选之间移动 |
| `PageUp` `PageDown` | 上一页 / 下一页候选 |
| `←` `→` `Home` `End` | 在已打出的码里移动光标 |
| `Backspace` `Delete` | 删一位码 |
| `Esc` | 清空当前输入，不落字 |
| `回车` | 把已打出的字母码串当文本落进文档 |
| `Shift` 轻敲 | 中文 / 西文切换 |
| `Ctrl` + `` ` `` | 方案选单 |

一页 9 个候选。

完整的编码规则见[《应物音形汉字键入方案》](docs/应物音形汉字键入方案.md) 第四节。

---

## 三、设置

两条路，改的是同一处：

- **设置界面**：开始菜单搜「足下输入法设置」，或在语言栏的「中／西」按钮上**右键**。带字体选择、取色器、五套配色预设（跟随系统／浅色／深色／护眼／高对比），下方有实时预览。
- **直接改文件**：`%LOCALAPPDATA%\Zuxia\设置.txt`，改完约半秒生效。设置界面里的「打开设置文件」按钮会直接打开它。

设置界面是独立的 `ZuxiaSettings.exe`，不跑在 Word 或浏览器的进程里——设置界面出问题也碰不到正在编辑的文档。

### 3.1 选过的候选会被记住

从解码候选里选中过的结果会记进 `%LOCALAPPDATA%\Zuxia\zuxia.decoder.user.tsv`，下次打同一串码时前置。规则刻意做得很窄：

- **选一次就生效**，不攒次数——这件事的全部价值就在第二次打的时候；
- **只按最近用过排序，不计次数**。错选一次的代价是下次选对，一步翻回来；
- **只在完全相同的码上命中**，最多前置 3 条，不改动解码器自身的输出顺序；
- 表最多留 2000 行，超出丢最旧的；
- 文件只追加，所以在 Word 里打过的词，切到别的程序第一次打就已经排在前面。

目前**还没有图形开关**（默认开启）。

### 3.2 隐私

两个文件，性质相反，都只留在本机，都不参与任何上传：

| 文件 | 内容 |
|---|---|
| `zuxia.decoder.user.tsv` | **记录你打了什么、选了什么。** 纯文本，随时可用记事本打开；删掉即等于忘掉全部 |
| `zuxia.log` | **只记事件，不记内容。** 你打了什么字、选了什么候选，一个字都不进这个文件 |

诊断日志里的文件路径会折成 `%ProgramFiles%`、`%LOCALAPPDATA%` 这类环境变量名，不写出 Windows 用户名。日志中保留宿主程序名与进程号——出问题时需要靠它们定位是哪个程序。

---

## 四、当前状态

**可安装、可打字。** 但请按草案对待。

### 4.1 自动化测试覆盖

| 层 | 覆盖 | 在哪跑 |
|---|---|---|
| 解码算法（C++） | 37 条断言 | Linux ＋ CI |
| 解码算法（Python 参考实现） | 15 条断言 | CI |
| 码表不变量、版本一致性 | 26 条 | CI |
| 引擎行为（真 librime、真词表） | 50 条断言 | CI，真 Windows |
| 出货载荷（自证提交、清单重算、数据比对） | 8 组硬断言 | CI，真 Windows |
| **文本服务（TSF）那一层** | **零** | **只能真机手动** |

TSF 层指按键派发、组字生命周期、编辑会话、候选窗这几件事。**那一层没有任何自动化测试碰得到**，是目前最大的风险所在。

### 4.2 真机验证到什么程度

已在 1 台 Windows 11 上验过：安装、在资源管理器／微信／Edge WebView／控制台等多个程序里打字、单字与词组与词表外人名三类都通、解码器正常出候选、包内文件哈希与 `MANIFEST.sha256` 全部一致。

**未验**：x86 宿主程序、管理员权限窗口、高 DPI 与多显示器、远程桌面、登录界面、商店应用（沙箱进程）、ARM64。

### 4.3 已知限制

- **无代码签名**（见 §1.3）
- **ARM64 原生应用不可用**。安装器能装，x86／x64 模拟与 Arm64EC 应用里可用，原生 ARM64 应用不行
- **Win7 / 8.1 不列入支持范围**——找不到静态阻断，但没有实测证据
- **没有反查**。想不起某个字的结构位或部件名时，目前没有出路
- **部件命名表待人工复核**。约 1,460 种部件的取码由程序按读音自动给出，未逐条审阅
- **升级需重启**（见 §1.2）

完整的欠账清单在 [docs/REVIEW.md](docs/REVIEW.md)。

---

## 五、从源码构建

需要 Visual Studio 2022（含「使用 C++ 的桌面开发」工作负载）、CMake、Python 3.13。

```powershell
# 词组码表（42 万行 12 MB，不在 git 里；缺了它 CMake 会直接报错）
python data-tools/generate_phrases.py

# 渲染图标（仅在重新克隆仓库后需要跑一次）
pwsh scripts/make-icon.ps1 -Char 足 -Out src/ZuxiaTSF.ico
pwsh scripts/make-icon.ps1 -Char 足 -Out installer/setup/app.ico

# x64 + x86 编译，暂存到 dist\Zuxia
# 产出 ZuxiaTSF.dll（文本服务）与 ZuxiaSettings.exe（设置界面）
pwsh scripts/build.ps1 -Arch all -Configuration Release

# 打单文件安装包，产出 dist\ZuxiaSetup-<版本>.exe 与同名 .sha256
pwsh installer/setup/make-setup.ps1 -SkipBuild
```

### 5.1 不装也能验证引擎

```powershell
pwsh tools/build-engine-test.ps1 -Arch x64
copy tools/engine-test.exe dist/Zuxia/x64/
cd dist/Zuxia/x64
.\engine-test.exe
```

必须在 `x64\` 目录里运行——引擎是从自己的模块路径去找 `rime.dll` 与 `..\data` 的，与文本服务运行时的行为一致。

### 5.2 开发期安装

不写注册表卸载项，只做 `regsvr32`，适合反复调试：

```powershell
pwsh installer/Install-Zuxia.ps1                      # 需要管理员
pwsh installer/Uninstall-Zuxia.ps1 -PurgeUserData
```

### 5.3 码表与数据

这些在 Linux / macOS 上就能跑，不需要 Windows：

```powershell
python3 data-tools/generate_zuxia.py                    # 生成单字词典
python3 data-tools/generate_phrases.py                  # 生成词组码表
python3 data-tools/measure_zuxia.py data/zuxia.dict.yaml # 命中率与击键数
python3 data-tools/audit_zuxia.py                       # 码表不变量自查
python3 data-tools/decode_zuxia.py --selftest           # 解码算法参考实现
```

想看某串码能解出什么，`decode_zuxia.py` 后面直接跟码即可。

### 5.4 与应物输入法并存

应物输入法（衡码）是本方案的前身，有独立的实现。两者可以同时安装，安装包、CLSID、注册表键、用户目录、图标全部独立：

```text
应物输入法           %ProgramFiles%\Yingwu     Win+空格 里显示「应物输入法」
应物音形足下输入法   %ProgramFiles%\Zuxia      Win+空格 里显示「应物音形足下输入法」
```

**此项未经真机验证。**

---

## 六、反馈

到本仓库的 [Issues](https://github.com/tyzpkaw-cpu/zuxia/issues) 提，一个问题一条。

涉及真机行为的，请附上 `%LOCALAPPDATA%\Zuxia\zuxia.log`——它只记事件，不含你打的字（见 §3.2）。

- 软件问题（崩溃、丢字、候选窗异常、安装失败）：直接提 Issue
- 方案问题（规则好不好学、好不好猜、与已有方案相比值不值）：见[《应物音形汉字键入方案》](docs/应物音形汉字键入方案.md) 第七节
- 代码审阅：请先读 [docs/REVIEW.md](docs/REVIEW.md)，那里写明了哪些验过、哪些没验过、哪些是已知欠账

---

## 七、许可

- 自研代码：MIT，见 `LICENSE-CODE.txt`
- 词库数据派生自雾凇拼音（rime-ice）：GPL-3.0
- 汉字结构数据来自 Make Me a Hanzi（LGPL-3.0）与 CJKVI-IDS
- 文本服务框架源自 Microsoft TSF 示例（MIT），内嵌 librime（BSD 3-Clause）

详见 `licenses/` 与 `THIRD_PARTY_NOTICES.md`。
