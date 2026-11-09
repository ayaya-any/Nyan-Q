# 内置语言插件

编辑器不再启动外部 LSP 进程。语法高亮、语法诊断和代码提示由
`src/lang/` 中的编译期插件提供，所有语言共用同一套编辑器集成代码。

## 结构

```text
src/lang/
  LanguageTypes.h               诊断、补全、主题等值类型
  LanguageProvider.h            语言插件接口
  LanguageRegistry.h/.cpp       插件注册表与扩展名解析
  YamlLanguageProvider.*        libyaml 诊断 + Mihomo 键/值补全 + 高亮
  JavaScriptLanguageProvider.*   QuickJS 编译诊断 + JS/配置补全 + 高亮
```

`CodeEditor` 只依赖 `LanguageProvider` 接口：

- 文本变化后防抖调用 `diagnose()`，以波浪线显示错误/警告；
- 输入或 `Ctrl+Space` 时调用 `completions()`，复用内置补全弹窗；
- 主题变化时重新调用 `createHighlighter()`；
- `Ctrl+/` 使用插件提供的行注释前缀。

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
    virtual QSyntaxHighlighter* createHighlighter(QTextDocument* document,
                                                  const LanguageTheme& theme) const = 0;
    virtual QString lineCommentPrefix() const = 0;
};
```

诊断行列均为 0-based；编辑器负责转换为文本光标位置。

## 内置 YAML 插件

- 使用 libyaml 事件解析，报告语法错误、重复映射键、未定义锚点和多文档警告；
- 顶层补全 Mihomo 常用字段（端口、mode、proxies、proxy-groups、dns、tun 等）；
- 根据顶层 section 提供嵌套字段补全；
- 为 `mode`、`log-level`、`dns.enhanced-mode`、`tun.stack`、代理组/节点类型等提供枚举值；
- `rules:` 下提供规则类型片段；
- 代理组 `proxies:` 下从当前文档解析节点名，提供节点补全。

## 内置 JavaScript 插件

- 使用 QuickJS 的 compile-only 模式做语法检查，不会执行脚本；
- 未定义 `main(config, profileName)` 时给出警告；
- 提供关键字、全局对象、`console.*`、`config.*`、`config.dns.*` 和
  `config["..."]` 补全；
- 提供 `main` / `async main` 代码片段，并定位光标到函数体；
- 语法高亮与脚本编辑器共用同一主题。

## 新增语言

1. 实现一个 `LanguageProvider` 子类；
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
