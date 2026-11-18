# Nyan-Q Helper 服务

Nyan-Q 的 TUN 模式不再让整个 GUI 以管理员身份重启。首次启用 TUN 时，会请求一次 UAC
安装常驻的 Windows 服务 `NyanQHelperService`；之后 GUI 保持普通权限，通过本地
命名管道让 Helper 启动带 TUN 的 Mihomo 内核。

## 工作方式

```text
Nyan-Q.exe（普通用户权限）
    │ 命名管道 \\.\pipe\nyanq-service
    ▼
NyanQHelperService.exe（Windows 服务，LocalSystem）
    │ 创建子进程
    ▼
mihomo.exe（TUN 配置）
```

- 首次启用 TUN：UAC 只用于安装/更新 Helper。
- 后续启动：服务已注册为 `AutoStart`，GUI 直接连接服务，不再请求 UAC。
- 服务只负责启动/停止内核，不重启 Nyan-Q。
- 关闭 TUN 时，GUI 停止服务中的内核并切回普通权限内核；服务本身继续常驻。

## 安装位置

```text
%ProgramData%\Nyan-Q\
  nyanq-service.exe
  service.token
  service.log
  core\
    mihomo.exe
    wintun.dll
```

GUI 从安装目录读取令牌并连接：

```text
\\.\pipe\nyanq-service
```

## 安全边界

- 令牌为 256 位随机数，保存在 `%ProgramData%\Nyan-Q\service.token`，ACL 仅允许
  当前用户和 SYSTEM 读取。
- 服务不接收任意可执行文件路径，只启动安装目录中的 `core\mihomo.exe`。
- Core 从受保护目录启动，GUI 目录中的 `mihomo.exe` 被替换不会直接变成 SYSTEM
  进程。
- 服务版本与 GUI 版本不一致时，会重新走一次安装/更新流程。

## 通信故障与恢复

- `ping`、`status` 请求遇到连接中断时，会新建管道重试一次，并重新读取令牌以适应
  Helper 更新。启动和停止命令不会因响应丢失而盲目重发。
- 连接、发送、读取和重试共用请求总时限，状态查询总时限为 800 ms，避免重试成倍延长等待。
- 状态查询失败后，GUI 继续自动重连，间隔逐步增加到最多 5 秒；恢复后回到每秒查询。
  首次中断、每隔 15 秒的持续失败，以及恢复成功都会记入 GUI 日志。
- 通信故障期间，GUI 会向 `127.0.0.1` 的控制器发送带认证的 `/version` 探活请求，
  并通过保留的 Windows 进程句柄核验内核。仅确认进程退出，或 Helper 明确报告停止
  且控制器也不可用时，才判定退出。无法获取进程权限属于“状态未知”，不会当作退出。
- 停止请求没有获得确认时，GUI 保留当前会话并恢复监测，提示稍后重试。
- 错误详情包含 `command`、`stage`、重试次数、`socketError`、连接状态、耗时、超时值、
  待发送/已接收字节数和系统错误文本；控制器核验包含 HTTP/网络错误及进程核验结果。
  不记录令牌、控制器密钥或完整请求/响应内容。

日志通常位于 `%LOCALAPPDATA%\Nyan-Q\nyan-q.log`；便携版位于应用目录的
`data\nyan-q.log`。排查时保留首次中断到恢复或确认退出期间的日志。

> 注意：Helper 安装程序位于当前用户的 Nyan-Q 安装目录，UAC 是这里的信任边界。
> 正式分发包应进行代码签名；不要从不可信或被替换过的安装目录批准 Helper 安装。

## 系统代理

TUN 关闭后，可以直接使用系统代理：

1. 在 Nyan-Q 中关闭 TUN；
2. 开启“系统代理”；
3. GUI 会把当前用户的 Windows Internet Settings 指向
   `127.0.0.1:<mixed-port>`，并通知 WinINET。

系统代理是用户级 HKCU 设置，不需要管理员权限，也不需要 Helper 服务。
不遵循 Windows 系统代理的程序仍需使用 TUN 或手动代理。

## 卸载 Helper

通过 Nyan Setup 安装包卸载 Nyan-Q 时，安装器会停止并删除
`NyanQHelperService`，并清理其服务注册。便携版或手动安装场景可以在管理员终端运行：

```powershell
& "$env:ProgramData\Nyan-Q\nyanq-service.exe" --uninstall
```

只删除 GUI 而保留 Helper 不会影响 GUI 的数据目录；如果不再使用 TUN，建议执行上面的
命令删除服务和受保护的 Core 副本。
