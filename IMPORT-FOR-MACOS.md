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
- `src/mucom88config.h` は非 Windows build で `MUCOM88UTF8` を自動定義する。このため
  macOS の MML/tag は UTF-8 扱いになるが、Z80 由来メッセージと legacy PCM/voice name は
  Shift_JIS のままであり、どの境界で変換するかが文書化されていない

推奨方針は、CLI/API/path/tag の外部表現を UTF-8 に統一し、Z80 コンパイラへ渡す必要が
ある byte 列と legacy data 表示だけを境界で CP932/Shift_JIS 変換することである。macOS の
system iconv は調査環境で `SHIFT_JIS` と `CP932` alias を提供し、現ソースも単体コンパイル
できた。変換先は互換性要件を確認して `CP932` または `SHIFT_JIS` の一方に固定する。

### P0-5: build target と source list を整理する

新しい root CMake 構成を推奨する。

- `mucom88_runtime`: Z80、fmgen、CMucom、VM、format writer、SDL2 OS implementation
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

## 11. macOS CLI 初期移植の具体案

この節は、GUI 等を含めない最初の実装作業の仕様と変更境界を確定するための検討結果である。
現段階では設計のみであり、この文書以外の source/build file は変更していない。

### 11.1 初期成果物と対応範囲

初期成果物は terminal から実行する単一の `mucom88` executable とする。`.app`、GUI editor、
Finder の document association、Quick Look 等は含めない。

| 利用形態 | 初期CLIでの扱い | 備考 |
|---|---|---|
| `.muc` → `.mub` compile | 必須 | `-g`、`-c`、拡張子自動判定を整理して維持 |
| `.muc`/`.mub` → WAV | 必須 | `-x -w` の offline step rendering |
| `.muc`/`.mub` → VGM/S98 | 必須 | `-x -b`、拡張子で writer を選択する現仕様を維持 |
| `.muc`/`.mub` realtime再生 | 必須 | SDL2、Ctrl-C/SIGTERMで正常停止 |
| tag情報表示 | 必須 | `-i`、音声deviceを初期化しない |
| driver 1.5 / 1.7 / EM | 必須 | 組み込み済みbinaryを利用 |
| `mucomDotNET` driver | 非対応 | macOS binary/sourceがこのrepositoryにない |
| DLL plugin (`-a`) | 非対応 | option指定時は成功扱いにせず、明確にエラー |
| SCCI2 real chip (`-s`) | 非対応 | Windows DLL/ABI依存。明確にエラー |
| 外部ROM (`-e`) | 条件付き維持 | legacy用途。必要fileと探索場所を明示する |
| rhythm WAV (`-r`) | 維持 | `chdir`依存を解消してdirectory pathを渡す |
| `miniplay` | 初期成果物から除外 | CLI本体と機能重複し、独自audio実装にも不具合がある |
| `pcmtool` | 次の変更単位 | CLI本体のacceptanceを阻害しないよう分離 |

ここで「必須」は初期リリース前の完成条件を意味する。最初の小さなpull requestでは build と
offline compile/render だけを成立させ、その後 realtime を追加してよい。

### 11.2 CLI modeを先に正規化する

現 `main.cpp` はoption解析中の `cmpopt` を、そのまま `CMucom::Init()` の runtime optionにも
渡す場合がある。しかし二つの名前空間は別物で、値が衝突している。

| bit | compile option側 | VM/runtime option側 |
|---:|---|---|
| 1 | `MUCOM_CMPOPT_USE_EXTROM` | `MUCOM_OPTION_FMMUTE` |
| 2 | `MUCOM_CMPOPT_COMPILE` | `MUCOM_OPTION_SCCI` |
| 8 | `MUCOM_CMPOPT_STEP` | `MUCOM_OPTION_STEP` |

例えば明示的な `-c -x` は `cmpopt == 10` を `Init()` へ渡すため、SDL実装では無処理とはいえ
SCCI bitまで立つ。`-e -x` はFM mute bitも立つ。また `.muc` 拡張子によるcompile判定は
`Init()` より後なので、同じcompileでも指定方法により初期化optionが変わる。

実装時は、argumentを一度 `CliOptions` のような構造へ全てparseしてから、次の独立値を生成する。

- `compile_options`: external ROM、compile、info等のCMucom compile制御
- `vm_options`: step、FM mute等の実行制御。macOS初期版ではSCCIを入れない
- `operation`: `Info` / `CompileOnly` / `OfflineRender` / `RealtimePlay`
- input/output/data/rhythm path

推奨mode決定規則は以下である。

1. `-i` は `Info`。他の出力・再生optionとの併用を拒否
2. `-g` は `CompileOnly`。入力は `.muc` または明示的 `-c` が必要
3. `-x` は `OfflineRender`。`VM_OPTION_STEP` を設定し、音声device/timerは作らない
4. 上記以外は `RealtimePlay`
5. `.muc` のcase-insensitive拡張子判定はparse完了後、初期化前に行う
6. `-w`/`-b`を指定して`-x`がない場合は、曖昧なrealtime録音にせずusage errorにする
7. `-l` は正の整数かつoffline時だけ許可し、上限を設定する

`CompileOnly` と `Info` もaudio不要である。現在の `-g` は通常の `mucom.Init()` を先に呼ぶため
audio/timerを開いてからcompileし、`-i`も同様である。これらはstep/no-audio初期化へ統一する。

### 11.3 option parserと終了code

初期移植で独自CLI frameworkを導入する必要はない。manual parserを維持する場合でも以下を満たす。

- value必須option (`-p`, `-v`, `-o`, `-w`, `-b`, `-a`, `-f`, `-l`, `-r`) は次の引数が
  存在し、optionではないことを確認してから読む
- `strcpy(fname, argv[b])` を廃止し、所有権の明確な `std::string` を使う
- 入力fileは1個だけ許可し、複数指定を最後の1個で黙って上書きしない
- unknown option、未対応の `-a`/`-s`、矛盾した組合せを明示する
- `-h`は成功終了、引数なしはhelpを表示した上でusage errorにする
- diagnosticはstderr、通常の情報または生成結果はstdoutへ分離する

初期CLIのexit codeは `0 = success`、`1 = compile/load/render/runtime error`、
`2 = usage/unsupported option` に固定する。現状のhelp/引数不足は `return -1` のためshellから
255に見え、automationに不向きである。

usageに最低限、次の実例を載せる。

```text
mucom88 -g -o song.mub song.muc
mucom88 -x -l 120 -w song.wav song.muc
mucom88 -x -l 120 -b song.vgm song.mub
mucom88 song.mub
mucom88 -i song.muc
```

### 11.4 初期CMake構成

最初の実装では `src/CMakeLists.txt` を唯一の正規build定義として更新し、実行方法を
`cmake -S src -B build` に揃える。旧Xcode projectを修理しない。Xcodeが必要な開発者は
`-G Xcode` で生成する。

採用案は次の通り。

- minimum CMake: 3.20
- language: C11およびC++17、compiler extensionは原則off
- dependency: `find_package(SDL2 CONFIG REQUIRED)` と `find_package(Iconv REQUIRED)`
- link: `SDL2::SDL2` と `Iconv::Iconv`。main wrapper targetが提供される環境だけ
  `SDL2::SDL2main`を条件付きで扱うが、macOS独自の旧`SDLMain.m`は使わない
- 初期段階ではsourceを`mucom88_runtime` static libraryと`mucom88` executableへ分離
- macOSで`MUCOM88WIN`を定義しない。`mucomvm.cpp`をcompileするruntime targetに
  `USE_SDL=1`を定義
- `codeconv_iconv.cpp`を選択し、dummyと同時にlinkしない
- warningは当初`-Wall -Wextra -Wpedantic`を可視化するが、既存warningを全て即時
  `-Werror`にはしない
- install先は最初はexecutableのみ。data配置規則が確定した変更でinstall ruleを追加

開発者向けnative buildはarchitectureを指定せずhost nativeとする。release/CIでは以下を分ける。

```text
# Apple Silicon native
cmake -S src -B build-arm64 -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0

# Universal Binary
cmake -S src -B build-universal -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
```

deployment target 11.0はApple Siliconを含む最小の初期案であり、製品としてどこまで古いmacOSを
支援するかにより最終決定する。Universal buildではSDL2 dependency自体も両architectureを
含む必要がある。arm64だけのHomebrew SDL2をlinkしてUniversal化することはできない。

既存Makefileは同じ変更内で無理に全面改修しない。CMakeでacceptanceが通った後、削除するか、
SDL2/pkg-config対応の薄い互換入口として維持するかを決める。二つのsource listを手管理し続けない。

### 11.5 CMake targetへ含めるsource

`mucom88_runtime`へ含める範囲は以下である。

```text
membuf.cpp adpcm.cpp md5.c soundbuf.cpp cmucom.cpp mucomvm.cpp
mucomerror.cpp callback.cpp osdep.cpp plugin/plugin.cpp
Z80/Z80.cpp
fmgen/file.cpp fmgen/fmgen.cpp fmgen/fmtimer.cpp
fmgen/opm.cpp fmgen/opna.cpp fmgen/psg.cpp
utils/vgmwrite.cpp utils/s98write.cpp utils/wavwrite.cpp
utils/codeconv/codeconv_iconv.cpp
sdl/osdep_sdl.cpp sdl/audiobuffer.cpp sdl/audiotime.cpp
```

`mucom88` executableは`main.cpp`だけをcompileし、`mucom88_runtime`へlinkする。現在の
`mucomvm_os.h`は`mucomvm.cpp`のcompile時に`USE_SDL`を見て`OSDEP_CLASS`を決めるため、SDL source
だけへdefinitionを付けても動かない。runtime target全体にprivate definitionとして付与する。
この構成は最小移植用であり、将来backendをruntime injectionする場合はportable coreと
platform backendを循環依存なしに分離する変更を別途行う。

次は初期targetへ含めない。

- `src/win32/**`、`src/lib/**`
- `src/dummy/**`（別のheadless test targetを作る時に修正して利用）
- `src/sdl/miniplay.cpp`、`src/sdl/audiosdl.cpp`
- `src/module/**`（library APIのtestを追加する段階で分離）
- `src/utils/pcmtool*`、`pcmentry*`

### 11.6 SDL2 backendの具体設計

CLI本体が使うのは `OsDependentSdl` であり、`AudioSdl` は`miniplay`専用である。初期移植では
重複する二つを同時に直さず、`OsDependentSdl`だけを対象にする。

`OsDependentSdl`には次のstateを持たせる。

- `SDL_AudioDeviceID device_id`（0なら未open）
- `SDL_TimerID timer_id`（0なら未登録）
- subsystemの初期化状態
- shutdown中であることを示すatomic flag
- Ctrl-C/SIGTERM用の`volatile sig_atomic_t`相当のbreak flag
- thread-safeなaudio queue/ring buffer

初期化・解放順を固定する。

1. realtime modeの時だけ `SDL_InitSubSystem(SDL_INIT_AUDIO | SDL_INIT_TIMER)`
2. desired specをzero初期化して `SDL_OpenAudioDevice`
3. obtained specのfrequency、format、channelを確認
4. callback/userdataを完全に設定してからtimerを追加
5. bufferをprefillしてからdeviceをunpause
6. 終了時はshutdown flag → timer削除 → audio device lock/close → buffer破棄
7. 自分が初期化したsubsystemだけ `SDL_QuitSubSystem` で解放

初期版はformat conversionを増やさず、obtained specが44.1 kHz、`AUDIO_S16SYS`、stereoでない場合は
理由付きで失敗させる方が挙動を固定しやすい。後で`SDL_AudioStream`を使う変更を独立して行える。

ring bufferは producer（timer）一つ、consumer（audio callback）一つである。最小の安全案は
SDL mutexでindex/countを保護することだが、audio callback内で長く待たないようcopy対象範囲だけを
短時間lockする。より厳密にはread/write indexをatomic化したSPSC bufferへ置換する。
現在の公開`WriteCount`等を複数threadが直接更新する状態は残さない。

`SetBreakHook()`はsignal handlerを登録し、handler内ではflag更新以外を行わない。
`PlayLoop()`側が20 msごとにflagを確認して抜け、通常のdestructor経路でtimer/device/FMを止める。
曲の自然終了は現在の`playflag`だけでは判定できないため、初期仕様は「Ctrl-Cまで再生」を維持し、
自動終了は別途driver statusの定義後に追加する。

### 11.7 offline modeをaudio deviceから分離する

`-g`、`-i`、`-x`はheadless環境でも動くことを完了条件にする。二つの実装案を比較すると、初期CLI
では案Aを推奨する。

- 案A: `OsDependentSdl`のconstructorではSDLを初期化せず、`InitAudio`/`InitTimer`でlazy init。
  step modeではこれらが呼ばれないためdevice不要。変更が小さい
- 案B: `OsDependentDummy`を修正し、modeごとにbackendを注入。長期設計は明快だが、現行
  `OSDEP_CLASS` macroと`CMucom` constructor/APIの変更範囲が広がる

案Aでもoffline renderingは`CMucom::RenderAudio()`が時刻を進め、fmgenのsampleを生成するため
機能する。今回もこの経路で1秒のWAV/VGMを生成できた。

### 11.8 file/resource path規則

初期CLIではprocess-global `chdir`を通常処理から排除する。path解決規則を次で固定する。

1. `-p`/`-v`/`-r`で明示されたpath
2. MML tagの`#pcm`/`#voice`はMML fileがあるdirectoryからの相対path
3. tagがない場合のdefault dataは入力file directory
4. 互換用fallbackとして起動時current directory
5. 将来installする場合のみcompile-time data directory（例: `share/mucom88`）

絶対pathはそのまま使い、正規化には`std::filesystem::path`を用いる。`-r`はseparatorを末尾に
要求せず、OPNAへdirectoryを明示する。現在はOPNA初期化前に`rhythmdir`へ`chdir`し、その後
復元するため、失敗時の復元、並行利用、相対input pathが不安定である。

日本語file nameはmacOS側ではUTF-8 byte列として`fopen`へ渡す。Z80/Shift_JISメッセージ変換と
filesystem pathを同じconverterへ通さない。Unicode normalization（NFC/NFD）が異なるfile名は
macOS test fixtureを一つ用意して確認する。

### 11.9 入力検証とエラー伝播

CLIは外部fileを直接扱うため、単にmacOSでlinkできるだけでなく、最初の配布前に以下を直す。

- `mucomvm::LoadAlloc`: 0 byte fileでも`fclose`し、`fseek`/`ftell`/`fread`失敗と`INT_MAX`超過を検査
- `mucomvm::LoadPcm`: 現在はfileがなくても常に0を返す。load結果を`CMucom::LoadPCM`へ返す
- `LoadPcmFromMem`: 最低0x400 byte、table entry範囲、ADPCM RAM上限を検査してからcopy
- `LoadFMVoice`: 読み込んだsizeが`MUCOM_FMVOICE_SIZE`未満なら固定長`memcpy`しない
- `LoadMusic`/`MUBGet*`: buffer sizeを渡し、header、offset、size、加算overflow、埋込PCM範囲を検査
- `SaveToFile`/writer: short write、`fclose`、header update失敗を上位へ返す
- WAV/ADPCM: P0-1記載の固定幅little-endian readerへ置換
- `WavWriter::WriteHeader`: RIFF chunk sizeは現在`PCM byte数 + 44`だが、仕様上はfile sizeから
  8 byteを引いた`PCM byte数 + 36`。header fieldを修正
- `CMucom::Record`: 16 frame単位で常に加算するため、rate×秒数が16の倍数でなければ末尾を
  overshootする。最終blockを残frame数に縮める
- `SetWavFilename`/`SetLogFilename`: writerの`Open()`失敗がCLIへ返らないため、戻り値を伝播

SDL buildでは`mucomvm::AddPlugins()`全体が`#ifndef USE_SDL`で除外され、最後に0を返す。そのため
現在のmacOS相当buildで`-a`を指定すると「何もしない成功」になる。同様にSCCI系SDL methodも
stubである。CLI parse時にunsupportedとしてexit 2にし、下層の成功風stubへ到達させない。

### 11.10 文字コードの具体方針

非Windowsでは`MUCOM88UTF8`が自動定義されるため、MMLのUTF-8 multibyte走査とMUB2 tagの
`MUCOM_FLAG_UTF8TAG`は維持する。一方、Z80 compilerが生成するShift_JIS messageとlegacy
PCM table nameは表示前にiconvする。

- CMakeでは`USE_ICONV=1`を正しく定義し、`codeconv_iconv.cpp`だけをlink
- converterはCP932入力→UTF-8出力を基本とし、必要ならstrict Shift_JISとの差をfixtureで確認
- `iconv_open == (iconv_t)-1`、`E2BIG`、`EILSEQ`、`EINVAL`を処理
- 変換不能byteは方針を決めてreplacement表示し、compile data自体は変更しない
- `vsprintf`を`vsnprintf`へ変更し、変換前後のbuffer終端を保証
- PCM nameの今回の文字化けをregression testにする

`src/Makefile`の`-DDUSE_ICONV` typoはCMake移行で迂回するだけでなく、Makefileを残す判断をした時点で
修正する。二つのbuild方式で文字コード挙動が変わる状態を許容しない。

### 11.11 実装変更単位

レビューと互換性確認をしやすくするため、以下の順で分割する。

1. **CLI-1: build + LP64** — `src/CMakeLists.txt`、`adpcm.h/.cpp`、codeconv source選択。
   arm64でcompile/linkし、`-x`のsample生成を確認
2. **CLI-2: parser/mode** — `main.cpp`。compile/runtime optionを分離し、headless `-g/-i/-x`、
   exit code、unsupported optionを確定
3. **CLI-3: input safety** — `mucomvm.cpp`、`cmucom.h/.cpp`、format reader。
   PCM/voice/MUB/WAVのsize・offset・I/O errorを伝播
4. **CLI-4: SDL2 realtime** — `sdl/osdep_sdl.h/.cpp`、`sdl/audiobuffer.h/.cpp`、必要なら
   `audiotime.*`。device API、同期、signal、shutdownを実装
5. **CLI-5: path/data** — `main.cpp`、`cmucom.*`、`fmgen/opna.*`。`chdir`を除去しpath規則を実装
6. **CLI-6: test/install/doc** — CTest、fixture/golden、README、install/package/license notice

各変更単位でWindows buildを壊していないことも確認する。固定幅file parserやpath resolverは
共通化し、`#ifdef __APPLE__`をportable coreへ散在させない。

### 11.12 CLI acceptance matrix

今回、一時的なSDL2 build（source未変更、`DWORD_PTR=uintptr_t`をcompiler optionで回避）で得た
baselineは次の通りである。これは正式goldenではなく、修正後の意図しない差を見つける比較材料とする。

| 入力/操作 | 結果 |
|---|---|
| `package/sampl1.muc`をcompile | MUB 65,647 byte、成功 |
| 同曲を1秒offline WAV出力 | RIFF PCM、44,100 Hz、16 bit、stereo、176,492 byte。ただし12 frame超過し、RIFF size fieldも8 byte過大 |
| 同曲を1秒VGM出力 | VGM 1.70、YM2608、2,396 byte |
| PCM table message | PCM16の日本語名が文字化け（既知の失敗baseline） |

修正後は最低限、次のmatrixをCIまたは実機testで満たす。

| case | 期待結果 |
|---|---|
| clean arm64 configure/build | warningを記録し、link成功 |
| x86_64またはUniversal build | architecture整合を`file`/`lipo -info`で確認 |
| `-h` | help、exit 0 |
| 引数なし | helpとusage diagnostic、exit 2 |
| value不足、unknown option、複数input | diagnostic、exit 2、生成物なし |
| `-a`、`-s` | unsupported diagnostic、exit 2 |
| `-g` sample 1～3 | audio deviceなしでMUB生成、exit 0 |
| `-i` MUC | audio deviceなしでUTF-8 tag表示、exit 0 |
| `-x -w/-b` | 指定秒数ちょうど、header/length/hash検証、exit 0。1秒stereo 16 bit WAVは44 byte header込み176,444 byte |
| realtime MUB | SDL2再生、Ctrl-C後exit 0、hang/crashなし |
| 存在しないMUC/MUB/PCM/voice | 適切なerror、exit 1 |
| truncate/不正MUB、短いPCM/voice、壊れたWAV | OOBなし、error、exit 1 |
| 日本語tag/name/path | UTF-8表示とfile open成功 |
| ASan/UBSan | sample compile/renderでerrorなし |
| TSanまたは同等検査 | realtime bufferのdata raceなし |

### 11.13 CLI初期移植で保留する判断

実装を開始する前に製品要件として最終確認が必要なのは以下だけである。技術調査上のblockerではなく、
上記推奨値で着手可能である。

- 最低対応macOSを11.0とするか、より新しくするか
- releaseをUniversal単一binaryにするか、arm64/x86_64別配布にするか
- dependencyをHomebrew前提にするか、SDL2を配布物へ同梱するか
- realtime再生をCtrl-Cまでとするか、非loop曲の自然終了検出も初期要件に含めるか
- default PCM/voice dataをinstall対象へ含めるか、利用者が明示指定する方式にするか

推奨defaultは「macOS 11.0、Universal release、開発時Homebrew・配布時SDL2同梱、初期はCtrl-C停止、
`mucompcm.bin`と`voice.dat`をlicense/attribution付きでdata directoryへ配置」である。

## 12. 初期CLI移植の実装結果

2026-09-12に、11章の具体案に基づく初期CLI移植を実装した。今回の範囲はCLI、SDL2 backend、
入力安全性、文字コード、build定義および利用手順であり、Windows GUI、HSP plugin、FM Tone Editor、
SCCI実chip、旧Xcode projectの再生は対象外である。

### 実装済み

- CMakeを3.20/C++17へ更新し、SDL2 config packageまたはpkg-config、macOS iconvをtarget単位でlink
- Makefileを`sdl2-config`へ移行し、Darwinの暗黙static linkと`-DDUSE_ICONV` typoを修正
- WAV/ADPCM parserを固定幅型と明示的little-endian readerへ変更し、chunk境界とpadを検証
- CLI optionの値不足、競合、複数入力、非対応plugin/SCCIを検査し、usage errorをexit 2へ統一
- compile-only、情報表示、offline renderingではSDL audio deviceを開かないlazy初期化を実装
- SDL2 device IDとobtained formatを管理し、timer/device/subsystemを所有順の逆順で解放
- Ctrl-C/SIGTERMをflag経由で処理し、通常のdestructor経路でrealtime再生を終了
- audio ring bufferをmutexで同期し、callbackのunderflowとframe境界を修正
- CP932からUTF-8へのiconv変換、変換不能byte、buffer不足、descriptor失敗を処理
- PCM、FM voice、MUB header/offset/size、file read/writeの検証とerror伝播を追加
- WAVのRIFF sizeと録音末尾blockを修正し、指定秒数ちょうどのsample数を出力
- 入出力pathを`std::filesystem`で解決し、入力file directoryをtag内相対resourceの基準に設定

### 実機確認結果

Apple Silicon/macOS 26.6.2、Apple clang 21、Homebrew SDL2/iconvの環境で、Makefileによるarm64
clean buildがwarningなしで成功した。CMake executableは調査環境に未導入のため、CMake configureは
未実行である。

| case | 結果 |
|---|---|
| `-h` / 引数なし / option値不足 / `-s` | exit 0 / 2 / 2 / 2 |
| `package/sampl1.muc` compile | 65,647 byte、移植前baselineとMD5一致 |
| sample 1～3 compile | 全て成功（65,647 / 3,886 / 1,309 byte） |
| 1秒WAV | 176,444 byte、44.1 kHz、16 bit、stereo、RIFF/data size正常 |
| 1秒VGM | 2,396 byte |
| legacy日本語PCM名 | `ｺｰﾗｽ`をUTF-8 terminalへ正常表示 |
| realtime再生 | SDL dummy deviceで開始し、Ctrl-C後exit 0、hangなし |

### 残課題

- CMake configure/build、x86_64およびUniversal Binary、最低対応macOSでの検証
- ASan/UBSanおよびTSanを含む自動test/CTestと、不正MUB/WAV/PCM fixtureの常設
- process-global `chdir`の完全排除。現実装はtag相対path互換のため処理全体を入力directoryで実行し、
  rhythm directoryへの変更だけを初期化中に限定して必ず復元する
- install後のdefault data directory、SDL2同梱、`@rpath`、codesign、notarization
- 曲の自然終了検出。初期仕様は従来通りCtrl-C停止
- `pcmtool`のpath/name境界修正と独立target化

## 13. macOS CLIのビルド手順

### 13.1 前提環境

初期CLI移植では、64 bit macOS、Apple clang、SDL2、iconvを使用する。開発環境の依存関係は
Homebrewで導入する方法を標準とする。

```sh
xcode-select --install
brew install cmake sdl2
```

`xcode-select --install`で既にCommand Line Toolsが導入済みの場合、再インストールは不要である。
iconvはmacOS SDKにも含まれるが、Makefile buildでは環境に応じてHomebrewのlibraryが使用される。
以下でtoolとSDL2を確認できる。

```sh
clang++ --version
cmake --version
sdl2-config --version
```

旧SDL 1.2の`SDL.framework`、`sdl-config`、`xcode/miniosx` projectは使用しない。

### 13.2 CMakeによる推奨ビルド

repositoryのtop directoryで次を実行する。

```sh
cmake -S src -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

生成されるCLI executableは通常`build/mucom88`である。build directoryを作り直す場合は、既存の
成果物が不要であることを確認してから別のbuild directoryを指定するか、既存directoryを削除する。
source tree内で直接CMakeを実行するin-source buildは行わない。

Debug buildは独立したdirectoryを使う。

```sh
cmake -S src -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug --parallel
```

任意のprefixへinstallする場合は次のように指定する。現在のinstall targetはCLI executableのみで、
`mucompcm.bin`、`voice.dat`、SDL2 dylibの配置はまだ自動化されていない。

```sh
cmake -S src -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/stage"
cmake --build build --parallel
cmake --install build
```

### 13.3 Makefileによるビルド

CMakeを使用しない場合は`src` directoryでbuildする。SDL2は`sdl2-config`から検出される。

```sh
cd src
make
```

生成物は`src/mucom88`である。compilerやSDL2 config helperを明示する場合は次のように指定できる。

```sh
make CC=clang CXX=clang++ SDL_CONFIG=sdl2-config
```

通常の成果物を削除する場合は`src` directoryで`make clean`を実行する。ただし同directory内の
対象名と`objs` directoryが削除対象になるため、利用者自身の同名fileを置かないこと。

### 13.4 最小動作確認

以下はrepository top directoryで、CMake buildのexecutableを使う例である。Makefile buildでは
`./build/mucom88`を`./src/mucom88`へ読み替える。

```sh
./build/mucom88 -h
./build/mucom88 -g -o /tmp/sampl1.mub package/sampl1.muc
./build/mucom88 -x -l 1 \
  -w /tmp/sampl1.wav \
  -b /tmp/sampl1.vgm \
  /tmp/sampl1.mub
```

期待する結果は、helpがexit 0、compileとoffline renderがexit 0である。`sampl1.mub`は65,647 byte、
1秒WAVは176,444 byte（44.1 kHz、16 bit、stereo）になる。VGMは実装確認時点で2,396 byteだった。

リアルタイム再生は次のように起動し、Ctrl-Cで終了する。

```sh
./build/mucom88 package/sampl1.muc
```

compile-only（`-g`）、情報表示（`-i`）、offline output（`-x`）はaudio deviceを開かないため、
GUI sessionや音声出力のないCI環境でも使用できる。

### 13.5 architectureの確認とUniversal Binary

通常のbuildは実行中macOSのnative architectureを生成する。生成物は次で確認する。

```sh
file build/mucom88
lipo -info build/mucom88
```

arm64とx86_64を含むUniversal Binaryを試作する場合は、CMake configure時にarchitectureを指定する。

```sh
cmake -S src -B build-universal \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build-universal --parallel
lipo -info build-universal/mucom88
```

この指定では、SDL2を含む全link dependencyが両architectureを収録している必要がある。Homebrewの
通常installが片方のarchitectureしか提供しない場合、Universal linkは失敗する。Universal Binary、
最低対応macOSの`CMAKE_OSX_DEPLOYMENT_TARGET`、SDL2同梱、codesign、notarizationは未検証であり、
release配布前に別途確認する。

## 14. miniplay音声処理のSDL2 device API移行

2026-09-21に、通常CLIとは別系統である`make mini`用の`AudioSdl`をSDL2 device APIへ移行した。
対象fileは`src/sdl/audiosdl.h`、`src/sdl/audiosdl.cpp`、呼び出し元の
`src/sdl/miniplay.cpp`である。通常CLIが使用する`OsDependentSdl`の実装は変更していない。

### 14.1 実装内容

- process-globalな`SDL_OpenAudio`を`SDL_OpenAudioDevice`へ置換し、返された
  `SDL_AudioDeviceID`を`AudioSdl`が保持
- `SDL_PauseAudio`と`SDL_CloseAudio`を、それぞれ`SDL_PauseAudioDevice`と
  `SDL_CloseAudioDevice`へ置換
- desired/obtainedの`SDL_AudioSpec`をzero初期化し、44.1 kHz、`AUDIO_S16SYS`、stereoという
  `MucomModule::Mix()`の前提を検査。不一致の場合は自動変換せず理由付きで失敗
- audio callbackのframe数を、16 bit stereoのbytes per frameから算出
- `SDL_AddTimer`の戻り値を`SDL_TimerID`として保持し、登録失敗を`Open()`の失敗として伝播
- audioおよびtimer subsystemのうち`AudioSdl`自身が初期化したものだけを記録し、
  process全体への`SDL_Quit()`を廃止して`SDL_QuitSubSystem()`で対称的に解放
- atomicなshutdown flagを追加し、終了開始後のtimer producerとaudio callbackによる処理を抑止
- timer callback専用mutexを追加し、timer削除時点ですでに実行中だったcallbackの完了を待機
- 終了順序を「shutdown設定 → timer削除 → 実行中timer callback完了待ち → device pause/close
  → subsystem解放」に固定
- `AudioSdl` destructorからも`Close()`を呼び、複数回の`Close()`を安全なno-opとして処理
- `miniplay`でmodule/audio初期化失敗をexit 1へ伝播し、pointerをnull初期化して部分初期化時も安全に解放
- SIGINT/SIGTERM handlerでは`sig_atomic_t` flagだけを更新し、event loopから通常のdestructor経路で終了

移行後、`src`以下の実行codeには`SDL_OpenAudio`、`SDL_PauseAudio`、`SDL_CloseAudio`の旧API呼び出しは
残っていない。CLIとminiplayの両方がdevice IDを持つSDL2 APIを使用する。

### 14.2 検証結果

Apple Silicon/macOS 26.6.2、Apple clang 21、Homebrew SDL2/iconvの環境で次を確認した。

| case | 結果 |
|---|---|
| `make mini` | arm64 executableのcompile/link成功、warningなし |
| SDL dummy driver | sampleのcompileと再生開始に成功 |
| SIGINT | Ctrl-C後exit 0、hangなし |
| 存在しないSDL audio driver | diagnosticを出しexit 1、hangなし |
| 旧audio API検索 | `src`以下で呼び出し0件 |
| 通常CLI回帰 | arm64のcompile/link成功、`-h`がexit 0 |
| ASan/UBSan build | compile/link成功、warningなし |

ASan/UBSan版の実行は、main到達前のmacOS LaunchServices/XPC errorにより完了していない。またmacOSの
当該AddressSanitizer runtimeではLeakSanitizerの`detect_leaks`が非対応だった。このため、sanitizerの
実行結果を移行完了の根拠には含めず、通常buildのSDL dummy driver試験を現時点のruntime確認とする。
TSanによるtimer/audio callback間のdata race検査も未実施であり、CI整備時の残課題である。

### 14.3 利用方法と注意点

miniplayはCMake targetにはまだ含まれていないため、`src` directoryでMakefileからbuildする。

```sh
cd src
make mini
./miniplay ../package/sampl1.muc
```

終了はCtrl-Cで行う。現行`MucomModule::Open()`はprocess-globalなcurrent directoryを使用し、compile時に
入力MUCのdirectoryへ`mucom88.mub`を生成する。既存の同名fileを上書きする可能性があるため、試験時は
MUC、PCM、voice dataを専用の一時directoryへcopyして実行すること。出力pathの明示化と`chdir`の排除は、
miniplay/module系統に残る別の移植課題である。

### 14.4 miniplayのPCM欠落修正

SDL2 device API移行後、repository rootや`src`から`package/sampl1.muc`を指定するとFM音源は鳴るが
PCM音声が鳴らない問題を確認した。原因はaudio backendではなく、resource pathとerror伝播にあった。

- `sampl1.muc`は`#pcm mucompcm.bin`と`#voice voice.dat`をMUC相対で指定する
- 旧`miniplay`は常に`MucomModule::Open(".", filename)`を呼び、起動時current directoryから
  `mucompcm.bin`を探索していた
- `MucomModule::Open()`は`CMucom::LoadPCM()`の失敗を無視してcompileを続行していた
- `CMucom::SaveMusic()`もtag指定PCMを開けない場合に埋め込みを省略し、成功を返していた
- その結果、`pcmdata=0`、`pcmsize=0`のMUBを生成し、正常再生に見える状態でPCMだけが無音になった

次の修正を実装した。

- `miniplay`で入力pathを絶対pathへ変換し、親directoryとfile nameを分離して`MucomModule::Open()`へ渡す
- PCMとvoiceの相対pathを入力MUCのdirectory基準で解決
- 作業directoryへの移動失敗、PCM読み込み失敗、明示voice読み込み失敗を`Open()`の失敗として伝播
- `CMucom::SaveMusic()`で`#pcm`指定fileを開けない場合はMUBを保存せずcompile errorを返す
- `MucomModule::mucom`をnull初期化し、部分初期化で失敗した場合も安全に解放

修正後は起動時current directoryに依存せず、入力MUCと同じdirectoryにあるPCM/voiceを読み込む。
一時directoryへsample一式を配置し、別directoryから絶対pathで起動して次を確認した。

| case | 結果 |
|---|---|
| `sampl1.muc`のcompile | PCM 16 entryを読み込み、成功 |
| 生成MUB | 65,647 byte、`pcmdata=2,007`、`pcmsize=63,640` |
| SDL dummy driver再生 | 再生開始成功、Ctrl-C後exit 0 |
| PCMあり/なしMUBの5秒WAV | SHA-256が異なり、PCMあり側だけPCM table読込を確認 |
| `mucompcm.bin`欠落 | diagnostic、exit 1、MUB生成なし |
| `-k`でPCM preloadを省略しtag PCMも欠落 | compileがexit 1、MUB生成なし |
| 通常CLI回帰 | arm64 compile/link成功、warningなし |

## 15. SDLリアルタイム再生のプチノイズ対策

2026-09-21に、`build/mucom88`では発生し、同じSDL2を使用する`miniplay`では再現しない
プチノイズについて、前節までの調査結果に基づく対策を実装した。変更対象は
`src/sdl/audiobuffer.h`、`src/sdl/audiobuffer.cpp`、`src/sdl/osdep_sdl.h`、
`src/sdl/osdep_sdl.cpp`、`src/sdl/audiosdl.h`、`src/sdl/audiosdl.cpp`である。

### 15.1 原因と対策方針

CLI側の旧`OsDependentSdl::SendAudio()`には、次のsample欠落経路があった。

- `TickToSamples()`で算出した未生成sampleの一部だけを書いた場合も、`ClearTick()`が未生成分を
  すべて破棄していた
- ring bufferが満杯のときは経過時間を加算する前にreturnするため、その時間に相当するsampleを
  生成しなかった
- timerの遅延などでring bufferが空になると、audio callbackが残りを0で埋めた。波形途中への0挿入が
  不連続を作り、クリック状ノイズになる可能性が高かった
- audio deviceをring bufferの初回充填前から開始しており、起動直後もunderflowし得た

`miniplay`側は`ClearTick()`を呼ばないためCLIよりbuffer残量を維持しやすく、同じ問題が表面化しにくい
状態だった。CLIとminiplayは同じSDL2 dylibをlinkしていたため、SDL library差は原因から除外した。

対策後は、初回充填およびunderflow後の再充填だけをSDL timerの経過時間からsample数へ変換し、再生開始後は
audio callbackが実際に消費した空き容量を補充する。これにより、非real-timeなSDL timerの短時間の遅延を
次回以降の補充で回復でき、device clockとの微小な差を未生成sampleとして際限なく蓄積することも避ける。

### 15.2 実装内容

- `AudioBuffer::Write()`を実際に書けたframe数を返すAPIへ変更
- `ClearTick()`を廃止し、実際に書けたsample数だけを差し引く`ConsumeSamples()`へ変更。小数sampleと
  未生成分を保持
- ring bufferが満杯でも、初回充填中または再充填中は経過時間を先に加算
- 未生成sampleの異常な増加をbuffer容量で制限し、超過量をdiagnostic用に記録
- audio deviceをpause状態でopenし、ring bufferがhigh-water markへ達してから再生開始
- 再生開始後は、callbackが消費してできた空き容量を最大1 blockずつ補充
- underflowを検出した場合は送出を停止して0を返し、high-water markまで再充填してから送出を再開
- CLI終了時、値が0でない場合だけunderflow回数と破棄sample数を標準errorへ表示
- 同じ`AudioBuffer`を使うminiplayにも、初回充填、device-clock基準の補充、underflow後の再充填を適用

ring bufferのhigh-water markは従来の`BufferSize - BlockSize`を維持する。現在の定数ではstereoの
14,336 sample、44.1 kHz換算で約162.5 msである。出力callbackは1,024 frame（約23.2 ms）単位、producerは
1回につき最大2,048 sampleを生成する。

### 15.3 検証結果

Apple Silicon/macOS 26.6.2、Apple clang 21、Homebrew SDL2/iconv環境で次を確認した。

| case | 結果 |
|---|---|
| CMake `build/mucom88` | compile/link成功。変更箇所以外に既存のunused parameter warningあり |
| Makefile CLI / `make mini` | `/tmp`の独立object/outputで双方compile/link成功 |
| `AudioBuffer`単体試験 | 経過時間換算、部分消費、小数残量、書込frame数、underflow停止、上限計数が成功 |
| `build/mucom88` realtime | SDL dummy driverで10秒継続、Ctrl-C後exit 0 |
| CLI diagnostic | 10秒試験でunderflow 0、破棄sample 0（非0時だけ出るdiagnosticが出ないことを確認） |
| `miniplay` realtime | SDL dummy driverで5秒継続、Ctrl-C後exit 0 |
| offline WAV | 1秒、176,444 byte、exit 0 |

SDL dummy driverではbuffer制御と終了経路を確認できるが、実際のCoreAudio出力におけるクリック音の有無は
聴感評価できない。次の実機確認では`build/mucom88 package/sampl1.muc`を十分な時間再生し、プチノイズ、
テンポ、PCMを含む音切れを確認する。終了時に`SDL audio diagnostics`が表示された場合は、その値と発生時刻を
記録する。なお、対策後もノイズが残る場合の次段階は、SDL timerでの生成を廃止してaudio callbackまたは
専用producer threadをsample clockの基準にする設計変更、およびcallbackで0埋めへ切り替える境界の
短いfade処理である。
