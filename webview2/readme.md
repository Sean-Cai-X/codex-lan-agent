# Windows 代理入口与 Clash Verge 规范

本文说明本机代理入口、Clash Verge 配置、审计网关链路和回退规范。当前生产链路以 Clash Verge 为准，`webview2` 目录里的 `clash-meta.exe` / `clash-native.exe` 只作为轻量控制面板原型和局部验证工具，不是当前 Codex 的主出口。

## 当前生产链路

```text
Windows 应用 / Codex / 浏览器
  -> Windows 当前用户系统代理 127.0.0.1:7897
  -> Clash Verge / verge-mihomo
  -> codex-audit-gateway
  -> 192.168.8.123:2080 sing-box 审计入口
  -> 网关规则判断
  -> 允许流量转发到 127.0.0.1:12790 codex-mihomo
  -> codex-mihomo 使用 Clash Verge 同步过来的节点出网
```

关键原则：

- 本机只负责把代理流量送到审计网关。
- `fonts.googleapis.com` 等审计规则必须在网关判断，不在本机 Clash Verge 里写本地域名 `REJECT`。
- 不要把 Clash Verge DNS 上游统一改成 `192.168.8.123` 来代替网关审计。
- OpenAI / Codex 的可用性由网关侧 `codex-mihomo` 上游出口保障，不应绕过审计网关。

## 当前端口与进程

| 项目 | 当前值 | 说明 |
| --- | --- | --- |
| Windows 系统代理 | `127.0.0.1:7897` | 当前用户代理入口 |
| Clash Verge 内核 | `verge-mihomo.exe` | 真实生产入口 |
| Clash Verge 配置目录 | `C:\Users\Ums_Ai_Coder\AppData\Roaming\io.github.clash-verge-rev.clash-verge-rev` | 真实运行配置目录 |
| Clash Verge 主配置 | `clash-verge.yaml` | `verge-mihomo.exe -f` 使用的文件 |
| 审计网关上游 | `codex-audit-gateway` | HTTP 代理上游 |
| 审计网关地址 | `192.168.8.123:2080` | 用户名 `codex`，密码使用私有运行配置，不提交 Git |
| webview2 测试端口 | `127.0.0.1:7890` | 仅用于本仓库原型，不是系统代理 |

检查当前系统代理：

```powershell
Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings' |
  Select-Object ProxyEnable,ProxyServer,AutoConfigURL
```

检查 Clash Verge 内核：

```powershell
Get-CimInstance Win32_Process |
  Where-Object { $_.Name -match 'clash-verge|verge-mihomo' } |
  Select-Object ProcessId,Name,CommandLine

Get-NetTCPConnection -LocalPort 7897 -ErrorAction SilentlyContinue |
  Select-Object LocalAddress,LocalPort,State,OwningProcess
```

## Clash Verge 配置要求

Clash Verge 的当前生效配置在：

```text
C:\Users\Ums_Ai_Coder\AppData\Roaming\io.github.clash-verge-rev.clash-verge-rev\clash-verge.yaml
```

配置必须满足：

```yaml
mixed-port: 7897
allow-lan: false

proxies:
- name: codex-audit-gateway
  type: http
  server: 192.168.8.123
  port: 2080
  username: codex
  password: CHANGE_ME_AUDIT_GATEWAY_PASSWORD

rules:
- DOMAIN,localhost,DIRECT
- DOMAIN-SUFFIX,localhost,DIRECT
- IP-CIDR,127.0.0.0/8,DIRECT,no-resolve
- IP-CIDR6,::1/128,DIRECT,no-resolve
- MATCH,codex-audit-gateway
```

说明：

- 本地 Codex MCP 例外必须放在任何 `MATCH` 规则之前，确保 `127.0.0.1:18080/mcp` 和 `/tools` 直连本机。
- `MATCH,codex-audit-gateway` 必须是第一条非本地 MCP 例外规则，确保其它流量不再按订阅规则自行分流。
- 文件后续订阅规则可以保留，但会被 `MATCH,codex-audit-gateway` 截住。
- 不要在本机添加 `DOMAIN,fonts.googleapis.com,REJECT`。
- 不要把 OpenAI / Codex 域名加成本机绕行规则，否则会绕过网关审计。

配置语法检查：

```powershell
$base = "C:\Users\Ums_Ai_Coder\AppData\Roaming\io.github.clash-verge-rev.clash-verge-rev"
& "C:\Program Files\Clash Verge\verge-mihomo.exe" -t -d $base -f "$base\clash-verge.yaml"
```

重启 Clash Verge 内核：

```powershell
$base = "C:\Users\Ums_Ai_Coder\AppData\Roaming\io.github.clash-verge-rev.clash-verge-rev"
Get-CimInstance Win32_Process |
  Where-Object { $_.Name -eq "verge-mihomo.exe" } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force }

Start-Sleep -Seconds 2
Start-Process -FilePath "C:\Program Files\Clash Verge\verge-mihomo.exe" `
  -ArgumentList "-d",$base,"-f","$base\clash-verge.yaml","-ext-ctl-pipe","\\.\pipe\verge-mihomo" `
  -WorkingDirectory $base `
  -WindowStyle Hidden
```

## 切换前保险丝

任何会影响 `clash-verge.yaml`、系统代理或网关入口的测试，必须先挂自动回退保险丝。保险丝会在超时后：

1. 恢复 Windows 系统代理到 `127.0.0.1:7897`。
2. 恢复 Clash Verge 的配置备份。
3. 重启 Clash Verge。

挂 15 分钟保险丝：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\scripts\arm_audit_gateway_rollback.ps1 `
  -DelaySeconds 900
```

确认测试成功后取消：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\scripts\cancel_audit_gateway_rollback.ps1
```

查看回退日志：

```powershell
Get-Content D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\logs\network_rollback\audit_gateway_rollback.log -Tail 50
```

备份目录：

```text
D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\logs\network_rollback\clash_verge_backup
```

## 验证标准

所有验证都应从本机系统代理端口 `127.0.0.1:7897` 发起。

OpenAI API 可达：

```powershell
curl.exe -sS -x http://127.0.0.1:7897 --connect-timeout 10 --max-time 25 `
  -I https://api.openai.com/v1/models
```

期望：`HTTP/1.1 401 Unauthorized`。这表示网络通，只是没有 API token。

`fonts.googleapis.com` 被网关截断：

```powershell
curl.exe -sS -x http://127.0.0.1:7897 --connect-timeout 8 --max-time 15 `
  -I https://fonts.googleapis.com/
```

期望：`CONNECT established` 后 TLS 握手失败或连接复位。不要期望本机 Clash 返回 `REJECT`，因为审计判断在网关。

普通网站可达：

```powershell
curl.exe -sS -x http://127.0.0.1:7897 --connect-timeout 8 --max-time 20 `
  -I http://example.com/
```

期望：`HTTP/1.1 200 OK`。

## webview2 原型说明

`webview2` 目录仍保留一个无 WebView 的 Clash Native 原型：

- 桌面端使用 Go + Fyne。
- 设置页使用 Mihomo `external-ui` 托管的本地静态页面。
- 通过 Mihomo/Clash Meta HTTP API 控制核心。
- 不使用 WebView、Chromium、Edge WebView2 或 JavaScript 运行时壳。

## 打开 Clash Native

双击或在 CMD/PowerShell 中运行：

```bat
D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\webview2\start_clash_native.bat
```

脚本会执行：

1. 停止旧的项目本地 `webview2\clash-meta.exe`。
2. 使用 `webview2\config.yaml` 启动新的 `clash-meta.exe`。
3. 等待 `http://127.0.0.1:9090/version` 可用。
4. 打开 `clash-native.exe`。

启动成功后应看到：

```text
webview2\clash-meta.exe 监听 127.0.0.1:7890 和 127.0.0.1:9090
webview2\clash-native.exe 已打开
```

检查命令：

```powershell
Get-CimInstance Win32_Process |
  Where-Object { $_.Name -match 'clash-native|clash-meta' } |
  Select-Object ProcessId,Name,CommandLine

Get-NetTCPConnection -LocalPort 7890,9090 -ErrorAction SilentlyContinue |
  Select-Object LocalAddress,LocalPort,State,OwningProcess

Invoke-RestMethod http://127.0.0.1:9090/version
```

Clash Native 桌面窗口关闭时默认隐藏到托盘；要完全退出，需要在托盘菜单选择 `Quit`，或结束 `clash-native.exe`。

## Clash Native 界面说明

桌面窗口主要字段：

| 字段/按钮 | 说明 |
| --- | --- |
| `Subscription URL` | 下载订阅并保存为 `webview2\config.yaml`。生产链路不建议通过这里覆盖配置。 |
| `API` | Clash API 地址。项目本地 core 默认是 `http://127.0.0.1:9090`。 |
| `Secret` | 如果 `config.yaml` 设置了 API secret，这里填写同值；当前通常为空。 |
| `Proxy` | `Proxy On` 写入 Windows 系统代理的地址。项目本地 core 默认是 `127.0.0.1:7890`。 |
| `Start` / `Stop` | 启停 `webview2\clash-meta.exe`。 |
| `Proxy On` / `Proxy Off` | 开关 Windows 当前用户系统代理。生产环境不要随意点 `Proxy On` 切到 `7890`。 |
| `Refresh` | 从 `/proxies` 读取节点列表。 |
| `Node` | 对 Clash API 的 `GLOBAL` 节点执行选择。 |
| `Delay` | 对当前选中节点做延迟测试。 |

网页设置页：

```text
http://127.0.0.1:9090/ui/
```

网页设置页可以查看 runtime、mode、GLOBAL 当前节点、节点状态，也可以切换 mode 和 GLOBAL 节点。

## GLOBAL Node 选择规范

Clash Native 里的 `Node` / `GLOBAL Node` 操作调用的是 Mihomo API：

```text
PUT /proxies/GLOBAL
```

它修改的是 `GLOBAL` 选择器当前选中的节点。它和规则模式的关系如下：

| 当前 Mode | 选择 `codex-audit-gateway` | 选择其它订阅节点 |
| --- | --- | --- |
| `rule` | 如果配置中本地 MCP 例外之后第一条是 `MATCH,codex-audit-gateway`，除本机 MCP 外的实际流量仍先走审计网关；`GLOBAL` 选择通常不会改变这条强制规则。 | 由于 `MATCH,codex-audit-gateway` 已经截住流量，其它节点选择通常不会生效。 |
| `global` | 所有项目本地 `7890` 流量都走 `192.168.8.123:2080`，由网关审计后再转发到网关内 `codex-mihomo`。这是审计合规选择。 | 所有项目本地 `7890` 流量直接走该订阅节点，绕过审计网关；`fonts.googleapis.com` 不再由网关判断。只允许临时排障，不作为正常运行方式。 |
| `direct` | 不使用代理节点，直接连接目标。 | 同左，节点选择不生效。 |

推荐状态：

```text
Mode: rule
rules 首部: local MCP DIRECT 例外，然后 MATCH,codex-audit-gateway
```

或在明确测试 `GLOBAL` 行为时使用：

```text
Mode: global
GLOBAL Node: codex-audit-gateway
```

不推荐状态：

```text
Mode: global
GLOBAL Node: 任意非 codex-audit-gateway 节点
```

原因：这会让 `webview2\clash-meta.exe` 直接使用订阅代理节点出网，绕过 `192.168.8.123:2080` 的审计规则。

注意：这些说明只针对 `webview2` 原型的 `127.0.0.1:7890`。当前真实生产系统代理仍是 Clash Verge 的 `127.0.0.1:7897`，不要把 Clash Native 的 `Proxy On` 当作生产切换按钮。

运行时文件不提交 Git：

```text
*.exe
*.dll
*.zip / *.7z / *.rar / *.tar.gz
*.png / *.jpg / *.ico / *.svg
config.yaml
*.local.yaml
profiles/
providers/
*.log
cache.db
Country.mmdb
geoip.dat
geosite.dat
.tools/
```

原型运行需要：

```text
clash-meta.exe
clash-native.exe
config.yaml
```

启动脚本：

```bat
start_clash_native.bat
```

注意：该脚本默认启动 `webview2\clash-meta.exe`，通常监听 `127.0.0.1:7890` 和 `127.0.0.1:9090`。这不是当前 Windows 系统代理入口。排查 Codex 断网问题时，优先检查 Clash Verge 的 `127.0.0.1:7897`。

## 禁止操作

- 禁止为了截断 `fonts.googleapis.com` 在本机 Clash Verge 或 `webview2/config.yaml` 顶部加 `REJECT` 规则。
- 禁止把本机 DNS 或 Clash DNS 改成单一 `192.168.8.123` 后就认为审计完成。
- 禁止在没有保险丝时改系统代理、重启 Clash Verge 或替换 `clash-verge.yaml`。
- 禁止让 OpenAI / Codex 域名绕过 `codex-audit-gateway`。
- 禁止把 `webview2\clash-meta.exe` 的 `7890` 误认为 Clash Verge 的真实系统代理入口。
