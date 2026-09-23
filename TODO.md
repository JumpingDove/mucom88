# macOS 自動試験・CI TODO

## 目的

macOS向けMUCOM88 CLIを、ローカルとGitHub Actionsの両方で再現可能に検証する。
最低対応OSはmacOS 26.0とし、arm64とx86_64のbuild、offline出力、SDL2音声制御を継続的に確認する。

作成日: 2026-09-21

## 基本方針

- ローカルとCIはCMake/CTestを共通の正規入口にする
- source treeへtest生成物を書かず、build tree内のtest別作業directoryを使う
- pull requestでは高速かつ決定的な試験だけを必須にする
- sanitizer、長時間試験、fuzzingはnightlyへ分離する
- GitHub-hosted runnerの音声試験は`SDL_AUDIODRIVER=dummy`を使用する
- CoreAudio実デバイスの試験はself-hosted Macまたはrelease前の手動試験とする
- golden値は現在の出力を無条件に採用せず、既知のWindows版または移植前baselineと照合して確定する
- 初期段階では既存warningを理由に`-Werror`を有効化しない。warning解消後に別途必須化する

## Phase 0: 既存テスト基盤の修復

### dummy backend

- [ ] `src/dummy/osdep_dummy.cpp`の`OsDependentSdl::GetStatus()`を
      `OsDependentDummy::GetStatus()`へ修正する
- [ ] `OsDependentDummy::SetBreakHook()`を実装する
- [ ] `OsDependentDummy::GetBreakStatus()`を実装する
- [ ] 時刻、file操作、plugin、real-chipのstubが成功を返すべきか失敗を返すべきか整理する
- [ ] dummy backend単独で`OsDependent`の全pure virtual methodを満たすことをcompile testで確認する

### 旧テストの整理

- [ ] `src/tests/module_test.cpp`と`src/tests/compile_test.cpp`の重複を解消する
- [ ] `CompileWav()`の正常系にreturnがない問題を修正する
- [ ] 固定30秒のWAV生成を短時間かつ引数指定可能にする
- [ ] `src/tests/depent_test.cpp`のbusy loopへtimeoutを設ける
- [ ] `StreamCount=0`の後で`SendAudio(StreamCount)`を呼ぶ誤りを修正する
- [ ] stdoutの目視確認を廃止し、失敗時に非0を返すassert/check方式へ変更する
- [ ] current directory固定のfixture参照と出力を廃止する
- [ ] repositoryに追跡されているLinux x86_64実行ファイル`src/tests/compile`を生成物扱いに変更する
- [ ] test executable、WAV、MUB、logをsource treeへ生成しないようにする

## Phase 1: CTest統合

### CMake構成

- [x] `src/CMakeLists.txt`のtop levelで`include(CTest)`を呼ぶ
- [x] `BUILD_TESTING=ON`の場合だけ`add_subdirectory(tests)`する
- [x] `src/tests/CMakeLists.txt`を新設する
- [x] test helperまたは小規模な`CHECK()` macroを用意する
- [x] 各testへ明示的なtimeoutを設定する
- [x] 各testの作業directoryを`${CMAKE_CURRENT_BINARY_DIR}/test-work/<test-name>`へ分離する
- [x] testを並列実行しても出力file名が衝突しないことを確認する
- [x] `ctest --test-dir build --output-on-failure`だけで全自動試験を実行可能にする

### 想定ファイル

- `src/tests/CMakeLists.txt`
- `src/tests/test_audiobuffer.cpp`
- `src/tests/test_mub.cpp`
- `src/tests/test_wav_adpcm.cpp`
- `src/tests/test_codeconv.cpp`
- `src/tests/test_cli.cpp`またはCLI検証用CMake script
- `src/tests/test_sdl_audio.cpp`
- `src/tests/golden/`または検証済みhash定義file

外部test frameworkは初期導入では必須としない。標準C/C++とCTestだけで十分な場合は依存を追加しない。

**Phase 1完了日: 2026-09-23**

## Phase 2: 単体試験

### AudioBuffer

- [x] 通常のread/write
- [x] ring buffer終端をまたぐwraparound
- [x] 空き容量より大きいwriteが実書込frame数を返すこと
- [x] `TickToSamples()`が小数sampleを保持すること
- [x] `ConsumeSamples()`が実書込分だけを差し引くこと
- [x] underflow回数が増えること
- [x] underflow後に送出停止状態になること
- [ ] high-water markまで再充填後に再開できること
- [x] 未生成sampleの上限とdropped sample計数
- [x] stereo frame境界が常に偶数sampleになること

### MUB

- [x] 正常なMUB headerとdata/tag/PCM offset
- [x] header未満の切断file
- [x] file末尾を越えるoffset/size
- [x] 整数overflowを誘発するoffset/size
- [x] 不正magic/version
- [x] PCMなしMUBとPCM内蔵MUB

### WAV/ADPCM

- [ ] 8/16 bit、mono/stereoの対応範囲
- [ ] `fmt `と`data`以外の未知chunk
- [ ] 奇数長chunkのpad byte
- [ ] 切断chunk
- [ ] 不正RIFF/WAVE magic
- [ ] 宣言sizeがfile sizeを越える入力
- [ ] ADPCM変換結果の長さとhash

可能なfixtureはtest code内でbyte列から生成し、不正binary fixtureを大量にrepositoryへ追加しない。

### 文字コードとpath

- [x] CP932/Shift_JISからUTF-8への既知文字列変換
- [x] legacy PCM名`ｺｰﾗｽ`の変換
- [x] 変換不能byte
- [x] 出力buffer不足
- [x] 空白、日本語、UTF-8を含むdirectory/file名
- [ ] macOSのUnicode正規化差を含むpath
- [x] 入力fileとは異なるcurrent directoryからの実行

### CLI契約

- [x] `-h`はexit 0
- [x] 引数なし、option値不足、未知optionはexit 2
- [x] 未対応のplugin/SCCI指定はexit 2
- [x] 存在しない入力、PCM、voiceはexit 1
- [x] `-g`、`-i`、`-x`ではaudio deviceを開かない
- [x] output作成失敗をexit 1として伝播する

## Phase 3: Offline結合試験

### sample compile

- [x] `package/sampl1.muc`をMUBへcompileする
- [x] `package/sampl2.muc`をMUBへcompileする
- [x] `package/sampl3.muc`をMUBへcompileする
- [x] 期待size（65,647 / 3,886 / 1,309 byte）を確認する
- [x] MUB header fieldを構造的に検査する
- [x] 検証済みbaselineとのSHA-256比較を追加する
- [x] PCMありMUBでPCM offset/sizeが非0であることを確認する

### offline出力

- [x] Sample Music 1から1秒WAVを生成する
- [x] WAVが176,444 byteであることを確認する
- [x] 44.1 kHz、16 bit、stereo、RIFF/data sizeを確認する
- [x] PCM sample領域を含むWAV全体のSHA-256を確認する
- [x] 1秒VGMのheader、size、data hashを確認する
- [x] S98のheader、size、data hashを確認する
- [x] 指定秒数が16 frameの倍数でなくても末尾sample数が正確であることを確認する

golden更新は通常のtest実行から分離し、明示的な更新commandとreviewを必要とする設計にする。

## Phase 4: SDL dummy audio試験

- [x] `SDL_AUDIODRIVER=dummy`をtest propertyへ設定する
- [x] `SDL_OpenAudioDevice`が成功することを確認する
- [x] 初回buffer充填後にdeviceを開始することを確認する
- [x] 5～10秒の固定時間で自動終了する専用test harnessを用意する
- [x] underrunが0であることを確認する
- [x] dropped samplesが0であることを確認する
- [x] timer停止、device pause/close、subsystem解放後にhangしないことを確認する
- [x] open/closeを複数回反復する
- [x] test全体へ20秒のtimeoutを設定する

realtime CLIはCtrl-Cまで終了しないため、CIから外部signalで終了させる方式を主要試験にしない。

## Phase 5: GitHub Actions

### 必須PR workflow

作成候補: `.github/workflows/macos-ci.yml`

- [ ] triggerを`pull_request`、主要branchへの`push`、`workflow_dispatch`とする
- [ ] `pull_request_target`は使用しない
- [ ] workflow/jobの権限を`permissions: contents: read`へ制限する
- [ ] 同一PRの古いrunを`concurrency`でcancelする
- [ ] 各jobへ`timeout-minutes`を設定する
- [ ] matrixの`fail-fast`をfalseにし、両architectureの結果を残す
- [ ] 使用するGitHub Actionを検証済みの完全commit SHAへ固定する
- [ ] runner imageの変更に備え、OS、Xcode、clang、CMake、SDL2 versionをlogへ出す

### runner matrix

| job | runner label | architecture | 必須 |
|---|---|---|---|
| `macos-arm64` | `macos-26` | arm64 | yes |
| `macos-intel` | `macos-26-intel` | x86_64 | yes |

`macos-latest`は参照先が変化するため使用しない。runner labelの提供状況はworkflow導入時に
GitHub公式documentationで再確認する。

### job内の基本command

```sh
brew install sdl2
cmake -S src -B build \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 \
  -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure --timeout 60
vtool -show-build build/mucom88
file build/mucom88
```

- [ ] Mach-Oの`minos`が26.0であることをscriptで検査する
- [ ] arm64 jobではarm64、Intel jobではx86_64であることを検査する
- [ ] 一方のjobでMakefile buildもsmoke testする
- [ ] CI初期段階ではHomebrew/build cacheを導入せず、再現性を優先する
- [ ] 失敗時だけtest log、hash差分、必要最小限の生成物をartifactへ保存する
- [ ] artifact retentionは7日程度に設定する

### branch protection

- [ ] `macos-arm64`をrequired status checkにする
- [ ] `macos-intel`をrequired status checkにする
- [ ] workflowが安定するまではsanitizerをrequiredにしない

## Phase 6: Nightly試験

作成候補: `.github/workflows/macos-nightly.yml`

- [ ] ASan + UBSan buildを追加する
- [ ] `-fno-omit-frame-pointer`を有効にする
- [ ] macOSで非対応のLeakSanitizerを必須条件にしない
- [ ] TSanをASan/UBSanとは別jobにする
- [ ] AudioBufferとSDL dummyの並行動作をTSanで実行する
- [ ] SDL deviceのopen/closeを100回程度反復する
- [ ] 長時間offline renderingを実行する
- [ ] 不正MUB/WAV corpusを実行する
- [ ] parserへlibFuzzerを導入するか別途判断する
- [ ] sanitizer jobが安定した後にrequired化の可否を再評価する

## Phase 7: CoreAudio実デバイス試験

GitHub-hosted runnerでは実音声出力を必須試験にしない。

### 手動release試験

- [ ] MacBook Air内蔵speakerでSample Music 1を60秒以上再生する
- [ ] PCM、FM、tempo、音切れ、クリックノイズを聴感確認する
- [ ] 終了時のunderrun/dropped samplesが0であることを確認する
- [ ] Ctrl-C後にexit 0かつhangしないことを確認する
- [ ] 48 kHzのCoreAudio device上で44.1 kHz SDL streamが動作することを確認する
- [ ] headphone/Bluetooth等、利用対象deviceが決まった場合は追加確認する

### self-hosted runnerを導入する場合

- [ ] login中のGUI sessionでrunnerを動かす
- [ ] `[self-hosted, macOS, ARM64, mucom88-audio]`のlabelを設定する
- [ ] 固定時間で終了するaudio smoke testだけを自動実行する
- [ ] device open、継続時間、underrun、dropped samples、正常終了を判定する
- [ ] 実音をloopback録音しない限り、聴感上のクリックノイズは手動判定として残す
- [ ] self-hosted runnerへrepository secretを不要にする

## 完了条件

### 最初のCI導入完了

- [ ] clean checkoutからarm64/x86_64のCMake buildが成功する
- [ ] `ctest --test-dir build --output-on-failure`が両architectureで成功する
- [ ] sample 1～3のcompile結果が検証済みgoldenと一致する
- [ ] 1秒WAVの構造、sample数、hashが一致する
- [ ] SDL dummy試験がtimeout、underrun、hangなしで終了する
- [ ] Mach-Oのdeployment targetが26.0である
- [ ] source treeがtest実行前後で変更されない

### macOS移植CIの最終完了

- [ ] ASan/UBSanの実行が成功する
- [ ] TSanで既知のdata raceがない
- [ ] 不正入力testが常設される
- [ ] 実CoreAudio deviceのrelease checklistが運用される
- [ ] CI手順とgolden更新手順がREADMEまたは開発者文書に記載される

## 参考資料

- CTest: <https://cmake.org/cmake/help/latest/module/CTest.html>
- CMake testing tutorial: <https://cmake.org/cmake/help/latest/guide/tutorial/Testing%20and%20CTest.html>
- GitHub-hosted runners: <https://docs.github.com/en/actions/reference/runners/github-hosted-runners>
- GitHub Actions matrix: <https://docs.github.com/en/actions/how-tos/write-workflows/choose-what-workflows-do/run-job-variations>
- GitHub Actions secure use: <https://docs.github.com/en/actions/reference/security/secure-use>
- macOS 26 runner image: <https://github.com/actions/runner-images/blob/main/images/macos/macos-26-Readme.md>
