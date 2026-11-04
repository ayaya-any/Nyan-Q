# Nyan-Q

<p align="center">
  <img src="docs/screenshots/app-icon.png" width="88" alt="Nyan-Q">
</p>

面向 Windows 的 Clash / Mihomo 桌面客户端，使用 Qt 6 Widgets、C++20 与
Material 3 风格组件，通过本机 REST API 管理 Mihomo 内核。

| 深色主题 | 浅色主题 |
|:---:|:---:|
| ![深色仪表盘](docs/screenshots/dashboard.png) | ![浅色仪表盘](docs/screenshots/dashboard-light.png) |

## 功能

- Mihomo 随应用启动和退出，设置页提供重启入口
- 规则、全局、直连三种代理模式，支持代理组与节点切换、延迟测速
- 仪表盘展示实时流量、内存、网络出口、服务延迟、活动连接和当前配置
- 系统代理、TUN、局域网代理开关
- 导入 Clash / Mihomo YAML 订阅或本地文件，支持配额、到期时间、自动更新和热重载
- 全窗口 YAML 编辑器，支持查找替换、未保存保护、配置重命名
- 编辑器接入开源 `yaml-language-server`，提供补全、诊断和格式化
- 深色 / 浅色 / 跟随系统主题
- 系统托盘、单实例、开机启动、启动时最小化、关闭到托盘

## 构建

环境：

- Windows 10 / 11 x64
- Qt 6.12.0 `msvc2022_64`
- Visual Studio 2022（MSVC v143）
- CMake 3.21+

```powershell
$QtDir = "D:\Qt\6.12.0\msvc2022_64"

cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="$QtDir" `
  -DBUILD_TESTING=ON

cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

可执行文件：

```text
build\Release\Nyan-Q.exe
```

直接运行需要 Qt DLL 在 `PATH` 中。制作完整运行目录：

```powershell
.\scripts\package.ps1 -QtDir "D:\Qt\6.12.0\msvc2022_64"
```

输出：

- `dist\Nyan-Q\`
- `dist\Nyan-Q-0.1.0-win64.zip`

制作简体中文安装包：

```powershell
.\scripts\package.ps1 -QtDir "D:\Qt\6.12.0\msvc2022_64" -Installer
```

输出：

- `dist\Nyan-Q-0.1.0-win64-setup.exe`
- 默认安装到 `%LOCALAPPDATA%\Programs\Nyan-Q`
- 安装前检测并安装所需的 MSVC x64 运行库
- 卸载默认保留 `%LOCALAPPDATA%\Nyan-Q` 中的配置和订阅

## 使用

1. 打开 **订阅**，导入订阅链接或本地 YAML 文件。
2. 在 **代理** 页选择代理组和节点。
3. 在仪表盘或设置页开启系统代理；TUN 模式通常需要管理员权限。
4. 在 **配置** 页编辑 YAML，保存后当前配置会自动热重载。

配置名称可直接点击编辑，右侧铅笔按钮用于快速聚焦名称输入框。

## 快捷键

| 快捷键 | 功能 |
| --- | --- |
| `Ctrl+1` … `Ctrl+6` | 切换仪表盘 / 代理 / 订阅 / 连接 / 规则 / 日志 |
| `Ctrl+7` 或 `Ctrl+,` | 打开设置 |
| `Ctrl+Q` | 退出 Nyan-Q |
| `Ctrl+S` | 保存配置 |
| `Ctrl+F` / `Ctrl+H` | 查找 / 替换 |
| `F3` / `Shift+F3` | 下一个 / 上一个匹配 |
| `Ctrl+/` | 切换行注释 |
| `Alt+↑` / `Alt+↓` | 移动当前行 |
| `Shift+Alt+F` | 格式化文档 |
| `Ctrl+Space` | 代码提示 |
| `Ctrl+W` | 关闭编辑器 |

## 数据目录

```text
%LOCALAPPDATA%\Nyan-Q\
```

主要文件：

```text
settings.ini       应用设置
profiles/          订阅与配置
runtime/           运行时配置和内核日志
nyan-q.log         应用日志
```

首次启动时，如新目录不存在而旧 `%LOCALAPPDATA%\Clash-Q` 存在，会自动迁移旧配置。

便携模式：在 `Nyan-Q.exe` 同目录放置空文件 `portable.flag`，数据目录改为 `.\data`。
也可以设置环境变量：

```powershell
$env:NYANQ_DATA_DIR = "D:\NyanQData"
.\Nyan-Q.exe
```

## 开发与测试

演示模式：

```powershell
.\build\Release\Nyan-Q.exe --demo
```

截图：

```powershell
.\build\Release\Nyan-Q.exe --demo --screenshot .\docs\screenshots\dashboard.png --page dashboard
.\build\Release\Nyan-Q.exe --screenshot-editor .\docs\screenshots\config-editor.png
```

冒烟测试：

```powershell
.\build\Release\Nyan-Q.exe --smoke-test
```

单元测试：

```powershell
ctest --test-dir build -C Release --output-on-failure
```

## 语言服务

编辑器使用标准 LSP stdio 客户端，发布包内置：

- Node.js 22
- [yaml-language-server](https://github.com/redhat-developer/yaml-language-server)

语言服务配置可通过程序目录下的 `lsp-servers.json` 覆盖，示例见
`lsp-servers.example.json`。

## 已知限制

- 主要面向 Windows；系统代理和 TUN 为 Windows 实现
- 仅支持 Clash / Mihomo YAML，纯 Base64 节点列表不会自动转换
- TUN 模式需要管理员权限
- 界面当前仅提供中文
- 内核版本固定为 `v1.19.33`，升级时替换 `third_party/mihomo/mihomo.exe`

## 第三方

完整许可证和第三方说明见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
及 [`licenses/`](licenses/)。项目源代码使用 MIT License，见 [LICENSE](LICENSE)。
