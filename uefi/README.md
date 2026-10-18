# UEFI 开发实验区

用两条完全不同的路线，各写一个 Hello World，看清 UEFI 应用的最小形态：

| 目录 | 路线 | 依赖 | 你能看到什么 |
| --- | --- | --- | --- |
| [`examples/gnu-efi-hello/`](examples/gnu-efi-hello) | **gnu-efi**（GNU EFI 工具链）：先用宿主 gcc 编成 ELF，再 `objcopy` 转成 PE32+ | gnu-efi + binutils | 最常见、最省事；库、字符串、格式化输出都有 |
| [`examples/freestanding-hello/`](examples/freestanding-hello) | **freestanding**：直接让 mingw-w64 的 ld 输出 PE32+，自己写入口桩和 PE 自重定位 | 只要 mingw-w64 | 三个 `.o`、几百行代码，固件到应用之间的每一步都在眼前 |

两者的 `.efi` 都会被 CI 丢进 QEMU + OVMF 里真机跑一遍（servelog 里必须看到那句 Hello World）。

> 这里的 “gcc-efi” 指的是 **gnu-efi**（<https://github.com/ncroxon/gnu-efi>），
> 也就是用 GNU 工具链编译 EFI 应用的那套头文件 + 库 + crt0。
> 如果你想说的是 rickh 那个同名项目 `gcc-efi`（clang/LLVM 版，产物一样但构建脚本不同），说一声我再调。

---

## 一分钟上手

```bash
cd uefi
make              # 构建两个 hello.efi
make check        # 静态检查产物是不是 PE32+ / Subsystem=10 / 带 .reloc
make qemu         # 在 QEMU + OVMF 里实际执行并断言输出
```

没有本地工具链也行，仓库根目录的 GitHub Actions 里已经配好了：

```
Actions → UEFI hello world → Run workflow
```

跑完去 Artifacts 下载 `efi-apps`（两个 `.efi` + 串口日志）。

### 本地依赖

| 需要什么 | 装它 |
| --- | --- |
| gnu-efi 示例 | `gcc`、`make`、`binutils`（`objcopy --help` 里要能看到 `efi-app-x86_64`） |
| freestanding 示例 | `x86_64-w64-mingw32-gcc`（Debian/Ubuntu: `apt install gcc-mingw-w64-x86-64`） |
| `make qemu` | `qemu-system-x86_64` + OVMF 固件（Ubuntu: `apt install qemu-system-x86 ovmf`） |

> **在 macOS 上构建不了**：gnu-efi 需要产出 ELF，它的 `inc/efi.h` 里直接写着
> `#error "Mach-O toolchain isn't supported"`；freestanding 示例则需要能输出
> PE/COFF 的链接器，而 macOS 自带的 `ld` 做不到。两条路都要一个 ELF/PE 工具链，
> 所以 macOS 上请直接用下面的 CI，或者在 Linux 容器/虚拟机里跑 `make`。

`GNU_EFI_DIR` 可以指到已有的 gnu-efi 源码树（跳过自动 clone）：

```bash
make GNU_EFI_DIR=/path/to/gnu-efi
```

---

## UEFI 应用到底是什么

一句话：**UEFI 应用就是一个 PE32+ 可执行文件**，只不过 PE 头里的 `Subsystem` 是 10 而不是 2/3。固件（UEFI 实现，比如 OVMF、EDK2）把它当 PE32+ 加载，调入口点，交给他一个 `EFI_SYSTEM_TABLE*`，他就直接跑在固件的地址空间里。

打印一行字所依赖的调用链就这么短：

```
efi_main(ImageHandle, SystemTable)
   └─ SystemTable->ConOut->OutputString(ConOut, L"Hello World!\r\n")
          └─ EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL，2 个参数
```

`SystemTable` 里还挂着 `BootServices`（内存分配、事件、协议查找、Handle/协议管理……）
和 `RuntimeServices`（变量、时间、capsule、复位……）。所以「UEFI 应用」本质上就是
一个跑在裸机环境、能调用这两张函数表的 PE 程序。

### 关键约定（踩坑最多的地方）

* **`CHAR16` 是 16 位**。编 gcc 要加 `-fshort-wchar`，否则 `L"..."` 编译出来是
  32 位 `wchar_t`，固件收到的是乱码。
* **`EFI_STATUS` 是 64 位**，高位为 1 表示错误：`#define EFI_ERROR(a) ((INTN)(a) < 0)`。
* **x86_64 上固件用 MS x64 ABI**，gcc 默认是 SysV ABI。参数 ≤4 个时两者寄存器
  分配完全一样；多于 4 个参数就要靠 `-maccumulate-outgoing-args` 补齐栈阴影空间。
  mingw-w64 的 x86_64 默认就是 MS ABI，天然对得上。
* **gnu-efi 上还有一个额外的坑**：`GNU_EFI_USE_MS_ABI` 这个宏决定
  `EFIAPI` 是不是 `__attribute__((ms_abi))`、`uefi_call_wrapper()` 是直接调用还是
  走 `efi_callN` 汇编跳板。**你的应用必须和编译 `libefi.a` 时用同一个设置**，
  否则链接就找不到符号了。
* **入口必须是 UEFI 入口**，不是 `_start`：不接收 argc/argv，不做 libc 初始化，
  不能修改非易失寄存器（rbx/rbp/rsi/rdi/r12-r15），DF 必须为 0。
* **镜像必须可重定位**。固件把它加载到哪里是不确定的，所有「代码里写了绝对地址」
  的地方都得在运行时修正。GNU-ELF 那边靠 `R_X86_64_RELATIVE` + `objcopy`，
  PE 那边靠 `.reloc` 段里的 `IMAGE_REL_BASED_HIGHLOW/DIR64`。
* **`Subsystem` 值**：10 = EFI application，11 = boot service driver，12 = runtime driver。

---

## 路线一：gnu-efi

源码：[`examples/gnu-efi-hello/hello.c`](examples/gnu-efi-hello/hello.c)，构建：[`Makefile`](examples/gnu-efi-hello/Makefile)

### 构建流水线

```
hello.c
  │  gcc -fPIC -ffreestanding -fshort-wchar -mno-red-zone -maccumulate-outgoing-args ...
  ▼
hello.o                              ELF 可重定位目标文件
  │  ld -shared -Bsymbolic -T elf_x86_64_efi.lds  crt0-efi-x86_64.o  -lgnuefi -lefi
  ▼
hello.so                             ELF PIE，自带 _relocate 自重定位器
  │  objcopy -O efi-app-x86_64
  ▼
hello.efi                            PE32+ / Subsystem=10
```

这里的关键点是 **gcc 只认 ELF，UEFI 只认 PE/COFF**，gnu-efi 的作用就是这条
「先编成 `.so`、再 `objcopy` 改头」的中转路线。省掉了它，你就得让 gcc 直接产出
PE —— 那就是下面路线二的做法。

### 运行时序

```
固件 LoadImage(hello.efi)
  → 分配页、把 PE 段贴进去、查 .reloc
  → 调 crt0 的 _start (rcx=ImageHandle, rdx=SystemTable)
      → _relocate()   遍历 _DYNAMIC 里的 R_X86_64_RELATIVE，加上实际加载基址
      → _entry()      把 SystemTable 存到全局 gST，初始化内存池
      → efi_main()
```

### 代码里值得注意的几行

```c
#include <efi.h>      /* 规范里的类型和结构体 */
#include <efilib.h>   /* gnu-efi 的便利库：Print / Pool 分配 / StrCpy… */

InitializeLib (image_handle, system_table);   /* 装好 gST、内存池 */

uefi_call_wrapper (conout->OutputString, 2, conout, L"Hello World!\r\n");
Print (L"revision 0x%x\n", system_table->Hdr.Revision);
```

* `uefi_call_wrapper(fn, nargs, ...)` 是 gnu-efi 用来跨 ABI 转发调用的宏。
  非 MS-ABI 构建时它展开成一层汇编 thunk，把 SysV 寄存器搬到 MS ABI 的位置上。
* `InitializeLib()` 是 `libefi.a` 里的函数，做的是「收下 SystemTable、初始化
  `PoolBoot`、让 `gST` 可用」。不调用它的话 `gST` 是空的。

### gnu-efi 提供了什么

| 产物 | 内容 |
| --- | --- |
| `crt0-efi-x86_64.o` | PE 入口 `_start`：重定位 + 调用 `efi_main` |
| `libgnuefi.a` | `_relocate()`、`InitializeLib()` |
| `libefi.a` | `Print`/`VPrint`、`PoolAlloc`/`PoolFree`、`StrCpy`/`StrSize`、`LocateProtocol`… |
| `gnuefi/elf_x86_64_efi.lds` | 链接脚本：section 布局、`.reloc` 段的占位 |
| `inc/` | UEFI 规范的头文件，加上 x86_64 专用修正 |

---

## 路线二：freestanding

源码：[`efi_min.h`](examples/freestanding-hello/efi_min.h)、[`crt0.S`](examples/freestanding-hello/crt0.S)、[`pe_reloc.c`](examples/freestanding-hello/pe_reloc.c)、[`hello.c`](examples/freestanding-hello/hello.c)，构建：[`Makefile`](examples/freestanding-hello/Makefile)

不碰 ELF，直接让 mingw-w64 的 `ld` 产出 PE32+：

```
x86_64-w64-mingw32-gcc -nostdlib -Wl,--subsystem,10 -Wl,-dll -Wl,-e,efi_entry ...
```

`-Wl,-dll` 让链接器保留 `.reloc` 段并打上 `IMAGE_FILE_DLL` 特性；`-e efi_entry`
把入口指到我们自己的汇编桩。

### 入口桩为什么必须先把参数留在寄存器里

```asm
efi_entry:
	cld                       /* UEFI 要求 DF=0 */
	andq	$-16, %rsp        /* 对齐；固件不保证入口时栈已对齐 */
	subq	$0x20, %rsp        /* 32 字节 home space，仍 16 字节对齐 */
	call	efi_boot          /* rcx=ImageHandle, rdx=SystemTable */
	addq	$0x20, %rsp
	ret
```

几乎所有真实入口桩（比如 gnu-efi 的 `crt0-efi-x86_64.S`、mingw-w64 的
`crt0-efi-x86_64.S`）第一件事都是 `movq %rdx, %gST` —— 把 SystemTable 存进一个
全局变量。**但那时候镜像还没重定位**，那个全局变量里写的是链接期的绝对地址，
可能是错的。

本例绕开这一点：不碰任何全局变量，直接把两个寄存器传给 C 函数。

### 自重定位怎么知道自己在哪

`efi_boot()` 第一件事是

```c
system_table->BootServices->HandleProtocol (image_handle,
					    &loaded_image_guid,
					    &loaded_image);
pe_relocate (loaded_image->ImageBase);   /* 固件告诉我们的「实际加载基址」 */
```

这在重定位之前做是**安全**的：`SystemTable` 是入口参数（在寄存器里），
`BootServices` 只是从它读出来的一层指针，整条路径上没有任何需要被修正的绝对地址。

拿到基址后 `pe_relocate()` 干的事和 Windows PE 加载器一样：

```
实际基址 - PE 头里的 OptionalHeader.ImageBase  ==  delta
遍历 DataDirectory[5] (BASERELOC)：
  每块 = { VirtualAddress, SizeOfBlock, 条目[] }
  条目 = { 类型<<12 | 标志, 相对偏移 }
  类型 3 (HIGHLOW)  -> *(u32*)(base+va+off) += delta
  类型 10 (DIR64)   -> *(u64*)(base+va+off) += delta
  类型 0 (ABSOLUTE) -> 填充，跳过
```

因为 `-Wl,--image-base,0x100000` 这个首选基址通常和固件实际给的地址不同，
**不做重定位的话镜像里的绝对地址全是错的，一进去就崩**。所以这一步不是可选的。

这个镜像里**确实**有东西需要修正。`hello.c` 里有一个故意写的全局变量：

```c
static CHAR16  greeting[]   = TAG L"Hello World!\r\n";
static CHAR16 *greeting_ptr = greeting;      /* <- 落在 .data，产生一条 DIR64 重定位 */
```

数据段里的指针必须存绝对地址，链接器因此会生成一条 `IMAGE_REL_BASED_DIR64`。
其余代码全是 `IMAGE_REL_AMD64_REL32`（RIP 相对，不需要基址修正）。
程序跑起来会打印这次实际施加的 delta：

```
[freestanding] Hello World!
[freestanding] SystemTable revision 0x0002001e, BootServices @ 0x7f7d9a0
[freestanding] relocation delta 0x7f7c0000
```

把 `efi_boot()` 里 `pe_relocate()` 那行注释掉再跑一次，就能亲眼看到它挂掉的样子。

### C 代码里只剩这些

```c
conout = system_table->ConOut;
conout->OutputString (conout, L"[freestanding] Hello World!\r\n");
```

没有 `InitializeLib`、没有 `PoolAlloc`、没有 `Print`，甚至十六进制输出都是自己写的。
整个镜像只有三个 `.o`。

---

## 两条路线的取舍

| | gnu-efi | freestanding |
| --- | --- | --- |
| 产物大小 | 几十 KB（带了 libefi 一堆东西） | 几 KB |
| 编译目标 | ELF，再 objcopy 成 PE | 直接 PE32+ |
| 重定位 | gnu-efi 的 `_relocate()`（ELF `R_X86_64_RELATIVE`） | 自己实现（PE `.reloc`） |
| 入口 | gnu-efi 的 `crt0-efi-x86_64.o` | 自己的 `crt0.S` |
| 库 | `Print`/`Pool`/`Str*`/`LocateProtocol` 全都有 | 全靠自己写 |
| 适合 | 写实际的 UEFI 应用/驱动 | 想搞明白加载过程 / 极小体积 / 移植到别的固件 |

---

## 在 QEMU 里跑

```bash
make qemu
```

脚本会：

1. 把两个 `.efi` 和一个 UEFI Shell 放进一个 FAT 目录；
2. 写一个 `startup.nsh` 依次执行它们，最后 `reset -s`；
3. 用 QEMU 以 vvfat 当硬盘、OVMF 当 pflash 启动，`-nographic -serial stdio` 把控制台
   接到串口；
4. 断言串口日志里出现了 `[gnu-efi] Hello World!` 和 `[freestanding] Hello World!`。

产物留在 `build/serial.log`。

`-nodefaults -nographic` 意味着没有 VGA 设备，OVMF 就会把 `ConOut` 接到串口——
这也正是 UEFI 应用输出能被日志抓到的原因。

固件和 Shell 的位置可以用环境变量覆盖：

```bash
OVMF_CODE=/path/to/OVMF_CODE.fd \
OVMF_VARS=/path/to/OVMF_VARS.fd \
UEFI_SHELL=/path/to/shellx64.efi \
make qemu
```

### 换到实体机

把 `hello.efi` 拷到 UEFI 系统分区（ESP）根目录，用 UEFI Shell 执行：

```
FS0:\hello.efi
```

或者想让它开机自动跑，就改名成 `EFI\BOOT\BOOTX64.EFI` 放在 ESP 上——
固件的 Boot Manager 会把它当作启动项。

---

## 目录

```
uefi/
├── Makefile                       顶层：all / check / qemu / clean
├── scripts/
│   ├── check-efi.sh               静态校验 PE32+ / Subsystem=10 / 有 .reloc
│   └── run-qemu.sh                QEMU + OVMF 冒烟测试
├── examples/
│   ├── gnu-efi-hello/
│   │   ├── hello.c                20 行业务代码
│   │   └── Makefile               显式写出 gcc / ld / objcopy 三步
│   └── freestanding-hello/
│       ├── efi_min.h              手写的最小 UEFI 类型
│       ├── crt0.S                 PE 入口桩
│       ├── pe_reloc.c             PE 基址重定位
│       ├── hello.c                业务代码
│       └── Makefile               一条链接命令搞定
└── build/                         所有中间产物与日志（gitignore）
```

## 继续往下玩

* 用 `x86_64-w64-mingw32-objdump -d hello.efi` 反汇编，看看 `efi_entry` 是不是
  你写的那个。
* 用 `x86_64-w64-mingw32-objdump -x hello.efi` 找 `Base Relocation Directory`，
  再和 `objdump -r` 里的 `IMAGE_REL_AMD64_ADDR64` 对上号。
* 改 `Print` 的格式串，加一个新例子跑一遍，熟悉 `libefi` 还提供什么。
* 试试 `BootServices->AllocatePool` / `LocateProtocol`，比 freestanding 版容易得多。
* 把 `examples/freestanding-hello/hello.c` 里 `pe_relocate()` 的调用注释掉，
  在 OVMF 上观察崩溃的样子。
* 换架构：gnu-efi 支持 x86_64 / ia32 / aarch64 / arm / riscv64 / loongarch64，
  `make ARCH=... CROSS_COMPILE=...-linux-gnu-` 即可；freestanding 版目前只做了 x86_64。

## 参考

* UEFI 规范 2.10 — <https://uefi.org/specs/UEFI/2.10/>
* PE/COFF 格式 — <https://learn.microsoft.com/en-us/windows/win32/debug/pe-format>
* gnu-efi — <https://github.com/ncroxon/gnu-efi>
* OSDev Wiki: GNU-EFI — <https://wiki.osdev.org/GNU-EFI>
* uefi-simple（另一套很好的最小示例，带 QEMU 一键运行）— <https://github.com/pbatard/uefi-simple>