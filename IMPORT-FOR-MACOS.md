# MUCOM88 macOS 移植調査

## 1. 結論

本リポジトリには、移植に利用できるクロスプラットフォームな C/C++ コアと SDL
抽象化層が既にある。したがって、最初の現実的な移植対象は `src/main.cpp` から作る
コマンドライン版である。MML のコンパイル、MUB の読み書き、fmgen による YM2608
エミュレーション、WAV/VGM/S98 出力は大部分を共有できる。

ただし、現状を「macOS 対応済み」とは判断できない。`xcode/miniosx` は 2019 年頃の
SDL 1.2/i386/macOS 10.6 用プロジェクトが途中の状態で残ったもので、現行 Xcode、
64 bit Intel、Apple Silicon のいずれにもそのまま使用できない。また、トップの
`src/Makefile` と `src/CMakeLists.txt` にも macOS で実際にビルドを妨げる不整合がある。

推奨する到達順は次の通り。

1. arm64/x86_64 の CLI（コンパイル、MUB 再生、WAV/VGM/S98 書き出し）
2. SDL2 によるリアルタイム音声再生と正常終了
3. `pcmtool` と自動テスト
4. 必要なら macOS ネイティブ GUI、プラグイン、実チップ対応

Windows GUI、HSP プラグイン、FM Tone Editor、SCCI2 は Win32 API に強く依存するため、
CLI 移植とは別プロジェクト相当の作業になる。

## 2. 調査基準

- 調査時点: 2026-09-12
- 対象コミット: `bf008a4` (`trial-import-for-macOS`、調査時の HEAD)
- 実機: Apple Silicon (`arm64`)、macOS 26.6.2、Apple clang 21.0.0
- ワークツリーには調査前から未追跡の `.vscode/` がある。本調査では変更していない。

実機確認では、標準の `make` は後述の理由で失敗した。一方、Homebrew の SDL2 用
include/link フラグと、未定義 `DWORD_PTR` に対する一時的なコンパイル時回避を与えると、
`src/main.cpp` の CLI は arm64 でリンクできた。その一時バイナリで
`package/sampl1.muc` を step/compile-only モードでコンパイルし、65,647 byte の MUB
を生成できた。これはコア処理が arm64 上で概ね動作することを示すが、正式な修正や
互換性試験の代わりにはならない。日本語 PCM 名は文字化けし、文字コード層の問題も
再現した。

## 3. リポジトリの構成と移植上の意味

| 領域 | 役割 | macOS での扱い |
|---|---|---|
| `src/cmucom.*` | 公開機能の中心。MML、MUB、音色、PCM、録音を統括 | 原則共有 |
| `src/mucomvm.*` | Z80/PC-8801 メモリ、ドライバ実行、FM I/O、時刻更新 | 原則共有 |
| `src/Z80/` | Portable Z80 エミュレータ | 共有 |
| `src/fmgen/` | YM2608/OPNA、PSG 等のソフトウェア音源 | 共有 |
| `src/bin_15`, `bin_17`, `bin_em` | 組み込み済み MUCOM ドライバ/データ | 共有、生成不要 |
| `src/utils/` | WAV/VGM/S98、PCM ツール、文字コード変換 | 一部修正 |
| `src/sdl/` | 音声、タイマー、最小プレイヤー、OS 抽象化 | SDL2 向けに修正して利用 |
| `src/win32/` | DirectSound、Win32 timer/thread、SCCI2、DLL loader | macOS では除外 |
| `src/dummy/` | 音声なしの OS 抽象化 | 現状はコンパイル不能、修正後テスト用 |
| `src/plugin/` | MUCOM88 DLL プラグイン ABI の共通構造 | ABI は共有可能だが loader は未実装 |
| `xcode/miniosx` | 旧 SDL 1.2 最小プレイヤー用 Xcode project | 廃止または全面再生成 |
| `hspplugin/` | HSP 製 Windows GUI と DLL bridge | Win32 専用、別途再設計 |
| `muplug_fmeditor/` | Win32 FM 音色エディタプラグイン | Win32 専用、別途再実装 |
| `FmToneEditorV2/` | Windows 音色エディタ | Win32 専用、別途再実装 |
| `pc8801src/` | オリジナル Z80 アセンブリ | ホスト OS 移植の対象外 |
| `package/` | Windows 配布物とサンプル/必須データ | `.exe`/`.dll` は除外、データは再利用 |

実行経路は概ね次の通りである。

`main.cpp` → `CMucom` → `mucomvm` → Z80 内の MUCOM ドライバ + `fmgen::OPNA`
→ `OsDependent`（Win32 / SDL / dummy）→ 音声デバイス、という構造になっている。
step モードではデバイスのタイマーを使わず、呼び出し側が `RenderAudio()` を進めるため、
オフライン WAV/VGM/S98 出力を先に移植しやすい。

## 4. 既存 macOS 対応資産の現状

### `xcode/miniosx` は現在のビルド定義として使えない

`xcode/miniosx.xcodeproj/project.pbxproj` には以下の固定値・破損参照がある。

- `SDKROOT = macosx10.6`
- `MACOSX_DEPLOYMENT_TARGET = 10.6`
- `ARCHS_STANDARD_32_BIT`、`VALID_ARCHS = i386`
- `CLANG_CXX_LIBRARY = libstdc++`
- `/Library/Frameworks/SDL.framework` の SDL 1.2
- 現在存在しない `src/wavout.cpp`, `src/wavout.h`, `src/bin_*.h` への参照
- 現在必要な `utils/wavwrite.cpp`, `utils/vgmwrite.cpp`, `utils/s98write.cpp`、
  `utils/codeconv/*.cpp` 等がターゲットに反映されていない

さらに旧 SDL 用の `SDLMain.m` を含んでいる。現行 SDL2 の CLI には通常不要である。
この project を差分修理するより、CMake を正として Xcode generator から作るか、新しい
project を生成し直す方が安全である。

### Makefile は Darwin を検出するが、実際の依存解決が古い

`src/Makefile.setting` は `uname -s == Darwin` を検出し `OS_OSX=1`、`STATIC=1` にする。
しかし `src/Makefile` は常に `sdl-config` を呼び出す。調査環境には Homebrew SDL2 の
`sdl2-config` はあるが SDL 1.2 の `sdl-config` はなく、標準手順は成立しない。

macOS だけを無条件に静的リンクへ寄せる設定も不適切である。Homebrew の SDL2 dylib、
アプリ bundle 内 framework、完全静的配布のどれを採るかを明示的な option にするべきで、
OS 判定から `STATIC=1` を暗黙設定しない方がよい。

### このリポジトリにおける SDL 1.2 と SDL2 の違い

SDL はライブラリ系列の名称でもあるため紛らわしいが、このリポジトリの
`SDL.framework`、`sdl-config`、`find_package(SDL)`、`SDLMain.m` は旧 SDL 1.2 を前提と
した指定である。一方、現在調査環境に導入されているのは SDL2 であり、build tool は
`sdl2-config`、library 名は `SDL2` である。SDL 1.2 と SDL2 は source/binary compatibility
が保証された同一製品の更新版ではなく、別 major version として扱う必要がある。

| 観点 | SDL 1.2（現リポジトリの想定） | SDL2（移植先として推奨） | 本プロジェクトへの影響 |
|---|---|---|---|
| build helper | `sdl-config` | `sdl2-config`、pkg-config の `sdl2`、CMake の SDL2 target | `Makefile` と CMake の探索を変更 |
| header/library | `SDL.h` / `SDL` | include path 経由の `SDL.h` または `SDL2/SDL.h` / `SDL2` | target include と link 設定を統一 |
| macOS entry point | project に `SDLMain.m` を加える旧方式 | 現行 SDL2 では独自 `SDLMain.m` を通常は使わない | `xcode/miniosx/SDLMain.*` を除外 |
| audio API | process-global な `SDL_OpenAudio` | 旧APIも残るが `SDL_OpenAudioDevice` が標準 | device ID と obtained format を管理する実装へ変更 |
| 複数device | 実質単一device中心 | device列挙・選択、複数deviceを扱える | 将来の出力device選択が可能 |
| audio format | desired format を暗黙に仮定しやすい | 実際に得た format/rate/channel の確認が容易 | 固定44.1 kHz/stereo/16 bit仮定を検証可能 |
| window/video | 単一surface中心 | 複数window、renderer、texture中心 | 現CLIにはほぼ無関係。GUI新設時に影響 |
| input/event | 旧event model | keyboard/text input、window/event体系を整理 | 現CLIでは終了処理程度。GUIでは書き換え必要 |
| platform support | i386 時代のmacOS資産 | 64 bit macOS/Apple Siliconで利用可能 | arm64/x86_64移植にはSDL2が現実的 |
| binary互換性 | SDL 1.2用binary/plugin | SDL2 ABI | SDL 1.2 binaryをSDL2としてlinkできない |

本体が実際に使う API は `SDL_Init`、audio callback、`SDL_AddTimer`、`SDL_GetTicks`、
`SDL_Delay` などに限られ、その多くは SDL2 にも存在する。このため、今回の一時 build では
source の大幅変更なしに SDL2 でリンクできた。ただし「コンパイルできる」ことと「安全に
SDL2へ移行済み」であることは同義ではない。特に以下は明示的に直す必要がある。

- `SDL_OpenAudioDevice` の戻り値を保持し、その device だけを pause/close する
- obtained `SDL_AudioSpec` が想定と違う場合、変換するか明確に失敗させる
- timer thread と audio callback 間の ring buffer を同期する
- `SDL_Quit()` で全 subsystem を一括終了せず、所有する subsystem/device を対称に解放する
- `miniplay` が window を作らないまま `SDL_QUIT` を待ち続ける設計を、signal、再生終了、
  または明示的な input によって終了できるようにする

なお、SDL3 はさらに API/命名や callback 周辺の変更が大きい。既存コードの変更量を抑えて
macOS CLI を成立させる目的では、まず SDL2 に揃えるのが妥当である。将来 SDL3 へ進む場合も、
SDL 呼び出しを `src/sdl/` target 内に隔離しておけばコアへの影響を限定できる。

### CMakeLists.txt も完成していない

`src/CMakeLists.txt` の主な問題は以下である。

- SDL 1.x 用 `find_package(SDL)` であり SDL2 を探索しない
- `utils/codeconv/codeconv_dummy.cpp` または iconv 実装が source list にないため、
  `CodeConvertDummy` の constructor 等が未解決になる
- `cmake_minimum_required(VERSION 2.8)` と、CMake 3.12/3.13 以降の
  `add_compile_definitions` / `target_link_options` が矛盾する
- CLI だけが定義され、`miniplay`、`pcmtool`、テスト、install、runtime data 配置がない
- warning、C++ standard、Universal Binary、deployment target の方針がない

## 5. 必須変更（CLI の移植）

### P0-1: Windows 互換型を固定幅型に置換する

最初に直すべき実コンパイル障害は `src/adpcm.h` と `src/adpcm.cpp` である。

- 非 Windows の `typedef unsigned long DWORD` は LP64 の macOS では 64 bit になる。
  Windows の `DWORD` および RIFF/WAVE の field は 32 bit なので `uint32_t` が必要。
- `DWORD_PTR` は非 Windows 側で定義されず、Apple clang でコンパイルエラーになる。
  ポインタを整数化せず、`uint8_t *` / `const uint8_t *` のポインタ演算に書き換える。
- RIFF/WAVE header を C struct に直接 cast すると、型幅、alignment、host endian に
  依存する。little-endian の 16/32 bit reader で byte 列から読む実装にする。
- chunk の範囲、overflow、奇数長 chunk の pad byte を検証する。
- magic 判定は現在 `!=` を `&&` で結んでおり、4 byte 全てが異なる場合しか拒否しない。
  `memcmp` 等に置換する。

Apple Silicon と Intel Mac はともに little-endian なので、現在の MUB header
`MUBHED`（`int`/`short`）は通常は同じ配置になる。しかし file format を native struct
の `sizeof` と直接書き出しに依存させる設計は将来も脆い。`static_assert` で期待サイズを
固定し、最終的には MUB も明示的な little-endian serialize/deserialize にする。

### P0-2: SDL2 を正式な依存としてビルドを作り直す

最小変更なら `src/sdl/` の SDL 1.2 API は SDL2 でも大部分が使用できる。実際に調査環境の
SDL2 header/library で CLI の全 object がコンパイル・リンクできた。必要な変更は以下。

- CMake を最低 3.20 程度に更新し、SDL2 の config package または pkg-config target を使用
- source から `#include <SDL.h>` を維持する場合は target include path で解決する。
  あるいは一貫して `<SDL2/SDL.h>` にする
- `USE_SDL` は target 単位の compile definition にする
- `SDLMain.m` と旧 `SDL.framework` 固定参照を除去
- arm64 と x86_64 を個別に CI build し、必要なら `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"`
  で Universal Binary を生成
- SDL2 を dylib として配布するなら `@rpath`、bundle 配置、codesign 対象を決める

CLI のみなら Homebrew 依存を README に明示する方法が最短である。自己完結した `.app` を
配る段階では SDL2 framework/dylib の同梱と署名・notarization が別途必要になる。

### P0-3: SDL OS 層の未実装とスレッド安全性を直す

`src/sdl/osdep_sdl.cpp` は音声再生の骨格はあるが、OS abstraction の複数メソッドが stub
のままである。

- `GetMilliseconds()` と `GetElapsedTime()` は常に 0、`ResetTime()` は no-op
- `GetDirectory()`、`ChangeDirectory()`、`KillFile()` は常に 0
- plugin 初期化・実行は no-op
- `GetStatus()` は常に 0
- `SetBreakHook()` は true を返すだけ、`GetBreakStatus()` は常に false。このため通常再生の
  `mucomvm::PlayLoop()` は内部条件では終了せず、Ctrl-C 時も graceful shutdown できない
- real chip 関数は成功風の値を返すが実処理をしない。macOS では `-s` を明示的に
  unsupported error とすべき

タイマー callback が ring buffer を書き、audio callback が同じ `WriteCount`、position、
buffer を同期なしで読む。これは C++ の data race である。SDL mutex/lock、single-producer
single-consumer 用 atomic index、または audio callback 主導設計のいずれかに変える。
終了時には timer callback と audio callback が止まったことを確認してから object を破棄する。

SDL2 では device ID を返す `SDL_OpenAudioDevice` を使い、desired/obtained format を確認する
実装が望ましい。現在は `SDL_OpenAudio` の obtained format を受け取らず、44.1 kHz、stereo、
signed 16 bit が必ず採用されたと仮定している。

### P0-4: 文字コード方針を統一する

macOS の path と terminal は UTF-8 が基本である。一方、MUCOM88 のコンパイラ本体と古い
メッセージ/音色名は Shift_JIS 系である。現状には次の不整合がある。

- `src/Makefile` は `USE_ICONV` 指定時に **`-DDUSE_ICONV`** と typo した definition を付ける
- `utils/codeconv/codeconv.h` が見るのは `USE_ICONV` なので dummy converter が選ばれる
- dummy converter は `bufSize` を無視して `strcpy` する
- iconv 実装も変換エラー、出力不足、descriptor 作成失敗を処理しない
- `mucomvm::Msgf()` は 4096 byte buffer に `vsprintf` しており overflow 可能
- 実機確認でも PCM16 の日本語名が文字化けした
- `MUCOM88UTF8` の有無で MML の multibyte 走査と MUB tag flag が変わるが、ビルドごとの
  入出力契約が文書化されていない

推奨方針は、CLI/API/path/tag の外部表現を UTF-8 に統一し、Z80 コンパイラへ渡す必要が
ある byte 列と legacy data 表示だけを境界で CP932/Shift_JIS 変換することである。macOS の
system iconv は調査環境で `SHIFT_JIS` と `CP932` alias を提供し、現ソースも単体コンパイル
できた。変換先は互換性要件を確認して `CP932` または `SHIFT_JIS` の一方に固定する。

### P0-5: build target と source list を整理する

新しい root CMake 構成を推奨する。

- `mucom88_core`: Z80、fmgen、CMucom、VM、format writer、plugin 共通構造
- `mucom88_os_sdl`: SDL2 OS implementation
- `mucom88`: CLI
- `miniplay`: 必要なら簡易プレイヤー
- `pcmtool`: PCM list/WAV → ADPCM bank
- test targets

Windows 専用 directory と bundled DirectX SDK (`src/lib`) は macOS target から完全に除外する。
`headers.h` は Win32 専用で、portable source から include されない状態を維持する。

## 6. 次に必要な修正

### P1: CLI とファイル処理

- `main.cpp` の option parser は引数を必要とする option でも `argv[b + 1]` の存在を確認しない。
- 入力名を `char fname[1024]` へ `strcpy` しており overflow 可能。
- `getcwd`、`chdir` と process-global current directory に依存する。特に
  `MucomModule::Open()` は working directory を変更して復元しないため、library として
  thread-safe ではない。`std::filesystem::path` と明示的な resource base directory に変える。
- default の `mucompcm.bin` と `voice.dat` は current directory 相対である。CLI 配布なら
  data directory option/探索順を定義し、`.app` なら `Contents/Resources` から解決する。
- `-r` の rhythm WAV も一時的な `chdir` ではなく path として fmgen に渡す。
- `fopen` は macOS の UTF-8 path で動くが、内部変換後の Shift_JIS 文字列を path に流さない。

### P1: PCM tool

`src/utils/pcmtool.cpp` には POSIX 分岐があり、基本方針は再利用できる。ただし以下を直す。

- `SplitPath()` は同じ buffer に対する `dirname()` と `basename()` の破壊的操作、拡張子なしで
  `strrchr()` が返す null、先頭の dot、複数 dot を安全に扱わない
- `PcmEntry::name[16]` へ `strcpy` して overflow 可能
- input file open 失敗後も null `FILE *` を `ConvertList()` へ渡す
- constructor の `header` / `body` 初期化を確認・修正
- WAV reader は P0-1 の安全な parser と共有する

### P1: テスト

既存 `src/tests` は standalone Makefile と手動生成 WAV が中心で、fixture の場所にも依存する。
最低限、次を arm64 と x86_64 で自動化する。

1. `package/sampl1.muc`～`sampl3.muc` の compile 成否
2. 生成 MUB の byte-for-byte golden test（意図的な header version 差は明示）
3. 生成 WAV の RIFF field、長さ、sample hash
4. MUB → PCM rendering の deterministic hash
5. Shift_JIS/UTF-8 の日本語 tag、error、voice/PCM name
6. mono/stereo、未知 chunk、奇数 chunk、truncate した WAV の ADPCM 変換
7. SDL audio device の open/close、timer stop、underrun（実機 test）
8. ASan/UBSan と ThreadSanitizer（SDL ring buffer）

`src/dummy/osdep_dummy.cpp` は現在 `OsDependentSdl::GetStatus` という誤った class 名で method を
定義し、`SetBreakHook` / `GetBreakStatus` を実装していない。そのため dummy target 自体が
abstract class の生成エラーになる。オフライン test backend として先に修正する価値がある。

### P1: 安全性と現行 clang warning

今回の試験では `mucomvm.cpp` の `vsprintf`、`cmucom.cpp` の `sprintf` が deprecated warning、
`fmgen/fmgen.cpp:752` が `|` と `?:` の優先順位 warning を出した。移植に伴う挙動変更を
避けるため、後者は意図を確認して括弧を付け、前者は長さ付き API にする。全体に古い
`strcpy`/`strcat` があるため、外部入力が到達する箇所から優先して除去する。

## 7. CLI 以外の機能差

### Windows DLL plugin

Windows 実装は `LoadLibrary` / `GetProcAddress` / `FreeLibrary` で DLL を読み、同一 process の
C++ object pointer を含む独自 ABI を渡す。SDL 実装は全て no-op である。

macOS で同じ機構が必要なら `dlopen` / `dlsym` / `dlclose` と `.dylib`/bundle を使えるが、
ABI version、architecture、symbol visibility、lifetime、codesign/hardened runtime を新たに
規定する必要がある。既存 Windows plugin binary は利用できない。初期 CLI リリースでは
`-a` を help に表示せず、指定時は明確に unsupported とするのが妥当。

### SCCI2 / real chip

`src/win32/realchip.*` は `scci2.dll` と Win32 ABI 専用である。macOS 版は SDL layer の空実装
しかなく、既存 SCCI2 binary は使えない。必要なら対象 hardware の公開 protocol/libusb
対応を個別実装する。初期移植の範囲外とする。

### GUI、HSP、FM Tone Editor

`hspplugin/hspmucom.cpp` と `muplug_fmeditor` / `FmToneEditorV2` は `windows.h`、window message、
GDI、MIDI/WinMM 等への依存が広い。`.hsp` GUI と同梱 DLL/EXE を macOS にリンクし直すだけでは
移植できない。GUI が要件なら、portable な `CMucom` API を library target にし、SwiftUI/
AppKit 等で editor/player/file dialog/preferences を新規実装する方が境界を明確にできる。
CoreMIDI 対応や FM editor はその後の独立 target とする。

### 音声 backend の選択

短期は SDL2 が最も変更量が少ない。配布サイズ、latency、Audio Unit integration が重要なら
後から `OsDependentCoreAudio` を追加できる。`OsDependent` interface が既にあるため、コアを
変えずに差し替えられる。ただし現在の interface は timer thread と audio producer を分離
する前提なので、CoreAudio callback 主導に合わせる場合は時刻更新設計も見直す。

## 8. 配布とライセンス

ルート `LICENSE` と README は本体を CC BY-NC-SA 4.0（非営利・表示・継承）として説明する。
fmgen には別条件があり、由来表示、free software としての配布、改変内容の明示、原文添付、
商用利用時の事前合意が要求されている。`package/sampl*.muc`、`mucompcm.bin`、`voice.dat` にも
古代祐三氏/Ancient の attribution 条件がある。Mac binary/app を配布する際は以下が必要。

- 本体ライセンスと fmgen 原文・credit を bundle/distribution に含める
- サンプル曲を含める場合は指定 attribution を含める
- SDL2 を同梱する場合は SDL2 の license notice を含める
- 改変した fmgen を配る場合は改変内容を記録する
- 商用配布は本体と fmgen 等の条件を別々に確認する

技術的には Developer ID 署名、hardened runtime、notarization、Universal Binary または
architecture 別 artifact が必要になる。CLI を Homebrew formula のみで配る場合も、data
file の install location と探索規則を決める必要がある。

## 9. 推奨実装順と完了条件

### Phase A: portable/offline core

対象: `adpcm.*`、dummy backend、codeconv、CMake、CLI path/argument。

完了条件: clean checkout から Apple clang で warning を把握可能な形で build でき、音声 device
なしで三つの sample を MUB/WAV/VGM/S98 に変換し、golden/hash test が通る。

### Phase B: SDL2 realtime player

対象: `src/sdl/*`、signal/break、ring buffer synchronization、device negotiation。

完了条件: arm64 と x86_64 で連続再生、停止、Ctrl-C、device open failure、終了時解放が正常。
underrun count と sanitizer test に既知の data race がない。

### Phase C: tool/package

対象: `pcmtool`、install/resource discovery、README、CI、license、署名。

完了条件: 任意 directory から CLI と data を利用でき、UTF-8 filename/tag を扱え、配布 archive
または Homebrew install の smoke test が通る。

### Phase D: optional native features

必要性を確認した上で CoreAudio、GUI、CoreMIDI、macOS plugin ABI、対応可能な real-chip backend
をそれぞれ独立して実装する。Windows GUI の完全同等性を Phase A～C の完了条件に含めない。

## 10. 現時点で未確認の事項

- Windows 公式 binary と生成 MUB/WAV の厳密な byte/sample 比較
- Intel Mac 実機または Rosetta での動作
- 実際の audio device での長時間再生、latency、underrun
- 日本語を含む実用 MML 一式での CP932/Shift_JIS/UTF-8 round trip
- rhythm WAV 一式を指定した再生
- sandboxed `.app`、署名、notarization
- 外部 plugin および real chip（現状 macOS 実装なし）

従って「コアは arm64 で動く見込みが高い」ことまでは実証済みだが、「互換な macOS 製品が
完成している」とはまだ言えない。最初の変更セットは Phase A に限定し、golden test で
Windows 互換性を固定してから realtime/GUI へ進むのが最も安全である。
