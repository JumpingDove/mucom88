# MUCOM88 Windows 0.70 golden生成

このdirectoryは、Apple Silicon上のWindows 11 ARM64 VMで、固定済みWindows版
`package/mucom88.exe`からMUB、WAV、VGM、S98の機能比較goldenを生成するための入口である。
goldenの通常更新は禁止し、固定VM snapshotからの明示的な生成、2回実行の決定性確認、reviewを必要とする。

## 前提

- Windows 11 ARM64 24H2
- 日本語system locale、ANSI code page 932、UTF-8 beta option無効
- repositoryと作業directoryはVM内のNTFS上に置く。shared folder上では生成しない
- repository pathと出力pathはASCIIのみ、空白なしとする
- network、plugin、SCCI2、外部ROM、YM2608 rhythm WAVを使用しない
- `package/mucom88.exe`は固定済みPE32/x86 binaryを使用し、再buildしない
- VM更新後は同じsnapshot扱いにせず、環境差分を記録する

scriptはWindows system locale、ANSI code page、Windows 24H2、入力7fileのsize/SHA-256、
`mucom88.exe`のPE32/x86 machine typeを事前検査する。環境差を無視する
`-AllowEnvironmentMismatch`は調査用であり、その結果を正式goldenへ昇格してはならない。

## 生成

Windows PowerShell 5.1またはそれ以降を開き、次のように実行する。値は実環境へ置き換える。

```powershell
Set-ExecutionPolicy -Scope Process Bypass

powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File C:\mucom88-golden\repo\tests\reference\windows-arm64-vm\mucom88-win-0.70\generate-golden.ps1 `
  -RepositoryRoot C:\mucom88-golden\repo `
  -OutputRoot C:\mucom88-golden\work-20260923 `
  -Hypervisor "<product>" `
  -HypervisorVersion "<version>" `
  -SnapshotName "<snapshot>" `
  -HostMacModel "<model>" `
  -HostSoc "<Apple SoC>" `
  -HostMacOS "<version/build>"
```

`OutputRoot`は存在しないpathを指定する。scriptは既存directoryを削除または上書きしない。

生成caseは次の通り。

| case | input | driver | PCM | output |
|---|---|---|---|---|
| `sample1-auto` | `sampl1.muc` | auto | embedded | MUB/WAV/VGM/S98 |
| `sample1-mucom88` | `sampl1.muc` | `mucom88` | embedded | MUB/WAV/VGM/S98 |
| `sample1-mucom88e` | `sampl1.muc` | `mucom88E` | embedded | MUB/WAV/VGM/S98 |
| `sample1-mucom88em` | `sampl1.muc` | `mucom88EM` | embedded | MUB/WAV/VGM/S98 |
| `sample2-auto` | `sampl2.muc` | auto | skipped | MUB |
| `sample3-auto` | `sampl3.muc` | auto | skipped | MUB |
| `reference-mub` | `mucom88.mub` | MUB metadata | none | WAV/VGM/S98 |

offline出力は1秒、44.1kHzである。各caseを`run-a`と`run-b`へ生成し、artifactのsizeと
SHA-256が完全一致した場合だけ`candidate/`を作成する。stdout/stderrはCP932の可能性があるため、
変換せず`.bin`として保存する。

## candidateの検証と搬出

VM内またはmacOSへcopyした後、repository top directoryから次を実行する。

```sh
cmake \
  -DCANDIDATE_DIR=/path/to/work-20260923/candidate \
  -P tests/reference/windows-arm64-vm/mucom88-win-0.70/verify-manifest.cmake
```

検証後、`candidate/`の内容をこのdirectoryへcopyする。最低限、次をreviewする。

- `environment.json`のhost、guest、hypervisor、snapshot、locale、code page
- `inputs.json`が固定fixtureと一致すること
- `determinism.json`の全artifactが`identical: true`であること
- `manifest.json`内のMUB/WAV/VGM/S98構造値が妥当であること
- 全commandのexit codeが0であること
- `manifest.sha256`が搬出前後で一致すること

正式goldenへ昇格するときは、生成binaryを差し替えず、`TODO-WIN.md`のPhase 0と実施履歴へ
VM metadata、artifact hash、実行日を記録する。Windowsまたはhypervisor更新後に結果が変わった場合は、
旧goldenを上書きせず別baselineとして原因を調査する。
