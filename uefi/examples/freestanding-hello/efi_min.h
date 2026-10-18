/*
 * efi_min.h —— 只手写「刚好够用」的一小撮 UEFI 类型
 *
 * 目的是让你看清：打印一行字其实只需要
 *   EFI_SYSTEM_TABLE -> ConOut -> OutputString
 * 这一个函数指针，以及为了拿到 ConOut 而必须知道的那点结构体布局。
 *
 * 所有结构体都按 UEFI 2.10 规范里的原始字段顺序排列，字段偏移必须与固件
 * 一致，否则读出来的指针会是垃圾。字段完整性对「我们只读」的应用来说
 * 足够了：我们从不回写 ConOut 结构体本身。
 */

#ifndef EFI_MIN_H
#define EFI_MIN_H

/* ---------------------------------------------------------------- 基础类型 */

typedef unsigned char       UINT8;
typedef signed char         INT8;
typedef unsigned short      UINT16;
typedef short               INT16;
typedef unsigned int        UINT32;
typedef int                 INT32;
typedef unsigned long long  UINT64;
typedef long long           INT64;
typedef void                VOID;

typedef UINT64  UINTN;
typedef INT64   INTN;

/* 规范里到处在用的方向性标注宏，对生成代码没有任何影响 */
#define IN
#define OUT
#define OPTIONAL

/*
 * EFIAPI = 「按 MS ABI 调用」。
 *
 * UEFI 固件是 Windows 出身的 PE，加载器用的是 MS x64 ABI。而
 * x86_64-w64-mingw32-gcc 的默认 -mabi 就是 ms，所以这里什么都不用加。
 * 如果换到默认 SysV ABI 的编译器（例如 macOS 自带的 clang 直接编），
 * 这一行就是让「我们声明的函数指针」和「固件导出的函数」保持一致的关键。
 */
#if defined(__x86_64__) || defined(_M_X64) || defined(_WIN64)
#define EFIAPI
#else
#define EFIAPI  __attribute__((ms_abi))
#endif

typedef UINT8   BOOLEAN;
typedef UINT16  CHAR16;
typedef char    CHAR8;

typedef enum {
	EFI_SUCCESS               = 0,
	EFI_ERROR_MASK            = 1 << (sizeof (UINTN) * 8 - 1),
	EFI_LOAD_ERROR            = EFI_ERROR_MASK | 1,
	EFI_INVALID_PARAMETER     = EFI_ERROR_MASK | 2
} EFI_STATUS;

#define EFI_ERROR(a)  (((INTN) (UINTN) (a)) < 0)

typedef UINTN    EFI_HANDLE;
typedef UINT64    EFI_PHYSICAL_ADDRESS;
typedef UINT64    EFI_VIRTUAL_ADDRESS;

typedef struct {
	UINT32  Data1;
	UINT16  Data2;
	UINT16  Data3;
	UINT8   Data4[8];
} EFI_GUID;

/* ------------------------------------------------------------ 表头与表项 */

/* 规范里的签名是 'IBI SYST'，这里用不到 */
#define EFI_SYSTEM_TABLE_SIGNATURE 0x5453595320494249ULL

typedef struct {
	UINT64  Signature;
	UINT32  Revision;
	UINT32  HeaderSize;
	UINT32  CRC32;
	UINT32  Reserved;
} EFI_TABLE_HEADER;

typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef struct _EFI_BOOT_SERVICES               EFI_BOOT_SERVICES;

typedef EFI_STATUS (EFIAPI *EFI_HANDLE_PROTOCOL)(
	IN  EFI_HANDLE  This,
	IN  EFI_GUID    *Protocol,
	OUT VOID        **Interface
	);

typedef struct _EFI_SYSTEM_TABLE {
	EFI_TABLE_HEADER                 Hdr;
	CHAR16                           *FirmwareVendor;
	UINT32                           FirmwareRevision;
	EFI_HANDLE                       ConsoleInHandle;
	VOID                             *ConIn;
	EFI_HANDLE                       ConsoleOutHandle;
	EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *ConOut;
	EFI_HANDLE                       StandardErrorHandle;
	EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *StdErr;
	VOID                             *RuntimeServices;
	EFI_BOOT_SERVICES                *BootServices;
	UINTN                            NumberOfTableEntries;
	VOID                             *ConfigurationTable;
} EFI_SYSTEM_TABLE;

/*
 * EFI_BOOT_SERVICES 一共有 30+ 个成员，HandleProtocol 是第 17 个。
 * 逐个定义太啰嗦，这里只把前面的位置用占位符填出来 —— 偏移必须对。
 */
struct _EFI_BOOT_SERVICES {
	VOID *Reserved_1_HighestPriorityTpl;       /*  RaiseTPL        */
	VOID *Reserved_2_RestoreTpl;              /*  RestoreTPL      */
	VOID *Reserved_3_AllocatePages;            /*  AllocatePages    */
	VOID *Reserved_4_FreePages;                /*  FreePages        */
	VOID *Reserved_5_GetMemoryMap;             /*  GetMemoryMap     */
	VOID *Reserved_6_AllocatePool;             /*  AllocatePool     */
	VOID *Reserved_7_FreePool;                 /*  FreePool         */
	VOID *Reserved_8_CreateEvent;              /*  CreateEvent      */
	VOID *Reserved_9_SetTimer;                 /*  SetTimer         */
	VOID *Reserved_10_WaitForEvent;            /*  WaitForEvent     */
	VOID *Reserved_11_SignalEvent;             /*  SignalEvent      */
	VOID *Reserved_12_CloseEvent;              /*  CloseEvent       */
	VOID *Reserved_13_CheckEvent;              /*  CheckEvent       */
	VOID *Reserved_14_InstallProtocolInterface;   /* Install...   */
	VOID *Reserved_15_ReinstallProtocolInterface; /* Reinstall... */
	VOID *Reserved_16_UninstallProtocolInterface; /* Uninstall... */
	EFI_HANDLE_PROTOCOL                     HandleProtocol;
	VOID *Reserved_18;                         /*  Reserved         */
};

/* ----------------------------------------------------- 控制台输出协议 */

typedef EFI_STATUS (EFIAPI *EFI_TEXT_RESET)(
	IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
	IN BOOLEAN                          Extended
	);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING)(
	IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
	IN CHAR16                           *String
	);

struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
	EFI_TEXT_RESET      Reset;
	EFI_TEXT_STRING     OutputString;
	EFI_TEXT_STRING     TestString;
	VOID               *QueryMode;
	VOID               *SetMode;
	VOID               *SetAttribute;
	VOID               *ClearScreen;
	VOID               *SetCursorPosition;
	VOID               *EnableCursor;
	VOID               *ShowCursor;
	VOID               *Mode;
};

/* ------------------------------------------ EFI_LOADED_IMAGE_PROTOCOL */

/*
 * 重定位代码需要知道「镜像实际被加载到哪个地址」。
 * 这个信息只有固件知道，通过 HandleProtocol 拿 LoadedImage 协议得到。
 * 注意：这一步在重定位之前执行是完全安全的 —— SystemTable 是通过寄存器
 * 传进来的，BootServices 也只是指针，没有任何绝对地址需要被修正。
 */
#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
	{ 0x5B1B31A1, 0x9562, 0x11D2, { 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }

typedef struct {
	UINT32    Revision;        /* 0x1000 */
	VOID     *ParentHandle;
	EFI_SYSTEM_TABLE *SystemTable;
	EFI_HANDLE DeviceHandle;
	VOID     *FilePath;
	VOID     *Reserved;
	UINT32    LoadOptionsSize;
	VOID     *LoadOptions;
	VOID     *ImageBase;       /* <-- 固件给出的实际加载基址 */
	UINT64    ImageSize;
	UINT32    ImageCodeType;
	UINT32    ImageDataType;
	VOID     *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

/* ------------------------------------------------- ResetSystem（可选） */

typedef struct {
	UINT32    ResetType;
	UINT32    ResetStatus;
	UINTN     DataSize;
	VOID     *ResetData;
} EFI_RESET_DATA;

typedef VOID (EFIAPI *EFI_RESET_SYSTEM)(
	IN UINTN  ResetType,
	IN EFI_STATUS ResetStatus,
	IN UINTN  DataSize,
	IN VOID  *ResetData
	);

#define EfiResetShutdown 2

/* ------------------------------------------------------------ 本例入口 */

typedef EFI_STATUS (EFIAPI *EFI_IMAGE_ENTRY)(VOID *ImageHandle,
					     EFI_SYSTEM_TABLE *SystemTable);

/* pe_reloc.c —— 返回 EFI_STATUS，并把「实际基址 - 首选基址」写进 out_delta */
EFI_STATUS  pe_relocate (VOID *image_base, UINT64 *out_delta);

/* hello.c */
EFI_STATUS  efi_boot    (VOID *image_handle, EFI_SYSTEM_TABLE *system_table);
EFI_STATUS  efi_main    (VOID *image_handle, EFI_SYSTEM_TABLE *system_table);

#endif /* EFI_MIN_H */