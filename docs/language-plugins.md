# 内置语言插件

编辑器不再启动外部 LSP 进程。语法高亮、语法诊断和代码提示由
`src/lang/` 中的编译期插件提供，所有语言共用同一套编辑器集成代码。

## 结构

```text
src/lang/
  LanguageTypes.h               诊断、补全、格式化结果等值类型
  LanguageProvider.h            语言插件接口
  LanguageRegistry.h/.cpp       插件注册表与扩展名解析
  YamlLanguageProvider.*        libyaml 诊断 + Mihomo 键/值补全 + 高亮 + 格式化
  JavaScriptLanguageProvider.*   QuickJS 编译诊断 + JS/配置补全 + 高亮 + 格式化
  DocumentFormatter.*           YAML / JavaScript 无外部进程的格式化实现
```

`CodeEditor` 只依赖 `LanguageProvider` 接口：

- 文本变化后防抖调用 `diagnose()`，以波浪线显示错误/警告；
- 输入或 `Ctrl+Space` 时调用 `completions()`，复用内置补全弹窗；
- 主题变化时重新调用 `createHighlighter()`；
- `Ctrl+/` 使用插件提供的行注释前缀；
- `Shift+Alt+F`（或编辑器工具栏的格式化按钮）调用 `format()` 并作为一个
  撤销步骤替换整个文档；失败时通过编辑器状态栏给出原因。

## 接口

```cpp
class LanguageProvider {
public:
    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual QStringList fileExtensions() const = 0;
    virtual QVector<Diagnostic> diagnose(const QString& text,
                                         const QString& filePath) const = 0;
    virtual QVector<CompletionItem> completions(const CompletionRequest& request) const = 0;
    // Whole-document formatting is optional.
    virtual bool supportsFormatting() const { return false; }
    virtual FormatResult format(const QString& text, const QString& filePath) const;
    virtual QSyntaxHighlighter* createHighlighter(QTextDocument* document,
                                                  const LanguageTheme& theme) const = 0;
    virtual QString lineCommentPrefix() const = 0;
};
```

诊断行列均为 0-based；编辑器负责转换为文本光标位置。`FormatResult` 包含
`ok`、格式化后的 `text` 与失败时的 `error`；不实现格式化的语言保持默认
`supportsFormatting() == false` 即可。

## 格式化行为

- **YAML**：按 libyaml 解析出的块结构把键/序列项重新缩进为 2 空格步进，
  保留注释并对齐到相邻节点，折叠多余空行；块标量（`|`/`>`）与多行标量的
  内容会随父级缩进整体平移，保持相对缩进。格式化后会重新解析并与原文档
  比较事件流（键、值、标签、锚点、文档数），一旦检测到语义变化就放弃并
  提示，不会破坏配置。
- **JavaScript**：只调整代码行缩进与多余空行，不改变换行位置、运算符空格或
  引号风格；字符串、模板字符串内容、正则字面量与注释内部保持原样（模板
  字符串跨行内容不会被重新缩进）。
- 超过 4 MB 的文档会跳过格式化，与实时诊断的限制一致。

## 内置 YAML 插件

- 使用 libyaml 事件解析，报告语法错误、重复映射键、未定义锚点和多文档警告；
- 顶层补全 Mihomo 常用字段（端口、mode、proxies、proxy-groups、dns、tun 等）；
- 根据顶层 section 提供嵌套字段补全；
- 为 `mode`、`log-level`、`dns.enhanced-mode`、`tun.stack`、代理组/节点类型等提供枚举值；
- `rules:` 下提供规则类型片段；
- 代理组 `proxies:` 下从当前文档解析节点名，提供节点补全；
- `Shift+Alt+F` 格式化：块结构 2 空格缩进、注释对齐、空行折叠，块标量内容
  随父级平移；格式化前后做语义校验，检测到变化时拒绝应用。

## 内置 JavaScript 插件

- 使用 QuickJS 的 compile-only 模式做语法检查，不会执行脚本；
- 未定义 `main(config, profileName)` 时给出警告；
- 提供关键字、全局对象、`console.*`、`config.*`、`config.dns.*` 和
  `config["..."]` 补全；
- 提供 `main` / `async main` 代码片段，并定位光标到函数体；
- `Shift+Alt+F` 格式化：整理代码缩进与多余空行，字符串、模板字符串、
  正则与注释内容保持原样；
- 语法高亮与脚本编辑器共用同一主题。

## 新增语言

1. 实现一个 `LanguageProvider` 子类；如需格式化，重写 `supportsFormatting()`
   与 `format()`（返回 `FormatResult`）；
2. 在 `LanguageRegistry::registerBuiltinPlugins()` 增加一个 `LanguagePlugin`：

```cpp
registerPlugin({QStringLiteral("mylang"), QStringLiteral("My Language"),
                {QStringLiteral("ml")},
                [] { return std::make_unique<MyLanguageProvider>(); }});
```

3. 在编辑器创建处按文件扩展名或 id 调用
   `LanguageRegistry::instance().providerForPath(path)`。

不需要修改 `CodeEditor`、配置编辑器或脚本编辑器。插件为编译期静态注册，
不加载外部 DLL，也不启动子进程。诊断在主线程防抖执行；超过 4 MB 的文档会
跳过实时诊断，避免阻塞输入。
