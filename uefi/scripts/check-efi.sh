#!/usr/bin/env bash
#
# check-efi.sh —— 确认产物确实是一个 PE32+ EFI application
#
# 这些检查不依赖 UEFI 环境，纯静态看 PE 头，能在 CI 里第一时间抓到
# 「忘了 objcopy」「subsystem 写错成 Windows CUI」「重定位段被剥掉」
# 这类低级错误。
#
set -euo pipefail

OBJDUMP="${OBJDUMP:-objdump}"
rc=0

for f in "$@"; do
	if [[ ! -f $f ]]; then
		echo "FAIL  $f : 文件不存在"
		rc=1
		continue
	fi

	head -c 2 "$f" | grep -q 'MZ' || { echo "FAIL  $f : 没有 MZ 头，不是 PE 文件"; rc=1; continue; }

	info=$($OBJDUMP -p "$f" 2>/dev/null || true)
	if [[ -z $info ]]; then
		echo "FAIL  $f : $OBJDUMP -p 读不出 PE 可选头"
		rc=1
		continue
	fi

	subsystem=$(sed -n 's/.*Subsystem *//p' <<<"$info" | head -1)
	magic_hex=$(sed -n 's/^Magic *//p' <<<"$info" | head -1)
	has_reloc=$(grep -c 'Base Relocation Directory' <<<"$info" || true)

	ok=1
	[[ $magic_hex == "020b" ]] || { echo "FAIL  $f : 不是 PE32+ (Magic=$magic_hex)"; ok=0; }
	[[ $subsystem == "10" ]]    || { echo "FAIL  $f : Subsystem=$subsystem，应为 10 (EFI_APPLICATION)"; ok=0; }
	[[ $has_reloc -ge 1 ]]      || { echo "FAIL  $f : 没有 Base Relocation Directory"; ok=0; }

	if [[ $ok == 1 ]]; then
		size=$(wc -c <"$f" | tr -d ' ')
		echo "OK    $f  (PE32+, Subsystem=10 EFI_APPLICATION, ${size} bytes)"
	else
		rc=1
	fi
done

exit $rc