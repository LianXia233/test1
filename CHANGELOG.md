---
AIGC:
    Label: "1"
    ContentProducer: 001191440300708461136T1XGW3
    ProduceID: 9768052f19c9c9070fcb40014c0e726f_cae75c5dc2d911f1bc7f525400638852
    ReservedCode1: drbmfY/CzP6vpfrKi6h9+RBlUHHhMrIOuQ9Lg1pFm95pYsK7g8UGsmEcaKnNllA0jPez2/LiioIXUREa7ZsgmEWYNMcePn+UlBzKHtRBHQQiIitfv599S+1kyntGgnknL+9IlzZDnAVUxFLKbhEdRqwzUV4DOOArxOO0b3/6j2xfPtSFoBKXZp1Wc3s=
    ContentPropagator: 001191440300708461136T1XGW3
    PropagateID: 9768052f19c9c9070fcb40014c0e726f_cae75c5dc2d911f1bc7f525400638852
    ReservedCode2: drbmfY/CzP6vpfrKi6h9+RBlUHHhMrIOuQ9Lg1pFm95pYsK7g8UGsmEcaKnNllA0jPez2/LiioIXUREa7ZsgmEWYNMcePn+UlBzKHtRBHQQiIitfv599S+1kyntGgnknL+9IlzZDnAVUxFLKbhEdRqwzUV4DOOArxOO0b3/6j2xfPtSFoBKXZp1Wc3s=
---

# CHANGELOG

> ⚠️ **警告：本项目当前仅为测试项目，可能不会落地。**
> 以下变更记录仅反映原型开发过程，不代表产品承诺。

## [v0.1.0] - 2026-10-08

### 新增
- **M1/M2 libnvram 配置层**：完成 Padavan nvram API 复刻
  - 双后端：纯文件 KV（零依赖）+ uci_cli / libuci（OpenWrt 原生）
  - API 兼容 `nvram_linux.h` 13 个接口签名
  - `nvram_set_config_file()` 设置配置路径；`nvram_safe_get_int()` 读开关
  - 内部 helper 加 `nv_` 前缀防符号冲突
  - 26 个测试用例全部通过
- **M3 rc-ng 服务层骨架**：声明式服务注册表 + notify 分发 + 依赖编排 + 级联
  - 服务：wan / firewall / dnsmasq / upnp / pptp
  - nvram 开关门控（如 `wan_enable`），禁用时跳过本体但级联照常
  - 32 项测试全部通过
- **文档**：重写架构设计（v0.1 原型阶段）、AirPi AP3000M 适配评估报告
- **本仓库初始化**：代码推送到 GitHub 归档（测试仓库，可能不落地）

### 变更
- 无

### 修复
- 无

### 已知限制
- 原型阶段，未经生产环境验证
- libuci 后端需 OpenWrt SDK 环境编译
- M4 LuCI 管理界面层尚未启动
*（内容由AI生成，仅供参考）*
