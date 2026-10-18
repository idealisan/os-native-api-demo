/*
 * crt0.S —— 极简 EFI 入口桩（x86_64 / PE32+）
 *
 * 固件加载 PE32+ 之后，唯一被调用的就是镜像入口点。UEFI 规范对 x86_64
 * 入口的要求：
 *
 *   1) 必须返回 EFI_STATUS（结果放在 rax）；
 *   2) 除 rax 外的所有寄存器都要保持原值；
 *   3) 栈指针必须还原；
 *   4) 方向标志 DF 必须为 0；
 *   5) 不允许指望 rbx/rbp/rsi/rdi/r12-r15 已经被别人保存好了。
 *
 * 这里刻意没有像 gnu-efi 的 crt0 那样把 ImageHandle / SystemTable 存进
 * 全局变量 —— 那会立刻产生一个需要重定位的绝对地址引用，而在重定位发生
 * 之前读写它是未定义的。
 *
 * 做法是把它们留在寄存器里（寄存器永远不需要重定位），先调一个 C 函数完成
 * 自重定位，再由那个 C 函数把参数原样转发给 efi_main。
 *
 * 入口参数：
 *   rcx = ImageHandle
 *   rdx = EFI_SYSTEM_TABLE*
 *
 * 栈对齐：call 之前 rsp 必须是 16 字节对齐，这样 callee 入口处
 * rsp ≡ 8 (mod 16)，同时满足 SysV x64 和 MS x64 两种 ABI 的要求。
 *
 * 注意「要求 3：栈指针必须还原」。`and $-16, %rsp` 会把 rsp 往下取整，
 * 丢掉固件给的 0~15 字节；返回前必须把原值塞回去，否则调用者（这里是
 * UEFI Shell）会拿着一个错位的栈继续跑，实测会直接跳进数据段里执行 ->
 * #UD Invalid Opcode。r11 是易失寄存器，调用者按 ABI 不能指望它被保留，
 * 所以正好拿来存原始 rsp。
 */

#if defined(__x86_64__)

	.text

	.globl	efi_entry
	.align	16
efi_entry:
	cld				/* 要求 4：方向标志必须清零 */
	movq	%rsp, %r11		/* 记住固件给的栈指针，返回前要还回去 */
	andq	$-16, %rsp		/* 固件不保证入口时栈已 16 字节对齐 */
	subq	$0x20, %rsp		/* 32 字节 home space，仍然 16 字节对齐 */
	call	efi_boot			/* rcx=ImageHandle, rdx=SystemTable */
	movq	%r11, %rsp		/* 要求 3：栈指针必须还原 */
	ret

#endif /* __x86_64__ */

/* 没有可执行栈，别让链接器留下警告 */
#if defined(__linux__) && defined(__ELF__)
	.section	.note.GNU-stack,"",%progbits
#endif