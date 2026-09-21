# Windows版との機能同等化 TODO

## 目的

macOS版MUCOM88をWindows版と比較し、機能同等化に必要な作業、依存関係、完了条件を管理する。
ここでいう同等化はWindows用バイナリやWin32 APIをそのまま再現することではなく、利用者から見た
入力、出力、編集、再生、連携機能をmacOS上で同等に提供することを指す。

作成日: 2026-09-21

## 運用ルール

- Windows版との機能同等化に関係する変更を実施したときは、本ファイルを同じ変更内で更新する
- 未着手は `[ ]`、一部完了は `[-]`、完了は `[x]` とする
- 完了へ変更するときは、実装ファイルと確認方法を「実施履歴」へ記録する
- Windows版と意図的に異なる仕様は、理由と代替手段を記録して合意済みとする
- 実機、外部ハードウェア、署名環境などが必要な項目は、コード完了と実環境検証を分けて管理する
- Windows版の動作をgolden値にするときは、使用したWindows版のversionと実行条件を記録する

## 同等化レベル

| レベル | 対象 | 現状 |
|---|---|---|
| Level 1 | CLIのコンパイル、再生、各種ファイル出力 | 一部達成。厳密なWindows比較が未完了 |
| Level 2 | 通常のGUI編集・再生ワークフロー | 未着手 |
| Level 3 | FM音色エディタ、PCMツール、MIDI、モニター | 未着手または部分実装 |
| Level 4 | プラグイン、実チップ、外部ドライバ | 未着手。仕様・外部依存の確認が必要 |

## Phase 0: 互換性基準の確定

- [ ] 比較対象とするWindows版のversion、配布物、driverを固定する
- [ ] Windows GUIの全機能を操作単位で一覧化し、必須・任意・廃止候補へ分類する
- [ ] Windows CLIとmacOS CLIで共通に使用するsample、PCM、voice、ROMを固定する
- [ ] MUB、WAV、VGM、S98のWindows版golden出力を保存する
- [ ] Windows版の終了code、標準出力、標準errorの期待値を記録する
- [ ] GUI機能、plugin、実chipを含む受入試験表を作成する
- [ ] Windows固有機能について、同等動作、macOS向け代替、非対応のいずれかを決定する

### 完了条件

- 全機能に比較元、macOSでの提供方針、検証方法が割り当てられている
- golden生成に使用したWindows環境を第三者が再現できる

## Phase 1: CLIと生成物の同等化

### 現在利用可能な機能

- [x] MUCのコンパイルとMUB読込の基本経路がmacOSで動作する
- [x] MUCOM88 1.7、1.5、EM driverを選択できる
- [x] SDL2でリアルタイム音声を出力できる
- [x] WAV、VGM、S98のoffline出力経路が存在する
- [x] PCM、voice、tag、外部ROM、rhythm dataを扱うCLI経路が存在する
- [x] Ctrl-Cによる通常終了経路が存在する

上記は機能経路の存在を示すもので、Windows版との完全一致を保証するものではない。

### 未完了作業

- [ ] 同じMUCから生成したMUBをWindows版とbyte単位で比較する
- [ ] WAVのsample数、format、PCM dataをWindows版と比較する
- [ ] VGMとS98のheader、command列、時間、dataをWindows版と比較する
- [ ] MUCOM88 1.7、1.5、EMの各driverで同じ試験を実行する
- [ ] PCM内蔵・外部PCM・PCMなしのMUBを比較する
- [ ] 外部ROMとrhythm WAVを使用する入力を比較する
- [ ] CP932、Shift_JIS、UTF-8、日本語pathとtagの互換性を確認する
- [ ] 正常系・異常系の終了codeとmessageをWindows版へ合わせる
- [ ] `pcmtool`をmacOS buildへ統合し、Windows版の変換結果と比較する
- [ ] `miniplay`を正式なCMake targetにし、入出力pathと終了条件を整理する
- [ ] CLIのdriver、PCM、voice、export optionを利用者向けに文書化する

### 完了条件

- 代表fixtureについてMUBとoffline出力が合意した規則でWindows版と一致する
- 一致させないfieldがある場合は、差分理由と許容条件が文書化されている
- 自動試験で退行を検出できる

関連するCI・自動試験の詳細は `TODO.md` で管理する。

## Phase 2: コアAPIとアプリケーション境界

Windows GUIは `hspplugin/hspmucom.cpp` を経由してコアを操作する。macOS GUIから
`CMucom`、`mucomvm`や内部bufferを直接操作せずに済む、platform非依存APIを用意する。

- [ ] 初期化、終了、resetのAPIを定義する
- [ ] MUC compile、文字列compile、MUB読込のAPIを定義する
- [ ] 再生、停止、fade、早送り、低速再生、音量のAPIを定義する
- [ ] PCM、voice、tag、UUID、driver optionのAPIを定義する
- [ ] compile結果、error、現在行、再生状態を取得できるようにする
- [ ] channel状態と音源状態をsnapshotとして取得できるようにする
- [ ] MML text更新、保存要求、editor要求をUI非依存のeventへ整理する
- [ ] voice取得、更新、保存、dumpのAPIを定義する
- [ ] WAV、VGM、S98出力をGUIから安全に実行できるAPIを定義する
- [ ] object寿命、thread、callback、memory所有権を明文化する
- [ ] C++例外や内部pointerがAPI境界を越えないようにする
- [ ] API単体試験を追加する

### 完了条件

- CLIとmacOS GUIが同じコアAPIを利用する
- HSP bridgeが提供していた必須操作にmacOS側の対応APIがある
- UI threadとaudio threadの責務が明確で、終了時の競合がない

## Phase 3: macOS GUI

Windows HSP画面を直接移植せず、SwiftUI/AppKitなどmacOSで保守可能な構成として実装する。

### Documentとeditor

- [ ] 新規作成、開く、保存、別名保存を実装する
- [ ] 未保存変更の確認、autosave、crash後の復旧を実装する
- [ ] MUC/MUBのFinder関連付け、drag and drop、最近使ったfileを実装する
- [ ] MML editor、行番号、検索、compile error行への移動を実装する
- [ ] 日本語入力、CP932/UTF-8変換、macOSのUnicode正規化を検証する
- [ ] sandboxを採用する場合はsecurity-scoped bookmarkで外部dataを保持する

### 再生と表示

- [ ] compile、再生、停止、fadeのtransport UIを実装する
- [ ] 早送り、低速再生、音量を実装する
- [ ] driver、PCM、voice、ROM、rhythm directoryを選択できるようにする
- [ ] compile message、再生状態、経過時間を表示する
- [ ] channel monitorを実装する
- [ ] channel muteなどWindows版にある操作の必要性を確定して実装する
- [ ] WAV、VGM、S98出力panelを実装する

### ファイル管理と補助機能

- [ ] directory内のMUC/MUB一覧と連続再生を実装する
- [ ] automatic playerとvisual player相当機能の要否を決定する
- [ ] 行番号除去、tag追加、使用voice定義追加を実装する
- [ ] G channelの `q` から `@` への変換を実装する
- [ ] N88-BASIC出力機能の要否を決定する
- [ ] DATA/WAV/listからPCM bankを作成する画面を実装する
- [ ] 言語、font、前景色、背景色、window sizeを設定できるようにする
- [ ] update確認、Web/share menuの要否を決定する

### 完了条件

- Windows版の主要な「編集→compile→再生→修正→保存→export」操作がmacOS GUI内で完結する
- 未保存dataを通常操作や異常終了で失わない
- 必須と分類したWindows GUI機能が受入試験を通過する

## Phase 4: Audio deviceと再生制御

- [ ] `WaitSendingAudio()`にdrainまたはflushの明確な意味を定義して実装する
- [ ] backendのread量、write量、pool量、総sample数などを取得できるようにする
- [ ] 出力deviceを列挙・選択できるようにする
- [ ] default device変更、切断、再接続を処理する
- [ ] pause、resume、stop、再初期化を繰り返してもhangしないことを確認する
- [ ] requested/obtained audio formatが異なる場合の変換またはerror処理を実装する
- [ ] latencyとbuffer設定を公開する必要性を判断する
- [ ] 長時間再生、PCM再生、曲切替、終了時のclick noiseを試験する
- [ ] underrun、dropped sample、再充填回数を診断情報として取得できるようにする
- [ ] SDL2で要件を満たせない場合にのみCoreAudio native backendを検討する

### 完了条件

- Windows版と同じ利用場面で途切れ、click noise、末尾欠落が発生しない
- device変更や切断から安全に復旧するか、利用者へ明確なerrorを通知する
- 自動試験と実device聴感試験の両方を通過する

## Phase 5: FM音色エディタとMIDI

現在のFM音色エディタはWin32 window、GDI、WinMM MIDI、Windows timerへ依存している。

- [ ] `ToneParam`とvoice data処理をWin32 UIから分離する
- [ ] AR/DR/SR/RR/SL/TL/KS/ML/DT、FB、ALの編集modelを試験する
- [ ] algorithmとenvelope表示をmacOS向けに実装する
- [ ] 鍵盤表示、key-on/key-off、試聴、volume操作を実装する
- [ ] MUCOM形式とMMLDRV形式のcopy/pasteを実装する
- [ ] 256 voiceの読込、編集、復元、保存を実装する
- [ ] MMLから使用voiceを解析できるようにする
- [ ] 編集中のvoiceを再生中の音源へ安全に反映する
- [ ] WinMM MIDI入力をCoreMIDIへ置換する
- [ ] MIDI device選択、接続解除、再接続を処理する
- [ ] FM editorをpluginとして提供するかapp内moduleにするか決定する

### 完了条件

- Windows版で利用可能な音色parameterを欠落なく編集・保存できる
- MIDI keyboardから安全に試聴できる
- Windows版とのvoice data相互交換試験を通過する

## Phase 6: Plugin

既存のWindows plugin ABIはDLL、`__stdcall`、Win32 handle、C++ objectの生pointerを含むため、
既存DLLをmacOSで直接利用することは対象外とする。

- [ ] macOS pluginで提供すべきuse caseを確定する
- [ ] version付きの安定したC ABIまたはprocess間protocolを設計する
- [ ] hostとplugin間のmemory所有権、thread、lifetime、errorを規定する
- [ ] editor command、VM command、noticeの対応範囲を確定する
- [ ] raw `CMucom`/`mucomvm` pointerを公開しないinterfaceへ変更する
- [ ] `dlopen`、`dlsym`、`dlclose`を用いるloaderを実装する
- [ ] architectureとABI version不一致を安全に拒否する
- [ ] plugin crashや不正値がhostへ与える影響を抑える
- [ ] Hardened Runtime、library validation、codesign方針を決定する
- [ ] FM音色エディタをreference pluginまたは内蔵moduleとして検証する
- [ ] Windows pluginとのsource-level移植guideを作成する

### 完了条件

- 仕様化されたplugin APIとsample implementationがある
- load、通知、command、unloadを反復してもresource leakやcrashがない
- 署名済みappで採用したplugin配布方式が動作する

## Phase 7: 実チップ

Windows版のSCCI2 DLLはmacOSでは利用できないため、対象hardwareと通信仕様の確定から行う。

- [ ] 対応対象とするSCCI/G.I.M.I.C等のhardwareを決定する
- [ ] 公開SDK、USB/serial/network protocol、licenseを確認する
- [ ] 実chip機能をprovider interfaceとしてコアから分離する
- [ ] libusb、IOKit、serial等からtransportを選択する
- [ ] YM2608/YM2203の検出とclock設定を実装する
- [ ] register writeの順序、wait、bufferingを実装する
- [ ] ADPCM RAM転送とbuffer空き待ちを実装する
- [ ] reset、停止、device切断、再接続、異常終了を処理する
- [ ] GUIへdevice選択と接続状態を追加する
- [ ] 実機による音程、tempo、PCM、長時間再生の適合試験を行う
- [ ] 仕様を入手できないhardwareは非対応として明記する

### 完了条件

- 対応対象として明記したhardwareでWindows版相当の曲を再生できる
- software音源と実chip出力を安全に切り替えられる
- 未接続や途中切断でappが停止・破損しない

## Phase 8: 外部driverとMucomDotNET

- [ ] Windows版でMucomDotNETを利用する機能と利用者需要を確定する
- [ ] macOSで.NET runtimeを同梱・要求する場合の配布条件を調査する
- [ ] process分離、共通file形式、native再実装の候補を比較する
- [ ] 採用方式のdriver選択と設定UIを実装する
- [ ] 採用しない場合は非対応理由と代替driverを明記する
- [ ] 外部driverのversion不一致や起動失敗を安全に処理する

## Phase 9: 配布とmacOS統合

- [ ] `.app` bundle、CLI、resourceの正式なinstall構成を決定する
- [ ] SDL2と他のdylibをbundleへ配置し、`@rpath`を検証する
- [ ] arm64/x86_64個別配布またはUniversal Binaryの方針を決定する
- [ ] 最低対応macOS 26.0を全targetへ設定する
- [ ] appと同梱componentをDeveloper IDで署名する
- [ ] Hardened Runtime entitlementを最小化する
- [ ] Apple notarizationとstaplingを自動化する
- [ ] Gatekeeper環境のclean Macで起動試験を行う
- [ ] MUC/MUBのUTI、icon、Finder関連付けを設定する
- [ ] license、third-party notice、sample dataの配布条件を整理する
- [ ] 設定、autosave、cache、logの保存場所をmacOS標準へ合わせる
- [ ] crash logと診断情報の収集方法を用意する

### 完了条件

- cleanな対応macOSへ配布物だけでinstall・起動できる
- Gatekeeper警告や欠落libraryなしで主要workflowを実行できる
- CLIとGUIのversion、resource、driverが一貫している

## Phase 10: リリース受入試験

- [ ] Windows/macOS比較matrixの全必須項目を実行する
- [ ] sample 1～3とPCM使用曲をCLI・GUIの両方で再生する
- [ ] compile、MUB、WAV、VGM、S98の結果を比較する
- [ ] 連続再生、停止、曲切替、device切替、終了を長時間試験する
- [ ] 日本語MML、tag、path、voice名を試験する
- [ ] FM editor、CoreMIDI、plugin、実chipの採用機能を実機試験する
- [ ] arm64とx86_64またはUniversal Binaryを試験する
- [ ] 既知の非互換、未対応機能、回避策をrelease noteへ記載する

## 優先順位

1. Phase 0～1: Windows版との比較基準とCLI出力互換
2. Phase 2: GUIから利用できる安定したコアAPI
3. Phase 3～4: 通常の編集・再生を行えるmacOS app
4. Phase 5: FM音色エディタ、PCM、MIDI、monitor
5. Phase 9～10: 署名済み配布物と受入試験
6. Phase 6～8: plugin、実chip、MucomDotNET。外部仕様と需要に応じて着手する

## 実施履歴

| 日付 | 状態 | 内容 | 検証・証跡 |
|---|---|---|---|
| 2026-09-21 | 文書作成 | Windows版との機能差分を整理し、段階別TODOと完了条件を作成 | repositoryのWindows/SDL実装、HSP GUI、plugin ABI、FM editorを比較 |

