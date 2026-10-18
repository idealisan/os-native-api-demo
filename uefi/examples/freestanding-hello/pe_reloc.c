/*
 * pe_reloc.c —— 自己给自己做 PE 基址重定位
 *
 * 为什么固件不替我们做？
 *   Windows 的 PE 加载器会调用 NtProcessWowk64 / RtlAddFunctionTable 之类
 *   的东西处理重定位，但 UEFI 的 PE/COFF 加载器（EDK2 的 PeCoffLoader）
 *   只在 ImageType != EfiImageTypeDynamic 时才重定位，行为依赖固件实现。
 *   gnu-efi 和 mingw-w64 都选择自己实现一份，就是下面这段逻辑。
 *
 * 流程：
 *   1. 通过 HandleProtocol 拿到 EFI_LOADED_IMAGE_PROTOCOL.ImageBase
 *      —— 这是固件实际加载的基址；
 *   2. 读 PE 可选头的 ImageBase —— 这是链接期假定的基址；
 *   3. delta = 实际 - 假定；
 *   4. 遍历 DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC] 里的重定位块，
 *      对每一条 HIGHLOW / DIR64 / DIR64NB / HIGHADJ 条目加上 delta。
 *
 * 第 1 步在「还没重定位」的状态下执行是安全的：SystemTable 是入口参数
 * （在寄存器里），BootServices 也只是一层指针，没有任何需要被修正的绝对地址。
 */

#include "efi_min.h"

/* ------------------------------------------------------- PE32+ 头部定义 */

#define DOS_MAGIC        0x5a4d		/* 'MZ' */
#define NT_MAGIC         0x00004550	/* 'PE\0\0' */
#define NT_OPTIONAL_HDR_MAGIC_PE32PLUS 0x020b

#define IMAGE_DIRECTORY_ENTRY_BASERELOC 5

/* DataDirectory 在 PE32+ 可选头里的偏移 */
#define PE32P_NUMBEROFRVAANDSIZES_OFFSET  108
#define PE32P_DATADIRECTORY_OFFSET         112

typedef struct {
	UINT16 e_magic;
	UINT16 e_cblp;
	UINT16 e_cp;
	UINT16 e_crlc;
	UINT16 e_cparhdr;
	UINT16 e_minalloc;
	UINT16 e_maxalloc;
	UINT16 e_ss;
	UINT16 e_sp;
	UINT16 e_csum;
	UINT16 e_ip;
	UINT16 e_cs;
	UINT16 e_lfarlc;
	UINT16 e_ovno;
	UINT16 e_res[4];
	UINT32 e_oemid;
	UINT32 e_oeminfo;
	UINT16 e_res2[10];
	INT32  e_lfanew;		/* PE 头相对本头的偏移 */
} DOS_HEADER;

typedef struct {
	UINT16 VirtualAddress;		/* 页 RVA */
	UINT16 SizeOfBlock;		/* 整个块的大小，含这个 4 字节头 */
} BASE_RELOCATION;

typedef struct {
	UINT16  Type;			/* 低 12 位是类型，高 4 位是标志 */
	UINT16  Offset;			/* 相对 VirtualAddress 的偏移 */
} BASE_RELOCATION_ENTRY;

/* ------------------------------------------------------------- 重定位器 */

EFI_STATUS
pe_relocate (VOID *image_base, UINT64 *out_delta)
{
	UINT8  *base = (UINT8 *) image_base;
	DOS_HEADER *dos;
	UINT8  *nt;
	UINT16  magic;
	UINT8  *opt;
	UINT8  *dd;
	UINT64  preferred;
	UINT64  delta = 0;
	UINT32  n_dirs;
	UINT32  dir_rva;
	UINT32  dir_size;
	UINT8  *blk;
	UINT8  *end;

	if (out_delta)
		*out_delta = 0;

	if (base == 0)
		return EFI_INVALID_PARAMETER;

	dos = (DOS_HEADER *) base;
	if (dos->e_magic != DOS_MAGIC)
		return EFI_LOAD_ERROR;

	nt = base + dos->e_lfanew;
	if (*(UINT32 *) nt != NT_MAGIC)
		return EFI_LOAD_ERROR;

	opt = nt + 4 + 20;			/* PE 签名 4 字节 + COFF 头 20 字节 */
	magic = *(UINT16 *) opt;
	if (magic != NT_OPTIONAL_HDR_MAGIC_PE32PLUS)
		return EFI_LOAD_ERROR;		/* 本例只支持 PE32+ (x86_64) */

	preferred = *(UINT64 *) (opt + 24);	/* OptionalHeader.ImageBase */

	delta = (UINT64) base - preferred;
	if (out_delta)
		*out_delta = delta;

	if (delta == 0)
		return EFI_SUCCESS;		/* 刚好加载在首选地址 */

	n_dirs  = *(UINT32 *) (opt + PE32P_NUMBEROFRVAANDSIZES_OFFSET);
	if (n_dirs <= IMAGE_DIRECTORY_ENTRY_BASERELOC)
		return EFI_SUCCESS;		/* 没有重定位目录 -> 不可重定位 */

	dd = opt + PE32P_DATADIRECTORY_OFFSET;
	dir_rva  = *(UINT32 *) (dd + IMAGE_DIRECTORY_ENTRY_BASERELOC * 8);
	dir_size = *(UINT32 *) (dd + IMAGE_DIRECTORY_ENTRY_BASERELOC * 8 + 4);

	/*
	 * 没有 .reloc 目录有两种可能，都不是错误：
	 *   1) 链接时根本没产生任何绝对地址引用（镜像确实不可重定位，
	 *      固件只能把它加载到首选地址）；
	 *   2) 加载时 delta 恰好为 0（上面已经 return 掉了）。
	 * 两种情况下「什么都不用改」就是正确答案。
	 */
	if (dir_rva == 0 || dir_size == 0)
		return EFI_SUCCESS;

	blk = base + dir_rva;
	end = blk + dir_size;

	while (blk + (INT32) sizeof (BASE_RELOCATION) <= end) {
		BASE_RELOCATION  *blk_hdr = (BASE_RELOCATION *) blk;
		UINT32  n_entries;
		UINT32  i;
		UINT8  *p;

		if (blk_hdr->VirtualAddress == 0 &&
		    blk_hdr->SizeOfBlock == 0)
			break;
		if (blk + blk_hdr->SizeOfBlock > end)
			break;

		n_entries = (blk_hdr->SizeOfBlock - sizeof (BASE_RELOCATION))
			    / sizeof (BASE_RELOCATION_ENTRY);

		p = blk + sizeof (BASE_RELOCATION);
		for (i = 0; i < n_entries; i++, p += sizeof (BASE_RELOCATION_ENTRY)) {
			BASE_RELOCATION_ENTRY *e = (BASE_RELOCATION_ENTRY *) p;
			UINT32  type = e->Type >> 12;
			UINT8  *site = base + blk_hdr->VirtualAddress + e->Offset;

			switch (type) {
			case 0:		/* IMAGE_REL_BASED_ABSOLUTE : 纯填充 */
				break;
			case 1:		/* IMAGE_REL_BASED_HIGH    */
				*(UINT16 *) site += (UINT16) delta;
				break;
			case 2:		/* IMAGE_REL_BASED_LOW     */
				*(UINT16 *) site += (UINT16) delta;
				break;
			case 3:		/* IMAGE_REL_BASED_HIGHLOW */
				*(UINT32 *) site += (UINT32) delta;
				break;
			case 4:		/* IMAGE_REL_BASED_HIGHADJ */
				/*
				 * 罕见：下一个条目的 16 位是调整量。
				 * GNU ld / mingw 的 EFI 产物不会出现，
				 * 遇到就直接跳过以免破坏镜像。
				 */
				i++;
				break;
			case 10:	/* IMAGE_REL_BASED_DIR64   */
				*(UINT64 *) site += delta;
				break;
			default:
				/* 未知的基址重定位类型，忽略 */
				break;
			}
		}

		blk += blk_hdr->SizeOfBlock;
	}

	return EFI_SUCCESS;
}