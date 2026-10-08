# luci-app-padavan - Padavan 管理界面层（M4 骨架）
#
# 该文件同时承担两个角色：
#   1) OpenWrt / ImmortalWrt 包定义（由 buildroot 通过 TOPDIR 调用）
#   2) 本地开发/测试入口（未设置 TOPDIR 时仅暴露 test 目标）
#
# 本地用法：
#   make -C luci-app-padavan test

ifeq ($(TOPDIR),)
# ---------- 非 OpenWrt 环境：仅本地测试 ----------
.PHONY: test lint clean

test:
	./test/run.sh

lint:
	./test/lint.sh

clean:
	rm -rf /tmp/padavan-luci-test

else
# ---------- OpenWrt / ImmortalWrt 包定义 ----------
include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-padavan
PKG_VERSION:=0.1.0
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0
PKG_MAINTAINER:=padavan-rewrite (原型项目)

LUCI_TITLE:=LuCI support for Padavan (rt-n56u) rewrite
LUCI_DEPENDS:=+libnvram +luci-base
LUCI_PKGARCH:=all

include $(TOPDIR)/feeds/luci/luci.mk

# call BuildPackage - OpenWrt buildroot signature
endif
