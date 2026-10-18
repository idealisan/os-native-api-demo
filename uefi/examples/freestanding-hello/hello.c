/*
 * 例 2 —— 极简 freestanding 版 Hello World
 *
 * 这个版本不链接 gnu-efi，也不链接 libc，只有三个 .o：
 *   crt0.S      入口桩（几条指令）
 *   pe_reloc.c  PE32+ 自重定位
 *   hello.c     本文件
 *
 * 编译出来的就是一个最小的 PE32+ EFI application：
 *   Machine=0x8664 / Subsystem=10 (EFI_APPLICATION) / 带 .reloc 段
 *
 * 好处是可以看清固件到底给了我们什么：两个寄存器（ImageHandle、
 * SystemTable），加上一个可以任意调用的 BootServices 函数表。
 */

#include "efi_min.h"

#define TAG  L"[freestanding] "

/*
 * 下面这两个全局变量是**故意**放在 .data 的。
 *
 * 数据段里的指针必须写成「绝对地址」，所以链接器一定会为它们生成一条
 * IMAGE_REL_BASED_DIR64 重定位记录。也就是说：只要固件把我们加载到
 * --image-base 之外的任何地址，greeting_ptr 在重定位之前就是一个死地址，
 * 重定位之后才指向真正的 greeting[]。
 *
 * 想亲眼看看重定位有多重要：把 pe_relocate() 的调用注释掉再跑一次，
 * 输出就会变成乱码或者直接崩。
 */
static CHAR16  greeting[]    = TAG L"Hello World!\r\n";
static CHAR16 *greeting_ptr  = greeting;

/* efi_boot 填进来，efi_main 打印，用来证明 delta 确实算出来了 */
static UINT64  g_reloc_delta;

/*
 * 由 crt0.S 调用。此时镜像尚未重定位，所以能做的操作非常有限：
 * 只能用「寄存器里传进来的指针」和「从这些指针一层层读出来的指针」。
 * EFI_LOADED_IMAGE_PROTOCOL.ImageBase 就是我们唯一的救命稻草。
 */
EFI_STATUS
efi_boot (VOID *image_handle, EFI_SYSTEM_TABLE *system_table)
{
	EFI_LOADED_IMAGE_PROTOCOL *loaded_image = 0;
	EFI_GUID     loaded_image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
	EFI_STATUS   status;

	if (system_table == 0)
		return EFI_INVALID_PARAMETER;

	system_table->BootServices->HandleProtocol ((EFI_HANDLE) image_handle,
						    &loaded_image_guid,
						    (VOID **) &loaded_image);

	status = pe_relocate (loaded_image->ImageBase, &g_reloc_delta);
	if (EFI_ERROR (status)) {
		system_table->ConOut->OutputString (system_table->ConOut,
						    TAG L"self-relocation FAILED\r\n");
		return status;
	}

	return efi_main (image_handle, system_table);
}

/* ------------------------------------------------------------------ */

/* 没有 libc 也没有 snprintf，手写一个够用的 16 进制输出 */
static
VOID
print_hex (EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *conout, UINT64 value, int digits)
{
	static const CHAR16 hexdigits[] = L"0123456789abcdef";
	CHAR16   buf[16 + 3];
	int      i;

	buf[0]  = L'0';
	buf[1]  = L'x';
	buf[2 + digits] = 0;
	for (i = 0; i < digits; i++)
		buf[2 + i] = hexdigits[(value >> ((digits - 1 - i) * 4)) & 0xf];

	conout->OutputString (conout, buf);
}

EFI_STATUS
efi_main (VOID *image_handle, EFI_SYSTEM_TABLE *system_table)
{
	EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *conout;

	(void) image_handle;
	conout = system_table->ConOut;

	/* 1) 最原始的 UEFI 调用：拿到 ConOut，调 OutputString。
	 *    注意 greeting_ptr 的值是在 pe_relocate() 里被修好的。 */
	conout->OutputString (conout, greeting_ptr);

	/* 2) 顺便读点 SystemTable 里的东西，验证手写的结构体偏移算对了 */
	conout->OutputString (conout, TAG L"SystemTable revision ");
	print_hex (conout, system_table->Hdr.Revision, 8);
	conout->OutputString (conout, L", BootServices @ ");
	print_hex (conout, (UINT64) (UINTN) system_table->BootServices, 16);
	conout->OutputString (conout, L"\r\n");

	/* 3) 本次启动实际施加的重定位量 */
	conout->OutputString (conout, TAG L"relocation delta ");
	print_hex (conout, g_reloc_delta, 16);
	conout->OutputString (conout, L"\r\n");

	return EFI_SUCCESS;
}