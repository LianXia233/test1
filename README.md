# padavan-rewrite

> ⚠️ **警告：本项目当前仅为测试项目，可能不会落地。**
> 本仓库用于验证 Padavan（老毛子）在 ARM64 + 现代 Linux（ImmortalWrt/OpenWrt 6.x）上重写的技术可行性，代码处于原型阶段，未经生产环境验证，**不保证最终落地**，请勿用于生产环境。

在 ARM64 + 现代 Linux 上重建 Padavan（rt-n56u）配置体系、服务模型与管理界面，适配 MT7981B / MT7987A 平台。

## 项目状态

| 模块 | 阶段 | 说明 |
|---|---|---|
| M1/M2 libnvram | 已完成 | 配置层复刻，API 兼容 `nvram_linux.h`，26 用例全过 |
| M3 rc-ng | 骨架完成 | 声明式服务注册表 + notify 分发 + 依赖编排，32 项测试全过 |
| M4 luci-app | 首屏完成 | 管理界面层：系统状态 / 网络 / 无线三页，113 项测试全过 |

## 目录结构

```
padavan-rewrite/
├── docs/                  # 设计文档与适配报告
│   ├── 重写架构设计.md
│   ├── 进度说明与要求.md
│   └── AirPi_AP3000M_适配评估报告.html
├── libnvram/              # M1/M2 配置层（API 兼容 nvram_linux.h）
│   ├── include/           # 公共头文件
│   ├── src/               # nvram_core / nvram_uci / nvram_uci_cli
│   └── test/              # 测试用例 + fake_uci 桩
├── rc/                    # M3 服务层（rc-ng 骨架）
│   ├── include/           # padavan_service.h
│   ├── init.d/            # procd init 脚本
│   ├── src/               # service_registry / notify_bus / main
│   └── test/              # test_rc.c
└── luci-app-padavan/      # M4 管理界面层（LuCI 模块）
    ├── luasrc/
    │   ├── controller/    # 菜单与路由
    │   ├── model/cbi/     # 网络页 / 无线页
    │   ├── view/          # 系统状态页
    │   └── padavan/       # map / apply / status / wifi（纯 Lua，可单测）
    ├── root/              # 默认 UCI 配置 + rpcd ACL
    └── test/              # run.sh（Lua 单测）/ lint.sh（语法 + LF）
```

## 快速构建

```bash
# 构建 libnvram（file + uci_cli 后端，默认零依赖）
make -C libnvram
make -C libnvram test

# 追加 libuci 后端（需 OpenWrt SDK 环境）
make -C libnvram WITH_UCI=1

# 构建 rc-ng（依赖 libnvram）
make -C rc
make -C rc test

# luci-app-padavan 本地测试（需 Lua 5.1；luac 可选）
make -C luci-app-padavan test
make -C luci-app-padavan lint
```

## 设计要点

- **API 兼容优先**：对外保持 `nvram_linux.h` 13 个 API 签名不变，老毛子应用改动最小
- **配置可迁移**：老毛子 nvram 变量表可一键导入导出 UCI
- **标准基座**：内核/BSP/无线/加速全部走 ImmortalWrt 主线
- **渐进式重写**：配置层 → 服务层 → 界面层 → 应用包，逐层替换验证
- **界面与后端同源**：LuCI 页面写 UCI 的落点与 libnvram 的 `section_key → padavan.section.key`
  命名映射共用同一份规则；无线页编辑 `padavan.rt/wl` 后同步到原生 `wireless`，保证
  `nvram get wl_ssid` 等老毛子语义不变

详见 [docs/重写架构设计.md](docs/重写架构设计.md)。

## 更新日志

见 [CHANGELOG.md](CHANGELOG.md)。
