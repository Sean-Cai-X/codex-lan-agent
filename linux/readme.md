# Linux 审计网关运维说明

本文说明 `192.168.8.123` 审计网关的当前架构、服务职责、部署文件、验证方法和回退规范。当前目标是：主机所有代理流量先进入网关审计，网关再决定拒绝、直连或交给上游代理出口。

## 当前架构

```text
Windows 主机 192.168.8.100
  -> Clash Verge 127.0.0.1:7897
  -> 192.168.8.123:2080 sing-box mixed inbound
  -> sing-box 审计规则
    -> block-out: fonts.googleapis.com 等审计域名
    -> mihomo-out: 127.0.0.1:12790
  -> codex-mihomo 使用 Clash Verge 同步配置中的节点出网
```

关键原则：

- 网关是审计权威点。
- 本机 Clash Verge 只做第一跳转发，不做 `fonts.googleapis.com` 本地拒绝。
- `sing-box` 负责入口认证、审计规则和流量转发。
- `codex-mihomo` 负责上游代理出口，配置来源是 Windows Clash Verge 当前生成配置。

## 网关基础信息

| 项目 | 当前值 |
| --- | --- |
| 网关地址 | `192.168.8.123` |
| 主机地址 | `192.168.8.100` |
| SSH 用户 | `root` |
| SSH key | `D:\Codex-WorkDir\Sean_WorkDir\tmpkeys\codex_kvm_ed25519` |
| 审计入口 | `192.168.8.123:2080` |
| 审计入口认证 | `codex / CHANGE_ME_AUDIT_GATEWAY_PASSWORD` |
| sing-box 配置 | `/etc/sing-box/config.json` |
| sing-box 服务 | `sing-box.service` |
| mihomo 程序 | `/usr/local/bin/mihomo` |
| mihomo 配置目录 | `/opt/codex-mihomo` |
| mihomo 服务 | `codex-mihomo.service` |
| mihomo 本地入口 | `127.0.0.1:12790` |
| mihomo 控制端口 | `127.0.0.1:19090` |

## Git 内配置文件

这些文件是可提交到 GitHub 的开发配置或模板：

| 文件 | 用途 |
| --- | --- |
| `linux/config/sing-box/codex-vm-mixed-inbound.json` | 当前审计网关 sing-box 配置模板，包含 `2080 -> mihomo-out` 链路 |
| `linux/config/publish/sing-box/codex-vm-mixed-inbound.json` | 发布/交付用 sing-box 同步配置 |
| `linux/config/systemd/codex-mihomo.service` | 网关侧 Mihomo systemd unit |
| `linux/config/mihomo/codex-mihomo.template.yaml` | 网关侧 Mihomo 配置模板，不包含真实订阅节点 |
| `linux/config/dnsmasq/codex-managed-dns.conf` | DNS 审计管理配置 |
| `linux/readme.md` | 本 runbook |

这些文件不能进 Git：

```text
真实 Clash Verge 订阅配置
真实 Mihomo 节点密码
/opt/codex-mihomo/config.yaml
Country.mmdb
geoip.dat
geosite.dat
*.exe / *.dll / 二进制
*.zip / *.gz / *.deb / *.rpm / 压缩包和安装包
*.png / *.jpg / *.ico / 图片
logs/
linux/config/ssh/
linux/tmp_mihomo_deploy/
linux/config/publish/kvm_delivery/
```

SSH 命令模板：

```powershell
ssh -i D:\Codex-WorkDir\Sean_WorkDir\tmpkeys\codex_kvm_ed25519 `
  -o UserKnownHostsFile=NUL `
  -o StrictHostKeyChecking=no `
  root@192.168.8.123 "hostname; date"
```

## 服务职责

### sing-box

`sing-box` 是正式审计入口。systemd unit 使用：

```text
/usr/bin/sing-box -D /var/lib/sing-box -C /etc/sing-box run
```

当前配置要求：

```json
{
  "inbounds": [
    {
      "type": "mixed",
      "tag": "mixed-eth2",
      "listen": "0.0.0.0",
      "listen_port": 2080,
      "bind_interface": "eth2",
      "users": [
        {
          "username": "codex",
          "password": "CHANGE_ME_AUDIT_GATEWAY_PASSWORD"
        }
      ]
    }
  ],
  "outbounds": [
    { "tag": "direct-out", "type": "direct" },
    { "tag": "block-out", "type": "block" },
    {
      "tag": "mihomo-out",
      "type": "http",
      "server": "127.0.0.1",
      "server_port": 12790
    }
  ],
  "route": {
    "final": "mihomo-out"
  }
}
```

注意：`2080` 绑定在 `eth2`，因此网关本机用 `127.0.0.1:2080` 测试会失败。实际测试要从 Windows 主机访问 `192.168.8.123:2080`。

### codex-mihomo

`codex-mihomo` 是网关内部上游代理出口。它复用 Clash Verge 当前生成配置，端口改为网关本地端口：

```yaml
mixed-port: 12790
allow-lan: false
bind-address: 127.0.0.1
external-controller: 127.0.0.1:19090
```

服务文件：

```text
/etc/systemd/system/codex-mihomo.service
```

服务启动命令：

```text
/usr/local/bin/mihomo -d /opt/codex-mihomo -f /opt/codex-mihomo/config.yaml
```

## 常用检查

服务状态：

```bash
systemctl is-active sing-box.service
systemctl is-active codex-mihomo.service
systemctl --no-pager --full status sing-box.service
systemctl --no-pager --full status codex-mihomo.service
```

监听端口：

```bash
ss -lntp | awk '$4 ~ /:2080$/ || $4 ~ /:12790$/ || $4 ~ /:19090$/ { print }'
```

配置摘要：

```bash
python3 - <<'PY'
import json
p='/etc/sing-box/config.json'
d=json.load(open(p))
print('final', d.get('route',{}).get('final'))
print('outbounds', [(o.get('tag'),o.get('type'),o.get('server'),o.get('server_port')) for o in d.get('outbounds',[])])
PY
```

日志：

```bash
journalctl -u sing-box.service -u codex-mihomo.service --since '10 minutes ago' --no-pager
```

期望能看到类似：

```text
inbound connection from 192.168.8.100
[codex] inbound connection to api.openai.com:443
outbound/http[mihomo-out]: outbound connection to api.openai.com:443
```

## 验证标准

从 Windows 主机验证网关入口：

```powershell
curl.exe -sS -x http://codex:CHANGE_ME_AUDIT_GATEWAY_PASSWORD@192.168.8.123:2080 --connect-timeout 10 --max-time 25 `
  -I https://api.openai.com/v1/models
```

期望：`HTTP/1.1 401 Unauthorized`。

```powershell
curl.exe -sS -x http://codex:CHANGE_ME_AUDIT_GATEWAY_PASSWORD@192.168.8.123:2080 --connect-timeout 8 --max-time 15 `
  -I https://fonts.googleapis.com/
```

期望：`CONNECT established` 后连接复位或 TLS 握手失败。

```powershell
curl.exe -sS -x http://codex:CHANGE_ME_AUDIT_GATEWAY_PASSWORD@192.168.8.123:2080 --connect-timeout 8 --max-time 20 `
  -I http://example.com/
```

期望：`HTTP/1.1 200 OK`。

从 Windows 当前系统代理验证完整链路：

```powershell
curl.exe -sS -x http://127.0.0.1:7897 --connect-timeout 10 --max-time 25 `
  -I https://api.openai.com/v1/models

curl.exe -sS -x http://127.0.0.1:7897 --connect-timeout 8 --max-time 15 `
  -I https://fonts.googleapis.com/

curl.exe -sS -x http://127.0.0.1:7897 --connect-timeout 8 --max-time 20 `
  -I http://example.com/
```

## 同步 Clash Verge 配置到网关

Windows 源配置目录：

```text
C:\Users\Ums_Ai_Coder\AppData\Roaming\io.github.clash-verge-rev.clash-verge-rev
```

需要同步到网关的文件：

```text
clash-verge.yaml -> /opt/codex-mihomo/config.yaml
geoip.dat        -> /opt/codex-mihomo/geoip.dat
geosite.dat      -> /opt/codex-mihomo/geosite.dat
Country.mmdb     -> /opt/codex-mihomo/Country.mmdb
```

同步前必须改写网关版配置：

```yaml
mixed-port: 12790
allow-lan: false
bind-address: 127.0.0.1
external-controller: 127.0.0.1:19090
```

同步后检查：

```bash
/usr/local/bin/mihomo -t -d /opt/codex-mihomo -f /opt/codex-mihomo/config.yaml
systemctl restart codex-mihomo.service
systemctl is-active codex-mihomo.service
```

同步到 Git 时只提交模板和结构配置，不提交真实 `/opt/codex-mihomo/config.yaml`。真实文件可能包含订阅节点、密码和服务商地址，只能留在运行机或私有备份。

## 修改 sing-box 前的流程

1. 在 Windows 主机挂自动回退保险丝。
2. 在网关备份 `/etc/sing-box/config.json`。
3. 写新配置到 `/tmp/sing-box.config.new.json`。
4. 执行 `sing-box check -c /tmp/sing-box.config.new.json`。
5. 通过后替换 `/etc/sing-box/config.json`。
6. 重启 `sing-box.service`。
7. 从 Windows 主机执行三条验证。
8. 验证通过后取消保险丝。

Windows 保险丝：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\scripts\arm_audit_gateway_rollback.ps1 `
  -DelaySeconds 900
```

取消保险丝：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\scripts\cancel_audit_gateway_rollback.ps1
```

网关备份：

```bash
install -d /root/codex-sing-box-backups
cp -a /etc/sing-box/config.json /root/codex-sing-box-backups/config.json.$(date +%Y%m%d_%H%M%S).bak
```

配置检查：

```bash
sing-box check -c /tmp/sing-box.config.new.json
```

重启：

```bash
systemctl restart sing-box.service
systemctl is-active sing-box.service
```

## 回退

Windows 侧自动回退：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\scripts\arm_audit_gateway_rollback.ps1 `
  -DelaySeconds 1
```

网关侧手动回退：

```bash
ls -lt /root/codex-sing-box-backups/
cp -f /root/codex-sing-box-backups/<backup-file> /etc/sing-box/config.json
sing-box check -C /etc/sing-box
systemctl restart sing-box.service
```

如果 `codex-mihomo` 异常：

```bash
systemctl restart codex-mihomo.service
journalctl -u codex-mihomo.service --since '5 minutes ago' --no-pager
```

如果需要临时恢复网关直连出口：

```bash
python3 - <<'PY'
import json
p='/etc/sing-box/config.json'
d=json.load(open(p))
d['route']['final']='direct-out'
d['outbounds']=[o for o in d.get('outbounds',[]) if o.get('tag')!='mihomo-out']
open('/tmp/sing-box.direct-out.json','w').write(json.dumps(d,indent=2,ensure_ascii=False))
PY
sing-box check -c /tmp/sing-box.direct-out.json
cp -f /tmp/sing-box.direct-out.json /etc/sing-box/config.json
systemctl restart sing-box.service
```

## 禁止操作

- 禁止在未挂 Windows 保险丝时切换系统代理或 Clash Verge 配置。
- 禁止把 `fonts.googleapis.com` 的截断放到本机 Clash Verge。
- 禁止让 OpenAI / Codex 直接绕过 `192.168.8.123:2080`。
- 禁止未执行 `sing-box check` 就替换 `/etc/sing-box/config.json`。
- 禁止用网关本机 `127.0.0.1:2080` 判断 `2080` 是否可用；该入口绑定 `eth2`，应从 Windows 主机测 `192.168.8.123:2080`。
- 禁止直接删除 `/root/codex-sing-box-backups/` 和 `D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\logs\network_rollback\clash_verge_backup`。
