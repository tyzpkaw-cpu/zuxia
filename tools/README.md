# 开发期工具

## port_from_yingwu.py

把应物输入法的 TSF 层、安装器与构建脚本移植成本仓库的产品。
**这是两份源码树之间唯一的同步手段**：足下对 `src/` 的任何私有改动都应写进它的
`EXTRA_PAIRS` / `DELETIONS` 表，而不是直接改 `src/`，否则下次重跑就会被覆盖。

```powershell
python tools/port_from_yingwu.py            # 移植（覆盖 src/ installer/ scripts/ 等）
python tools/port_from_yingwu.py --check    # 只报告，不写盘
```

结束时若有任何旧名字（Yingwu / hengma / 应物输入法 …）残留，脚本报错退出。
细节见 [`docs/工程排查.md`](../docs/工程排查.md) 第四节。

## engine-test

`engine-test.cpp` 直接把**出货的** `RimeEngine` 跑起来，验证候选编码注释、
多音字取音、中文标点与中英切换，不需要装输入法、不需要界面。

```powershell
pwsh tools/build-engine-test.ps1 -Arch x64
copy tools/engine-test.exe dist/Zuxia/x64/
cd dist/Zuxia/x64; .\engine-test.exe
```

必须**在 `x64\` 目录里运行**：`RimeEngine` 从自己的模块路径找 `rime.dll` 与 `..\data`，
和文本服务运行时的行为一致。首次运行会编译词典（约 7 秒），之后走缓存。

编译脚本把 `vcvars` 的环境变量导入当前 PowerShell 进程，而不是生成一个 `.cmd`：
cmd.exe 按控制台 OEM 代码页读批处理文件，路径里的中文用户名会变成问号，cl.exe 就找不到源文件了。

## schema-probe

把方案与词典喂进 librime，重放若干编码，打印候选。用来验证编码规则在
真实引擎里的行为，而不只是在表格里成立。它是接文本服务之前写的，
现在 `engine-test` 覆盖得更好，保留作为最小复现手段。

需要 `rime.dll`（`third_party/librime/x64/dist/lib/rime.dll`）。

```bat
cl /nologo /EHsc /std:c++17 /MT ^
   /I "third_party\librime\x64\dist\include" ^
   /Fe:tools\schema-probe.exe /Fo:build\ tools\schema-probe.cpp ^
   /link user32.lib

tools\schema-probe.exe third_party\librime\x64\dist\lib\rime.dll data build\rimeuser zuxia qingsq zibz
```

参数须用**相对路径** —— 绝对路径若含非 ASCII 字符（中文用户名），
经命令行传递时会被损坏。
