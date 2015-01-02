#
# Original Source Copywrite BarDweller https://github.com/BarDweller/HD-PiVR
# Modifications and Packaging ian@wheep.co.uk
#

include $(TOPDIR)/rules.mk

PKG_NAME:=hdpvrd
PKG_RELEASE:=1
PKG_BUILD_DIR := $(BUILD_DIR)/$(PKG_NAME)

include $(INCLUDE_DIR)/package.mk

define Package/hdpvrd
	SECTION:=multimedia
	CATEGORY:=Multimedia
	TITLE:=HD-PVR Video Streamer Daemon
	DEPENDS:=+libpthread
endef

define Package/hdpvrd/description
	Stream video from the Hauppauge HD-PVR
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Package/hdpvrd/install
	$(INSTALL_DIR) $(1)/bin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/hdpvrd $(1)/bin/hdpvrd
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/hdpvrd.init $(1)/etc/init.d/hdpvrd

endef
$(eval $(call BuildPackage,hdpvrd))
