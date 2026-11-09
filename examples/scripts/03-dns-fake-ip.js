// 示例 3：统一 DNS 与 fake-ip 过滤规则
// 只修改 DNS 数据面；不要在这里修改 mixed-port、external-controller、secret 等
// 由 Nyan-Q 管理的客户端字段，它们在脚本之后会被重新应用。
function main(config, profileName) {
    const dns = config.dns || {};
    dns.enable = true;
    dns["enhanced-mode"] = "fake-ip";
    dns["fake-ip-range"] = dns["fake-ip-range"] || "198.18.0.1/16";
    dns["default-nameserver"] = ["223.5.5.5", "1.1.1.1"];
    dns.nameserver = ["https://doh.pub/dns-query", "https://dns.alidns.com/dns-query"];

    const filter = Array.isArray(dns["fake-ip-filter"]) ? dns["fake-ip-filter"] : [];
    for (const domain of ["+.msftconnecttest.com", "+.msftncsi.com", "*.lan", "*.local"]) {
        if (!filter.includes(domain)) {
            filter.push(domain);
        }
    }
    dns["fake-ip-filter"] = filter;
    config.dns = dns;

    console.log("DNS 已更新，fake-ip-filter 共", filter.length, "项");
    return config;
}
