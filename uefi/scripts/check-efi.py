#!/usr/bin/env python3
"""
check-efi.py —— 直接读 PE 头，静态校验产物是不是 EFI application

刻意**不**去解析 `objdump -p` 的输出：binutils 各版本的字段排版和措辞一直在变
（比如 Subsystem 那行在 2.4x 上被拆成了 `Subsystem  Version`），
跟着它的格式写 sed 迟早再炸。这里直接按 PE/COFF 规范里的字节偏移来读，
任何 binutils 版本都一致。

需要 Python 3（GitHub runner 和 macOS 自带）。
用法：./check-efi.py foo.efi bar.efi
"""

import struct
import sys

DOS_MAGIC = b"MZ"
NT_MAGIC = b"PE\0\0"
NT32P_MAGIC = 0x20B

IMAGE_FILE_MACHINE_I386 = 0x014C
IMAGE_FILE_MACHINE_AMD64 = 0x8664

EFI_SUBSYSTEM_NAMES = {
    0: "unknown",
    1: "native",
    2: "windows_gui",
    3: "windows_cui",
    5: "os2_cui",
    7: "posix_cui",
    9: "windows_ce_gui",
    10: "EFI_APPLICATION",
    11: "EFI_BOOT_SERVICE_DRIVER",
    12: "EFI_RUNTIME_DRIVER",
    13: "EFI_ROM",
    14: "XBOX",
}

IMAGE_DIRECTORY_ENTRY_BASERELOC = 5


class PeError(Exception):
    pass


def u16(buf, off):
    return struct.unpack_from("<H", buf, off)[0]


def u32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


def u64(buf, off):
    return struct.unpack_from("<Q", buf, off)[0]


def parse(path):
    with open(path, "rb") as fp:
        buf = fp.read()

    if len(buf) < 0x40 or buf[:2] != DOS_MAGIC:
        raise PeError("没有 MZ 头，不是 PE 文件")

    pe_off = u32(buf, 0x3C)
    if buf[pe_off : pe_off + 4] != NT_MAGIC:
        raise PeError("没有 PE\\0\\0 签名")

    coff = pe_off + 4
    machine = u16(buf, coff + 0)
    nsections = u16(buf, coff + 2)
    sizeof_opt = u16(buf, coff + 16)
    characteristics = u16(buf, coff + 18)

    opt = coff + 20
    magic = u16(buf, opt + 0)
    if magic != NT32P_MAGIC:
        raise PeError("不是 PE32+ (Magic=0x%04x)，本检查只支持 PE32+/x86_64" % magic)

    entry_rva = u32(buf, opt + 16)
    image_base = u64(buf, opt + 24)
    section_align = u32(buf, opt + 32)
    file_align = u32(buf, opt + 36)
    subsystem = u16(buf, opt + 68)
    num_dirs = u32(buf, opt + 108)

    reloc_rva, reloc_size = 0, 0
    if num_dirs > IMAGE_DIRECTORY_ENTRY_BASERELOC:
        base = opt + 112 + IMAGE_DIRECTORY_ENTRY_BASERELOC * 8
        reloc_rva, reloc_size = u32(buf, base), u32(buf, base + 4)

    return {
        "machine": machine,
        "nsections": nsections,
        "characteristics": characteristics,
        "entry_rva": entry_rva,
        "image_base": image_base,
        "section_align": section_align,
        "file_align": file_align,
        "subsystem": subsystem,
        "reloc_rva": reloc_rva,
        "reloc_size": reloc_size,
        "size": len(buf),
    }


def check(path):
    try:
        info = parse(path)
    except PeError as exc:
        return ["%s : %s" % (path, exc)]
    except struct.error:
        return ["%s : PE 头被截断，文件不完整" % path]
    except OSError as exc:
        return ["%s : 打不开（%s）" % (path, exc.strerror)]

    errors = []
    notes = []

    if info["machine"] != IMAGE_FILE_MACHINE_AMD64:
        errors.append(
            "Machine=0x%04x，应为 0x%04x (AMD64)"
            % (info["machine"], IMAGE_FILE_MACHINE_AMD64)
        )

    if info["subsystem"] != 10:
        errors.append(
            "Subsystem=%d (%s)，应为 10 (EFI_APPLICATION)"
            % (
                info["subsystem"],
                EFI_SUBSYSTEM_NAMES.get(info["subsystem"], "unknown"),
            )
        )

    if not info["reloc_rva"] or not info["reloc_size"]:
        errors.append(
            "没有 Base Relocation Directory（.reloc）—— 镜像一条重定位都没有，"
            "固件只能把它加载到首选基址 ImageBase=0x%x；"
            "UEFI 规范要求 PE/COFF 镜像必须可重定位" % info["image_base"]
        )

    if info["entry_rva"] == 0:
        errors.append("AddressOfEntryPoint 为 0")

    if info["section_align"] < 0x1000:
        errors.append(
            "SectionAlignment=0x%x，UEFI 要求 >= 0x1000" % info["section_align"]
        )

    if not errors:
        notes.append(
            "Machine=AMD64 Subsystem=10(EFI_APPLICATION) "
            "ImageBase=0x%x Entry=0x%x SectionAlign=0x%x "
            "RelocDir=0x%x+%d Sections=%d Size=%d"
            % (
                info["image_base"],
                info["entry_rva"],
                info["section_align"],
                info["reloc_rva"],
                info["reloc_size"],
                info["nsections"],
                info["size"],
            )
        )

    return errors, notes


def main(argv):
    if len(argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2

    rc = 0
    for path in argv[1:]:
        result = check(path)
        if isinstance(result, list):
            for err in result:
                print("FAIL  " + err)
            rc = 1
            continue
        errors, notes = result
        for note in notes:
            print("OK    %s  %s" % (path, note))
        for err in errors:
            print("FAIL  %s : %s" % (path, err))
        if errors:
            rc = 1

    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv))