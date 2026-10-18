# 例 2 —— 极简 freestanding 版 Hello World
#
# 直接让 mingw-w64 的 ld 输出 PE32+，不经过 ELF -> PE 的 objcopy 转换，
# 所以也不需要 gnu-efi，只需要一个 x86_64-w64-mingw32 交叉工具链。
#
# 链接参数说明：
#   -nostdlib            不链接任何 CRT/库
#   -Wl,--subsystem,10   PE Subsystem = 10 = EFI_APPLICATION
#                        （11 = EFI_BOOT_SERVICE_DRIVER，12 = EFI_RUNTIME_DRIVER）
#   -Wl,-dll             产出带 .reloc 的 PE32+，并设置 IMAGE_FILE_DLL 特性
#   -Wl,-e,efi_entry     入口点，就是 crt0.S 里的 efi_entry
#   --section-alignment  UEFI 要求 >= 4KiB 页面大小
#
# 由 uefi/Makefile 顶层驱动，也可以单独在本目录执行。

CROSS   ?= x86_64-w64-mingw32-
CC      := $(CROSS)gcc
OBJDUMP := $(CROSS)objdump

SRCDIR  := .
TARGET  := hello.efi

OBJS    := crt0.o pe_reloc.o hello.o

EFI_CFLAGS := \
	-I$(SRCDIR) \
	-std=c11 \
	-O2 -g \
	-ffreestanding \
	-fno-builtin \
	-fno-stack-protector \
	-fno-strict-aliasing \
	-fno-asynchronous-unwind-tables \
	-fno-unwind-tables \
	-fshort-wchar \
	-mno-red-zone \
	-maccumulate-outgoing-args \
	-Wall -Wextra -Wstrict-prototypes

LDFLAGS := \
	-nostdlib \
	-Wl,--subsystem,10 \
	-Wl,-dll \
	-Wl,-e,efi_entry \
	-Wl,--image-base,0x100000 \
	-Wl,--section-alignment,0x1000 \
	-Wl,--file-alignment,0x200 \
	-Wl,--no-insert-timestamp

# 别让 make 把中间产物（*.o / *.so）当临时文件删掉
.SECONDARY:

.PHONY: all clean check dump

all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "  LD       $@"
	@$(CC) $(LDFLAGS) $(OBJS) -o $@

%.o: %.c
	@echo "  CC       $@"
	@$(CC) $(EFI_CFLAGS) -c $< -o $@

%.o: %.S
	@echo "  AS       $@"
	@$(CC) $(EFI_CFLAGS) -c $< -o $@

# 确认产物是 PE32+ / EFI application / 带 .reloc
check: $(TARGET)
	@$(OBJDUMP) -p $(TARGET) | sed -n '/Magic/,/Base Relocation/p'

dump: $(TARGET)
	@$(OBJDUMP) -d -j .text $(TARGET)

clean:
	@rm -fv $(OBJS) $(TARGET)