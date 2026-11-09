// 示例 2：按关键词过滤节点，并同步清理代理组中的失效引用
// 注意：删除节点后必须同步删除代理组和规则中对节点的引用，否则 Mihomo 会拒绝配置。
function main(config, profileName) {
    const blocked = ["过期", "官网", "剩余流量", "套餐"];
    const proxies = Array.isArray(config.proxies) ? config.proxies : [];

    const kept = proxies.filter((proxy) => {
        const name = String(proxy && proxy.name ? proxy.name : "");
        return !blocked.some((word) => name.includes(word));
    });
    const removed = new Set(
        proxies
            .filter((proxy) => !kept.includes(proxy))
            .map((proxy) => String(proxy.name))
    );

    config.proxies = kept;
    config["proxy-groups"] = (config["proxy-groups"] || []).map((group) => {
        if (Array.isArray(group.proxies)) {
            group.proxies = group.proxies.filter((name) => !removed.has(String(name)));
        }
        return group;
    });

    console.log("保留节点", kept.length, "移除节点", removed.size);
    return config;
}
