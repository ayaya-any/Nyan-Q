// 示例 1：给订阅补充本地直连规则和规则集
// config 是当前 Mihomo 配置对象，profileName 是订阅名称。
function main(config, profileName) {
    console.log("正在整理规则：" + profileName);

    config.rules = Array.isArray(config.rules) ? config.rules : [];
    const localDirect = "DOMAIN-SUFFIX,lan.local,DIRECT";
    if (!config.rules.includes(localDirect)) {
        config.rules.unshift(localDirect);
    }

    // 保留订阅原有的 rule-providers，并补充一个自定义规则集。
    config["rule-providers"] = config["rule-providers"] || {};
    config["rule-providers"]["nyanq-custom"] = {
        type: "http",
        behavior: "domain",
        url: "https://example.com/nyanq-custom.yaml",
        path: "./rules/nyanq-custom.yaml",
        interval: 86400
    };
    const ruleSet = "RULE-SET,nyanq-custom,DIRECT";
    if (!config.rules.includes(ruleSet)) {
        config.rules.unshift(ruleSet);
    }
    return config;
}
