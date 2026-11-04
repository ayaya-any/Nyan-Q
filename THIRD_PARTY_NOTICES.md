# Third-party notices

Clash-Q 自身源代码以 MIT License 发布（见 `LICENSE`）。发布包还包含下列
第三方软件、运行时、字体和资源。它们各自保留原始版权与许可证。

## Qt 6.12.0

- 用途：应用框架（Core、Gui、Widgets、Network）。
- 授权：Qt 商业许可 / GNU LGPL v3 / GNU GPL v3（不同模块和分发方式适用
  不同条款）。
- 本项目以动态链接方式使用 Qt。发布包中的 Qt DLL 可由用户替换为兼容版本。
- 许可证摘要与完整条款：`licenses/Qt-LICENSE.txt`。
- 项目主页：https://www.qt.io/
- 源码获取：https://download.qt.io/official_releases/qt/6.12/

## Mihomo

- 版本：v1.19.33（Windows amd64 compatible build, with_gvisor）。
- 用途：作为独立子进程运行的代理内核。
- 授权：GNU GPL v3。
- 许可证：`licenses/Mihomo-LICENSE.txt`。
- 源码：https://github.com/MetaCubeX/mihomo
- 发布页：https://github.com/MetaCubeX/mihomo/releases

Mihomo 进程通过本机 HTTP REST API 由 Clash-Q 控制，未与 Clash-Q 静态链接。

## Microsoft Visual C++ Runtime

- 用途：应用所需的 MSVC x64 运行库（`msvcp140.dll`、`vcruntime140.dll` 等）。
- 授权：Microsoft Visual C++ Redistributable 许可。
- 说明：发布脚本从已安装的 Visual Studio 可再发行组件目录复制这些 DLL；
  安装包同时内置官方 `vc_redist.x64.exe`，在运行库缺失或版本过旧时检测并安装。
- 安装包内的运行库来源：https://aka.ms/vs/17/release/vc_redist.x64.exe
- 参考：https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist

## Node.js

- 版本：Node.js 22.18.0 Windows x64（随包 `lsp/node.exe`）。
- 用途：运行 YAML Language Server，作为编辑器的 stdio LSP 子进程。
- 授权：Node.js 使用 MIT 及第三方组件许可。
- 许可证：`licenses/Node-LICENSE.txt`。
- 项目主页：https://nodejs.org/

## YAML Language Server

- 版本：2.0.0。
- 用途：为配置编辑器提供 YAML 的 LSP 补全、诊断与格式化。
- 授权：MIT License。
- 许可证：`licenses/Yaml-Language-Server-LICENSE.txt`。
- 源码：https://github.com/redhat-developer/yaml-language-server

Node.js 与 YAML Language Server 以独立子进程运行，未与 Clash-Q 静态链接。

## Wintun

- 版本：0.14.1（amd64 预编译 `wintun.dll`）。
- 用途：供 Mihomo 的 Windows TUN 模式创建虚拟网卡。
- 版权：WireGuard LLC。
- 许可证：`licenses/Wintun-LICENSE.txt`（预编译二进制许可）。
- 项目主页：https://www.wintun.net/

## Roboto

- 用途：界面拉丁字母与数字字体。
- 授权：SIL Open Font License 1.1。
- 许可证：`licenses/Roboto-OFL.txt`。
- 来源：https://fonts.google.com/specimen/Roboto

## Material Icons

- 用途：Material 图标字体。
- 授权：Apache License 2.0。
- 许可证：`licenses/Material-Icons-Apache-2.0.txt`。
- 来源：https://github.com/google/material-design-icons

## 说明

本文件不构成法律意见。分发二进制包前，请根据实际分发方式重新审阅所有
许可证的要求，并确保相应的源代码获取方式与许可证文本随包提供。
