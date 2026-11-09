#include "yaml_sample.h"

QString yamlSampleConfig()
{
    return QStringLiteral(R"(mixed-port: 7890
allow-lan: false
secret: "s3cret"
unified-delay: true
ipv6: false
profile:
  store-selected: true
  store-fake-ip: true
dns:
  enable: true
  enhanced-mode: fake-ip
  fake-ip-range: 198.18.0.1/16
  nameserver:
    - 223.5.5.5
    - "https://doh.pub/dns-query"
  fallback-filter:
    geoip: true
    ipcidr:
      - 240.0.0.0/4
proxies:
  - name: "香港 · 01"
    type: ss
    server: hk1.example.com
    port: 443
    cipher: aes-128-gcm
    password: "p@ss word"
    udp: true
    plugin-opts:
      mode: tls
      host: hk1.example.com
  - name: DIRECT
    type: direct
proxy-groups:
  - name: 节点选择
    type: select
    proxies:
      - 香港 · 01
      - DIRECT
rule-providers:
  reject:
    type: http
    behavior: domain
    url: "https://example.com/reject.yaml"
    path: ./rules/reject.yaml
    interval: 86400
rules:
  - RULE-SET,reject,REJECT
  - DOMAIN-SUFFIX,example.com,节点选择
  - GEOIP,CN,DIRECT
  - MATCH,节点选择
custom-extension:
  keep: "yes"
  nested:
    - a: 1
      b: 2
    - null
)");
}

QString yamlSampleConfigWithoutProxyProvider()
{
    return QStringLiteral(R"(mixed-port: 7890
mode: rule
dns:
  enable: true
  enhanced-mode: fake-ip
  nameserver:
    - 223.5.5.5
proxies:
  - name: Node A
    type: ss
    server: a.example.com
    port: 443
    cipher: aes-128-gcm
    password: pass
proxy-groups:
  - name: PROXY
    type: select
    proxies:
      - Node A
      - DIRECT
rules:
  - MATCH,PROXY
)");
}
