# MiniKey Forge

将 MSU2 MINI 改造成基于 CH32X033F8P6 的 FIDO2 硬件原型，支持触摸确认、PIN 验证和驻留式 passkey。

## 项目状态

- v1 固件已在 CH32X033F8P6 实机完成注册、拔插后登录和独立 ES256 验签。
- 当前候选版通过 56 项主机测试，已烧录并回读校验，FIDO HID 枚举正常。
- PIN 设置、UP/UV 标志、无 allowList 的驻留账号发现、RP 绑定和独立 ES256 验签已通过实机检查。旧版浏览器凭据在迁移后登录成功，签名计数器连续递增。
- 当前待验收项：Windows 浏览器中的驻留式 passkey 登录，以及普通拔插后的驻留凭据持久化。
- LCD 初始化和绘图已关闭，LCD RESET 保持低电平；当前固件运行时屏幕熄灭。

## 功能

- **PIN 与用户验证**：实现 CTAP2.0 ClientPIN 协议 1，支持设置和修改 PIN。密钥协商使用 P-256 ECDH，PIN 加密传输使用 SHA-256、AES-256-CBC 和 HMAC。总重试额度为 8 次，连续输错 3 次后需重新上电；重试数持久保存。PIN token 保存在 RAM，重启或修改 PIN 后失效。
- **触摸确认**：输入 PIN 完成用户验证；触摸操作提供用户在场（UP）确认。设备没有生物识别传感器。
- **驻留凭据**：最多保存 48 个有效凭据，含旧版非驻留凭据；用户 ID 最多 64 B，姓名和显示名称各最多 64 B。支持无需 allowList 的账号发现和多账号 GetNextAssertion。PIN 验证前不返回姓名。
- **协议与接口**：ES256、`none` attestation、64 B CTAPHID 报告，USB VID/PID 为 `1209:0001`（原型标识）。当前固件实现 CTAP2.0，支持范围不含 U2F、NFC、内置生物验证、CTAP2.1 权限 token、凭据管理和 Large Blobs。

## Flash 存储

外置 Flash 仅使用最后 64 KiB（`0xF0000–0xFFFFF`），前 960 KiB 保持不变。保留区分为两个 32 KiB bank，每个 bank 有 56 条 512 B 记录。

签名计数器按次追加；bank 写满时，固件把最新 PIN 配置和有效凭据复制到另一个 bank，回读校验后提交带 HMAC 的库头。断电恢复时使用已完成提交的 bank。凭据上限为 48 个；记录回收后可以继续签名。

同一 RP 和 user 的新驻留注册会替换该账号的旧凭据；为其他网站注册会保留已有凭据。

固件会迁移兼容的 `MINIF2v1` 数据库，并保留 credential ID、私钥、RP 和最新签名计数器。自动迁移范围为已用记录不超过 112 条、有效凭据不超过 48 个。测试板已完成旧库迁移，升级前创建的浏览器凭据可继续登录；超出范围时固件拒绝写入并保留旧库。

## 密钥与安全实现

外置 Flash 中的私钥、RP、用户资料和 PIN 哈希使用 AES-256-CTR 与 HMAC-SHA256 保护。设备主密钥编译进 MCU 内部 Flash。

电脑端主密钥放在 `.env` 的 `MINI_MASTER_KEY_HEX`，也可使用同名进程环境变量。构建时生成被 Git 忽略的 `private/provision.c`；HEX、BIN、ELF 均包含设备主密钥，只能用于对应设备，不要提交或公开发布。更新已有设备时沿用原密钥；新设备可用 `python provision.py --generate` 生成专属密钥。PIN 由用户在系统提示中输入，不放入 `.env`。请离线备份主密钥；丢失后凭据无法恢复。

主机测试使用独立的公开测试密钥。随机种子由设备密钥混合启动 ADC 和计时噪声，熵质量尚未测量。原型目前没有硬件安全审计记录；回滚、故障注入、实机栈水位和 Flash 耐久性仍待验证。

应用代码与初始化数据为 35,976 B，静态数据约 7.3 KiB；编译配置预留 6 KiB 栈。

## Windows 构建

使用 PowerShell 7，在项目目录运行：

```powershell
python -m venv .venv
./.venv/Scripts/python.exe -m pip install -r requirements-lock.txt
./.venv/Scripts/python.exe fetch-dependencies.py
./.venv/Scripts/python.exe setup-tools.py
./build.ps1 -HostTest
./.venv/Scripts/python.exe -m pytest tests -q
./build.ps1
```

编译器使用项目 `tools/` 下的 xPack RISC-V GCC 15.2.0-1 和 Zig 0.15.2。上游依赖的固定提交见 [`dependencies.json`](dependencies.json)，工具下载地址和 SHA-256 见 [`toolchain.json`](toolchain.json)。

烧录工具 `wchisp` 需从 [ch32-rs/wchisp 官方 Releases](https://github.com/ch32-rs/wchisp/releases) 下载 Windows x64 版本，放入 `tools/wchisp-win-x64/`。第三方代码及适用许可见 [`THIRD_PARTY.md`](THIRD_PARTY.md)。项目新增代码当前未设置统一许可证。

## 烧录与验收

1. 启动 `wait-flash.py --execute`，确认状态为 `ARMED`。
2. 在前台管理员 PowerShell 7 中运行 `enter-isp.py --execute` 并触摸设备。
3. 等待程序核对设备和芯片、完成烧录与回读校验；成功后运行本地 `wchisp reset`。
4. 升级已有设备时保留原主密钥和凭据存储。不要运行初始化、CTAP Reset 或保护选项变更。

常用检查命令：

- `verify-device.py --info-only`：读取设备能力。
- `verify-pin-resident.py`：设置或输入 PIN，创建本地驻留测试凭据，并通过账号发现完成登录与独立验签。不会擦除已有凭据；再次运行会替换同一测试账号。
- `web-test.py`：启动 `http://localhost:8765`，用于旧非驻留浏览器凭据。
- `web-test.py --passkey`：启动 `http://localhost:8766`，使用要求驻留凭据和 UV 的独立测试账号。登录时不提供 allowList，PIN 在 Windows 安全提示中输入。

建议先验证旧浏览器登录，再验证 passkey，最后拔插设备后重新登录，检查凭据持久化。

## 记录

实现和验收进度见 [`CHANGELOG.md`](CHANGELOG.md)。
