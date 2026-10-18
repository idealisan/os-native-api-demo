/*
 * 例 1 —— gnu-efi 版 Hello World
 *
 * 工具链流水线（见本目录 Makefile）：
 *
 *   hello.c
 *     |  x86_64-linux-gnu-gcc -fPIC -ffreestanding -fshort-wchar ... -c
 *     v
 *   hello.o                      (ELF relocatable object)
 *     |  ld -shared -Bsymbolic -T elf_x86_64_efi.lds + crt0-efi-x86_64.o
 *     v
 *   hello.so                     (ELF PIE，链入了 gnu-efi 的自重定位器)
 *     |  objcopy -O efi-app-x86_64
 *     v
 *   hello.efi                    (PE32+，Subsystem=10 EFI_APPLICATION)
 *
 * 运行时序：
 *
 *   固件把 hello.efi 当作 PE32+ 加载 -> crt0 的 _start
 *     -> _relocate()   应用 R_X86_64_RELATIVE 重定位
 *     -> _entry()       把 SystemTable 存进全局 gST
 *     -> efi_main()
 *
 * 关于 ABI：x86_64 上 UEFI 固件使用 MS x64 ABI，而 gcc 默认是 SysV ABI。
 * 两者在「第 5 个及以后的参数」和「栈阴影空间」的处理上不同，
 * gnu-efi 用 -maccumulate-outgoing-args 对齐这一点，
 * 并提供 uefi_call_wrapper() 来安全地转发调用。
 */

#include <efi.h>
#include <efilib.h>

#define TAG  L"[gnu-efi] "

EFI_STATUS
efi_main (EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table)
{
	SIMPLE_TEXT_OUTPUT_INTERFACE *conout;

	/* gnu-efi 的 crt0 -> _entry() 其实已经调过一次 InitializeLib；
	 * 官方示例也照样在这里再调一次（它是幂等的）。写出来是为了让
	 * 「gST 是从哪来的」这件事变得显眼。 */
	InitializeLib (image_handle, system_table);
	conout = system_table->ConOut;

	/* 1) 直接调用固件协议接口：最原始的 UEFI 调用方式。
	 *    这里 EFIAPI 已经是 __attribute__((ms_abi))，所以 2 个参数
	 *    分别落在 rcx/rdx，和固件导出的函数对得上。 */
	uefi_call_wrapper (conout->OutputString, 2, conout, TAG L"Hello World!\r\n");

	/* 2) 用 gnu-efi 的便利库打印，顺便展示格式化输出。
	 *    注意 gnu-efi 的 Print 不是 printf：%x/%d 取的是 32 位参数，
	 *    %lx/%lu 才取 64 位。所以这里都显式转成 UINT64 再传。 */
	Print (TAG L"SystemTable revision 0x%lx, %lu configuration tables\r\n",
	       (UINT64) system_table->Hdr.Revision,
	       (UINT64) system_table->NumberOfTableEntries);

	/* 3) 返回 EFI_SUCCESS 即退出，固件随后卸载这个 image。
	 * 想关机/重启就改成：
	 *   uefi_call_wrapper (ST->ResetSystem, 4,
	 *                       EfiResetShutdown, EFI_SUCCESS, 0, NULL);
	 */
	return EFI_SUCCESS;
}