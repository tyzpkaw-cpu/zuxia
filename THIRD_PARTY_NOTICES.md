# 第三方组件与数据声明

## 1. Microsoft TSF 示例

- 来源：Microsoft Windows classic samples，`textservice-step06-3`
- 用途：`src/` 中 COM/TSF 框架的起点；原始快照保存在 `vendor/`
- 许可：MIT
- 全文：`licenses/LICENSE-Microsoft-TSF-sample.txt`

## 2. librime 1.17.0

- 项目：<https://github.com/rime/librime>
- 发布资产：commit `33e7814` 的 Windows MSVC x64/x86 SDK
- 用途：嵌入式本地输入法引擎；本源码包附带构建所需头文件和运行库
- 许可：BSD 3-Clause
- 全文：`licenses/LICENSE-librime.txt`

官方 SDK 的可复现资产与 SHA-256：

| 架构 | 资产 | SHA-256 |
|---|---|---|
| x64 | `rime-33e7814-Windows-msvc-x64.7z` | `7478c7caa4ff6b37de86daba1f7ce4a994a4f5ba24872a820fb2b3a9b01fed15` |
| x86 | `rime-33e7814-Windows-msvc-x86.7z` | `af235c26c06152ce09ceb8fe9d9ab9fba7ab43ce30aa952b40174d806f5cc3d9` |

该官方构建还列出 `librime-lua`、`librime-octagram`、`librime-predict` 插件。足下方案本身不调用语言模型、Lua 逻辑或预测功能；正式分发前仍应对官方二进制所包含插件的许可证进行一次发布审计。

## 3. 雾凇拼音（rime-ice）数据

- 项目：<https://github.com/iDvel/rime-ice>
- 用途：8105 单字拼音/固定权重与 2—12 字基础静态词组
- 许可：GPL-3.0（按来源仓库声明）
- 全文：`licenses/LICENSE-rime-ice-GPL-3.0.txt`

`data/*.dict.yaml` 是经足下编码规则转换的生成数据；其分发应继续遵守相应数据许可。

## 4. Make Me a Hanzi `dictionary.txt`

- 项目：<https://github.com/skishore/makemeahanzi>
- 用途：部分汉字结构、部件、部首和读音元数据
- 许可：LGPL-3.0-or-later（该数据文件的来源声明）
- 声明：`licenses/NOTICE-Make-Me-a-Hanzi.txt`
- 许可全文：`licenses/LICENSE-LGPL-3.0.txt`

## 5. CJKVI IDS

- 项目：<https://github.com/cjkvi/cjkvi-ids>
- 用途：补充汉字 IDS 结构描述
- 许可：不同子数据可能继承各自来源条款
- 原始说明：`licenses/NOTICE-CJKVI-IDS.md`

正式发布词库前，应按实际使用到的 IDS 文件逐项复核其来源条款。

## 6. 输入法图标

`src/ZuxiaTSF.ico` 与 `installer/setup/app.ico` 是「足」字的位图图标，由
`scripts/make-icon.ps1` 渲染生成。

- 字体：**思源黑体 / Source Han Sans**（Adobe 与 Google 联合开发）
- 许可：SIL Open Font License 1.1
- 项目：<https://github.com/adobe-fonts/source-han-sans>

OFL-1.1 允许自由使用、修改和再分发字形，包括将渲染结果嵌入软件，因此图标的
分发不受额外限制。

脚本在渲染前会校验字体家族是否真的存在，找不到就报错退出——GDI+ 在字体缺失时
会静默替换成默认字体，那样会把错误的字形悄悄发布出去。默认按
`思源黑体` → `Source Han Sans SC` → `Noto Sans SC` 的顺序查找（同源字形的不同
发行名）。

## 7. 商标与责任

所有第三方项目的名称、商标和著作权归其各自权利人所有。此声明不改变任何第三方许可证，也不构成对 alpha 工程适销性或特定用途适用性的保证。
