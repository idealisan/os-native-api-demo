#!/usr/bin/env bash
#
# run-qemu.sh —— 在 QEMU + OVMF 里真的把两个 .efi 跑起来，并检查串口输出
#
# 需要：
#   qemu-system-x86_64
#   一份 OVMF 固件（apt install ovmf，或自己下）
#   一个 UEFI Shell（用来代替固件去启动 .efi；.efi 本身没法被固件自动执行，
#     除非改名成 EFI\BOOT\BOOTX64.EFI 做成启动项）
#
# 可用环境变量覆盖：
#   QEMU        qemu-system-x86_64 的路径
#   OVMF_CODE   OVMF CODE 镜像路径
#   OVMF_VARS   OVMF VARS 模板路径（会被复制一份再使用）
#   UEFI_SHELL  shellx64.efi 路径（不存在则自动下载）
#   QEMU_TIMEOUT  秒，默认 90
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${HERE}/build"
FAT_DIR="${BUILD_DIR}/fat"
QEMU_TIMEOUT="${QEMU_TIMEOUT:-90}"

SHELL_URL="https://github.com/pbatard/UEFI-Shell/releases/download/24H2/shellx64.efi"

die() { echo "ERROR: $*" >&2; exit 1; }

# ------------------------------------------------------------ 可执行文件

QEMU="${QEMU:-$(command -v qemu-system-x86_64 || true)}"
[[ -n $QEMU ]] || die "找不到 qemu-system-x86_64（brew install qemu / apt install qemu-system-x86）"

# ------------------------------------------------------------- 固件定位

find_first() {
	for p in "$@"; do
		if [[ -f $p ]]; then echo "$p"; return 0; fi
	done
	return 1
}

if [[ -z ${OVMF_CODE:-} ]]; then
	OVMF_CODE=$(find_first \
		/usr/share/ovmf/OVMF_CODE_4M.fd \
		/usr/share/ovmf/OVMF_CODE.fd \
		/usr/share/ovmf/OVMF.fd \
		/usr/share/OVMF/OVMF_CODE.fd \
		/usr/share/edk2/ovmf/OVMF_CODE.fd \
		"$BUILD_DIR/OVMF_CODE.fd" \
		"$BUILD_DIR/OVMF.fd") \
		|| die "找不到 OVMF 固件，装一下：sudo apt install ovmf（macOS 可用 brew install qemu 后从 https://github.com/tianocore/tianocore-build 的 releases 取）"
fi

# 只有单文件合并版（OVMF.fd）时，pflash 只需要挂一个 unit
if [[ -z ${OVMF_VARS:-} ]]; then
	OVMF_VARS=$(find_first \
		/usr/share/ovmf/OVMF_VARS_4M.fd \
		/usr/share/ovmf/OVMF_VARS.fd \
		/usr/share/OVMF/OVMF_VARS.fd \
		/usr/share/edk2/ovmf/OVMF_VARS.fd) \
		|| OVMF_VARS=""
fi

# ---------------------------------------------------------------- Shell

mkdir -p "$BUILD_DIR"

if [[ -z ${UEFI_SHELL:-} ]]; then
	UEFI_SHELL=$(find_first \
		/usr/share/ovmf/Shell.efi \
		/usr/share/ovmf/shellx64.efi \
		/usr/share/shell/shellx64.efi) || UEFI_SHELL="$BUILD_DIR/shellx64.efi"

	if [[ ! -f $UEFI_SHELL ]]; then
		echo "  DOWNLOAD UEFI Shell"
		UEFI_SHELL="$BUILD_DIR/shellx64.efi"
		curl -fsSL "$SHELL_URL" -o "$UEFI_SHELL" \
			|| die "下载 UEFI Shell 失败：$SHELL_URL"
	fi
fi
[[ -f $UEFI_SHELL ]] || die "UEFI Shell 不存在：$UEFI_SHELL"

# --------------------------------------------------- 组装 FAT + 启动脚本

rm -rf "$FAT_DIR"
mkdir -p "$FAT_DIR/EFI/BOOT"

declare -a EXPECT=()

add_app() {
	local src="$1" name="$2" expect="$3"
	[[ -f $src ]] || die "缺少产物 $src（先跑 make）"
	cp "$src" "$FAT_DIR/$name"
	EXPECT+=("$expect")
}

add_app "${HERE}/examples/gnu-efi-hello/hello.efi" \
         "hello-gnu-efi.efi" \
         "[gnu-efi] Hello World!"

add_app "${HERE}/examples/freestanding-hello/hello.efi" \
         "hello-freestanding.efi" \
         "[freestanding] Hello World!"

{
	echo "@echo -off"
	for name in hello-gnu-efi.efi hello-freestanding.efi; do
		echo "FS0:\\$name"
	done
	echo "reset -s"
} > "$FAT_DIR/startup.nsh"

cp "$UEFI_SHELL" "$FAT_DIR/EFI/BOOT/BOOTX64.EFI"

echo "  QEMU     boot OVMF (code=$OVMF_CODE)"

# --------------------------------------------------------------- 跑 QEMU

# 只有拆分版固件（CODE + VARS）才需要第二个 pflash unit；
# 单文件合并版（OVMF.fd）一个 unit 就够了。
PFLASH=(-drive "if=pflash,format=raw,unit=0,file=${OVMF_CODE},readonly=on")
if [[ -n $OVMF_VARS ]]; then
	cp "$OVMF_VARS" "$BUILD_DIR/OVMF_VARS.fd"
	PFLASH+=(-drive "if=pflash,format=raw,unit=1,file=${BUILD_DIR}/OVMF_VARS.fd")
fi

QEMU_CMD=(
	"$QEMU"
	-accel tcg
	-machine q35
	-m 256 -smp 1
	-nodefaults -nographic -serial stdio -no-reboot -net none
	"${PFLASH[@]}"
	-drive "format=raw,file=fat:rw:${FAT_DIR}"
)

set +e
if command -v timeout >/dev/null 2>&1; then
	timeout --foreground "$QEMU_TIMEOUT" "${QEMU_CMD[@]}" 2>&1 | tee "$BUILD_DIR/serial.log"
	pipe_rc=${PIPESTATUS[0]}
else
	# macOS 默认没有 timeout(1)
	"${QEMU_CMD[@]}" > "$BUILD_DIR/serial.log" 2>&1 &
	qemu_pid=$!
	( sleep "$QEMU_TIMEOUT"; kill -TERM "$qemu_pid" 2>/dev/null ) &
	watchdog=$!
	cat "$BUILD_DIR/serial.log"
	wait "$qemu_pid" 2>/dev/null
	pipe_rc=$?
	kill "$watchdog" 2>/dev/null
fi
set -e

echo
echo "  LOG      build/serial.log"
if [[ $pipe_rc -ne 0 && $pipe_rc -ne 124 ]]; then
	echo "  NOTE     qemu exited with $pipe_rc"
fi

# --------------------------------------------------------------- 断言

rc=0
for e in "${EXPECT[@]}"; do
	if grep -qF -- "$e" "$BUILD_DIR/serial.log"; then
		echo "  PASS    \"$e\""
	else
		echo "  FAIL    \"$e\" 没出现在串口输出里"
		rc=1
	fi
done

# 只断言「打印出来了」是不够的：应用返回后如果没把栈指针还原回去，
# UEFI Shell 会拿着错位的栈继续跑，跳进 .text 之外执行 ->
# #UD Invalid Opcode —— 而 Hello World 其实早就打印完了。
# 所以额外检查固件有没有报异常。
if grep -qE 'X64 Exception Type|ASSERT_[A-Z]|!!! ' "$BUILD_DIR/serial.log"; then
	echo "  FAIL    固件报告了异常（见 build/serial.log）："
	grep -E 'X64 Exception Type|ASSERT_[A-Z]|!!! ' "$BUILD_DIR/serial.log" \
		| head -5 | sed 's/^/          /'
	rc=1
else
	echo "  PASS    固件没有报告异常"
fi

# startup.nsh 末尾的 reset -s 应该让 QEMU 自己退出，而不是被超时杀掉
if [[ $pipe_rc -eq 124 ]]; then
	echo "  FAIL    qemu 跑满 ${QEMU_TIMEOUT}s 才被超时杀掉（reset -s 没生效？）"
	rc=1
fi

exit $rc