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
| Level 1 | CLI compile、再生、MUB/WAV/VGM/S98出力 | 基本経路とmacOS native常設回帰試験17件を実装済み |
| Level 2 | MML editor、compile、再生、browser、export | editorの編集・保存・非同期compileとPhase 2 core serviceを実装。再生等のUI接続は未完了 |
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
- [x] 全53機能IDへ自動試験または手動受入手順を割り当てる
  （`tests/manual/macos-gui-acceptance.md`）
- [x] SCCI／外部driver拡張profileのprovider境界と非対応表示を確定する
  （`docs/MACOS-PROVIDER-CONTRACT.md`）
- [x] release note用の非保証事項を定型化する
  （`docs/RELEASE-NOTES-TEMPLATE.md`）

### 受入仕様の割当

| 機能群 | ID数 | 主な自動試験 | 主な手動受入 | 実装Phase |
|---|---:|---|---|---:|
| Editor・document | 9 | document、encoding、diagnostic、dirty state | menu、shortcut、window、保存確認 | 3 |
| Compile・再生 | 11 | playback state、fake clock、driver／resource解決 | transport、実音声、monitor遷移 | 2、4 |
| Home・browser | 6 | directory列挙、metadata、action dispatch | sidebar、preview、直接再生 | 5 |
| Player | 4 | playlist、skip、loop、Now Playing | 連続再生と曲切替 | 5 |
| Sound monitor | 2 | immutable snapshotと11 channel mapping | 実再生中の更新と性能 | 2、5 |
| Tool・export | 9 | text変換、PCM bank、format parser | preview、save panel、cancel | 6 |
| Settings・連携・情報 | 12 | settings migration、voice、provider、URL／署名mock | preferences、FM試聴、共有、About | 7、8 |

全53項目は標準版49、拡張profile 2、除外2に対応する。受入手順の割当完了は機能実装の完了を意味しない。
以下のチェックリストはPhase 3～8にまたがる実装進捗として管理する。

**Phase 0完了日: 2026-09-23**

## GUI機能別実装進捗（Phase 3～8横断）

### Editor・document

- [x] `GUI-EDIT-01` 複数行MML editorと日本語encoding round trip
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

- [x] CTestを`src/tests/CMakeLists.txt`へ分離し、testごとのtimeoutと作業directoryを設定する
- [x] sample 1～3を各2回compileし、macOS版内で決定的であることを確認する
- [x] 生成MUBを再読込し、PCMあり・なしを再生できることを確認する
- [x] MUCOM88 1.7、1.5、EMを個別に試験する
- [x] MUBのheader、section、offset、size、tag、PCM領域を独立parserで検査する
- [x] WAVのRIFF size、44.1 kHz、16 bit、stereo、sample数、PCM非無音を検査する
- [x] VGM 1.70 header、YM2608 clock、command列、総sample数、終端を検査する
- [x] S98 v3 header、device table、wait command、終端を検査する
- [x] 切断file、不正magic、不正offset、整数overflowを拒否する
- [x] Debug／Release、`-O0`／`-O3`で意味的な結果が一致することを確認する
- [x] CP932、Shift_JIS、UTF-8、日本語・空白pathを試験する
- [x] macOS版として定義したCLI終了codeとstdout／stderrを固定する
- [x] `-g`、`-i`、`-x`でaudio deviceを開かないことを確認する
- [x] SDL dummyでopen／play／stop／closeを反復し、hang、underrun、破棄sampleがないことを確認する
- [x] ASan／UBSan／TSanを実行する
- [x] test前後でsource treeが変更されないことを確認する

macOS native baselineには入力hash、artifactの構造値とhash、macOS、clang、SDL、build type、architectureを
記録する。これはWindows互換goldenではなく、macOS版のregression baselineとする。

**Phase 1完了日: 2026-09-23**

## Phase 2: Core APIとapplication境界

- [x] `DocumentService`: encoding、読込、保存、autosave data、recovery
- [x] `CompileService`: text snapshot、driver、resource解決、構造化diagnostic
- [x] `PlaybackSession`: play、pause、resume、stop、早送り、曲末尾、progress
- [x] `ExportService`: MUB、WAV、VGM、S98、progress、cancel、error
- [x] `MonitorSnapshot`: 11 channelとinterrupt／count状態
- [x] `VoiceService`: voice bankの読込、編集、保存、試聴request
- [x] audio device列挙、選択、切断、再接続を行う`AudioDeviceService`
- [x] 全serviceでobject寿命、thread、callback、memory所有権を明文化する
- [x] C++例外、内部pointer、`CMucom`／`mucomvm`をUI境界へ公開しない
- [x] operation IDとcancelを導入し、古い非同期結果が新しいdocument状態を上書きしないようにする
- [x] service単体試験を追加する

AppKit UIは上記serviceだけを利用する。compile、再生、exportはworker queueで実行し、UI更新はmain threadへ
immutable snapshotとして渡す。一つのaudio outputを複数documentが競合して所有しないよう、active sessionの
切替規則を定義する。

### Phase 2設計の前提

Phase 2は既存の`CMucom`をそのままUI向けclassで包む作業としない。次の3点を先に
成立させる。

1. compile結果を`CMucom`内部の`musbuf[0]`から切り離し、所有権を持つ不変なMUB dataにする
2. GUI再生時はVM、Z80、channel dataを単一のplayback workerからだけ操作する
3. audio deviceとactive playback sessionをdocumentごとではなくapplication全体で1つだけ所有する

現在の`MucomCompileService`はcompile、`PlayCompiled`、`Stop`を同一runtimeで扱い、AppKit側は
documentごとにそのruntimeを所有している。この構造のまま再生を追加すると複数documentがaudio
deviceを競合するため、compileとplaybackの分離を先行する。

また、現在のSDL timer callbackは`mucomvm::UpdateTime`を呼び出すが、`busyflag`と`tmflag`は
thread同期用ではない。GUI側からstop、resume、monitor取得を並行させる設計にはしない。

### 目標とする所有関係

```text
NSDocument
   |
   +-- DocumentService -- DocumentSnapshot
   |                         |
   |                         v
   +------------------ CompileService -- CompiledSong
                                          |
                         +----------------+----------------+
                         v                                 v
                 PlaybackCoordinator                 ExportService
                         |
                  PlaybackSession
                         |
                 AudioDeviceService
                         |
                 SDL_OpenAudioDevice

PlaybackSession -- immutable MonitorSnapshot
VoiceService    -- immutable VoiceBank
```

`CMucom`、`mucomvm`、`PCHDATA`、`MUCOM88_VOICEFORMAT`はservice実装の内側だけで使用する。
AppKitと将来のbrowser、player、FM音色editorはserviceの値型だけを参照する。

### 全service共通contract

次の値型をserviceに先行して定義する。

- `DocumentId`: documentを識別するUUID
- `Revision`: textまたはvoiceの変更ごとに増える番号
- `OperationId`: compile、save、export等の非同期要求ごとの番号
- `SessionId`: active playbackの切替ごとに増える番号
- `CancellationToken`: workerが処理単位ごとに確認する協調的なcancel
- `ServiceError`: domain、code、message、path、recoverableを持つerror値
- `OperationResult<T>`: 成功値または`ServiceError`
- `OperationHandle`: `OperationId`と`Cancel()`だけを公開するhandle

共通規則は次の通りとする。

- public service APIの外へC++例外を出さず、入口でcatchして`ServiceError`へ変換する
- callbackに`OperationId`、`DocumentId`、`Revision`を含め、UIは現在revisionと一致しない結果を破棄する
- completion callbackはUI dispatcher経由でmain threadへ送る
- operationのcompletionは原則1回とし、購読解除済みのUI objectには送らない
- serviceはraw pointer、内部bufferへの参照、不定な寿命の`string_view`を返さない
- documentが閉じた後もworkerがAppKit objectを参照しない
- shutdownは「新規受付停止→operation cancel→playback worker停止→audio device close→queue破棄」の順に行う

### `DocumentService`

service内部のtextはUTF-8に正規化し、UIへは次の情報を持つ不変な`DocumentSnapshot`を渡す。

- UTF-8 text、元encoding、BOMの有無、改行形式
- path、resource root、`DocumentId`、現在revision、保存済みrevision
- 元fileのsize、mtime、hashとrecovery ID

encodingはBOM付きUTF-8、strict UTF-8、CP932／Shift_JISの順に判定する。CP932とShift_JISを
自動判別できない場合は推定値として保持し、UIから明示変更できるcontractにする。
保存時に元encodingへ無損失変換できない場合は暗黙置換せずerrorとし、UTF-8での別名保存を
選択できるようにする。

保存は同一directoryの一時fileへ書き、close成功後にrenameするatomic saveとする。非同期保存中に
編集が進んだ場合は、保存対象revisionだけを保存済みとし、新しいrevisionを誤ってcleanにしない。
autosave／recoveryは元fileを上書きせず、Application Support配下へdocument ID、revision、encoding、
元path、text hashを持つsnapshotを保存する。

### `CompileService`

`PlayCompiled`と`Stop`をCompileServiceから分離し、compileの入出力を不変な値に限定する。

`CompileRequest`はtext snapshot、document ID／revision、driver、compile option、resource root、
voice／PCM／rhythmの明示的な解決結果を持つ。`CompileResult`はstatus、driver、構造化diagnostic、
transcriptと`CompiledSong`を返す。

`CompiledSong`は完全なMUB byte列、driver、max count、tag、source revision、resource情報、
artifact hashを所有する。一時file経由ではなく、`CMucom`内部の`CMemBuf`から
`std::vector<uint8_t>`へcopyする内部APIを追加する。runtimeを破棄した後も`CompiledSong`を
再生、export、検査で使用できることを保証する。

diagnosticはmessage textへの`sscanf`だけに依存せず、compilerが検出したmessage IDと行番号を
runtime adapterで構造化する。legacy runtimeへの再入を避けるため、当面は1本のserial compile queueで
実行する。

### `PlaybackSession`とGUI用audio engine

application全体で`PlaybackCoordinator`を1つだけ作成し、documentは`CompiledSong`と再生optionを
渡す。状態遷移は次の通りとする。

```text
Idle -> Preparing -> Buffering -> Playing <-> Paused
                                  |
                                  +-> Draining -> Finished -> Idle
                                  +-> Stopping -> Idle
                                  +-> DeviceLost / Failed
```

- `Pause`は`CMucom::Stop`を呼ぶがruntimeとsongを保持する
- `Resume`は`CMucom::Restart`を呼び、pause前の位置から再開する
- `Stop`はaudio bufferをflushし、runtimeとsongを破棄する
- 別documentの`Play`は旧sessionをstopし、bufferをflushして`SessionId`を更新する
- 旧`SessionId`から届いたstate、progress、monitor callbackは破棄する
- 早送り倍率は`x1`、`x2`、`x4`、`x6`、`x8`、`x10`に制限する。`x1`はFASTFW解除、それ以外は
  倍率設定後にFASTFWを有効化する

GUI再生では`CMucom`を`MUCOM_OPTION_STEP`で初期化する。専用playback workerがcommandを処理し、
`RenderAudio`で一定frameを生成してSDL ring bufferへ書き、同じthreadでmonitor snapshotを生成する。
SDL audio callbackはring bufferの消費だけを行い、VMやZ80に触れない。CLIの既存realtime経路は
変更せず併存させる。

#### 曲末判定

`MUCOM_STATUS_COUNT >= MUCOM_STATUS_MAXCOUNT`は曲末判定に使用しない。`COUNT`はmax countで
剩余を取り、loop曲を誤停止するためである。

loopなし短曲、明示loop曲、PCM曲、空channelを含む曲をMUCOM88 1.7、1.5、EMでcompile・再生し、
compilerが返すchannel別total／loop countとabsolute interrupt countの関係をcharacterization testで固定した。
runtimeには3 driverで共通利用できる安定した終端flag APIがないため、公開済みcompile metadataを
`PlaybackTerminationState`相当の判定値として使用する。

- loopなし曲は全有効channelの終了を1回だけ通知する
- loop曲は`Finished`にせず、loop境界とloop回数を通知する
- driver終端検出後、既にring bufferにある音声が排出された時点を`Finished`とする
- Windows automatic playerの時間／曲長割合によるskipはPhase 5のpolicyとし、自然終了と分離する

### `MonitorSnapshot`

`PCHDATA`はUIへ渡さず、playback workerで11 channelを一括copyする。channel値はA～Kの識別子、
mute、voice番号、volume、detune、data address、note／key-on、LFO、reverb、pan、quantizeを持つ。
全体値は`SessionId`、playback state、driver、absolute interrupt count、current count、max count、
loop count、underrun、dropped sampleを持つ。

snapshotは不変値として公開し、UIは15～30 Hzで最新snapshotだけを取得する。callback内で
AppKit描画完了を待たず、audio callbackやplayback workerをblockしない。

### `AudioDeviceService`

次の責務をplayback runtimeから分離する。

- output device列挙、default device表現、device選択とopen
- requested／obtained formatの報告
- device切断通知、再列挙、明示的な再接続
- underrun、dropped sample、bufferの診断値

SDL2のdevice indexは永続IDとして保存せず、列挙generation内だけ有効とする。設定には
device名とdefault選択を保存し、起動時に再解決する。安定したmacOS固有IDが必要になった場合は
CoreAudio UID backendを追加する。

Phase 2では44.1 kHz、signed 16 bit、stereoを取得できない場合に構造化した
`UnsupportedFormat`を返す。暗黙のformat変換は行わず、必要な場合はPhase 4で変換処理を追加する。
device切断時に別deviceへ無断で切り替えず、sessionを`DeviceLost`へ遷移させて明示的な再接続を行う。

### `ExportService`

PlaybackSessionとruntimeを共有せず、export operationごとにstep mode runtimeを所有する。

- MUBは`CompiledSong`のbyte列をatomic saveする
- WAV／VGM／S98は同一directoryの一時fileへ一定frameずつrenderする
- 各chunk間でcancelを確認し、生成sample数からprogressを通知する
- writerを明示closeした後だけ完成file名へrenameする
- cancelまたはerror時は一時fileを削除する
- 生成物を既存の独立parserで検査し、構造不正時は完成扱いにしない

既存の`CMucom::Record(seconds)`は同期loopでprogressとcancelを挿入できないため、ExportServiceから
直接使用せずchunk単位のrenderへ分解する。

### `VoiceService`

compiler依存のbit-field配置を持つ`MUCOM88_VOICEFORMAT`をUI境界へ出さない。次の正規化した
値型と256音色の`VoiceBank`を作る。

- operatorごとのDT、ML、TL、KS、AR、DR、SR、SL、RR、AM
- AL、FB、6文字名、voice番号
- 元bank、編集中bank、revision、dirty state

load、field範囲のvalidate、undo用copy、atomic save、8192 byte round tripをserviceの責務にする。
compileまたはplaybackへ適用する時だけ内部形式へserializeする。試聴はVoiceServiceがaudio deviceを
開かず、`PlaybackCoordinator`へ`VoicePreviewRequest`を送り、通常再生との排他を同じ場所で管理する。

### 実装順序と各段階の完了gate

| 段階 | 実施内容 | 完了gate |
|---|---|---|
| 2-0 | 曲末、pause／resume、channel countのcharacterization test | 1.7、1.5、EMの差を固定 |
| 2-1 | 共通値型、operation ID、cancel、dispatcher、error | stale結果、cancel、callback回数の単体試験 |
| 2-2 | `DocumentService`完成 | encoding round trip、atomic save、外部変更、recovery試験 |
| 2-3 | `CompileService`の純粋化とowned MUB | runtime破棄後のMUB再読込・再生 |
| 2-4 | `AudioDeviceService`とfake audio output | 列挙、open失敗、切断、再接続試験 |
| 2-5 | step mode `PlaybackSession`と状態機械 | play、pause、resume、stop、早送り、曲末試験 |
| 2-6 | `MonitorSnapshot` | 11 channel mapping、古いsessionの破棄、thread raceなし |
| 2-7 | `ExportService` | 4形式、progress、cancel、partial file削除 |
| 2-8 | `VoiceService` | 256音色、8192 byte round trip、全field境界値試験 |
| 2-9 | AppKitのservice経由化 | main thread compileなし、複数documentのaudio競合なし |

2-0と2-1を先行し、2-2と2-3でdocument snapshotから`CompiledSong`までの不変data flowを
完成させる。その後に2-4と2-5を実施し、audio所有権とplayback threadを固定してから
monitor、export、voice、AppKitの順に接続する。

### Phase 2で追加する常設試験

- `document_service_test`: encoding、改行、atomic save、外部変更、recovery
- `compile_service_test`: snapshot compile、driver／resource解決、diagnostic、owned MUB
- `operation_lifecycle_test`: cancel、stale revision、completion回数、document close
- `playback_session_test`: SDL dummyで状態遷移、早送り、active session切替
- `playback_end_detection_test`: 3 driverの有限曲、loop曲、PCM曲
- `monitor_snapshot_test`: 11 channel変換、count、session ID、snapshot不変性
- `audio_device_service_test`: SDL dummyのopen／close、format error、切断／再接続mock
- `export_service_test`: MUB／WAV／VGM／S98、progress、cancel、生成物の独立構造検査
- `voice_service_test`: 8192 byte round trip、field範囲、dirty state、atomic save
- `app_service_lifetime_test`: shutdown順序、callback破棄、use-after-free防止

既存のPhase 1試験7件を残し、Phase 2試験もRelease、Debug、ASan／UBSan、TSanで実行する。
実CoreAudio deviceの60分連続再生と聴感試験はPhase 4の完了条件とし、Phase 2の自動試験と分離する。

**Phase 2完了日: 2026-09-23**

### Phase 2完了条件

- AppKitから`cmucom.h`、`mucomvm.h`、`PCHDATA`、`MUCOM88_VOICEFORMAT`が見えない
- compile結果がruntime寿命から独立した`CompiledSong`になっている
- compile、playback、exportがAppKit main threadをblockしない
- application内のaudio outputが常に1つで、active session切替規則が自動試験されている
- playback runtimeを変更するthreadが1つに限定されている
- 全非同期処理にoperation ID、revision、cancelがある
- document、compile、playback、export、monitor、voice、audio deviceの単体試験がある
- 旧session、古いdocument revision、破棄済みUIへのcallbackが無視される
- service実装がprocess current directoryを変更しない
- CLIとPhase 1の常設回帰試験が引き続き成功する
- 未完了の実device長時間試験とGUI操作試験がPhase 3／4の項目として明確に分離されている

## Phase 3: MML editor完成

- [x] 新規作成、開く、UTF-8保存、別名保存、標準dirty確認
- [-] compile結果と先頭error行の表示
- [-] UTF-8、CP932、Shift_JISの判定と元encodingへのround trip
- [ ] 行番号gutter、検索、置換、指定行移動
- [ ] MUC、N88-BASIC source、任意textのtype判定
- [ ] drag and drop、最近使ったfile、複数document
- [ ] autosave、世代backup、crash recovery
- [ ] macOS標準shortcutとWindows互換shortcutの割当
- [ ] Finder関連付けとUTI
- [ ] sandbox採用時のsecurity-scoped bookmark

encodingは一般的なUTF-8／CP932文書のround tripまで実装済みである。ただしCP932はShift_JISの
大部分を包含するため、現在の「CP932変換を先に試す」実装では両者を厳密に自動判別できない。
また、最初に見つけた改行形式へ文書全体を正規化するため、混在改行の保持は未完了である。
このため、明示的なencoding選択と混在改行の扱いが完成するまで一部完了とする。

### Phase 3の実装原則

- AppKitの`NSDocument`をfile coordination、safe save、window、最近使ったfileの入口とする
- `DocumentService`をtext、encoding、改行、document type、revision、dirty状態の唯一のmodelとする
- `MmlDocument`を独立したproduction modelとして残さず、`DocumentService`の互換wrapperにするか利用側を移行する
- AppKitのUTF-16 indexとcoreのUTF-8 byte offsetを直接混在させず、行・文字位置変換を一箇所へ集約する
- compile、recovery書込等の重い処理はmain threadで実行せず、結果はdocument IDとrevisionを照合して反映する
- Phase 3ではeditorを完成させ、再生、停止、早送り、audio device操作はPhase 4に残す

### 3-0: document状態と保存処理の一元化

現状のAppKit保存は`DocumentService::EncodedData()`から得たdataを`NSDocument`へ渡すが、
`DocumentService::SaveAs()`を通らない。このため保存成功後もservice側の保存済みrevision、path、
resource directory、encoding推定状態、file fingerprintが更新されない。autosave、外部更新検出、
crash recoveryを追加する前に次のcontractへ変更する。

- `PrepareSave`は保存先、encoding、対象revision、書込byte列、content IDを持つ`SavePlan`を返す
- `NSDocument`は`SavePlan`を使い、標準のsafe save／file coordinationで書き込む
- 書込成功後だけ`AcknowledgeSave`を呼び、path、resource directory、encoding、fingerprintを更新する
- 保存中に追加編集された場合、保存対象revisionだけを記録し、現在内容はdirtyのままにする
- open、Save As、Finder上のrename後は`AssociateLocation`でserviceのlocationを同期する
- `DocumentSnapshot::IsModified()`はrevisionの単純比較だけに依存せず、現在content IDと保存済みcontent IDを
  比較する。revisionは非同期結果のstale判定用に単調増加を維持する
- Undoで保存時と同じ内容へ戻った場合はcleanへ戻り、Redoで再びdirtyになることを試験する
- 将来のFM音色変更はdirty contributorとして追加できる境界だけを用意し、音色editor自体はPhase 7で実装する

### 3-1: encoding、改行、document type

`DocumentSnapshot`へ`DocumentKind`、encoding判定の信頼度、必要な場合は元byte列と行単位の改行情報を追加する。

- `DocumentKind`は`Muc`、`N88Basic`、`PlainText`の3値とする
- type判定はUTI、拡張子、内容の順に行い、確定できない有効textは`PlainText`へfallbackする
- N88 sourceはopen時に行番号を除去せず、そのまま編集、保存、compileする。行番号除去はPhase 6のtoolとする
- BOM付きUTF-8とstrict UTF-8は確定判定する
- CP932／Shift_JISの曖昧な入力はCP932をdefault推定値とし、status表示とEncoding menuで明示変更できるようにする
- 未編集状態でencodingを変更する場合は元byte列から再decodeし、編集後は保存encodingの変更として扱う
- 元encodingで表現不能な文字を暗黙置換せず、UTF-8で別名保存するか取消する
- 改行は`LF`、`CRLF`、`CR`、`Mixed`を区別し、未編集行の改行を保持する。新規行は文書の優先改行を使う
- 未知encoding、embedded NUL、binary dataは理由を示して拒否し、replacement文字でcompile dataを変更しない

### 3-2: compile diagnosticとsource位置移動

raw transcriptだけでなく構造化diagnosticを一覧表示し、選択したerrorへ移動できるようにする。

- `CompileDiagnostic`はseverity、code、line、optional column、messageを持つ
- legacy compilerがcolumnを返さない場合は未設定とし、推測したcolumnを表示しない
- message領域をcompile概要、diagnostic一覧、raw transcriptに分ける
- diagnosticの選択またはdouble clickで該当する論理行へ移動し、一時的に強調表示する
- UTF-8 byte位置を`NSRange`へ直接渡さず、`NSString`のUTF-16 indexへ変換する
- compile開始時のdocument ID／revisionと現在値が異なる結果は表示しない
- 複数diagnostic、行番号なしerror、範囲外行番号、空文書を試験する

### 3-3: 行番号、検索、置換、指定行移動

行番号は第二の`NSTextView`ではなく`NSRulerView` subclassで実装し、editorのlayout managerと同じ
表示情報を利用する。

- platform-neutralな`EditorLineModel`が論理行の開始位置を管理する
- rulerはvisible glyph rangeだけを描画し、折返し行へ同じ番号を重複表示しない
- 空の最終行、CRLF、CR、日本語、結合文字を正しく数える
- cursorを含む論理行を強調し、text変更、scroll、font変更、resizeで必要範囲を再描画する
- 大規模file向けに行頭offsetをcacheし、scrollごとの全文走査を避ける
- 検索と置換は`NSTextView`／`NSTextFinder`の標準find barを使用する
- `Command-F`、`Command-G`、`Shift-Command-G`を標準動作へ接続する
- 指定行移動は独自sheetとし、同じ`EditorLineModel`で選択範囲を求める

### 3-4: drag and drop、最近使ったfile、複数document

すべてのfile openを`NSDocumentController`経由へ統一する。

- File menuへOpen RecentとClear Menu、main menuへWindow menuを追加する
- Finderから複数fileをdropした場合は各fileを別documentで開く
- dropによって現在のdirty documentを置換しない
- text payloadのdropは通常のtext挿入として扱い、file URLとは区別する
- 各documentがcompile operationとUI状態を所有し、別documentの開始・終了で誤cancelしない
- compile serviceのserial queueは共有してよいが、callbackはdocument ID／revisionで分離する
- untitledを含む2文書以上でopen、編集、Save As、compile、close確認を反復する

### 3-5: autosave、世代backup、crash recovery

Phase 3のautosaveは元fileを無断で上書きする機能ではなく、異常終了復旧用snapshotとする。
`RecoveryService`を`DocumentService`から分離し、testでは保存rootを一時directoryへ差し替えられるようにする。

- Application Support配下のdocument固有UUID directoryへrecoveryを保存する
- 編集停止から5秒後を目安にdebounceし、dirtyな最新snapshotだけをworkerで保存する
- recoveryはschema version、timestamp、元pathとfingerprint、encoding、改行、document type、revision、
  content checksumを持つ
- atomic writeし、documentごとに最大10世代を保持して古い世代を削除する
- 正常保存または明示的な破棄でrecoveryを削除し、crash時は残す
- 起動時に候補をscanし、復元、破棄、後で確認を選択できるようにする
- 復元内容はdirtyなuntitled documentとして開き、元fileを直ちに上書きしない
- manual saveで既存fileを置換する前にApplication Support配下へ世代backupを作成する
- 破損checksum、途中で切れたfile、元fileの外部更新、保存中の追加編集、世代上限を自動試験する

### 3-6: commandとshortcut

menu itemから直接処理を分岐させず、`EditorCommand`とcommand dispatcherを定義し、menu、button、
keyboardの全経路を同じcommandへ接続する。`validateUserInterfaceItem`で実行可能状態を同期する。

- macOS標準として`Command-N/O/S/Shift-S/W/Z/Shift-Z/F/G/Shift-G`を提供する
- compileは`Command-R`、指定行移動は`Command-L`とする
- Windows互換の`Control-S`は保存へ割り当てる
- Windows版のF5／F12はcompile後の再生、Escは停止、Control-F1は早送りであるため、Phase 3で
  compile-only等の異なる意味へ割り当てない
- F5／F12、Esc、Control-F1はcommand定義だけを先行できるが、有効化と受入はPhase 4で行う
- F1のmenu遷移はmacOSの常設menuで目的を達成するため、同一操作の再現を要求しない

### 3-7: Finder、UTI、sandbox方針

- `org.mucom88.muc`はEditor／Ownerとして維持する
- `.n88`用の`org.mucom88.n88`を`public.plain-text`準拠のtypeとして追加する
- 任意textは`public.plain-text`をEditor／Alternateとして扱い、全text fileの既定appを奪わない
- Open panelではMUC、N88、plain text、All Filesを選択可能にする
- `plutil`、`mdls`、Launch Services登録、Finder double click、複数file openで確認する
- 現在の非sandbox buildではsecurity-scoped bookmarkは不要なため、Phase 3では「sandbox非採用につきN/A」とする
- 将来sandboxを採用する場合はdocument外のPCM、voice、ROM、rhythm directoryだけをbookmark化し、
  stale bookmark更新とaccess開始／終了を対にする

### Phase 3のfile分割方針

単一の`mucom_editor.mm`へ機能を追加し続けず、次の単位へ分離する。

| 層 | 実装単位 | 責務 |
|---|---|---|
| core | `DocumentService` | text、encoding、改行、type、save plan、dirty状態 |
| core | `EditorLineModel` | 論理行、行頭位置、指定行移動 |
| core | `RecoveryService` | recovery／backupの保存、世代管理、scan、cleanup |
| core | `EditorCommand` | command ID、実行可否、dispatch contract |
| AppKit | `MucomDocument` | `NSDocument` lifecycleとcore modelの同期 |
| AppKit | `MucomEditorWindowController` | editor、status、message layout |
| AppKit | `LineNumberRulerView` | visible行番号とcursor行表示 |
| AppKit | `DiagnosticController` | diagnostic一覧とsource位置移動 |

### Phase 3の実装順序と完了gate

| 段階 | 実施内容 | 完了gate |
|---|---|---|
| 3-0 | save acknowledgement、dirty／Undo、location同期、旧document model統合 | save中の追加編集、Undo、Save As、外部更新の試験が成功 |
| 3-1 | encoding選択、混在改行、MUC／N88／text判定 | 日本語・混在改行・3 typeのround tripが成功 |
| 3-2 | diagnostic一覧と任意error行移動 | 複数error、stale revision、Unicode行移動が成功 |
| 3-3 | gutter、find／replace、Go to Line | 折返し、最終空行、大規模fileで表示と移動が一致 |
| 3-4 | Open Recent、drop、複数document | dirty文書を失わず複数windowを独立操作できる |
| 3-5 | recovery autosave、10世代backup、起動時復旧 | crash相当、破損、cleanup、復元の試験が成功 |
| 3-6 | command router、macOS／互換shortcut | menuとkeyboardが同じcommandを実行する |
| 3-7 | UTI、Finder、sandbox判定、最終受入 | MUC／N88をFinderから開き、全Phase 3受入項目がPASS |

3-0を完了するまでrecoveryと複数documentを実装しない。3-1と3-2でmodel contractを固定した後に
AppKit表示を追加し、最後にFinder／Launch Servicesを含むapp bundle統合を検証する。

### Phase 3で追加する常設試験

- `document_service_test`: save acknowledgement、dirty／Undo相当、encoding指定、混在改行、type、外部更新
- `editor_line_model_test`: LF／CRLF／CR／Mixed、日本語、結合文字、折返し元の論理行、最終空行
- `editor_command_test`: menu／keyboard dispatch、実行可否、Phase 4 commandの無効状態
- `document_recovery_test`: debounce対象snapshot、世代上限、checksum、scan、restore、cleanup
- `compile_service_test`: 複数diagnostic、optional column、document ID／revision
- macOS GUI手動試験: find／replace、ruler、drop、Open Recent、複数window、Finder関連付け、起動時復旧

core testは既存と同様にDebug、Release、ASan／UBSan、TSanで実行する。AppKit固有表示とFinder／
Launch Servicesは自動試験だけで完了扱いにせず、`tests/manual/macos-gui-acceptance.md`へ結果を記録する。

### Phase 3完了条件

- UTF-8、UTF-8 BOM、CP932、Shift_JISの日本語文書を明示したencodingで開き、編集、保存、再openして
  文字と改行を失わない
- 曖昧なlegacy encodingを推定と表示し、利用者が明示変更できる
- MUC、N88、plain textを区別し、N88を暗黙変換しない
- compileの全diagnosticを表示し、選択したerrorの論理行へ移動できる
- gutter、検索、置換、指定行移動がUnicodeと折返しを含む文書で正しく動作する
- drop、Open Recent、複数documentでdirty内容と非同期結果が混線しない
- 異常終了後に最新の正常なrecovery世代を元fileへ上書きせず復元できる
- MUC／N88をFinderから開け、任意textの既定appを不必要に変更しない
- sandbox非採用ならbookmark項目を理由付きN/Aとし、採用する場合だけsecurity-scoped bookmarkを試験する
- CLI、Phase 1、Phase 2の全常設試験が引き続き成功する

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

次に実装すべき対象は、Phase 3のeditor残機能とPhase 4の再生serviceのGUI接続である。Windows golden生成、
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
| 2026-09-23 | Phase 0完了 | 全53機能の受入手順、実chip／外部driverのprovider契約、release note雛形を確定 | 受入仕様の件数検査（標準49、拡張2、除外2）と3文書の差分検査を実施 |
| 2026-09-23 | Phase 1完了 | CTestを7件へ拡張し、native artifact、CLI契約、encoding、SDL audio、sanitizer回帰を常設化 | Release／Debug／ASan+UBSan／TSanで全7件成功。VGM wait不整合、fmgen UB、SDL終了raceも修正 |
| 2026-09-23 | Phase 2計画具体化 | owned MUB、単一playback worker、application単位のaudio所有を軸にservice契約、実装順序、完了gateを確定 | 現行の`CMucom`、compile service、AppKit editor、SDL backend、Windows HSP再生／monitor仕様を照合 |
| 2026-09-23 | Phase 2完了 | document、owned MUB compile、単一worker再生、audio、monitor、4形式export、voice、application共有serviceを実装し、AppKit compileを非同期service経由化 | Phase 2試験10件を追加し、既存7件を含む全17件がDebug／Release／ASan+UBSan／TSanで成功 |
| 2026-09-23 | Makefile依存修正 | `miniplay`で旧・新class layoutのobjectが混在してmutex例外になる問題を防止 | `.d`自動生成、Makefile変更時の全object再build、SDL dummy／実deviceで`sampl1.muc`再生開始とCtrl-C終了を確認 |
| 2026-09-23 | Phase 3計画具体化 | document保存状態の一元化を先行し、encoding、diagnostic、editor操作、複数document、recovery、shortcut、UTIの実装方法と順序を確定 | AppKit editor、DocumentService、compile diagnostic、recovery形式、Info.plist、Windows shortcut、GUI受入仕様を照合 |
