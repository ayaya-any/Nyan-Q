// 示例 5：让指定 IP/CIDR 绕过 TUN 虚拟网卡
// 只有在启用 TUN 时生效；适合固定 IP 的服务器、游戏或交易终端。
function main(config, profileName) {
    const excludes = [
        "175.45.34.88/32",
        "222.73.105.170/32"
    ];

    config.tun = config.tun || {};
    const current = Array.isArray(config.tun["route-exclude-address"])
        ? config.tun["route-exclude-address"]
        : [];
    config.tun["route-exclude-address"] = [
        ...new Set([...current, ...excludes])
    ];

    console.log("TUN 排除地址共 " + config.tun["route-exclude-address"].length + " 条");
    return config;
}
