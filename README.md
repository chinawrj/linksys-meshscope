# MeshScope

MeshScope 是一个面向 Linksys Velop / Intelligent Mesh 的本地拓扑仪表盘。它把
Node 的 Parent/Child 关系、`5GH` / `5GL` 回程、信号与速率，以及每个 Node
下的 Client/STA 放在同一页里。拓扑读取保持只读；唯一开放的写操作是向用户
选中的在线 Node 直接发送一次 `core/Reboot`。

MeshScope is a local-first Linksys Mesh topology and Client/STA dashboard. The
UI is Chinese today, while the code and research notes are kept readable for
international contributors.

[![CI](https://github.com/chinawrj/linksys-meshscope/actions/workflows/ci.yml/badge.svg)](https://github.com/chinawrj/linksys-meshscope/actions/workflows/ci.yml)
![Linksys](https://img.shields.io/badge/Linksys-local%20JNAP-16769b)
![Runtime](https://img.shields.io/badge/runtime-Python%20%7C%20ESPHome-2d705b)

> MeshScope 不是 Linksys 官方产品。请只管理你拥有或获准管理的网络。

## 先选择运行方式

| | 桌面本地版 | ESPHome / ESP32-C5 常开版 |
|---|---|---|
| 适合 | 先试用、调试、偶尔查看 | 长期运行、手机访问、接入 Home Assistant |
| 运行位置 | Mac / Linux / Windows 上的 Python | ESP32-C5，页面直接由设备提供 |
| 依赖 | Python 3.10+，运行时无第三方包 | ESPHome 2026.7.2、8 MB Flash、至少 4 MB PSRAM |
| 路由器凭证 | 仅保存在 Python 进程内存 | 保存在忽略提交的 YAML、构建产物与设备 Flash |
| 网页访问 | 默认仅 `127.0.0.1:8765` | 家庭 LAN 上的 HTTP Basic 登录 |
| Home Assistant | 无 | 加密 ESPHome Native API |

如果只是想确认页面是否适合自己的 Mesh，先运行桌面版的 `--demo`。它包含完整
的多层 Parent 拓扑、`5GH` / `5GL`、Node/Client 详情和重启状态演示，但不会
认证或控制任何路由器。

## 页面提供什么

- 按 Parent 子树排列的拓扑，回程路径带动画和方向
- `5GH` / `5GL`、信道、协商速率与 RSSI，不合并或隐藏字段
- 10 / 30 / 60 秒自动刷新、暂停、手动刷新，以及后台页面恢复后的补刷新
- 点击 Node 查看其 Client/STA；点击 Client 可回到所属 Node
- Client 的频段、信号、速率、IP、MAC、型号和在线状态
- 自动 Client Steering / Node Steering 状态
- 直接读取子 Node 的身份、同步凭证和固件能力
- Linksys 子 Node 支持页入口：`https://<node-ip>/ca`
- 选定在线 Node 的即时重启，以及离线、恢复过程跟踪
- 路由器暂时不可达时保留最后完整拓扑，并明确标记“离线 · 最后缓存”

## 桌面版：5 分钟开始

```bash
git clone https://github.com/chinawrj/linksys-meshscope.git
cd linksys-meshscope
python3 linksys_mesh_app.py
```

然后打开 [http://127.0.0.1:8765](http://127.0.0.1:8765)。macOS 也可以双击
`Start MeshScope.command`。

首次打开时输入：

1. 主 Linksys 路由器的 LAN 地址，例如 `192.168.1.1`；不要填写当前电脑或
   ESP32 的地址。
2. Linksys 本地管理密码。它通常会自动同步到所有子 Node。

也可以在完全离线的情况下查看完整演示：

```bash
python3 linksys_mesh_app.py --demo
```

如需自动连接，可通过环境变量启动：

```bash
LINKSYS_PASSWORD='your-local-router-password' python3 linksys_mesh_app.py \
  --router 192.168.1.1
```

交互式网页输入更适合共享电脑，因为命令行和 shell history 可能记录环境变量。
桌面服务默认只监听 `127.0.0.1`，密码仅存在于该 Python 进程内存，服务停止后
清除。

## ESPHome / ESP32-C5 常开版

### 已验证目标和前提

- `esp32-c5-devkitc-1`
- 8 MB Flash
- 至少 4 MB PSRAM；没有 PSRAM 会在配置阶段直接失败
- ESP32、Linksys 主路由器和访问网页的设备位于同一个可信家庭 LAN
- Linksys 固件提供本地 HTTPS JNAP

开发时在由 MX42、MX5300 和 WHW03 组成的混合 Mesh 上做过真实验证。其他
Linksys 型号可能使用不同 JNAP 字段，建议先用桌面版确认兼容性。

### 1. 创建只在本机保存的配置

```bash
git clone https://github.com/chinawrj/linksys-meshscope.git
cd linksys-meshscope
cp esphome_meshscope_c5.local.example.yaml esphome_meshscope_c5.local.yaml
```

先生成三组互不复用的凭证：

```bash
# 隐藏输入 Linksys 本地密码，输出可安全放进 C++ 配置的 Base64 值
python3 tools/encode_secret.py

# ESPHome Native API key：把输出放到 meshscope_api_key
openssl rand -base64 32

# 分别生成 Web 登录密码和 OTA 密码；不要复用
openssl rand -hex 24
openssl rand -hex 24
```

编辑 `esphome_meshscope_c5.local.yaml`：

- `meshscope_wifi_ssid` / `meshscope_wifi_password`：家庭 Wi-Fi
- `meshscope_router_host`：主 Linksys 路由器的 LAN 地址
- `meshscope_router_password_b64`：`encode_secret.py` 的输出，不是明文密码
- `meshscope_web_username` / `meshscope_web_password`：浏览器访问 ESP32 页面时
  使用；建议用户名只含字母、数字、`-` 或 `_`，密码使用上面的 hex 输出
- `meshscope_api_key`：Home Assistant 使用的 32-byte Base64 key
- `meshscope_ota_password`：后续 OTA 更新密码
- `meshscope_timezone`：ESPHome 日志与时间组件使用的时区，例如
  `Asia/Shanghai`

这个 local 文件已加入 `.gitignore`。不要把它、`.esphome/`、构建日志或固件
二进制上传到仓库。

### 2. 首次 USB 安装

```bash
python3 -m venv .esphome-venv
.esphome-venv/bin/pip install -r requirements-esphome.txt
python3 tools/generate_esp32_meshscope_assets.py
.esphome-venv/bin/esphome config esphome_meshscope_c5.local.yaml
.esphome-venv/bin/esphome run esphome_meshscope_c5.local.yaml
```

第一次 `run` 时连接 USB 并选择串口。首次下载 ESP-IDF 与编译可能需要数分钟；
后续构建会使用缓存。安装完成后可从以下位置找到设备 IP：

- ESPHome 串口日志
- Linksys 的 DHCP Client 列表
- Home Assistant 的 ESPHome 发现通知

建议在路由器中为 ESP32 建立 DHCP reservation。`.local` 可用时可访问
`http://meshscope-c5.local/`；不能解析 mDNS 时直接使用
`http://<esp32-ip>/`。浏览器会要求输入上一步设置的 MeshScope Web 用户名和
密码。

### 3. 接入 Home Assistant

ESPHome Native API 使用独立加密 key，和网页的 HTTP Basic 登录互不相同。

1. 等待最多约五分钟，Home Assistant 通常会在“设置 → 设备与服务”中发现
   `MeshScope C5`。
2. 没有自动发现时，选择“添加集成 → ESPHome”。
3. 输入 `meshscope-c5.local` 或固定的 ESP32 IP。
4. 按提示输入 local YAML 中的 `meshscope_api_key`。

设备会暴露：

- Router Connected
- Online / Total Mesh Nodes
- Online Clients
- Weak Mesh Nodes
- Backhaul Total
- Topology Summary / Last Topology Update
- MeshScope URL
- ESP32 Free Heap
- Refresh Mesh Topology 按钮

即使 Home Assistant 停机或从未接入，ESP32 也会继续采集并提供网页；配置显式
关闭了“Native API 长时间无客户端就重启”的默认联动。

### 4. 后续 OTA 更新

先拉取代码并检查本地配置，再通过设备 IP 更新：

```bash
git pull --ff-only
python3 tools/generate_esp32_meshscope_assets.py --check
.esphome-venv/bin/esphome run esphome_meshscope_c5.local.yaml \
  --device <esp32-ip>
```

如果 Wi-Fi、OTA 密码或设备地址配置错误，请重新连接 USB 运行同一个 `esphome
run` 命令。设备没有开放 fallback AP，避免在家庭网络中意外出现未规划的配置
入口。

## 日常使用和 Node 重启

页面中的自动刷新周期是浏览器的显示节奏；ESP32 后台本身每 10 秒采集一次完整
拓扑。浏览器进入后台后不会持续制造刷新请求，重新可见且数据过期时会立即补刷。

点击拓扑卡片会打开 Node 详情并列出该 Node 下的 Client/STA。`5GH` 和 `5GL`
始终显示在回程线上，附带能够读取到的信道、速率和 RSSI。

Node 详情中的“立即重启”是即时操作，没有第二次确认：

- 请求只会发送到当前选中、在线且仍在实时拓扑中的 Node
- 该 Node 和接在它下面的客户端会短暂离线
- 如果选择 Main，整个 Mesh 的管理与联网可能短暂中断
- 页面会标记“已请求 → 已离线 → 已恢复”，90 秒内拒绝重复请求
- Demo、离线 Node、未知地址和不在当前拓扑中的 ID 都不能重启

如果不希望家庭 LAN 用户具备重启权限，不要向其提供 MeshScope Web 登录。

## 常见问题

| 现象 | 处理 |
|---|---|
| `meshscope-c5.local` 打不开 | 使用 Linksys DHCP 列表或串口日志中的 IP；为 ESP32 建立 DHCP reservation |
| 页面提示路由器离线但仍有拓扑 | 这是最后一次成功缓存，不是假装在线；检查主路由器地址、Wi-Fi 和 Linksys 本地密码 |
| 首次页面显示“正在读取” | 给 ESP32 最多一个完整 JNAP 周期；持续失败时查看 ESPHome 日志 |
| 浏览器反复要求登录 | 核对 `meshscope_web_username/password`，清除该地址的错误 Basic Auth 凭证后重试 |
| Home Assistant 未发现 | 手动添加 ESPHome 集成并输入 ESP32 IP 与 `meshscope_api_key` |
| 配置报告 PSRAM 缺失 | 当前固件不能在无 PSRAM 的板上运行；确认开发板型号和硬件规格 |
| 改了前端但 ESP32 页面没变 | 运行资产生成器，再重新编译/OTA |
| Wi-Fi 或 OTA 配错后设备失联 | USB 连接设备并重新运行 `esphome run` |
| Linksys 密码含特殊字符 | 使用 `tools/encode_secret.py` 生成 Base64；不要把明文直接插入固件 lambda |

## 安全与隐私边界

桌面版和 ESPHome 版的凭证模型不同：

- 桌面版密码只保存在 Python 进程内存，默认网页也只绑定 localhost。
- ESPHome 版必须把 Wi-Fi、Linksys、Web、API 和 OTA 凭证保存在忽略提交的
  local YAML，并编译到设备 Flash；HTTP API 不会把这些值返回给浏览器。
- ESP32 页面和所有页面 API 受独立 HTTP Basic 登录保护，但 HTTP 本身不提供
  传输加密。只在可信家庭 LAN 使用，禁止端口转发或直接暴露到互联网。
- ESPHome Native API 使用单独的加密 key；它不会自动加密 MeshScope 网页。
- Linksys 本地 JNAP 使用路由器的自签名 HTTPS 证书。客户端为本地兼容性跳过
  证书验证，因此安全假设包含“家庭 LAN 和网关未被恶意劫持”。

读取操作由允许名单限制为 `Get*` / `Check*`。写操作允许名单只有
`core/Reboot`，并且必须解析到实时拓扑中已知的私有地址和在线 Node。Reset、
Parent Steering、固件更新以及其他变更在生成路由器请求之前即被拒绝。

仓库里的 MQTT / BLE / 固件 overlay 是离线、owner-controlled 的研究材料；
运行中的网页无法调用它们，生成的 IMG 和官方固件输入也不会发布。

## Node 支持页、Steering 与高级研究

在线 Node 详情会进行以下只读调用：

- `core/CheckAdminPassword`
- `core/GetDeviceInfo`
- `nodes/smartmode/GetDeviceMode`
- `nodes/topologyoptimization/GetTopologyOptimizationSettings2`

Linksys 子 Node 的重定向绕过入口为：

```text
https://<node-ip>/ca
```

浏览器会因路由器的自签名证书显示警告。当前公开 JNAP 只提供自动 Client
Steering / Node Steering。离线固件分析已经确认指定 Parent 的内部数据路径，
但尚无适合普通网页暴露的受支持传输，所以 MeshScope 不提供手动 Parent 控制。

详细材料：

- [Node control feasibility](docs/node-control-findings.md)
- [Node Steering findings](docs/node-steering-findings.md)
- [Hidden firmware interfaces and Parent steering](docs/hidden-firmware-interfaces.md)
- [WHW03 firmware and SSH scaffold analysis](docs/whw03-firmware-analysis.md)
- [MX4200 firmware, steering, reboot, and SSH analysis](docs/mx4200-firmware-analysis.md)
- [MX4200 SSH bootstrap and exact-Parent control plan](docs/mx4200-ssh-parent-control.md)
- [MX4200 MQTT control and custom-IMG feasibility](docs/mx4200-mqtt-and-custom-img.md)
- [MX5300 MQTT, exact-Parent control, and custom-IMG feasibility](docs/mx5300-mqtt-and-custom-img.md)
- [MX4200/MX5300 BLE and exact-Parent steering](docs/linksys-ble-parent-steering.md)
- [Offline BLE-JNAP advanced-Action proof overlay](firmware-overlays/ble-parent-steering/README.md)
- [MQTT experiment-image builder](firmware-overlays/mqtt-parent-steering/README.md)

## 测试与前端开发

```bash
python3 -m unittest discover -s tests -p 'test_*.py' -v
node --test \
  tests/test_connection_mode.js \
  tests/test_refresh_state.js \
  tests/test_topology_layout.js \
  tests/test_detail_data.js \
  tests/test_node_restart_state.js \
  tests/test_linksys_normalize.js
```

修改 `mesh_web/` 后必须同步生成 ESP32 内嵌资产：

```bash
python3 tools/generate_esp32_meshscope_assets.py
python3 tools/generate_esp32_meshscope_assets.py --check
```

CI 会检查 Python/JavaScript 语法、所有单元测试和内嵌资产是否与网页源文件一致。

## 许可证状态

仓库目前公开可见，但尚未选择 `LICENSE`。在 owner 明确选择许可证之前，请不要
把“public repository”理解为已经授予复制、修改或再分发许可。
