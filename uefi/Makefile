# UEFI 开发实验区 —— 顶层 Makefile
#
#   make                  构建两个 hello world
#   make gnu-efi-lib      只下载并构建 gnu-efi 依赖库
#   make qemu             在 QEMU + OVMF 里实际跑一遍并检查输出
#   make check            只检查产物是不是合法的 PE32+ EFI application
#   make clean / distclean
#
# 环境变量：
#   GNU_EFI_REV   gnu-efi 的 tag，默认 4.0.4
#   QEMU_TIMEOUT  QEMU 测试超时，默认 60 秒
#
# 本地跑需要：gcc、binutils（带 efi-app-x86_64 的 objcopy）、
#             x86_64-w64-mingw32-gcc、qemu-system-x86_64 + OVMF。
# 只有 CI 里才需要全部，本地可以只构建其中一种。

SHELL       := /bin/bash

GNU_EFI_REV ?= 4.0.4
GNU_EFI_URL ?= https://github.com/ncroxon/gnu-efi.git

BUILD_DIR   := build
GNU_EFI_DIR := $(BUILD_DIR)/gnu-efi
FAT_DIR     := $(BUILD_DIR)/fat

APPS := \
	examples/gnu-efi-hello/hello.efi \
	examples/freestanding-hello/hello.efi

.PHONY: all gnu-efi-lib gnu-efi-app freestanding-app check qemu clean distclean help

all: $(APPS)

help:
	@sed -n '2,10p' Makefile

# --------------------------------------------------------------- gnu-efi

$(GNU_EFI_DIR)/.built: | $(GNU_EFI_DIR)
	@echo "  MAKE     gnu-efi $(GNU_EFI_REV)"
	@$(MAKE) -C $(GNU_EFI_DIR) ARCH=x64 gnuefi
	@touch $@

$(GNU_EFI_DIR):
	@echo "  GIT      clone gnu-efi $(GNU_EFI_REV)"
	@git clone --depth 1 --branch $(GNU_EFI_REV) $(GNU_EFI_URL) $@

gnu-efi-lib: $(GNU_EFI_DIR)/.built

gnu-efi-app: examples/gnu-efi-hello/hello.efi

examples/gnu-efi-hello/hello.efi: $(GNU_EFI_DIR)/.built
	@$(MAKE) -C examples/gnu-efi-hello GNU_EFI_DIR=$(CURDIR)/$(GNU_EFI_DIR)

# ------------------------------------------------------- freestanding

freestanding-app: examples/freestanding-hello/hello.efi

examples/freestanding-hello/hello.efi:
	@$(MAKE) -C examples/freestanding-hello

# ------------------------------------------------------------- 检查

# 直接按 PE/COFF 的字节偏移读 PE 头，不依赖 objdump 的输出排版
check: all
	@./scripts/check-efi.py $(APPS)

# ------------------------------------------------------------- QEMU

qemu: all
	@./scripts/run-qemu.sh

# ------------------------------------------------------------- 清理

clean:
	@$(MAKE) -C examples/gnu-efi-hello clean 2>/dev/null || true
	@$(MAKE) -C examples/freestanding-hello clean 2>/dev/null || true
	@rm -rf $(FAT_DIR)

distclean: clean
	@rm -rf $(BUILD_DIR)