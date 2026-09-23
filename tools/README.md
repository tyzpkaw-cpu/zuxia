# 开发期探针

## schema-probe

把方案与词典喂进 librime，重放若干编码，打印候选。用来验证编码规则在
真实引擎里的行为，而不只是在表格里成立。

需要 `rime.dll`（从 `../hengma-native/third_party/librime/x64/dist/lib/`
取即可，不随本仓库分发）。

```bat
cl /nologo /EHsc /std:c++17 /MT ^
   /I "..\hengma-native\third_party\librime\x64\dist\include" ^
   /Fe:tools\schema-probe.exe /Fo:build\ tools\schema-probe.cpp ^
   /link user32.lib

copy ..\hengma-native\third_party\librime\x64\dist\lib\rime.dll tools\
tools\schema-probe.exe tools\rime.dll data build\rimeuser zuxia qingsq zibz
```

参数须用**相对路径** —— 绝对路径若含非 ASCII 字符（中文用户名），
经命令行传递时会被损坏。
