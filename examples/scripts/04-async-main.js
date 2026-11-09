// 示例 4：异步 main 与规则调整
// main 可以返回 Promise。脚本没有定时器、网络或文件接口，因此 Promise 必须
// 通过微任务完成；等待永远不会发生的异步事件会被判定为失败。
async function main(config, profileName) {
    await Promise.resolve();

    const remoteDns = "https://dns.google/dns-query";
    const dns = config.dns || {};
    const nameservers = Array.isArray(dns.nameserver) ? dns.nameserver : [];
    if (!nameservers.includes(remoteDns)) {
        nameservers.push(remoteDns);
    }
    dns.nameserver = nameservers;
    config.dns = dns;

    console.log("异步脚本完成：" + profileName);
    return config;
}
