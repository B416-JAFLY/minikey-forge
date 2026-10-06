# MiniKey Forge

将原 MSU2 MINI 改为可触摸确认的独立 FIDO2 硬件原型。

## 当前状态

原版已在 CH32X033F8P6 实机完成注册、普通拔插后登录、独立 ES256 验签，以及触摸命令进入工厂 ISP。显示修复版仍黑屏，用户已决定暂缓动画；当前源码彻底关闭 LCD 初始化与绘图，黑屏是预期行为。问题事实、尚未验证的原因和后续排查方法见 [display-diagnosis.log](display-diagnosis.log)。

本次升级实现 **CTAP2.0 ClientPIN 协议 1、PIN 用户验证（UV）、驻留凭据（passkey）和双分区垃圾回收**。56 项协议/存储等主机测试与 1 项真实 Git 克隆测试已通过；新版已完成实机烧录/回读校验并正常枚举；旧浏览器凭据在升级后登录成功（计数器连续）；PIN、UV、无凭据 ID 的驻留账号发现与独立验签已在实机通过，驻留凭据的 Windows 浏览器和普通拔插验证仍待完成。驻留凭据的浏览器注册与登录也已通过服务端验签；普通拔插后的验证待记录。

- PIN：设置、修改、P-256 ECDH / SHA-256 / AES-256-CBC / HMAC 加密传输；不存明文 PIN。8 次总重试、连续 3 次错误需重新上电，总重试数持久保存；PIN token 在 RAM 中、重启或改 PIN 后失效。
- UV 通过输入 PIN 获得；触摸只代表用户在场（UP），没有生物识别或内置 UV。
- 驻留凭据：最多 48 个有效凭据（含原型旧非驻留凭据），用户 ID 最多 64B，姓名/显示名称各保留最多 64B。支持无需 allowList 的账号发现、多账号 GetNextAssertion；未验证 PIN 时不返回姓名。
- ES256、none attestation、CTAPHID 64B、USB `1209:0001`（本地原型 VID/PID，公开源码不授予正式使用该标识的权利）。不支持 U2F、NFC、内置生物验证、CTAP2.1 权限 token、凭据管理或大型 Blob。

## 存储与升级

外置 Flash 只写最后 **64 KiB（0xF0000–0xFFFFF）**，其余 960 KiB 不改。两个 32 KiB bank，各有 56 条 512B 记录。每个有效凭据和 PIN 配置只保留最新记录；填满时复制到另一个 bank，回读校验后最后提交带 HMAC 的新库头。中途断电时保留旧 bank，新 bank 提交完成后才可选用。

签名仍会追加计数器记录，但自动回收历史记录，**不再在第 240 条记录后停用**。这不是无限 Flash 寿命承诺；仍有 48 个有效凭据的数量限制。相同 RP/user 的新驻留注册替换旧凭据；注册新网站不会静默删除其他网站的凭据。

兼容本项目旧 `MINIF2v1`，保留 credential ID、私钥、RP 与最新签名计数器。为避免覆盖尚未迁移的旧数据，只自动迁移已用记录数不超过 112、有效凭据数不超过 48 的旧库；超过时拒绝写入，不能用初始化掩盖迁移失败。当前测试板已完成旧库迁移，升级前创建的浏览器凭据在升级后登录成功，签名计数器继续增加。

外置私钥、RP、用户资料和 PIN 哈希使用 AES-256-CTR + HMAC-SHA256；主密钥编译进 MCU 内部 Flash。`.env` 只是**电脑端构建秘密的隔离**，不是板端安全芯片。原型没有经过硬件安全审计；回滚、防故障注入、真正硬件随机数、栈水位与 Flash 耐久性均未完成验证。应用代码与初始化数据 35,976B、静态数据约 7.3KiB，独立预留 6KiB 栈（编译器输出各函数栈用量；尚未进行实机水位测量）。

随机种子使用设备密钥与启动 ADC/计时噪声，不宣称硬件 TRNG。

## 密钥与构建

本机设备主密钥在 `.env` 的 `MINI_MASTER_KEY_HEX`，或同名进程环境变量；**普通更新必须保持原密钥**。构建时生成被 Git 忽略的 `private/provision.c`。如果已有 provision 与配置不同，构建拒绝继续。不要把设备 PIN 放入 `.env`；PIN 由用户在终端/系统提示中输入。

新设备可运行 `python provision.py --generate` 生成专属 `.env`；已有设备禁止运行它轮换密钥。丢失主密钥会导致凭据不可恢复，请离线备份 `.env`。主机测试使用另一把公开测试密钥，无需读取真实 `.env`。

PowerShell 7，在本项目目录：

```powershell
python -m venv .venv
./.venv/Scripts/python.exe -m pip install -r requirements-lock.txt
./.venv/Scripts/python.exe fetch-dependencies.py
./.venv/Scripts/python.exe setup-tools.py
./build.ps1 -HostTest
./.venv/Scripts/python.exe -m pytest tests -q
./build.ps1
```

编译工具均使用项目 `tools/`，不全局安装：xPack RISC-V GCC 15.2.0-1、Zig 0.15.2。上游源码版本见 `dependencies.json`，`fetch-dependencies.py` 检出精确提交。下载工具配置见 `toolchain.json`，安装辅助脚本见 `setup-tools.py`。烧录工具另从 ch32-rs/wchisp 官方 Release 获取 Windows x64 版本放入 tools/wchisp-win-x64/；本项目未提供会随 nightly 变化的自动下载。WCH SDK 与各密码库保留原版权/许可；说明见 `THIRD_PARTY.md`。

构建 HEX/BIN/ELF **包含设备主密钥，不可上传公开仓库或 Release**。`.env`、private/build/vendor/tools/archive、测试凭据和硬件日志已排除。源码中固定的 host 测试密钥不能刷作真实设备密钥。

## 刷写与验收

先启动 `wait-flash.py --execute` 并确认 ARMED，再在前台管理员 PowerShell 7 执行 `enter-isp.py --execute` 并触摸。等待器核对唯一设备、芯片类型并刷写/回读校验，停留在 ISP；确认成功后执行本地 `wchisp reset`。保留主密钥；**不要重复初始化、CTAP Reset 或修改保护选项**。

- `verify-device.py --info-only`：查看能力。
- `verify-pin-resident.py`：用户自行输入/设置 PIN，注册独立本地测试驻留凭据，再不提供 credential ID 发现并独立验签。不会擦除原凭据；再次运行替换相同测试账号。
- `web-test.py`：http://localhost:8765，保持旧非驻留浏览器测试凭据。
- `web-test.py --passkey`：http://localhost:8766，独立测试账号，要求驻留凭据和 UV，登录不提供 allowList；PIN 在 Windows 安全提示中输入。
- 先验证旧浏览器登录，再验证 passkey，普通拔插后再登录以验证持久化。

新能力实机成功前，`build/build-manifest.json` 保持 `verified_on_hardware: false`。已有旧版本实机记录保存在本地 build 下，不发布设备身份与凭据日志。

## 版本维护

本地 Git 根目录是 `firmware-mini/`，不上传原资料压缩包、其他型号资料或上位机二进制。重要改动经对应检查后提交 commit；未来推送须有发布/同步授权。请执行 `python audit-staged.py` 检查暂存区后再提交。硬件验收结果通过脱敏的 CHANGELOG 记录，原始日志留本地。
