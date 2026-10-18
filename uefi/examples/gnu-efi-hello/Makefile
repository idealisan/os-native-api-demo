# 例 1 —— gnu-efi 版 Hello World
#
# 这份 Makefile 只是把 gnu-efi 自己 apps/Makefile 里的编译/链接/转换步骤
# 显式写出来，方便看清楚 "ELF -> PE32+" 这条流水线。
# 由 uefi/Makefile 顶层驱动，也可以单独在本目录执行：
#
#   make GNU_EFI_DIR=/path/to/gnu-efi
#
# GNU_EFI_DIR 指向已经 `make` 构建过的 gnu-efi 源码树（需要里面存在
# <ARCH>/lib/libefi.a、<ARCH>/gnuefi/libgnuefi.a 等产物）。

ARCH          ?= x86_64
GNU_EFI_DIR   ?= ../../build/gnu-efi

TOPDIR        := $(GNU_EFI_DIR)
OBJDIR        := $(TOPDIR)/$(ARCH)

CC            := $(CROSS_COMPILE)gcc
LD            := $(CROSS_COMPILE)ld
OBJCOPY       := $(CROSS_COMPILE)objcopy
OBJDUMP       := $(CROSS_COMPILE)objdump

SRCDIR        := .
TARGET        := hello.efi

OBJS          := hello.o

# 与 gnu-efi/Make.defaults + apps/Makefile 保持一致。
#
#   -DGNU_EFI_USE_MS_ABI : **必须和编译 libefi.a 时用的一致**，否则 ABI 对不上。
#                          定义它之后 x86_64 的 EFIAPI 变成 __attribute__((ms_abi))，
#                          uefi_call_wrapper() 直接展开成普通调用；同时
#                          lib/x86_64/efi_stub.S 里那套 efi_callN 汇编跳板
#                          根本不会被编进 libefi.a。少定义或多定义都会链崩。
#   -fshort-wchar           : UEFI 的 CHAR16 是 16 位，wchar_t 也得是 16 位
#   -ffreestanding          : 没有宿主 libc
#   -fno-stack-protector    : 固件环境不需要栈 canary
#   -mno-red-zone           : 中断/异常进入时没有红区可用
#   -maccumulate-outgoing-args : 对齐 MS x64 ABI 的栈参数规则
#   -fPIC                   : PE 镜像必须可在任意地址加载
#   -fno-merge-all-constants : 同 gnu-efi，避免常量合并影响 efi_callN 分派
#   -DCONFIG_$(ARCH)        : gnu-efi 头文件用它选择 x86_64 专用定义
EFI_CFLAGS := \
	-I$(SRCDIR) \
	-I$(TOPDIR)/inc \
	-I$(TOPDIR)/inc/$(ARCH) \
	-I$(TOPDIR)/inc/protocol \
	-DCONFIG_$(ARCH) \
	-DGNU_EFI_USE_MS_ABI \
	-std=c11 \
	-O2 -g \
	-fPIC \
	-ffreestanding \
	-fno-stack-protector \
	-fno-strict-aliasing \
	-fno-merge-all-constants \
	-fshort-wchar \
	-mno-red-zone \
	-maccumulate-outgoing-args \
	-mno-avx \
	-Wall -Wextra -Wstrict-prototypes

# 链接成 PIE/so，链接脚本负责产出 EFI 需要的 section 布局与 .reloc。
#   crt0-efi-x86_64.o 必须放在最前面（它提供 PE 入口 _start）
#   -Bsymbolic : 内部符号不出现在动态符号表里，减少重定位项
LDSCRIPT  := $(TOPDIR)/gnuefi/elf_$(ARCH)_efi.lds
CRT0      := $(OBJDIR)/gnuefi/crt0-efi-$(ARCH).o
LIBS      := -L$(OBJDIR)/lib -L$(OBJDIR)/gnuefi -lgnuefi -lefi

LDFLAGS := \
	-nostdlib \
	-shared \
	-Bsymbolic \
	-z nocombreloc \
	-z norelro \
	-T $(LDSCRIPT) \
	-L$(OBJDIR)/lib \
	-L$(OBJDIR)/gnuefi \
	$(CRT0) \
	$(LIBS)

# 只保留 PE32+ 需要的 section，--output-target efi-app-x86_64 让 objcopy
# 生成 Subsystem=10 (EFI_APPLICATION) 的 PE32+ 头。
OBJCOPY_SECTIONS := \
	-j .text -j .sdata -j .data -j .dynamic -j .rodata \
	-j .rel -j .rela -j .rel.* -j .rela.* -j .rel* -j .rela* \
	-j .areloc -j .reloc

# 别让 make 把中间产物（*.o / *.so）当临时文件删掉
.SECONDARY:

.PHONY: all clean check

all: $(TARGET)

$(TARGET): hello.so
	@echo "  OBJCOPY  $@"
	@$(OBJCOPY) $(OBJCOPY_SECTIONS) \
		-O efi-app-$(ARCH) \
		$< $@

%.o: %.c
	@echo "  CC       $@"
	@$(CC) $(EFI_CFLAGS) -c $< -o $@

%.so: $(OBJS)
	@echo "  LD       $@"
	@$(LD) $(LDFLAGS) $^ -o $@ \
		$$($(CC) -print-libgcc-file-name)

# 简单校验产物确实是一个 PE32+ EFI application
check: $(TARGET)
	@$(OBJDUMP) -f $(TARGET) | sed -n '1,20p'

clean:
	@rm -fv $(OBJS) hello.so $(TARGET)