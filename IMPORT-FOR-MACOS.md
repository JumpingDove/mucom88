# MUCOM88 macOS 移植調査

## 1. 結論

本リポジトリには、移植に利用できるクロスプラットフォームな C/C++ コアと SDL
抽象化層が既にある。したがって、最初の現実的な移植対象は `src/main.cpp` から作る
コマンドライン版である。MML のコンパイル、MUB の読み書き、fmgen による YM2608
エミュレーション、WAV/VGM/S98 出力は大部分を共有できる。

初期調査時に確認した`xcode/miniosx`は、2019年頃のSDL 1.2/i386/macOS 10.6用projectが
途中の状態で残ったものであり、現在も正規buildには使用しない。一方、`src/CMakeLists.txt`と
`src/Makefile`の初期CLI移植上の不整合は修正済みで、現在はCMakeを正規build入口として
Apple Silicon上のRelease/Debug build、CLI、editor core test、およびAppKit版MML editorを実行できる。

推奨する到達順は次の通り。

1. arm64 CLI（コンパイル、MUB再生、WAV/VGM/S98書き出し）とSDL2再生（実装済み）
2. MML document/compile/playback serviceと自動試験の拡充（Phase 2完了）
3. macOS native GUI（Phase 3～5完了）、`pcmtool`／汎用export UI（Phase 6）、配布物の整備
4. 必要性を確認した場合のみFM音色editor、plugin、実chip対応

Windows GUI、HSP プラグイン、FM Tone Editor、SCCI2 は Win32 API に強く依存するため、
CLI 移植とは別プロジェクト相当の作業になる。

## 2. 調査基準

- 初期調査時点: 2026-09-12
- 最終更新: 2026-10-06
- 初期調査対象コミット: `bf008a4` (`trial-import-for-macOS`、調査時の HEAD)
- 最新受入基準: `523c72e`を基点とする作業tree
- 実機: Apple Silicon (`arm64`)、最新受入時macOS 27.0.1（初期移植時26.6.2）、Apple clang 21.0.0
- 現行build環境: Command Line Tools、CMake 4.4.3、Homebrew SDL2-compat 2.32.72、macOS SDK iconv

実機確認では、標準の `make` は後述の理由で失敗した。一方、Homebrew の SDL2 用
include/link フラグと、未定義 `DWORD_PTR` に対する一時的なコンパイル時回避を与えると、
`src/main.cpp` の CLI は arm64 でリンクできた。その一時バイナリで
`package/sampl1.muc` を step/compile-only モードでコンパイルし、65,647 byte の MUB
を生成できた。これはコア処理が arm64 上で概ね動作することを示すが、正式な修正や
互換性試験の代わりにはならない。日本語 PCM 名は文字化けし、文字コード層の問題も
再現した。

上記は初期調査時の記録である。その後、文字コードを含む初期CLI移植とCMake buildを実装し、
2026-09-22時点では`build/mucom88`と`build/editor_core_test`をarm64 Mach-Oとして生成し、
Release/Debug双方のCTestを実行済みである。現在の再現手順は13章を正とする。

2026-10-05時点ではPhase 0～3およびPhase 5が完了し、Phase 6の実装前test 6件を追加した。Phase 4は実装、
自動試験、実CoreAudio 60分再生まで完了し、内蔵speakerの聴感と物理device抜き差しだけをrelease受入へ残す。
次の実装対象はPhase 6のtext transform、PCM tool、汎用MUB／WAV／VGM／S98 export UIである。
最新の横断進捗は`TODO-WIN.md`を正とする。

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
2. 生成 MUB のmacOS版内での決定性検査と再読込・再生（header／sectionも独立検査）
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
なしで三つの sample を MUB/WAV/VGM/S98 に変換し、macOS native regressionと構造検査が通る。

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

- Windows固定binaryとmacOS版が生成するMUB/WAV/VGM/S98の厳密なbyte・構造・PCM sample比較。
  Windows ARM VM用harnessは任意調査用であり、標準版の完了条件およびrelease gateには含めない
- Intel Mac 実機または Rosetta での動作
- 実際の audio device での長時間再生、latency、underrun
- 日本語を含む実用 MML 一式での CP932/Shift_JIS/UTF-8 round trip
- rhythm WAV 一式を指定した再生
- sandboxed `.app`、署名、notarization
- 外部 plugin および real chip（現状 macOS 実装なし）

従って「コアは arm64 で動く見込みが高い」ことまでは実証済みだが、GUI機能、長時間audio、配布まで含む
macOS製品が完成しているとはまだ言えない。以後は`TODO-WIN.md`の53機能受入仕様とmacOS native regressionを
完了判定に用いる。Windows比較は仕様理解を補助する任意調査とし、実装の先行条件にしない。

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
- dependency: まず`find_package(SDL2 CONFIG QUIET)`でSDL2 CMake packageを探索し、
  targetがなければpkg-configの`sdl2`へfallbackする。非Windowsでは`find_package(Iconv REQUIRED)`を使う
- link: 検出した`SDL2::SDL2`または`PkgConfig::SDL2`と`Iconv::Iconv`。
  macOS独自の旧`SDLMain.m`は使わない
- 初期段階ではsourceを`mucom88_runtime` static libraryと`mucom88` executableへ分離
- macOSで`MUCOM88WIN`を定義しない。`mucomvm.cpp`をcompileするruntime targetに
  `USE_SDL=1`を定義
- `codeconv_iconv.cpp`を選択し、dummyと同時にlinkしない
- warningは当初`-Wall -Wextra -Wpedantic`を可視化するが、既存warningを全て即時
  `-Werror`にはしない
- install先は最初はexecutableのみ。data配置規則が確定した変更でinstall ruleを追加

開発者向けnative buildはarchitectureを指定せずhost nativeとする。標準版のrelease／CIはApple Silicon
`arm64`とし、Universal Binaryは任意の追加検証としてのみ扱う。

```text
# Apple Silicon native
cmake -S src -B build-arm64 -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0

# Universal Binary
cmake -S src -B build-universal -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0
```

deployment targetは16章の決定に従って26.0を既定とする。Universal buildではSDL2 dependency自体も
両architectureを含む必要がある。arm64だけのHomebrew SDL2互換libraryをlinkしてUniversal化することは
できない。

既存Makefileは同じ変更内で無理に全面改修しない。CMakeでacceptanceが通った後、削除するか、
SDL2/pkg-config対応の薄い互換入口として維持するかを決める。二つのsource listを手管理し続けない。

### 11.5 CMake targetへ含めるsource

`mucom88_runtime`へ含める範囲は以下である。

```text
membuf.cpp adpcm.cpp md5.c soundbuf.cpp cmucom.cpp mucomvm.cpp
mucomerror.cpp callback.cpp osdep.cpp
editor/mml_document.cpp editor/mucom_compile_service.cpp
plugin/plugin.cpp
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

`BUILD_TESTING=ON`の場合は`tests/editor_core_test.cpp`から`editor_core_test` executableも生成し、
CTestへ登録する。このtestはMML documentの保存とdirty状態、文書相対のvoice/PCM compile、
再生開始・停止、`voice.dat`の読み取り専用性、FM editor用一時音色fileの無視、埋め込みNUL拒否を確認する。

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

### 11.13 CLI初期移植時の判断と現在の決定

初期移植時に保留していた判断は、現在次のように整理している。

- 最低対応macOSは検証機と同系列の26.0とする（16章で決定・実装済み）
- 標準architectureはarm64とし、Intel Mac、Rosetta、Universal Binaryは完了条件にしない
- 開発時はHomebrew SDL2-compatを使用する。配布時のSDL2同梱と`@rpath`はPhase 9で確定する
- CLIはCtrl-C停止に対応し、service／GUIは非loop曲の自然終了も実装済みである
- default PCM／voiceは明示resourceとして利用できる。正式install時の同梱場所とlicense表示はPhase 9で確定する

このため現在の標準は「macOS 26.0以降、arm64、開発時Homebrew SDL2-compat」である。

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

Apple Silicon/macOS 26.6.2、Apple clang 21の環境で、Makefileによるarm64 clean buildに加え、
2026-09-22にはCMake 4.4.3とHomebrew SDL2-compat 2.32.72を使用したRelease/Debug buildを確認した。
iconvはHomebrew版ではなくCommand Line Tools SDKの`libiconv.tbd`が検出されている。

| case | 結果 |
|---|---|
| `-h` / 引数なし / option値不足 / `-s` | exit 0 / 2 / 2 / 2 |
| `package/sampl1.muc` compile | 65,647 byte、移植前baselineとMD5一致 |
| sample 1～3 compile | 全て成功（65,647 / 3,886 / 1,309 byte） |
| 1秒WAV | 176,444 byte、44.1 kHz、16 bit、stereo、RIFF/data size正常 |
| 1秒VGM | 2,396 byte |
| legacy日本語PCM名 | `ｺｰﾗｽ`をUTF-8 terminalへ正常表示 |
| realtime再生 | SDL dummy deviceで開始し、Ctrl-C後exit 0、hangなし |
| CMake Release/Debug | `build/`と`build-debug/`のconfigure、compile、link成功 |
| CTest | Release/Debugの`editor_core_test`が成功 |
| architecture | `mucom88`と`editor_core_test`はいずれもMach-O 64-bit arm64 |

### 残課題

- x86_64およびUniversal Binaryでの検証
- CTestはPhase 1の7件、Phase 2の10件、Phase 3の3件、計20件を常設し、macOS版の決定性、round trip、format構造、
  MUB異常系、CLI契約、SDL dummy、service境界、ASan/UBSan、TSanを確認済み。WAV/ADPCM readerの
  全異常系とfuzzingは今後の追加対象である。
  Windows ARM VM用harnessは任意調査用であり先行条件ではない
- process-global `chdir`の完全排除。現実装はtag相対path互換のため処理全体を入力directoryで実行し、
  rhythm directoryへの変更だけを初期化中に限定して必ず復元する
- install後のdefault data directory、SDL2同梱、`@rpath`、codesign、notarization
- 曲の自然終了検出。初期仕様は従来通りCtrl-C停止
- `pcmtool`のpath/name境界修正と独立target化

## 13. macOSのビルド・テスト手順

### 13.1 前提環境

初期CLI移植では、64 bit macOS、Apple clang、SDL2互換library、iconvを使用する。
compiler/SDKはCommand Line Tools、CMakeとSDL2互換libraryはHomebrewで導入する方法を標準とする。
2026-09-22の検証機ではSDL2 API/ABIを提供する`SDL2-compat`を使用している。

```sh
xcode-select --install
brew install cmake sdl2-compat
```

`xcode-select --install`で既にCommand Line Toolsが導入済みの場合、再インストールは不要である。
iconvはmacOS SDKに含まれ、現行CMake buildではSDKのheaderと`libiconv.tbd`を使用する。
SDL2本体のCMake packageを利用できる環境でもbuild可能だが、検証済み構成はSDL2-compatである。
以下でtoolとSDL2互換versionを確認できる。

```sh
clang++ --version
cmake --version
sdl2-config --version
```

検証済み環境の値は次の通りである。

| 項目 | 値 |
|---|---|
| host | Apple Silicon、`arm64` |
| macOS | 27.0.1、build 26A434（Phase 5受入。初期移植は26.6.2） |
| Command Line Tools | `/Library/Developer/CommandLineTools` |
| Apple clang | 21.0.0 (`clang-2100.1.1.101`) |
| macOS SDK | 26.5 |
| CMake | 4.4.3（projectの要求minimumは3.20） |
| SDL2 provider | Homebrew SDL2-compat 2.32.72 |
| SDL2 CMake package | `/opt/homebrew/lib/cmake/SDL2` |
| iconv | SDKの`libiconv.tbd`（runtimeは`/usr/lib/libiconv.2.dylib`） |
| pkg-config | 未導入。SDL2 config packageが見つかるため現構成では不要 |

旧SDL 1.2の`SDL.framework`、`sdl-config`、`xcode/miniosx` projectは使用しない。

### 13.2 CMakeによる推奨ビルド

repositoryのtop directoryで次を実行する。

```sh
cmake -S src -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

macOSでは、検証機と同じOS系列を最低対応とし、deployment targetは既定で26.0になる。
明示指定した`CMAKE_OSX_DEPLOYMENT_TARGET`は既定値より優先される。

生成されるCLI executableは通常`build/mucom88`である。build directoryを作り直す場合は、既存の
成果物が不要であることを確認してから別のbuild directoryを指定するか、既存directoryを削除する。
source tree内で直接CMakeを実行するin-source buildは行わない。

Debug buildは独立したdirectoryを使う。

```sh
cmake -S src -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug --parallel
```

`include(CTest)`により`BUILD_TESTING`の既定値は`ON`である。CLIだけを生成する場合はconfigure時に
`-DBUILD_TESTING=OFF`を指定できる。通常の開発・検証ではtestを明示して次の形を推奨する。

```sh
cmake -S src -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure

cmake -S src -B build-debug -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build-debug --parallel
ctest --test-dir build-debug --output-on-failure
```

生成物は`build/mucom88`、`build/MUCOM88Editor.app`、`build/libmucom88_runtime.a`、test有効時は
`build/tests/`以下のtest executableである。2026-10-06時点では全35 CTestが登録され、未実装serviceに
対応する4件はSkipとなる。Phase 6完了時にはSkip 0が必要である。

任意のprefixへinstallする場合は次のように指定する。macOSのinstall targetはCLI executableと
`MUCOM88Editor.app`を含むが、`mucompcm.bin`、`voice.dat`、SDL2 dylibの配置はまだ自動化されていない。

```sh
cmake -S src -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/stage"
cmake --build build --parallel
cmake --install build
```

### 13.3 CTestと回帰確認

登録済みtestの一覧と実行方法は次の通りである。

```sh
ctest --test-dir build -N
ctest --test-dir build --output-on-failure
```

| Test | 主な検証内容 |
|---|---|
| `editor_core_test` | document保存、相対resource compile、再生開始・停止、NUL拒否 |
| `document_service_test` | UTF-8／CP932、改行、atomic save、外部変更競合、recovery |
| `editor_line_model_test` | Unicode、最終空行、論理行とUTF-8 byte range |
| `editor_command_test` | editor commandとplayback state別のPause／Stop／早送り／Reconnect実行可否 |
| `document_recovery_test` | recovery世代上限、scan、restore、cleanup、checksum |
| `voice_service_test` | 256音色、8192 byte round trip、field検証、保存 |
| `compile_service_test` | owned MUB、構造化diagnostic、非同期compile |
| `operation_lifecycle_test` | operation ID、cancel、stale revision、破棄後callback |
| `playback_session_test` | play／pause／resume／stop、速度、session切替、device loss |
| `playback_coordinator_test` | stale play intent、複数document／observer、Stop、document close |
| `playback_end_detection_test` | 1.7／1.5／EMの有限曲、loop曲、PCM曲の終端規則 |
| `monitor_snapshot_test` | A～K、count、session ID、immutable snapshot |
| `audio_device_service_test` | SDL dummy列挙、format拒否、切断、再接続 |
| `audio_fade_test` | 開始／停止fade、単調性、callback境界、zero fill |
| `playback_transport_stress_test` | transportとdevice lifecycleの100回反復、underrun／drop |
| `export_service_test` | MUB／WAV／VGM／S98、構造、progress、cancel、partial削除 |
| `app_service_lifetime_test` | application共有所有、shutdown順、遅延callback寿命 |
| `phase5_metadata_contract_test` | tag、4 encoding、改行、重複、preview上限、entry単位error |
| `phase5_library_contract_test` | MUC／N88 filter、安定sort、Back、scan cancel／世代、resource解決 |
| `phase5_playlist_policy_contract_test` | skip／loop／Next／Previous、単一Playing行、時間／比率policy、owner切替 |
| `phase5_presentation_contract_test` | A～K表示、Idle clear、15 Hz throttle、immutable presentation |
| `phase5_integration_contract_test` | Browser／Playlist／Editor owner、MUB export、shutdown lifetime |
| `phase6_text_transform_contract_test` | N88行番号、G channel、tag、N88出力、preview／stale apply。active |
| `phase6_g_channel_contract_test` | G channel対象判定、引用・comment保護、encoding／改行、N88連続操作、実曲compile。active |
| `phase6_voice_append_contract_test` | 使用FM voiceの重複除去、順序、preview／再適用。service実装までSkip |
| `phase6_pcm_bank_contract_test` | DATA／VOICE、list／WAV／ADPCM、32件上限、path／error。service実装までSkip |
| `phase6_format_validator_contract_test` | WAV／VGM／S98の独立構造検査と破損入力拒否。validator実装までSkip |
| `phase6_export_operation_test` | 実render中cancel、progress、既存file保護、partial削除。現行serviceで実行 |
| `phase6_integration_contract_test` | PCM bank、tag、compile、MUB／WAV exportの結合。service実装までSkip |
| `mub_validation_test` | MUB header／range、PCM有無、切断・overflow・不正magic拒否 |
| `audiobuffer_test` | ring buffer、fractional sample、underflow、drop、frame境界 |
| `codeconv_test` | CP932／Shift_JIS、UTF-8、半角PCM名、不正byte、短いbuffer |
| `sdl_audio_lifecycle_test` | SDL dummy deviceの5回open／prefill／play／stop／close |
| `cli_contract_test` | exit code、stdout／stderr、未対応option、audio非初期化 |
| `native_regression_test` | sample 1～3二重生成、3 driver、MUB round trip、WAV／VGM／S98構造とhash |

各testは`build/tests/test-work/<test-name>`を専用作業directoryとして使う。native regressionは入力fixtureと
生成artifactのSHA-256、構造値、build環境を検査・記録し、repository内のsample、PCM、voiceを変更しない。
Windows生成物との一致は要求しない。

sanitizer buildは通常buildとdirectoryを分離して実行する。Phase 3以降はObjective-C++を含むapp targetの
compile／linkも検査するため、native editorを有効にする。

```sh
cmake -S src -B build-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DMUCOM88_BUILD_MACOS_EDITOR=ON \
  -DMUCOM88_ADHOC_SIGN_EDITOR=OFF \
  -DMUCOM88_ENABLE_ASAN_UBSAN=ON
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure

cmake -S src -B build-tsan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DMUCOM88_BUILD_MACOS_EDITOR=ON \
  -DMUCOM88_ADHOC_SIGN_EDITOR=OFF \
  -DMUCOM88_ENABLE_TSAN=ON
cmake --build build-tsan --parallel
ctest --test-dir build-tsan --output-on-failure
```

二つのsanitizer optionは同時指定できない。2026-09-24にPhase 3追加後のRelease、Debug、
ASan+UBSan、TSan各構成で全20件の成功を確認した。試験導入時に検出したVGM wait値の
不整合、fmgenのsigned overflow／未初期化値、SDL timer callbackとaudio終了処理のdata raceも修正済みである。

### 13.4 Makefileによるビルド

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

MakefileはCLIと`miniplay`の補助入口として残している。新しい`MmlDocument`、
`MucomCompileService`、`editor_core_test`はCMake targetで管理しているため、editor基盤を含む
正式な検証にはCMakeを使用する。

Makefile buildは`-MMD -MP`で各objectのheader依存を`objs/**/*.d`へ記録する。さらにobjectは
`Makefile`と`Makefile.setting`にも依存するため、build optionまたは依存生成方式を変更した直後は
自動的に再buildされる。これにより、classへ`std::mutex`等を追加した後に旧class sizeでcompileされた
`mucomvm.o`と新しい`osdep_sdl.o`が混在することを防ぐ。過去のbuild生成物を持つ環境で問題を切り分ける
場合は、`make clean && make mini`で完全再buildできる。

### 13.5 最小動作確認

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

### 13.6 architectureとlink dependencyの確認

通常のbuildは実行中macOSのnative architectureを生成する。生成物は次で確認する。

```sh
file build/mucom88
lipo -info build/mucom88
otool -L build/mucom88
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
通常installが片方のarchitectureしか提供しない場合、Universal linkは失敗する。deployment target
26.0はarm64検証機で確認済みである。Universal Binaryは標準版対象外であり、必要になった場合だけ追加検証する。
SDL2同梱、Developer ID署名、notarizationはPhase 9のrelease配布前に確認する。

現在のarm64 buildはHomebrewの
`/opt/homebrew/opt/sdl2-compat/lib/libSDL2-2.0.0.dylib`へlinkしている。この絶対pathは開発機での
実行には使用できるが、そのまま配布可能なapp/CLI構成ではない。配布時はSDL2 dylibの同梱、
`@rpath`の設定、codesign、notarizationを別途実施する。

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

Apple Silicon/macOS 26.6.2、Apple clang 21、Homebrew SDL2-compat 2.32.72、SDK iconvの環境で
次を確認した。

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

Apple Silicon/macOS 26.6.2、Apple clang 21、Homebrew SDL2-compat 2.32.72、SDK iconv環境で
次を確認した。

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

## 16. 最低対応macOSの確定

2026-09-21に、最低対応OSを移植検証機と同じmacOS 26系列とし、deployment targetを26.0に確定した。
検証機はmacOS 26.6.2（build 25G83）、arm64、Apple clang 21.0.0、macOS SDK 26.5である。patch version
26.6.2をtarget値にせず26.0とするのは、同じmacOS major系列のdeployment基準として扱うためである。

- CMakeはmacOS上で`CMAKE_OSX_DEPLOYMENT_TARGET`が未指定または空の場合だけ26.0をcacheへ設定する
- 利用者が`-DCMAKE_OSX_DEPLOYMENT_TARGET=<version>`を指定した場合は、その値を優先する
- Makefileはcompile/linkの両方へ`-mmacosx-version-min=26.0`を渡す
- Makefileの値は`make MACOSX_DEPLOYMENT_TARGET=<version>`で上書きできる
- この決定は古いmacOSでの互換性を保証しない。対応範囲を広げる場合は、対象OS・SDK・SDL2で改めて検証する

## 17. macOS MML editorのbuildと起動

### 17.1 実装済み範囲

AppKitとObjective-C++で実装したnative document applicationを、CMake target
`mucom88_editor`から`MUCOM88Editor.app`として生成する。full Xcode projectやSwift packageは不要で、
Command Line Tools環境でbuildできる。UIは`DocumentService`とapplication共有の
`ApplicationServices`／`MucomCompileService`だけを使用し、`CMucom`やVMの内部型を参照しない。
Windows HSP GUI、FM音色editor、pluginとは分離している。

Phase 3完了時点で利用できる機能は次の通り。

- 新規文書、MUC／N88／plain textを開く、safe save、別名保存、未保存文書を閉じる際の標準確認
- UTF-8／BOM／CP932／Shift_JISの判定と明示選択、表現不能文字の拒否、混在改行の保持
- monospaced fontの複数行MML編集、Undo/Redo、Cut/Copy/Paste、46pt幅の行番号gutter
- 行番号は`NSTextView`のdocument座標から`NSRulerView`の表示座標へ
  `convertPoint:fromView:`／`convertRect:fromView:`で変換し、縦scroll offset、view hierarchy、
  flipped座標系へ追従。本文の`visibleRect`で行番号もclipし、Text Kitのglyph baselineへ揃えて
  上下端の部分表示を一致させる
- AppKitの動的system colorによるLight／Dark Mode表示。Dark Modeでは本文、status、compile transcriptを実機確認済み
- macOS標準find barによる検索／置換、Command-G／Shift-Command-G、Command-Lによる指定行移動
- window titleへのfile名表示、MUC／N88／plain textの種別表示とUTI登録
- Finder／Launch Services、複数fileのdrag and drop、標準Open Recent、複数document window
- `Compile` buttonまたはCommand-Rによる編集中snapshotの非同期compileと旧operationのcancel
- compile transcriptと構造化diagnostic linkの表示、選択したerror行への移動
- MUCと同じdirectoryを基準にした`#voice`および`#pcm`相対pathの解決
- 編集停止5秒後のrecovery snapshot、documentごとの最大10世代、保存前backup、起動時のcrash recovery
- Command-N/O/S/Shift-S/W/F/G/Shift-G/L/RとWindows互換Control-S

保存時は`DocumentService::PrepareSave`で対象revisionとbyte列を固定し、`NSDocument`のsafe save成功後だけ
`AcknowledgeSave`でpath、resource directory、encoding、fingerprint、保存済みcontent IDを更新する。
保存中に追加編集された場合は現在内容をdirtyのまま保持する。旧`MmlDocument`は
`DocumentService`のcompatibility wrapperとし、文書状態を二重管理しない。

### 17.2 `.app`のbuild

repository top directoryから次を実行する。macOSでは`MUCOM88_BUILD_MACOS_EDITOR`が既定で`ON`である。

```sh
cmake -S src -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DMUCOM88_BUILD_MACOS_EDITOR=ON
cmake --build build --target mucom88_editor --parallel
```

成果物は`build/MUCOM88Editor.app`である。development buildではCMakeのpost-build処理がad-hoc署名を
適用する。署名を別工程で行う場合はconfigure時に`-DMUCOM88_ADHOC_SIGN_EDITOR=OFF`を指定する。

appを含む全targetと回帰試験を実行する場合は次の通り。

```sh
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

bundle、architecture、署名は次のcommandで確認できる。

```sh
plutil -lint build/MUCOM88Editor.app/Contents/Info.plist
file build/MUCOM88Editor.app/Contents/MacOS/MUCOM88Editor
codesign --verify --deep --strict --verbose=2 build/MUCOM88Editor.app
```

### 17.3 起動とMUC fileのopen

新規文書で起動する。

```sh
open "$PWD/build/MUCOM88Editor.app"
```

既存MUCを直接開く場合はappとfileを指定する。

```sh
open -a "$PWD/build/MUCOM88Editor.app" "$PWD/package/sampl1.muc"
```

一度Launch Servicesへ登録された後はbundle identifierでも開ける。

```sh
open -b org.mucom88.editor "$PWD/package/sampl1.muc"
```

Finderからは`MUCOM88Editor.app`を起動してFile > Openを使う。開発用bundleを`/Applications`へ
copyしなくても上記commandで起動できる。`open`は起動要求後にterminalへ戻る。対して
`build/MUCOM88Editor.app/Contents/MacOS/MUCOM88Editor`を直接実行すると、GUI processが終了するまで
terminalが待機するが、hangではなく通常のforeground process動作である。

compileはfileへ再保存した内容ではなく、editor上の最新text snapshotを使用する。未保存の新規文書も
compileできるが、相対resourceを使う文書は先に保存して基準directoryを確定する。保存済み文書の`#voice`と
`#pcm`の相対pathはMUCと同じdirectoryから解決する。編集中に新しいcompileを要求した場合は旧operationを
cancelし、完了時のdocument ID／revisionが現在値と異なる結果をUIへ反映しない。

### 17.4 現在の制約

- CP932とShift_JISはbyte列だけで常に厳密判別できない。曖昧な入力はCP932を推定値として表示し、
  Encoding menuから利用者が明示変更する
- legacy compilerはerror columnを公開しないため、diagnosticのcolumnは未設定として扱う。UIは複数
  diagnosticを表示できるが、現在のcompilerが返すprimary diagnosticは通常1件である
- compileはserial workerで非同期実行する。GUIの再生、停止、速度、Home、playlist、monitor、Homeからの
  単曲MUB exportはPhase 4～5で接続済みである。WAV／VGM／S98と汎用MUB exportのsave panel、progress、cancelは
  Phase 6で接続する
- MUBはeditor documentとして開かない。生成MUBはmacOS CLIで再読込／再生でき、Homeは選択MUCをMUBへ保存できる。
  MUBをGUI playerへ直接loadする操作はPhase 6以降で判断する
- sandboxは採用していないためsecurity-scoped bookmarkはN/Aである。sandboxを採用する場合は文書外の
  PCM／voice／ROM／rhythm directoryにbookmark対応が必要となる
- app icon、Developer ID署名、notarizationは未実装
- bundleはHomebrewのSDL2-compat dylibを参照する開発用成果物であり、別Macへそのまま配布できる形ではない

GUIで保存したMUCを再生する場合はrepository top directoryから次を実行する。

```sh
./build/mucom88 path/to/song.muc
```

### 17.5 実機検証結果

2026-09-24にApple Silicon/macOS 26.6.2上でPhase 3のRelease／Debug bundleをbuildし、Info.plist、
ad-hoc署名、全20件のCTestを検証した。同じ20件をASan／UBSanおよびTSan構成でも成功させた。

実GUIでは行番号ruler、標準find／replace UI、Go to Line、compile errorの3行目選択、diagnostic link、
encoding menu、複数windowを確認した。未保存編集から5秒後にApplication Supportへrecoveryが生成され、
process強制終了後の次回起動で内容をuntitled documentへ復元できることを確認した。`.n88`は
Launch Services経由で開き、N88-BASIC文書として判定できた。再生serviceはSDL dummyで検証しているが、
再生操作そのものはPhase 4のGUI実機検証対象である。

## 18. Windows ARM VMでの機能比較golden生成

この章のharnessは任意の比較調査用である。2026-09-23に確定したPhase 0以降、Windowsでの実行、生成hashの
一致、Windowsへの生成file互換は、macOS標準版の機能同等化やreleaseの完了条件に含めない。

### 18.1 目的と実装範囲

Windows版との機能同等性を固定するため、Apple Silicon上のWindows 11 ARM64 24H2 VMで、固定済みの
MUCOM88 Windows 0.70 PE32/x86 binaryからMUB、WAV、VGM、S98を生成するharnessを追加した。
macOS上でWindows binaryを代替実行してgoldenを作る仕組みではなく、Windows on Armのx86
user-mode emulationで公式package binaryを実行し、その結果をmacOSへ搬出する方式である。

実装の入口は次のdirectoryにある。

```text
tests/reference/windows-arm64-vm/mucom88-win-0.70/
  README.md
  generate-golden.ps1
  verify-manifest.cmake
```

`generate-golden.ps1`は、固定入力7fileのsize/SHA-256、`mucom88.exe`のPE32/x86 machine type、
Windows 11 ARM64 24H2、日本語system locale、code page 932を事前検査する。driverとPCM条件を分けた
7 caseを各2回`run-a`/`run-b`へ生成し、全artifactのsizeとSHA-256が一致した場合だけ`candidate/`を
作成する。MUB section、WAV format/PCM data、VGM/S98 header/command領域の構造値に加え、実行command、
終了code、変換しないraw stdout/stderr、host/guest/hypervisor情報を保存する。

### 18.2 Windows VMでの生成

repositoryと出力先はshared folderではなく、ASCIIのみ・空白なしのVM内NTFS pathを使用する。
PowerShellから次の形式で実行する。実際の環境値とsnapshot名を指定し、既存の`OutputRoot`は使用しない。

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

正式goldenでは環境不一致を許容するoptionを使用しない。詳細な前提、case matrix、review項目は同directoryの
`README.md`を正とする。

### 18.3 macOSへの搬出と検証

生成された`candidate/`をmacOSへcopyした後、repository top directoryで次を実行する。

```sh
cmake \
  -DCANDIDATE_DIR=/path/to/work-20260923/candidate \
  -P tests/reference/windows-arm64-vm/mucom88-win-0.70/verify-manifest.cmake
```

検証では`environment.json`、`inputs.json`、`determinism.json`、`manifest.json`、全caseの終了codeと
raw logをreviewする。搬出前後の`manifest.sha256`一致も確認し、Windowsまたはhypervisor更新後に結果が
変化した場合は既存baselineを上書きせず、別baselineとして原因を調査する。

### 18.4 現在の状態と任意調査の完了条件

固定fixtureのsize/SHA-256はmacOS上で再確認済みであり、`verify-manifest.cmake`はsynthetic candidateを
用いた検証に成功している。一方、このMacにはWindows ARM VM/hypervisorおよびPowerShell実行環境がないため、
Windows binaryによる実artifact生成、PowerShell scriptのWindows上での実行確認、golden hashの確定は
未実施である。

この任意調査を実施する場合の完了条件は、固定VM snapshotで二重生成を成功させ、全artifactの決定性と構造を
reviewし、搬出したcandidateのmanifest検証に成功した上で、artifact、hash、VM metadataを記録することである。
結果は参考情報として扱い、標準CTestへ必須のWindows比較として追加しない。

## 19. Phase 2 Core APIとapplication境界

2026-09-23に、GUI機能を安全に追加するためのplatform-neutral service境界を実装した。AppKitは
`CMucom`、`mucomvm`、`PCHDATA`、voice bit-fieldへ直接依存せず、値型とimmutable snapshotだけを扱う。

### 19.1 serviceと所有権

| service | 実装済み責務 |
|---|---|
| `DocumentService` | UTF-8／BOM／CP932／Shift_JIS、改行保持、atomic save、外部変更競合、recovery |
| `MucomCompileService` | serial worker、driver／resource解決、構造化diagnostic、owned MUB |
| `PlaybackSession` | 単一worker上のVM、play／pause／resume／stop、x1～x10、曲末、session切替 |
| `AudioDeviceService` | SDL output列挙、44.1 kHz S16 stereo、ring buffer、切断、再open、診断値 |
| `MonitorSnapshot` | A～Kの11 channel、interrupt／current／max／loop count、audio診断 |
| `ExportService` | MUB／WAV／VGM／S98の非同期出力、progress、cancel、partial file削除 |
| `VoiceService` | 256音色の正規化model、8192 byte round trip、検証、atomic save、preview request |
| `ApplicationServices` | application単位で1つのaudio output、playback、compiler、exporter、voiceを所有 |

compile結果の`CompiledSong`はMUB全byte列、driver、channelごとのtotal／loop count、source revision、
resource directory、content IDを所有する。compiler runtimeを破棄した後も再読込、再生、exportできる。
再生用`CMucom`は`MUCOM_OPTION_STEP`で初期化し、VM操作とmonitor取得を1本のplayback workerへ限定した。
SDL callbackはVMに触れず、事前充填したring bufferを消費するだけである。

全非同期結果は`OperationId`を持ち、document由来の処理は`DocumentId`と`Revision`も返す。新しいplay要求は
`SessionId`を更新し、遅れて届いた旧sessionのobserver eventを破棄する。callbackは指定dispatcherへ渡し、
AppKitではmain queue dispatcherを使用する。shutdownはobserver解除、export／compile queue停止、playback
worker停止、audio closeの順で行い、global service registryのmutexを保持したままworker完了を待たない。

### 19.2 曲末とmonitorの規則

`MUCOM_STATUS_COUNT`はloop時に剰余となるため自然終了判定には使わない。compile時に取得した各channelの
loop countがすべて0である有限曲は、absolute interrupt countがmax countへ達した後にruntimeを停止し、
ring buffer排出後に`Finished`とする。1 channelでもloop countが正なら自動終了せず、absolute countから
loop回数を通知する。この規則はMUCOM88 1.7、1.5、EMそれぞれについて、有限曲、`L`指定loop曲、PCMを
含む`sampl1.muc`で固定した。

monitorは1回のworker更新で11 channelを値型へcopyし、`shared_ptr<const MonitorSnapshot>`として公開する。
UIが保持済みのsnapshotは後続更新で書き換わらない。Phase 2完了時点で未接続だった描画頻度制限とmonitor viewは、
Phase 5でapplication共有15 Hz presentationとPlayerのA～K tableとして実装済みである。

### 19.3 buildと検証

build commandは13章および17.2節と同じである。Phase 2追加後は次の17件を常設し、Debug、Release、
ASan+UBSan、TSanで全件成功している。

```sh
cmake -S src -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Phase 2固有の10件は`document_service_test`、`voice_service_test`、`compile_service_test`、
`operation_lifecycle_test`、`playback_session_test`、`playback_end_detection_test`、
`monitor_snapshot_test`、`audio_device_service_test`、`export_service_test`、
`app_service_lifetime_test`である。既存Phase 1の7件も同時に成功し、CLI経路を維持している。

### 19.4 Phase 2外の項目

Core APIは実装済みだが、editor windowのtransport、monitor view、audio device picker、export save panel、
FM音色editorはまだ接続していない。実CoreAudio deviceでの60分連続再生、聴感、切断試験もPhase 4で行う。
したがってPhase 2完了はGUI再生機能や配布版の完成を意味しない。

## 20. Phase 4再生GUIの基盤（4-0）

2026-09-26にPhase 4の最初の実装単位として、GUI接続より先にapplication全体の再生調停と値型contractを
追加した。transport button、shortcut、resource picker、audio device pickerはまだ接続していない。

### 20.1 resource snapshot

`ResourceConfiguration`はdocument directory、default PCM／voice、外部ROM directory、rhythm directory、
外部ROM使用flagを保持する。`DocumentService`が作る`CompileRequest`からcompile結果の`CompiledSong`まで
値copyされるため、非同期compile中に後続のUI設定が変化しても、その結果が参照するresource snapshotは
変化しない。各resourceの優先順位、検証、実読込はPhase 4の4-1で接続する。

### 20.2 application共有Coordinator

`ApplicationServices`は`PlaybackCoordinator`を1つ所有する。Coordinatorだけが単一の`PlaybackSession`
observerを使用し、document側にはtoken付きの複数購読を提供する。compile-and-play要求にはapplication全体の
世代番号を付け、前要求のcancel、Stop後の遅延完了、別documentから届いたstale結果を再生しない。active
documentを閉じた場合は画面のない再生を残さず停止する。

`EditorCommandState`は`PlaybackState`を入力にし、Pause／Resume、Stop、FastForward、Reconnectを状態別に
有効化する。AppKitのmenu、button、F5／F12、Esc、Control-F1への接続は4-2で行う。

### 20.3 audio診断契約

`AudioDiagnostics`へ`refill_events`を追加した。`rendered_frames`はruntimeが生成したframe数、
`dropped_frames`はringへ渡せず実際に破棄したframe数と定義する。部分書込み後に再試行したframeをdropへ
加算しない。初回prefillはrefillに含めず、再生開始後にbufferが空になった後の供給再開だけを数える。

### 20.4 buildと検証

Release構成で`MUCOM88Editor.app`を含むbuildに成功し、新設した`playback_coordinator_test`を含む全21件の
CTestに成功した。Coordinator試験は2 documentの競合、複数observer、pause、active document close、
Stop後の遅延compile無効化を確認する。`compile_service_test`はresource snapshot、
`audio_device_service_test`はrender／drop／refill、`editor_command_test`はplayback state別commandを確認する。

4-0完了時点ではDebug、ASan／UBSan、TSanと実CoreAudio受入を後続段階へ残した。その後4-5で4構成試験、
4-6で60分再生を完了した。聴感と物理device切断は25.3の未完了項目である。

## 21. Phase 4 resource読込と再生GUI（4-1／4-2）

2026-09-27にPhase 4の4-1と4-2を実装した。MML editorから編集中snapshotを非同期compileして直ちに
再生でき、transport、簡易progress、速度変更、resource選択をapplication共有の再生sessionへ接続した。
output device picker、hotplug、Reconnectは4-3以降であり、この章の完了範囲には含めない。

### 21.1 resource解決

Playback menuのResources submenuから、Default PCM、Default Voice、Rhythm Directory、External ROM
Directoryを選択できる。Use External ROMを有効にした場合だけ外部ROMを使用する。選択値はapplication実行中の
全documentで共有し、再起動後には保持しない。

PCM／voiceはMML内の`#pcm`／`#voice`を最優先し、タグがない場合だけ選択済みdefaultを使用する。相対pathは
MML documentのdirectoryを基準に解決する。未保存documentに相対pathがある場合はprocess current directoryへ
fallbackせず、保存を要求するerrorにする。default voiceはcompile前に読み込み、default PCMは生成MUBにPCMが
埋め込まれていない場合だけ再生／export runtimeへpreloadする。

rhythm directoryは`CMucom::Init`、`mucomvm::InitSoundSystem`、FMGENの順に明示的に渡し、6個の
`2608_*.WAV`をdirectoryとfile名のpath結合で読む。CLIの`-r`も一時的なrhythm directoryへの`chdir`を廃止し、
同じAPIを使用する。外部ROM modeでは`expand`、`errmsg`、`msub`、`muc88`、`ssgdat`、`time`、`smon`、
`music`を選択directory内で事前検査し、不足file名をerrorへ列挙する。

### 21.2 再生操作

editor上部の操作は次のとおりである。

| 操作 | button／menu | shortcut |
|---|---|---|
| compileのみ | Compile／Build > Compile | Command-R |
| compile後に再生 | Compile & Play／Build > Compile & Play | F5またはF12 |
| pause／resume | Pause／Resume／Playback > Pause / Resume | Esc |
| 完全停止 | Stop／Playback > Stop | なし |
| 早送り切替 | Fast／Playback > Fast Forward | なし |
| 押下中だけ早送り | 選択済みx2、x4、x6、x8、x10 | Control-F1 |

Control-F1はkey-upまたはapplicationが非activeになった時にx1へ戻る。function keyをmacOS側が予約している
環境でもbuttonとmenuから同じactionを実行できる。本文編集でrevisionが変化した場合、処理中の
compile-and-play intentは無効化され、古いsnapshotが後から再生を開始しない。別documentから再生すると
application内の旧sessionを停止し、active documentを閉じると再生も停止する。

status／progress行はmain threadの15 Hz timerでimmutable snapshotを読み、playback state、driver、
current／max count、loop回数、現在速度を表示する。有限曲はdeterminate progress、準備中または最大countが
ない場合はindeterminate表示にする。4-1／4-2完了時点で後続へ残したaudio診断は4-4、11 channel詳細monitorは
Phase 5で実装済みである。

### 21.3 buildと検証

build手順は13章と同じである。

```sh
cmake -S src -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

`compile_service_test`でタグ優先、default PCM／voice、未保存documentの相対path拒否、外部ROMとrhythmの
不足file errorを確認した。`playback_session_test`では`#pcm`を除いたMMLをdefault PCM付きでcompileし、
PCM非埋込MUBをSDL dummy deviceで再生できることを確認した。`playback_coordinator_test`はdocument編集相当の
pending play intent取消を確認する。Release構成で`MUCOM88Editor.app`を含むbuildと全21件のCTestに成功した。

4-1／4-2完了時点ではdevice picker／hotplug／Reconnect、fade／診断、sanitizer、実CoreAudio 60分受入を
後続段階へ残したが、これらは4-3～4-6で実装・検証済みである。外部ROMは再配布可能な実dataがrepositoryに
ないため、path検証とerror伝播までを自動試験し、実ROMでの再生は手動受入項目として残す。

## 22. Phase 4 output deviceとReconnect（4-3）

2026-09-27にPhase 4の4-3を実装した。editorのAudio Output popupで`System Default`またはSDL2が列挙した
出力deviceを選択できる。選択はapplication内で共有し、Compile & Play時に現在のdevice名を再列挙結果へ
解決してopenする。SDLの一時的な列挙indexは保存せず、device名をselection IDにする。同名deviceが実機で
識別問題になる場合だけ、将来CoreAudio UID backendを追加する。

`System Default`はmacOS全体のdefault出力を変更する操作ではない。app側の選択をdefaultへ戻し、次回open時の
system defaultを使用する。4-1のresource選択と同様、Phase 8のSettingsServiceが実装されるまでは再起動後に
選択を保持しない。

### 22.1 hotplugと再接続

AppKit main threadの0.2秒timerがSDL event queueからaudio-device eventだけを取得する。出力一覧が変わると
各documentのpopupを更新する。使用中の`SDL_AudioDeviceID`と一致する`SDL_AUDIODEVICEREMOVED`を受けた場合は
次の状態遷移を行う。

```text
Playing／Buffering／Paused
  -> MarkDeviceLost
  -> DeviceLost（自動切替しない）
  -> 利用可能な出力を明示選択
  -> Reconnect
  -> 同じCompiledSongを新SessionIdで曲頭から再生
```

選択済みdeviceが一覧から消えた場合はpopupへ`Unavailable:`として残し、Reconnectを無効にする。別deviceまたは
`System Default`を選択するとReconnectが有効になる。安全なseek契約がないため切断位置からの再開は行わない。
Stopは従来どおり再生内容を破棄するため、その後のReconnectはできない。

### 22.2 audio format表示

初期実装は44.1 kHz、signed 16-bit、stereo、1024 frames/bufferのexact openを要求する。成功時はdevice名と
requested／obtainedのsample rate、bit数、channel数、buffer frame数をeditorへ表示する。exact openに失敗した
場合はSDLへallow-any-changeで一度probeし、requestedとavailable formatを含むerrorを表示して閉じる。自動sample
rate変換やchannel変換は追加していない。実機で必要性が確認された場合に限り、`SDL_AudioStream`導入を別途判断する。

### 22.3 検証

build手順は13章と同じである。Release構成で`MUCOM88Editor.app`を含むbuildと全21件のCTestに成功した。
`audio_device_service_test`はSDL dummy device上で追加／削除event、active instanceの切断、generation更新、
requested／obtained snapshotを確認する。`playback_coordinator_test`は切断後に`DeviceLost`で停止して自動切替
しないこと、明示Reconnect後に異なる`SessionId`でPlayingへ戻ることを確認する。

この自動試験はSDL eventと状態機械を検証するもので、実CoreAudio deviceの物理的な抜き差し、同名device、
内蔵speakerでの聴感を完了させるものではない。これらは4-6の手動受入に残す。audio診断表示、fade、次曲hookは
続く4-4で実装した。

## 23. Phase 4 audio診断、fade、曲末hook（4-4）

2026-09-27にPhase 4の4-4を実装した。再生開始／resume、pause／stop／再初期化、自然終了の波形境界を
明示的に処理し、editorからaudio bufferの状態を確認できるようにした。folder browserやplaylistは追加せず、
Phase 5が利用できるplatform-neutralな次曲hookまでを実装範囲とした。

### 23.1 audio diagnostics表示

device／playback行の下に診断行を追加し、次を15 Hzの既存snapshot更新で表示する。

- ringに残る`queued_frames`
- callbackの`underruns`
- rendererがringへ渡せなかった`dropped_frames`
- 空buffer後に供給を再開した`refill_events`
- runtimeが生成した`rendered_frames`
- 出力中の`active`または`device lost`

GUIは`MonitorSnapshot::audio`のimmutable copyだけを参照し、SDL audio callbackからAppKitを呼ばない。
再生終了後も最後の診断値を確認できる。deviceを次にopenした時点で各counterは0へ戻る。

### 23.2 click抑止

`AudioFadeEnvelope`を追加し、stereoのframe位置を基準に整数演算の線形gainを適用する。fade長は44.1 kHzで
256 frame、約5.8 msである。

| 境界 | 処理 |
|---|---|
| 開始／resume | SDL callbackが最初の256 frameを0から等倍へfade-in |
| pause／stop／別曲再初期化 | callbackが256 frameを等倍から0へfade-outし、playback workerだけが最大250 ms待機 |
| 有限曲の自然終了 | 終了検出blockの末尾256 frameをfade-outしてから`Draining`へ遷移 |
| device lost | callback完了を待たず直ちに`DeviceLost`へ遷移 |

SDL callbackの要求frame数がfade長を超える場合、fade-out完了後の同じcallback内はzero fillする。これにより
256 frame後に元の音量へ戻る波形を出さない。fade待機、pause、flush、closeはplayback workerで行うため、
AppKit main threadはblockしない。自然終了がprefill量より短い曲も、蓄積済みframeを開始して排出してから
`Finished`にする。

### 23.3 次曲hook

`PlaybackCoordinator::SetNextSongProvider`は終了した`CompiledSong`を受け取り、次の`CompiledSong`またはnullを
返す任意hookである。未設定またはnullなら従来どおり`Finished`に留まる。次曲がある場合は選択device、format、
現在速度を引き継ぎ、新しい`SessionId`で曲頭から再生する。hook実行中にStop、document close、別のPlayが
発生した場合はplay-intent世代、active song、状態を再照合し、古い結果を再生しない。provider例外はplayback
worker外へ伝播させない。

4-4完了時点の`MUCOM88Editor.app`はproviderを設定していなかった。その後Phase 5でapplication共有
`PlaylistService`がこのhookを使用するようになり、folder選択、compile失敗skip、playlist loop、Now Playingを
実装済みである。

### 23.4 buildと検証

build手順は13章と同じである。

```sh
cmake -S src -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

`audio_fade_test`はfade-in／outの端点、単調性、最大隣接sample差、128 frameずつに分割した処理、
1024-frame callback内のfade後zero fillを検証する。`playback_transport_stress_test`はSDL dummy deviceで
Play／Pause／Resume／Stopおよびaudio deviceのopen／closeを100回反復する。`playback_coordinator_test`は
有限曲終了後にproviderが返したloop曲へ移り、documentとsessionが切り替わることを確認する。

Apple Silicon、Release構成で`MUCOM88Editor.app`を含むbuildと全23件のCTestに成功した。これは波形処理、
状態機械、hangしない反復操作の自動検証であり、実CoreAudio出力の聴感を保証しない。次は4-5でDebug、
ASan／UBSan、TSanを含む全構成の回帰を行い、その後4-6で内蔵speakerの60分再生、click、音切れ、tempo、
PCM、物理device切断を手動受入する。

## 24. Phase 4 dummy／sanitizer回帰（4-5）

2026-09-27にPhase 4の4-5を実施した。4-4までの全23 CTestをRelease、Debug、ASan／UBSan、TSanで再実行し、
全構成で成功した。各buildはCLIだけでなく`MUCOM88Editor.app`も生成し、ad-hoc署名まで完了している。

### 24.1 実行手順

Releaseは13章の標準手順を使用する。追加3構成は次のとおりである。

```sh
cmake -S src -B build-debug \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON
cmake --build build-debug --parallel
ctest --test-dir build-debug --output-on-failure

cmake -S src -B build-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DMUCOM88_ENABLE_ASAN_UBSAN=ON
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure

cmake -S src -B build-tsan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DMUCOM88_ENABLE_TSAN=ON
cmake --build build-tsan --parallel
ctest --test-dir build-tsan --output-on-failure
```

`MUCOM88_ENABLE_ASAN_UBSAN`と`MUCOM88_ENABLE_TSAN`は排他的であり、同じbuild directoryへ同時指定しない。
手順で使用する`build-asan/`と`build-tsan/`は`.gitignore`対象へ追加した。

### 24.2 結果

| 構成 | CTest | 結果 |
|---|---:|---|
| Release | 23/23 | 成功 |
| Debug | 23/23 | 成功 |
| ASan／UBSan | 23/23 | 成功、AddressSanitizer／UndefinedBehaviorSanitizer報告なし |
| TSan | 23/23 | 成功、ThreadSanitizerのdata race報告なし |

SDL dummyを使用する`playback_transport_stress_test`は全構成でPlay／Pause／Resume／Stop、fade完了待機、
audio deviceのopen／closeを100回完走した。`audio_fade_test`、有限曲のbuffer排出、次曲hook、device lost、
application／callback lifetimeを含め、timeoutやhangは発生していない。4-5では試験で不具合を検出しなかったため、
runtime／editor sourceの追加修正は行っていない。

この結果はSDL dummyとoffline処理の自動回帰であり、実際のspeaker出力やCoreAudio hotplugを代替しない。
次の4-6ではこのApple Silicon Macの内蔵speakerでPCM曲を60分以上再生し、GUI診断値、click、音切れ、tempo、
終了noise、物理device切断／再接続を記録する。

## 25. Phase 4実CoreAudio受入（4-6、一部完了）

2026-09-27にMacBook Air（Mac17,3、Apple M5、macOS 27.0 build 26A428）でRelease版
`MUCOM88Editor.app`を使用し、`package/sampl1.muc`を実CoreAudioへ出力した。Audio Outputは
`System Default`、requested／obtained formatはいずれも44.1 kHz、signed 16-bit、stereo、
1024 framesである。

### 25.1 実機で検出したbackpressure不具合

最初のrunでは`underruns=0`のまま`dropped_frames`が増加し、最終的に107008 framesへ達した。
原因はring buffer満杯時の`AudioDeviceService::WriteFrames`が20 msで待機を打ち切る一方、実deviceの
callback周期が1024 / 44100 = 約23.2 msだったことである。producerはcallbackが空きを作る直前にtimeoutし、
PlaybackSessionが未書込みの512-frame render blockをdropとして計上していた。

待機上限を250 msへ変更した。これはcallback周期の揺らぎを吸収するための上限であり、空きができれば
condition variableで直ちに復帰する。cancel、device lost、shutdownでも同じ通知とpredicateにより即時解除されるため、
停止応答を250 ms固定で遅延させる変更ではない。

`playback_transport_stress_test`には各Play／Pause／Resume／Stop cycle後の`underruns == 0`と
`dropped_frames == 0`を追加した。`audio_device_service_test`はdummy callbackが20 ms以内に必ず動くという
時刻依存を除去し、最大2秒のdeadline内で実際のcallback／underrunを待ってからrefillを検証する。

### 25.2 60分連続再生結果

修正版appを再起動して`sampl1.muc`をCompile & Playし、実時間60分にわたり50秒間隔でAppKitの状態と
audio diagnosticsを監視した。

| 観測点 | state | loop | queued | underruns | dropped | refills | rendered |
|---|---|---:|---:|---:|---:|---:|---:|
| 60分 | Playing | 73 | 16384 | 0 | 0 | 0 | 160259072 |
| Stop後 | Idle | - | 0 | 0 | 0 | 0 | 163442176 |

60分を通して`Playing`とloop進行は継続し、異常診断値は増加しなかった。監視終了後も再生を継続してから
Stopを実行し、`Idle`への遷移、ring queueの解放、device非active化を確認した。

修正後はRelease、Debug、ASan／UBSan、TSanを再buildし、各23件、計92件のCTestがすべて成功した。
ASan／UBSanとTSanからmemory error、undefined behavior、data raceの報告はない。

### 25.3 未完了の手動受入

Computer Useで検証できるのはGUI状態、format、診断値、時間経過、transport操作までであり、実際の音を
聴取することはできない。また`System Default`が内蔵speakerへ向いているかの判定と、外部deviceの物理的な
抜き差しも自動化していない。次の項目は人間による手動受入として残す。

- macOSの出力先を内蔵speakerへ固定し、PCMが聞こえることを確認する
- 原曲と比べてtempo変動、音切れ、click、停止時noiseがないことを確認する
- 外部output deviceを再生中に抜き、`DeviceLost`へ遷移して別deviceへ無断切替しないことを確認する
- deviceを戻して明示`Reconnect`し、同じ曲が曲頭から再生されることを確認する

したがって、実CoreAudioの60分連続動作と停止／解放は完了したが、4-6およびPhase 4全体の受入状態は
「一部完了」である。手動項目の記録先は`tests/manual/macos-gui-acceptance.md`とする。

## 26. Phase 5 Home／player／monitor実装設計

2026-09-27にPhase 5の設計を具体化した。この節は実装結果ではなく、macOS native GUIへHome、automatic player、
sound monitorを追加する際の実装境界である。Windowsの`mucom88win.hsp`と`aplayer.hsp`を参照するが、HSPの
画面遷移や別process構成は再現せず、現在の`ApplicationServices`とAppKit複数window構成へ統合する。

### 26.1 現行実装から利用できるもの

Phase 2～4で次の基盤は完成している。

- `DocumentService`: MUC／N88、UTF-8／BOM／CP932／Shift_JISのdecodeと絶対path基準のcompile request
- `MucomCompileService`: main thread外のserial compile、owned MUB、resource解決、cancel
- `PlaybackCoordinator`: application内1再生、複数observer、transport、device、stale play intent抑止
- `PlaybackSession`: immutable `MonitorSnapshot`、A～K、count、speed、audio診断
- `ExportService`: MUBのatomic saveを含む非同期export
- `SetNextSongProvider`: 自然終了後にcompile済み曲へ切り替えるPhase 5向けhook

一方、folder列挙、source tagだけを読むmetadata API、playlist状態機械、Now Playing値、monitor viewはない。
`CompiledSong`にもmetadataは保持されず、Coordinatorはactive playbackの由来を`DocumentId`以外で区別できない。
また、各editor windowが独自の15 Hz timerを持つため、そのままHomeとmonitorを増やすとwindow数に比例して
snapshot pollが増える。

### 26.2 追加するplatform-neutral file

実装時は次の単位へ分割する。名称は責務を固定するための予定名であり、AppKit型を含めない。

| file | 責務 |
|---|---|
| `editor/song_metadata.h/.cpp` | `SongMetadata`、source tag parse、表示title fallback |
| `editor/library_service.h/.cpp` | 非同期directory列挙、MUC／N88 filter、metadata、scan generation、compile request load |
| `editor/playlist_service.h/.cpp` | immutable queue、compile-ahead、失敗skip、loop、時間／比率policy、owner監視 |
| `editor/playback_presentation.h/.cpp` | channel表示値、note／pan整形、SessionId clear、15 Hz coalescingをtest可能な値型にする |

`ApplicationServices`へlibraryとplaylistを追加する。playlistはlibrary、compiler、Coordinatorを参照するため、
終了時はplaylistのprovider解除とworker停止を最初に行い、その後Coordinator、compiler、playback、audioの順で
破棄する。`mucom88_runtime`へ上記sourceを追加し、macOS以外でもCTest可能にする。

既存fileは次の範囲で拡張する。

- `mucom_compile_service.h/.cpp`: compile成功時に同じparserで`CompiledSong::metadata`を設定する
- `playback_coordinator.h/.cpp`: Editor／Browser／Playlist owner、`NowPlayingInfo`、compile済み曲を安全に開始する
  `PlayCompiledSong`を追加する
- `application_services.h/.cpp`: library／playlistの所有と明示shutdown順序を追加する
- `src/CMakeLists.txt`と`src/tests/CMakeLists.txt`: runtime source、AppKit source、Phase 5 CTestを登録する

### 26.3 Home window

`MucomHomeWindowController`はapp delegateが1個だけ所有し、`NSDocument`にはしない。Window menuから表示する。
`NSSplitViewController`でsidebar、song table、metadata inspectorを構成し、toolbarにChoose Folder、Back、Refresh、
Open in Editor、Play、Export MUB、Start Playlistを置く。

directory scanとdecodeは`LibraryService`のworkerで行い、main queueへimmutable `LibrarySnapshot`を返す。
folder移動またはRefreshのたびにgenerationを更新し、遅れて完了した旧scanを表示しない。初期版は現在folderの
1階層だけを扱い、hidden entry、再帰index、filesystem watcherを実装しない。MUC／N88はextensionを
case-insensitiveに判定し、directory先行で安定sortする。

Open in Editorは`NSDocumentController`を使用する。複数document方式ではactive dirty documentを置換しないため、
Windowsのsingle-document確認dialogを追加する必要はない。既に同じURLを開いている場合は既存windowを前面化する。

Playはdocument windowを作らず、libraryでdecodeしたimmutable requestをBrowser ownerとして
`PlaybackCoordinator::CompileAndPlay`へ渡す。Export MUBは`NSSavePanel`の確定後に同じrequestをcompileし、
`ExportService`へ渡す。resource解決はeditorと同じ`ResourceConfiguration`の値copyを使用し、Home側で`chdir`しない。

### 26.4 playlistとautomatic player

Windows automatic playerはfolder内の`*.muc`をfile順にcompile／playし、失敗曲をskipして末尾から先頭へ戻る。
macOS版もStart Playlist時のMUC sort順をimmutable queueへcopyする。N88はHomeのopen／direct playには対応するが、
初期automatic queueには含めない。

`PlaylistService`は現在曲の再生中に次候補を1曲だけ非同期compileする。ready曲は既存
`SetNextSongProvider`から同期返却できるが、provider自身はI/Oやcompileを行わない。自然終了時にprefetchが
未完了ならCoordinatorを`Finished`に残し、完了後に`PlayCompiledSong`する。これによりmain thread compileを避け、
既存のstale generation検査を維持する。

compile errorはentry単位で保持して次へ進む。1周で全件失敗した場合は探索を止め、aggregate errorを表示する。
playlist中にeditorまたはHome direct playが別ownerで開始された場合は、playlistのpending load／compileだけをcancelし、
新しい曲をStopしない。Playlist windowを閉じてもserviceは継続し、明示Stopまたは別ownerへ移るまで再生を続ける。

自然終了以外の90秒／150% threshold、Next、Previousでは、readyな対象があれば`PlayCompiledSong`へ即時切替し、
未readyなら現在曲をStopして`Advancing`表示にした後、load／compile完了時に開始する。Previousはprefetchをcancelし、
先頭から末尾へのwrapはfolder loopが有効な場合だけ行う。

skip policyはWindows設定に合わせてsession defaultを90秒、max countの150%とする。0は無効である。
実時間は`steady_clock`で`Playing`中だけ加算し、比率はabsolute interrupt countを64-bitで比較する。両方有効なら
先着、有限曲の自然終了なら即座に次曲へ進む。Pause、Buffering、DeviceLost中は時間skipしない。
policy判定はPlaylistService workerが50～100 ms間隔でsnapshotを読むため、AppKit windowの表示有無に依存しない。

### 26.5 Player／Sound Monitor window

`MucomPlayerWindowController`もapp delegateが1個所有する。Now Playing metadata、playlist、共有transport、
Next／Previous、loop／skip policy、compile error log、A～Kのchannel tableを1 windowへまとめる。WindowsのSMONを
別runtimeとして起動せず、Coordinatorのactive sessionだけを表示する。

channel tableはMute、Voice、Volume、Detune、Address、Key／Key On、LFO、Reverb、Pan、Quantizeを持つ。
headerにはdriver、state、absolute／current／maximum／loop count、speed、audio診断値を表示する。
IdleまたはSessionId変更時は旧曲の値を残さない。

AppKit側には`PlaybackPresentationController`を1個設ける。Coordinator observerからstate／error／device lostを
即時反映し、1本の15 Hz timerが`Snapshot()`を1回だけ取得して全editor、Home、Playerへfan-outする。現在の
documentごとの`_playbackTimer`はこの経路へ置き換える。同一snapshotの重複描画を省き、UI close時にsubscriptionを
解除する。audio callbackやplayback workerからAppKitを呼ばず、描画完了も待たない。

AppKit sourceは現在の`mucom_editor.mm`へ集中させず、少なくとも次の単位へ分ける。

```text
editor/macos/mucom_home_window.h/.mm
editor/macos/mucom_player_window.h/.mm
editor/macos/playback_presentation_controller.h/.mm
```

### 26.6 実装と検証の順序

1. metadata、library、owner、Now Playingの値型と単体試験を追加する。
2. 非同期scanとHome一覧／inspectorを接続する。
3. Open、direct play、MUB exportを接続し、単曲経路を完成させる。
4. PlaylistService、compile-ahead、失敗skip、loop、fake clock policyを実装する。
5. app-wide presentation controllerへ既存editor timerを移し、Player／11 channel monitorを追加する。
6. Release、Debug、ASan／UBSan、TSanの全CTestとmacOS GUI受入を実施する。

追加する主要CTestは`metadata_service_test`、`library_service_test`、`library_action_test`、
`playlist_service_test`、`playlist_policy_test`、`playback_presentation_test`である。既存の
`playback_coordinator_test`、`monitor_snapshot_test`、`app_service_lifetime_test`もowner、次曲、表示clear、
shutdown順序を拡張する。

実GUIではfolder 1周、compile error skip、末尾loop、90秒／150% skip、dirty editorからのopen、direct play、
MUB保存、playlist中のeditor Play、monitor 20回開閉を確認する。実CoreAudio診断値は操作前後とも
`underruns=0`、`dropped_frames=0`を必要とする。詳細な手順と記録欄は
`tests/manual/macos-gui-acceptance.md`に定義した。

Phase 5の設計後、production serviceとAppKit UIを実装した。実装前に追加したcontract testと、その有効化後の
検証結果を次節以降に記録する。

### 26.7 実装前contract test（2026-09-28）

Phase 5 production実装より先に、metadata 10、library 12、playlist 12、policy 10、presentation 12、
integration／lifetime 8の計64ケースを`src/tests/phase5/README.md`へ抽出した。要件だけを文書化するのではなく、
次の5 C++ executable specificationを`src/tests/CMakeLists.txt`へ登録している。

| CTest | 有効化するproduction header | 対象段階 |
|---|---|---|
| `phase5_metadata_contract_test` | `editor/song_metadata.h` | 5-0 |
| `phase5_library_contract_test` | `editor/library_service.h` | 5-1 |
| `phase5_playlist_policy_contract_test` | `editor/playlist_service.h` | 5-3 |
| `phase5_presentation_contract_test` | `editor/playback_presentation.h` | 5-4 |
| `phase5_integration_contract_test` | 上記4 headerすべて | 5-5 |

実装前はproduction headerが存在しないため各testが終了code 77を返していた。現在は4 headerが追加されて
`__has_include` guardが外れ、5件すべてのcontract本体がcompile／実行される。77 fallbackは部分適用tree向けに
残しているが、現行treeではSkipを許容しない。

### 26.8 Phase 5実装結果（2026-09-29）

次のplatform-neutral serviceを追加した。

- `SongMetadata`／`MetadataService`: MUC／N88 tag抽出、UTF-8 validation、16 MiB preview上限、file stem fallback
- `LibraryService`: 1階層の非同期scan、directory／MUC／N88の安定sort、hidden除外、entry単位error、stale世代破棄
- `PlaybackCoordinator`拡張: Editor／Browser／Playlist owner、`NowPlayingInfo`、compile済み曲の`PlayCompiledSong`
- `PlaylistService`: immutable MUC queue、1曲先読み、compile失敗skip、loop、Next／Previous／Stop、90秒／150% policy、
  別ownerによる停止、provider解除を含むshutdown
- `PlaybackPresentation`: A～Kの11 channel表示値、note／pan／address整形、Idle／Preparing clear、15 Hz throttle contract

`ApplicationServices`がLibrary／Playlistをapplication単位で所有し、終了時はPlaylistとproviderをCoordinatorより先に
停止する。`CompiledSong`へmetadataを保持するため、editor、Home direct play、playlistの全経路で同じNow Playingを
表示できる。

AppKitには`phase5_windows.h/.mm`を追加し、Window menuから次を表示できるようにした。

- Home: Choose Folder、Back、Refresh、MUC／N88一覧、metadata inspector、Open in Editor、Play、Export MUB、
  Start Playlist
- Player / Sound Monitor: Now Playing、Pause／Resume、Previous／Next／Stop、loop／時間／比率policy、playlist状態、
  A～K channel table、interrupt／loop／speed／audio診断

`MucomPlaybackPresentationController`をAppDelegateが1個所有し、1本の15 Hz timerでCoordinator snapshotを1回だけ
取得して全editorとPlayerへimmutable notificationとして配る。従来のdocumentごとの`_playbackTimer`とPlayer固有timerは
廃止した。Coordinator observerによる状態変化はmain queueへ即時配送し、audio callbackやplayback workerはAppKitを
呼ばない。

compile-aheadにより複数のMUCOM compilerが同時に動くため、FMGENのPSG emit／envelope／noise tableとOPN LFO tableを
static共有からinstance所有へ変更した。これによりTSanで検出したcompile worker間のdata raceを除去した。また
HomebrewのSDL2-compatで`SDL_AddTimer`のuserdataが反復停止時にnullになるクラッシュをTSanで再現したため、通常runtimeの
10 ms timerを`std::thread`／condition variableへ置き換え、`FreeTimer()`でstop後にjoinしてからsubsystemを解放する。
audio device callbackは従来どおり`SDL_OpenAudioDevice`系を使用する。

Release、Debug、ASan／UBSan、TSanの4構成で`MUCOM88Editor.app`を含むbuildに成功し、各構成の全28 CTestが成功した。
Phase 5 contract 5件はすべてactive、Skip 0である。TSanの`sdl_audio_lifecycle_test`はさらに5回反復して全回成功した。
これで5-0～5-4と自動回帰gateは完了した。5-5の実CoreAudio GUI受入結果は次節に記録する。

### 26.9 Phase 5実CoreAudio GUI受入（2026-10-04）

`build/MUCOM88Editor.app`をmacOS 27.0.1（26A434）、Apple Silicon上で起動し、SDL 2.32.72の
`System Default`を使用した。requested／obtained formatはいずれも44.1 kHz、signed 16-bit、stereo、
1024 framesである。実施時binaryのSHA-256は
`e8b5b09506176c25dc5154674a892c18c416851f15ccda2f7f3fe18a18237091`だった。

- Homeで`package`の3 folder／3 MUCを表示し、Back、子folder移動、Refresh、metadata inspectorを確認した
- dirtyな`sampl1.muc`を保持したまま`sampl2.muc`を別windowで開き、未保存内容が失われないことを確認した
- Home direct Playでeditor windowを増やさず再生し、Playerと同じactive sessionを表示した
- `sampl1.muc`を65,647-byteの`build/phase5-acceptance.mub`へ保存し、`build/mucom88 -i`で再読込した。
  save panelのCancelも確認した
- 一時的なmissing voice fixtureを4曲目に追加し、1～3曲目の再生、4曲目の`Failed`と具体的error、
  error skip後の末尾から先頭へのloopを確認した。fixtureは試験後に削除した
- policyを1秒／0%および0秒／1%で個別に確認した。Pause 3秒間はsession／countが変化せず、Pause時間を
  最大演奏時間へ加算しなかった
- Next／Previous／Stop、playlist再生中のHome direct Playによるowner切替を確認した。後者ではplaylistだけが
  `Stopped`となり、新しいBrowser ownerの曲が継続した
- Playerを再生中に20回開閉し、A～K、Now Playing、count／maximum／loopがactive sessionに追従した。
  最終診断値は`underruns=0`、`dropped_frames=0`、`refill_events=0`で、hangはなかった

受入中に3件の不具合を検出して修正した。`NSSplitView`内のtableが高さ0へcollapseする問題には上下paneの
minimum heightを指定した。Idle／Preparingで前曲のdriver、count、loop、speedが残る問題はpresentation modelで
曲固有値をdefaultへ戻し、GUIを`Idle`／`Preparing`表示へ切り替えた。曲切替後に過去entryも`Playing`のまま残る
問題は、active entryを設定するときに以前の`Playing`を`Pending`へ戻し、Stop／owner変更時にもclearするよう修正した。

修正後はRelease、Debug、ASan／UBSan、TSanの4構成で各28 CTestが成功し、Phase 5 contractのSkipは0だった。
ad-hoc署名の`codesign --verify --deep --strict`も成功した。これによりPhase 5の5-0～5-5を完了とする。
GUI automationでは聴感を判定できず物理deviceも抜き差ししていないため、この2点は25.3に記載したPhase 4の
release受入として引き続き未完了である。

## 27. Phase 6実装前contract test（2026-10-05）

Phase 6のproduction serviceより先に、text transform、voice定義追記、PCM bank、独立format parser、
export非同期操作、service結合の6 CTestを登録した。caseと想定する公開APIは`src/tests/phase6/README.md`に記録した。
既存`ExportService`を使う1件は実render途中のcancel、progress IDと単調性、既存destinationの保護、
失敗時のpartial削除を実行する。新serviceのheaderが必要な5件は現時点でSkipとなり、header追加後に本体を
compile・実行する。Phase 6完了判定ではSkipを認めない。

Release buildでは`ctest --test-dir build --output-on-failure -R '^phase6_'`を実行し、1 Pass／5 Skipだった。
全suiteは34件登録で29 Pass／5 Skipを確認した。Phase 6の6 test executableはDebug、ASan／UBSan、TSanでも
buildし、activeなexport試験は全4構成でPassした。Phase 6のGUI実操作と5件のcontract有効化は
production実装後のgateとして残る。既存Phase 5受入の28件成功という記録は、その時点の履歴である。


## 28. Phase 6初回実装: text transform（2026-10-05）

`TextTransformService`へN88行番号除去、G channelの小文字q変換、欠落metadata tag追加、
N88-BASIC出力の4変換を実装した。previewは文書を変更せず、document ID／revisionを保持する。
Applyは`DocumentService::ReplaceTextIfCurrent`でID／revision確認と更新を同じmutex内で行い、
古いpreviewや別文書への適用をConflictで拒否する。本文変更は一revision、変更なしはrevisionを保持する。

macOSのTools → Remove N88 Line Numbers…に変換前後previewとApply／Cancelを追加した。
Applyは一つのUndo groupへ登録し、Undo／Redoで本文と選択範囲を復元する。編集通知は既存の
model同期、compile取消、dirty状態、recovery処理へ接続する。実GUIでのGUI-TOOL-01受入は未確認である。
G channel変換、tag追加、N88出力のGUIとvoice追記、PCM bank、validatorは後続作業とする。

Release／Debug／ASan+UBSan／TSanでGUI buildと全34 CTestが成功し、30 Pass／4 Skipになった。
text transform契約は有効化され、異なる文書、変更なし、不正UTF-8、apostrophe保持、tag値の改行拒否、
同時Applyの競合も検証する。Phase 6全体の完了条件は引き続きSkip 0と実GUI受入である。

## 29. Phase 6次優先項目: G channel変換の実装前test（2026-10-06）

`GUI-TOOL-02`のGUI接続を次に優先する。既存`TextTransformService`で変換可能で、N88除去と同じpreview、
document ID／revision確認、Undoの操作経路を共用できるためである。実装前に`phase6_g_channel_contract_test`を
CTestへ追加し、`GCH-01`～`08`を`src/tests/phase6/README.md`に定義した。G以外のA～F／H～K、quoted text、
comment、tag、非commandの`q`を保護し、UTF-8 BOM／CP932とLF／CRLF／CRの保存、N88除去との連続操作、
no-op、stale preview、別document拒否を確認する。`sampl1.muc`の有効なG行の`@8`を`q8`へ置き換えた
fixtureを変換して元曲へ戻し、PCM内蔵MUBへcompileできることも確認した。

Release全suiteは35件中31 Pass／4 Skipで、専用testはDebug、ASan／UBSan、TSanでもPassした。
GUIのOriginal／Preview、Cancel、Apply、Undo／Redo、選択範囲、2 window、preview中の編集競合は
`tests/manual/macos-gui-acceptance.md`に手順を定義した。GUI入口の実装と実操作受入はまだ行っていない。

## 30. G channel変換のGUI入口実装（2026-10-06）

`build/MUCOM88Editor.app`のTools menuに「Convert G Channel q to @…」を追加した。
N88行番号除去と同じ変換実行経路を共有し、Original／Preview、Cancel、条件付きApply、
1回のUndo／Redoと操作名を提供する。既存N88操作のUndo名もこの共通経路で維持する。
Release GUI buildと全35 CTestは31 Pass／4 Skip。Debug、ASan／UBSan、TSanでもGUI buildと
`phase6_g_channel_contract_test`が成功した。

実GUIで4行の未保存MUCを操作し、G行の数値付き`q`だけのpreview、Cancel時の本文不変、
Apply後の本文、1回のUndo／Redoとその操作名を確認した。非対象行`A q3d`の選択もUndo後に復元された。
保存済み文書のdirty状態、2 window、競合、N88からの連続操作、実GUI compileは未確認のため、
`GUI-TOOL-02`の
手動受入全体は継続する。詳細な結果と残項目は`tests/manual/macos-gui-acceptance.md`を参照。

## 31. Phase 6次項目: metadata tag追加の実装前test（2026-10-06）

`GUI-TOOL-03`を次に選択した。Windows GUIの7項目（title、composer、author、voice、pcm、date、
comment）に対し、macOSでは既存tagを上書きせず、空欄を追加せず、Original／Previewと一回のUndoを
提供する設計とした。`phase6_metadata_tag_contract_test`をCTestへ登録し、`TAG-01`～`08`の詳細と
GUI受入条件を`src/tests/phase6/README.md`および`tests/manual/macos-gui-acceptance.md`に記載した。

Release全36 CTestは31 Pass／4 Skip／1 Fail。専用testはDebug、ASan／UBSan、TSanでもbuildし、
同じ2条件だけで失敗した。`#TITLE`しかない文書で小文字`#title`が追加されない点と、
混在改行の文書にtag行を挿入すると元行の改行種別がずれる点で失敗する。原因はそれぞれ
`TextTransformService`の大文字小文字を無視した既存tag判定と、`DocumentService`が改行種別を
行indexだけで保持する方式にある。testは期待仕様として失敗のまま残し、次の実装でproduction側を
修正する。metadata tagのGUI入口はまだ追加していない。
