# Windows版との機能同等化 TODO

## 目的

macOS版MUCOM88を、Windows版と同じ主要な編集・compile・再生・出力・補助作業を行える
native applicationとして完成させる。本書でいう機能同等化は、Windows binary、Win32 API、画面配置、
または生成fileのbyte互換を再現することではなく、利用者がmacOS上で同じ目的を達成できることを指す。

作成日: 2026-09-21

方針更新日: 2026-09-23

## 運用ルール

- Windows版との機能同等化に関係する変更を実施したときは、本ファイルを同じ変更内で更新する
- 未着手は `[ ]`、一部完了は `[-]`、完了は `[x]` とする
- 完了へ変更するときは、実装fileと確認方法を「実施履歴」へ記録する
- Windows GUIのHSP sourceと同梱文書を機能仕様の出典とし、Windows実行環境を完了判定に使用しない
- Windows版と同じ内部実装ではなくても、同じ利用目的と操作結果を安全なmacOS native方式で提供できれば
  機能同等とする
- macOS固有のshortcut、document model、file picker、autosave、共有、更新方式を優先してよい
- 意図的に異なる仕様、除外機能、外部hardware依存機能は、理由と代替手段を明記する
- 自動試験はmacOS版の契約、構造的妥当性、round trip、決定性を検証し、Windows生成hashとの一致を要求しない

## 完了範囲と非保証事項

### 保証する範囲

- MMLの新規作成、読込、編集、保存、compile、再生
- Windows GUIの主要なdocument、transport、browser、player、monitor、tool、設定機能と同等の結果
- MUCOM88 1.7、1.5、EM driverの選択
- PCM、voice、YM2608 rhythm dataの利用
- MUB、WAV、VGM、S98のmacOS版内での生成、構造検査、再読込
- FM音色編集とCoreMIDIによる試聴
- 日本語MMLとUTF-8／CP932／Shift_JISの明示的な取扱い
- arm64、macOS 26.0以降向けの署名・notarization済み配布物

### 保証しない範囲

- macOS版が生成したMUB、VGM、S98等をWindows版が読めること
- Windows版とmacOS版の生成file、PCM sample、終了code、messageがbyte単位で一致すること
- Windows DLL/plugin ABI、HSP内部API、DirectSound、WinMMの再現
- Windows版と同一の画面配置、menu構成、shortcut表記
- Intel Mac、Rosetta、Universal Binary
- 対応hardwareと公開仕様を確保できないSCCI/G.I.M.I.C等の実chip動作

WAV、VGM、S98はWindows版との一致を保証しないが、それぞれのformatとして構造的に妥当であることは
検査する。MUBはmacOS版自身による生成・再読込・再生を保証する。

## 機能同等化レベル

| レベル | 対象 | 現状 |
|---|---|---|
| Level 1 | CLI compile、再生、MUB/WAV/VGM/S98出力 | 基本経路は実装済み。常設回帰試験と文書化が未完了 |
| Level 2 | MML editor、compile、再生、browser、export | 編集・保存・compileまで一部実装。再生以降は未完了 |
| Level 3 | player、monitor、PCM tool、FM音色editor、CoreMIDI | 未着手または旧Makefile targetのみ |
| Level 4 | 署名済み配布、更新、共有、外部provider | 未着手。実chipは拡張profile |

## 機能一覧の扱い

従来一覧化した53項目を次のように再分類する。

- **標準版必須: 49項目**
  - 従来の必須33項目
  - 従来の任意項目からSCCIとMucomDotNETを除いた14項目
  - update確認と固定SNS連携をmacOS native機能へ置換する2項目
- **拡張profile: 2項目**
  - SCCI等の実chip
  - Windows固有MucomDotNETに相当する外部driver
- **除外: 2項目**
  - Windows sourceでも値が再生処理から参照されないslow再生設定
  - 配布物に含まれずmain GUIから使用されない`vplayer.hsp`の3D visual effect

FM音色editorは標準版必須とする。ただしWindows plugin ABIは移植せず、MML editorとmodel/APIで接続する
native moduleまたは別appとして実装する。汎用plugin機構は標準版の完了条件に含めない。

## 仕様参照元

Windows実行環境の代わりに、次を機能仕様の出典とする。

| 対象 | 参照元 |
|---|---|
| main editor、menu、shortcut、設定 | `hspplugin/mucom88win.hsp`、`hspplugin/mod_mucom88.as` |
| automatic player | `hspplugin/aplayer.hsp` |
| sound monitor | `hspplugin/mucom88win.hsp`の`to_smon`関連処理 |
| FM音色editor | `muplug_fmeditor/`、`package/FmToneEditoe.txt` |
| CLI/core | `src/`、`package/readme.txt` |
| 製品version | MUCOM88 Windows 0.70、OpenMucom88 1.7d |
| 参照snapshot | repository commit `535a65c472ac67abe5f9a852092ee755e4c202c6` |

Windows package binaryのsizeとhashは出典の識別情報として保持してよいが、実行または生成物比較を
完了条件にしない。既存のWindows ARM VM用golden生成harnessは将来の任意調査用として残し、標準build、
CTest、release受入試験からは呼び出さない。

`package/mucom88.mub`は`.gitignore`対象のローカル生成物であり、参照snapshotにも含まれないため、
固定fixtureまたはWindows由来goldenとして扱わない。MUB試験はbuild directory内に都度生成する。

### 固定入力fixture

| ID | repository相対path | size (byte) | SHA-256 |
|---|---|---:|---|
| `muc-sample-1` | `package/sampl1.muc` | 1,940 | `8194fd26cee9be5f60bc57b9c6c819ce89f774bee7f6640a2ba0e21b47f898e4` |
| `muc-sample-2` | `package/sampl2.muc` | 3,968 | `0e09a4a2a475408be30e65cc28c37ace52a7d591a8b7f49e00f36ba43afed409` |
| `muc-sample-3` | `package/sampl3.muc` | 1,283 | `ff1ae3b5e3dbe8a66a165ecd286d4f71f5c3d7e36ba6f92d2c84d61506e21617` |
| `pcm-default` | `package/mucompcm.bin` | 63,640 | `29e3a31a38388eaa7cf93fb00af85e806f393a9ea5e26342f6996c8ab4af0609` |
| `voice-default` | `package/voice.dat` | 8,192 | `5a1c7121804d3e486949357d122792cb2d9cb33d18e481ae0a1a283a367c5a0f` |

外部ROMとrhythm WAVはrepositoryに再配布可能なfixtureがないため、出典とlicenseを確認してから専用fixtureを
追加する。基本fixtureを暗黙に差し替えない。

## Phase 0: 受入仕様の確定

- [x] Windows GUIを操作単位で53項目に分解する
- [x] Windows runtimeと生成物互換を完了条件から除外する
- [x] Windows GUI sourceと同梱文書を仕様参照元として固定する
- [x] 標準版49項目、拡張profile 2項目、除外2項目へ再分類する
- [x] 初期MML editorからFM音色editorとWindows plugin ABIを分離する
- [ ] 下記の各機能IDへ自動試験または手動受入手順を割り当てる
- [ ] SCCI／外部driver拡張profileのprovider境界と非対応表示を確定する
- [ ] release note用の非保証事項を定型化する

### Editor・document

- [-] `GUI-EDIT-01` 複数行MML editorと日本語encoding round trip
- [-] `GUI-EDIT-02` compile結果・error message pane
- [ ] `GUI-EDIT-03` cursor位置に追従する行番号表示
- [x] `GUI-EDIT-04` New、Open、Save、Save As、上書き確認
- [-] `GUI-EDIT-05` MUC、N88-BASIC source、任意textの読込
- [-] `GUI-EDIT-06` MMLとvoice編集のdirty state
- [x] `GUI-EDIT-07` document titleへのfile名反映
- [-] `GUI-EDIT-08` macOS標準shortcutと互換shortcut
- [x] `GUI-EDIT-09` 終了時の未保存確認

### Compile・再生

- [-] `GUI-PLAY-01` 編集中snapshotのcompileと即時再生
- [-] `GUI-PLAY-02` 構造化diagnosticとerror行への移動
- [ ] `GUI-PLAY-03` play、pause、resume、stopとEsc操作
- [ ] `GUI-PLAY-04` 押下中またはtoggleによる早送り
- [ ] `GUI-PLAY-05` x2、x4、x6、x8、x10の早送り倍率
- [x] `GUI-PLAY-06` slow再生設定を対象外と決定する
- [ ] `GUI-PLAY-07` 再生位置と最大countのprogress表示
- [ ] `GUI-PLAY-08` 再生中のsound monitor表示
- [ ] `GUI-PLAY-09` MUCOM88 1.7、1.5、EMの選択結果表示
- [ ] `GUI-PLAY-10` document相対およびdefault PCM／voice読込
- [ ] `GUI-PLAY-11` YM2608 rhythm WAV directory指定と再生

### Home・file browser・player

- [ ] `GUI-HOME-01` folder移動とMUC／N88一覧
- [ ] `GUI-HOME-02` title、author、composer、date、voice、PCM、comment preview
- [ ] `GUI-HOME-03` 選択fileをeditorで開く
- [ ] `GUI-HOME-04` editorを開かずcompile・再生
- [ ] `GUI-HOME-05` MUB export
- [ ] `GUI-HOME-06` app内playlist／automatic player
- [ ] `GUI-PLAYER-01` folder内MUCの連続再生とcompile失敗skip
- [ ] `GUI-PLAYER-02` Now Playing、tag、channel詳細
- [ ] `GUI-PLAYER-03` 最大時間・曲長割合による自動skip
- [x] `GUI-PLAYER-04` 未配布3D visualizerを対象外と決定する

### Sound monitor

- [ ] `GUI-MON-01` A～Kのvoice、volume、detune、address、key、LFO、reverb、pan、quantize表示
- [ ] `GUI-MON-02` interrupt count、current count、最大値表示

Windows GUIでchannel mute値は表示のみであり操作機能は確認できないため、mute操作は追加要件にしない。

### Tool・export

- [ ] `GUI-TOOL-01` N88行番号除去
- [ ] `GUI-TOOL-02` G channelの`q`から`@`への変換
- [ ] `GUI-TOOL-03` metadata tag追加
- [ ] `GUI-TOOL-04` 使用FM voice定義のMML追記
- [ ] `GUI-TOOL-05` PC-8801 DATAと`VOICE._n`群からPCM bank作成
- [ ] `GUI-TOOL-06` listとWAV／ADPCMからPCM bank作成
- [ ] `GUI-TOOL-07` N88-BASIC source出力
- [ ] `GUI-EXPORT-01` 指定時間のWAV出力
- [ ] `GUI-EXPORT-02` VGM／S98出力

### Settings・連携・情報

- [ ] `GUI-SET-01` user名、default voice、default PCM、rhythm directory
- [ ] `GUI-SET-02` autosave、crash recovery、世代backup
- [ ] `GUI-SET-03` 日本語・英語localization
- [ ] `GUI-SET-04` window restoration、font、文字色、背景色
- [ ] `GUI-SET-05` version付き設定schemaと安全なdefault復帰
- [ ] `GUI-FMEDIT-01` native FM音色editorの起動、編集、試聴、反映、保存
- [ ] `GUI-SCCI-01` provider選択、非対応表示、software音源への安全なfallback（拡張profile）
- [ ] `GUI-DOTNET-01` nativeまたはprocess分離した外部driver provider（拡張profile）
- [ ] `GUI-UPD-01` 署名を検証でき、無効化可能なmacOS向け更新確認
- [ ] `GUI-WEB-01` Help menuからHTTPS documentationを開く
- [ ] `GUI-SHARE-01` dataを無断送信しないmacOS標準共有
- [ ] `GUI-ABOUT-01` app/core version、著作権、license、credit表示

## Phase 1: macOS native回帰試験

### 現在利用可能な機能

- [x] MUC compileとMUB読込の基本経路
- [x] MUCOM88 1.7、1.5、EM driver選択
- [x] SDL2 realtime audio
- [x] WAV、VGM、S98 offline出力
- [x] PCM、voice、tag、外部ROM、rhythm dataを扱うCLI経路
- [x] Ctrl-Cによる通常終了
- [x] `miniplay` compile時の不要な別audio device／timer起動を排除

### 追加する常設試験

- [ ] CTestを`src/tests/CMakeLists.txt`へ分離し、testごとのtimeoutと作業directoryを設定する
- [ ] sample 1～3を各2回compileし、macOS版内で決定的であることを確認する
- [ ] 生成MUBを再読込し、PCMあり・なしを再生できることを確認する
- [ ] MUCOM88 1.7、1.5、EMを個別に試験する
- [ ] MUBのheader、section、offset、size、tag、PCM領域を独立parserで検査する
- [ ] WAVのRIFF size、44.1 kHz、16 bit、stereo、sample数、PCM非無音を検査する
- [ ] VGM 1.70 header、YM2608 clock、command列、総sample数、終端を検査する
- [ ] S98 v3 header、device table、wait command、終端を検査する
- [ ] 切断file、不正magic、不正offset、整数overflowを拒否する
- [ ] Debug／Release、`-O0`／`-O3`で意味的な結果が一致することを確認する
- [ ] CP932、Shift_JIS、UTF-8、日本語・空白pathを試験する
- [ ] macOS版として定義したCLI終了codeとstdout／stderrを固定する
- [ ] `-g`、`-i`、`-x`でaudio deviceを開かないことを確認する
- [ ] SDL dummyでopen／play／stop／closeを反復し、hang、underrun、破棄sampleがないことを確認する
- [ ] ASan／UBSan／TSanを実行する
- [ ] test前後でsource treeが変更されないことを確認する

macOS native baselineには入力hash、artifactの構造値とhash、macOS、clang、SDL、build type、architectureを
記録する。これはWindows互換goldenではなく、macOS版のregression baselineとする。

## Phase 2: Core APIとapplication境界

- [-] `DocumentService`: encoding、読込、保存、autosave、recovery
- [-] `CompileService`: text snapshot、driver、resource解決、構造化diagnostic
- [ ] `PlaybackSession`: play、pause、resume、stop、早送り、曲末尾、progress
- [ ] `ExportService`: MUB、WAV、VGM、S98、progress、cancel、error
- [ ] `MonitorSnapshot`: 11 channelとinterrupt／count状態
- [ ] `VoiceService`: voice bankの読込、編集、保存、試聴
- [ ] audio device列挙、選択、切断、再接続を行う`AudioDeviceService`
- [ ] 全serviceでobject寿命、thread、callback、memory所有権を明文化する
- [ ] C++例外、内部pointer、`CMucom`／`mucomvm`をUI境界へ公開しない
- [ ] operation IDとcancelを導入し、古い非同期結果が新しいdocument状態を上書きしないようにする
- [ ] service単体試験を追加する

AppKit UIは上記serviceだけを利用する。compile、再生、exportはworker queueで実行し、UI更新はmain threadへ
immutable snapshotとして渡す。一つのaudio outputを複数documentが競合して所有しないよう、active sessionの
切替規則を定義する。

## Phase 3: MML editor完成

- [x] 新規作成、開く、UTF-8保存、別名保存、標準dirty確認
- [-] compile結果と先頭error行の表示
- [ ] UTF-8、CP932、Shift_JISの判定と元encodingへのround trip
- [ ] 行番号gutter、検索、置換、指定行移動
- [ ] MUC、N88-BASIC source、任意textのtype判定
- [ ] drag and drop、最近使ったfile、複数document
- [ ] autosave、世代backup、crash recovery
- [ ] macOS標準shortcutとWindows互換shortcutの割当
- [ ] Finder関連付けとUTI
- [ ] sandbox採用時のsecurity-scoped bookmark

完了条件: 日本語を含む既存MMLを無損失で開き、編集、保存、再openでき、compile errorから該当行へ
移動できる。

## Phase 4: 再生GUIとAudio

- [ ] 編集中snapshotの非同期compile・即時再生
- [ ] play、pause、resume、stop、Esc操作
- [ ] x2、x4、x6、x8、x10の早送り
- [ ] 再生位置、最大count、driver、状態表示
- [ ] PCM、voice、ROM、rhythm directoryの選択
- [ ] output device列挙、選択、default変更、切断、再接続
- [ ] requested／obtained format差の変換または明確なerror
- [ ] 曲末尾の自動停止と次曲への遷移
- [ ] pause／resume／stop／再初期化の反復試験
- [ ] 内蔵speakerでPCMを含む曲を60分以上再生する
- [ ] tempo、音切れ、click、終了時noiseを聴感確認する
- [ ] underrun、dropped sample、再充填回数を診断表示する

完了条件: 実CoreAudio deviceで長時間再生し、操作、曲切替、終了にhangがなく、既知のclick、tempo変動、
PCM欠落がない。

## Phase 5: Home、player、monitor

- [ ] folder sidebarとMUC／N88一覧
- [ ] 選択fileのmetadata inspector
- [ ] editorで開く、直接再生、MUB export
- [ ] playlist、連続再生、compile失敗skip、loop
- [ ] 最大演奏時間・曲長割合による自動skip
- [ ] Now Playingと曲切替表示
- [ ] 11 channelのmonitor view
- [ ] monitor更新頻度を制限し、audio threadをblockしない
- [ ] document、browser、playlist間で同じactive playback状態を共有する

## Phase 6: Toolとexport

- [ ] text transformをUIから分離し、previewとUndoを提供する
- [ ] N88行番号除去、G channel変換、metadata tag追加
- [ ] 使用FM voice定義の追記
- [ ] N88-BASIC source出力
- [ ] `pcmtool`をportable libraryとCMake targetへ整理する
- [ ] DATA／`VOICE._n`、list、WAV／ADPCMからPCM bankを作成する
- [ ] MUB、WAV、VGM、S98 save panelと非同期export
- [ ] export中のprogress、cancel、失敗時の一時file削除
- [ ] 生成物をmacOS版で再読込できることを確認する
- [ ] WAV／VGM／S98を独立parserで構造検査する

Windows版での読込可否とbyte一致は完了条件にしない。

## Phase 7: Native FM音色editorとCoreMIDI

- [ ] `ToneParam`とvoice処理をWin32 UIから分離する
- [ ] 256 voiceの読込、編集、復元、保存model
- [ ] AR／DR／SR／RR／SL／TL／KS／ML／DT、FB、AL
- [ ] algorithm、envelope、keyboard表示
- [ ] key-on／key-off、volume、試聴
- [ ] MUCOM形式とMMLDRV形式のcopy／paste
- [ ] MMLから使用voiceを抽出する
- [ ] 編集中voiceを再生へ安全に反映する
- [ ] voice変更をdocument dirty stateへ統合する
- [ ] CoreMIDI device選択、入力、切断、再接続
- [ ] MML editorとFM editorをmodel/APIで接続し、Windows plugin ABIを使用しない

完了条件: WindowsのV.EDITで行う主要な音色作成・試聴・保存を、native UIだけで完結できる。

## Phase 8: Settings、更新、共有、外部provider

- [ ] `UserDefaults`へversion付き設定schemaを実装する
- [ ] user名、default resource、autosave、早送り、window、font、色を保存する
- [ ] 日本語・英語localization resourceを作成する
- [ ] standard About panelとlicense／credit表示
- [ ] Help menuからHTTPS documentationを開く
- [ ] `NSSharingService`等で明示操作時だけ共有する
- [ ] 更新機構を採用する場合、署名検証、無効化、失敗時の通常起動を保証する
- [ ] 実chip／外部driverをversion付きprovider interfaceでcoreから分離する
- [ ] providerがない環境で理由とsoftware音源への代替を表示する

### 拡張profile

- [ ] SCCI／G.I.M.I.C等で対応対象とするhardwareと公開protocolを確保する
- [ ] libusb、IOKit、serial、networkからtransportを選択する
- [ ] register write、wait、ADPCM転送、切断、再接続を実装・実機検証する
- [ ] MucomDotNET固有の利用目的を特定し、native再実装またはprocess分離providerを用意する

拡張profileは標準版releaseを妨げない。対応済みと表示する機能だけを、対象hardwareまたは外部driverで
実際に検証する。

## Phase 9: 配布とmacOS統合

- [ ] `.app`、CLI、resourceの正式なinstall構成を決定する
- [ ] SDL2と必要なdylibをbundleへ配置し、`@rpath`を検証する
- [x] 最低対応macOSを26.0、主architectureをarm64とする
- [ ] app icon、MUC／MUB UTI、Finder関連付け
- [ ] Developer ID署名とHardened Runtime entitlement
- [ ] notarizationとstapling
- [ ] license、third-party notice、sample dataの配布条件
- [ ] Application Support、Preferences、Autosave、Caches、Logsの配置
- [ ] crash logと診断情報のexport
- [ ] clean user環境で配布物だけを使ったinstall／起動試験

Universal BinaryとIntel Macは標準版の完了条件にしない。

## Phase 10: 最終受入試験

- [ ] 標準版49項目すべてに実装結果と受入証跡がある
- [ ] GUIだけで新規作成、編集、compile、再生、停止、exportが完結する
- [ ] sample 1～3とPCM使用曲をCLI・GUIの両方で再生する
- [ ] browser、playlist、monitor、PCM tool、FM音色editorが動作する
- [ ] 日本語MML、tag、path、voice名をround tripする
- [ ] 連続再生、停止、曲切替、device切替、終了を長時間試験する
- [ ] macOS native regression、format parser、異常系、sanitizerが成功する
- [ ] 署名・notarization済みappをcleanなmacOS 26環境で起動する
- [ ] appとCLIのversion、resource、driverが一致する
- [ ] 非保証事項、除外2項目、拡張profileの対応状況をrelease noteへ記載する
- [ ] macOS版で生成したMUBをmacOS版自身が再読込・再生できる

## 実装順序

1. Phase 0～1: 受入仕様とmacOS native回帰試験
2. Phase 2: service境界、thread、lifetime、cancel
3. Phase 3～4: editor完成、GUI再生、実audio device
4. Phase 5～6: browser、player、monitor、tool、export
5. Phase 7: native FM音色editorとCoreMIDI
6. Phase 8: settings、localization、更新、共有、provider境界
7. Phase 9～10: 署名済み配布物と最終受入
8. 拡張profile: 実chip、外部driver。標準版後または外部仕様・hardware確保後

次に実装すべき対象は、Phase 1の常設回帰試験と、Phase 2の`PlaybackSession`である。Windows golden生成、
VM導入、Windows CLI比較は先行条件にしない。

## 実施履歴

| 日付 | 状態 | 内容 | 検証・証跡 |
|---|---|---|---|
| 2026-09-21 | 文書作成 | Windows版との機能差分を整理し、段階別TODOと完了条件を作成 | Windows／SDL実装、HSP GUI、plugin ABI、FM editorを比較 |
| 2026-09-21 | Phase 0一部完了 | Windows GUIを操作単位で53項目へ分解 | `mucom88win.hsp`、`mod_mucom88.as`、`aplayer.hsp`、`vplayer.hsp`、同梱文書を照合 |
| 2026-09-21 | Phase 2/3一部実装 | MML editorからFM音色editor／pluginを分離し、text-only documentとsnapshot compileを追加 | `editor_core_test`で相対voice／PCM compile、dirty状態、NUL拒否を確認 |
| 2026-09-23 | Phase 1/4一部実装 | compile時の不要なSDL audio／timer初期化を廃止し、audio生成threadの停止順を修正 | SDL dummyで60回連続起動・停止、MUB hash一致、CTest成功。実deviceは未完了 |
| 2026-09-23 | Phase 3一部実装 | AppKitの`MUCOM88Editor.app`を追加 | 新規、open、UTF-8保存、別名保存、dirty確認、compile、error行表示、ad-hoc署名を確認 |
| 2026-09-23 | 任意調査基盤 | Windows ARM VM用golden生成harnessを追加 | manifest verifierをsynthetic candidateで確認。Windows実行は未実施 |
| 2026-09-23 | 方針再構築 | Windows runtime／生成物互換を完了条件から除外し、GUI機能同等化とmacOS native受入へ変更 | 53項目を標準版49、拡張profile 2、除外2へ再分類。VMとWindows goldenをrelease gateから除外 |
