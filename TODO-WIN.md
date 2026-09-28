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
| Level 1 | CLI compile、再生、MUB/WAV/VGM/S98出力 | 基本経路とmacOS native常設回帰試験20件を実装済み |
| Level 2 | MML editor、compile、再生、browser、export | Phase 3 editorを完成。Phase 2再生／export serviceのUI接続とbrowserは未完了 |
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
- [x] compile結果、構造化diagnostic一覧、error行への移動
- [x] UTF-8、UTF-8 BOM、CP932、Shift_JISの判定、明示選択、元encodingへのround trip
- [x] 行番号gutter、検索、置換、指定行移動
- [x] MUC、N88-BASIC source、任意textのtype判定
- [x] drag and drop、最近使ったfile、複数document
- [x] recovery autosave、10世代backup、crash recovery
- [x] macOS標準shortcutとPhase 3範囲のWindows互換shortcut
- [x] Finder関連付けとUTI
- [x] sandbox非採用を確定し、security-scoped bookmarkをN/Aとする

CP932はShift_JISの大部分を包含するため両者を常に自動判別することはできない。曖昧な入力はCP932を
推定値として表示し、Encoding menuからShift_JISを含む保存encodingを明示選択する仕様で完了とする。
混在改行は行単位の改行情報を保持し、編集で増えた行には文書の優先改行を使用する。

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

### Phase 3の責務分割

coreは次の単位へ物理分割した。AppKit側は同じObjective-C++ translation unit内でclass単位に責務を
分けている。AppKit fileの物理分割は保守上の改善候補だが、Phase 3の機能完了条件にはしない。

| 層 | 実装単位 | 責務 |
|---|---|---|
| core | `DocumentService` | text、encoding、改行、type、save plan、dirty状態 |
| core | `EditorLineModel` | 論理行、行頭位置、指定行移動 |
| core | `RecoveryService` | recovery／backupの保存、世代管理、scan、cleanup |
| core | `EditorCommand` | command ID、実行可否、dispatch contract |
| AppKit | `MucomDocument` | `NSDocument` lifecycleとcore modelの同期 |
| AppKit | `MucomDocument`のwindow構築 | editor、status、message layout |
| AppKit | `LineNumberRulerView` | visible行番号とcursor行表示 |
| AppKit | `MucomDocument`のdiagnostic表示 | diagnostic一覧とsource位置移動 |

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

**Phase 3完了日: 2026-09-24**

実装後はRelease、Debug、ASan／UBSan、TSanの各構成で全20 CTestに成功した。実GUIでは行番号ruler、
標準find barと置換UI、Go to Line、compile errorの3行目選択、diagnostic link、encoding menu、複数window、
5秒後のrecovery生成、process強制終了後の復元、N88 fileのLaunch Services openを確認した。
legacy compilerが返すcolumnは未提供のためoptionalの未設定値とし、現在のcompilerが返すprimary
diagnosticは通常1件だが、UIとservice contractは複数件を保持・選択できる。

## Phase 4: 再生GUIとAudio

- [x] 編集中snapshotの非同期compile・即時再生
- [x] play、pause、resume、stop、Esc操作
- [x] x2、x4、x6、x8、x10の早送り
- [x] 再生位置、最大count、driver、状態表示
- [x] PCM、voice、ROM、rhythm directoryの選択
- [x] output device列挙、選択、default変更、切断、再接続
- [x] requested／obtained format差の変換または明確なerror
- [x] 曲末尾の自動停止と次曲への遷移
- [x] pause／resume／stop／再初期化の反復試験
- [x] 実CoreAudioの`System Default`でPCMを含む曲を60分以上再生する
- [ ] 出力先が内蔵speakerであることを確認し、tempo、音切れ、click、終了時noiseを聴感確認する
- [ ] 実物output deviceを切断／再接続し、`DeviceLost`と明示`Reconnect`を確認する
- [x] underrun、dropped sample、再充填回数を診断表示する

### Phase 4の実装境界

Phase 4はPhase 2で実装済みの`MucomCompileService`、`PlaybackSession`、`AudioDeviceService`、
`MonitorSnapshot`をAppKitへ接続し、実CoreAudio deviceで受け入れる段階とする。folder browser、playlist、
Now Playing、11 channelの詳細monitorはPhase 5へ残し、Phase 4のeditorにはtransport、簡易progress、driver、
状態、audio診断だけを表示する。

`TODO.md`の「Phase 4: SDL dummy audio試験」は既に完了した自動試験基盤を指し、本節のGUI／実device
Phase 4とは別の段階である。

### application単位の`PlaybackCoordinator`

`PlaybackSession`のobserverは1つだけであるため、各`MucomDocument`から直接`SetObserver`を呼ばない。
`ApplicationServices`がplatform-neutralな`PlaybackCoordinator`を1つ所有し、Coordinatorだけが
`PlaybackSession`を購読する。document windowはtoken付きでCoordinatorを購読し、close時に解除する。

Coordinatorは次を所有・調停する。

- activeな`DocumentId`、`Revision`、`SessionId`、`CompiledSong`と再生option
- application全体で単調増加するcompile-and-play要求世代
- pause、resume、stop、速度変更、device再接続のcommand dispatch
- output device選択とresource設定のimmutable snapshot
- 複数windowへの状態通知と、Phase 5が利用する次曲要求hook

F5／F12のcompile-and-playは、現在の再生停止、前回要求のcancel、新しい要求世代の発行、編集内容の
snapshot compile、結果検証、`PlaybackSession::Play`の順で実行する。callbackではoperation ID、要求世代、
document ID、revisionのすべてが一致した場合だけ再生する。Stopは再生だけでなく未完了のplay intentも
無効化し、別documentの遅延compile結果が再生を奪わないようにする。compile開始後に本文が変更された場合は
自動再生せず、再度Playが必要であることを表示する。

application内のactive playbackは常に1つとする。別documentからPlayした場合は旧sessionを停止して
`SessionId`を更新する。active documentを閉じた場合は、画面のない再生を残さないため停止する。

### transport、状態表示、shortcut

editor上部をtransport行とstatus／progress行に分け、次を配置する。

- Compile、Compile & Play、Pause／Resume、Stop
- x2、x4、x6、x8、x10の速度popup
- output device popupとReconnect
- current／max count、loop回数、driver、playback state、現在速度
- underrun、dropped frame、refill event、queued frame

UIはaudio callbackやplayback workerから描画を待たせない。main threadのtimerで15 Hz程度に
`LatestSnapshot`を取得し、有限曲はdeterminate progress、loop曲はloop内位置とloop回数、max count不明時は
indeterminate progressとして表示する。A～Kのchannel詳細表示はPhase 5で追加する。

shortcutはWindows GUIの実処理に合わせ、F5／F12をCompile & Play、EscをPlaying／Buffering時のPauseと
Paused時のResumeにする。完全停止はStop buttonとPlayback menuから実行する。Control-F1は押下中だけ選択済み
倍率へ変更し、key-up、window非active化、再生終了のいずれでもx1へ戻す。function keyをmacOSが使用する環境を
考慮し、同じ操作をmenuとbuttonから常に実行可能にする。Control-F1のkey-upが必要なためglobal event tapは
使用せず、application内のlocal event monitorを使用する。

`EditorCommandState`は単一の`playback_ui_ready`だけでなく、現在の`PlaybackState`からPlay、Pause、Resume、
Stop、速度変更、Reconnectの可否を導出する。`Stopping`中の重複操作は禁止し、`DeviceLost`ではStop、device選択、
Reconnectだけを有効にする。

### PCM、voice、外部ROM、rhythm directory

compile要求ごとに次のresource設定を値としてcopyし、非同期処理中にUI設定が変わっても意味が変化しないようにする。

```text
ResourceConfiguration
  document_directory
  default_pcm_file
  default_voice_file
  external_rom_directory
  rhythm_directory
  use_external_rom
```

PCMとvoiceはMML内の`#pcm`／`#voice`を最優先し、タグがない場合だけ選択済みdefaultを使用する。タグ内の
相対pathはdocument directory基準とする。未保存documentで相対resourceを指定した場合はprocess current
directoryへ暗黙解決せず、保存またはresource root選択を要求する。

default voiceはcompile前に読み込む。default PCMは`CompiledSong`へ解決済みpathを保持し、MUBにPCMが
埋め込まれていない場合にplayback runtimeへpreloadする。MUB埋込PCMがある場合は埋込dataを優先する。

rhythm directoryは`CMucom::Init`から`mucomvm::InitSoundSystem`、`FM::OPNA`へ明示的に渡す。現在のCLIの
一時的な`chdir`や、文字列へのseparator連結には依存せず、directoryと`2608_BD.WAV`、`2608_SD.WAV`、
`2608_TOP.WAV`、`2608_HH.WAV`、`2608_TOM.WAV`、`2608_RIM.WAV`をpathとして結合する。一部欠落時は
不足fileを列挙したerrorにする。CLIの`-r`も同じAPIを使用する。

外部ROMはCLIの`-e`に相当する外部MUCOM driver file群のdirectoryとして扱う。driverごとに必要な
`expand`、`errmsg`、`msub`、`muc88`、`ssgdat`、`time`、`smon`、`music`を事前検査し、相対
`LoadMem`の失敗を無視しない。repositoryに再配布可能なfixtureがないため、path検証とerror伝播は自動試験し、
実dataでの動作は利用者が用意したresourceによる手動受入とする。

Phase 4ではresource選択をapplication実行中の設定として保持する。永続化、schema migration、破損設定からの
default復帰はPhase 8の`SettingsService`で追加し、Phase 4へ設定file形式を先行導入しない。

### output device、format、切断、再接続

SDL2の列挙indexを永続IDとして使用しない。選択値は`System Default`またはdevice名として保持し、再生開始と
device list更新時に再列挙して現在のdescriptorへ解決する。同名deviceが実機で問題になる場合だけCoreAudio UIDを
使用するbackendを追加する。「default変更」はmacOS全体のsystem defaultを書き換える操作ではなく、appの選択を
`System Default`へ戻す操作とする。

SDL audio device eventを処理するため、AppKit main threadの短周期timerからaudio device eventだけをpumpする。
`SDL_AUDIODEVICEREMOVED`のinstance IDがopen中deviceと一致した場合は`MarkDeviceLost`を呼び、sessionを
`DeviceLost`へ遷移させる。別deviceへ無断で切り替えない。Reconnectでは再列挙後、保持している同じ
`CompiledSong`を曲頭から再生する。runtimeに安全なseek契約がないため、切断位置からの再開は保証しない。

初期実装では44.1 kHz、signed 16-bit、stereoのexact openを維持し、暗黙のcustom resamplingは追加しない。
format不一致時はdevice名、requested format、obtainedまたはpreferred formatを含む`UnsupportedFormat`を表示する。
実機でexact openできない対象deviceが確認された場合に限り、`SDL_AudioStream`による変換を追加する。

### audio診断とclick対策

診断値を次の意味に固定する。

- `underruns`: callbackが要求sampleを取得できなかった回数
- `dropped_frames`: rendererが生成したがringへ渡せず実際に破棄したframe数
- `refill_events`: underflowまたは空bufferの後に音声供給を再開した回数
- `rendered_frames`: runtimeが生成したframe数
- `queued_frames`: 現在ringに残っているframe数

現在の部分書込みは未書込み部分を再試行しても`dropped_frames`へ加算し、書込み0で破棄された部分を数えない場合が
あるため、ringへの受入数とPlaybackSessionが最終的に破棄した数を分離して計数する。初回prefillは
`refill_events`へ含めず、再生開始後の回復だけを数える。

pause、resume、stop、自然終了の波形切断を避けるため、開始／resume時に約256 frameのfade-in、pause／stop時に
audio callback側で約256 frameのfade-outを行う。自然終了は最終render blockへfade-outを適用してから
`Draining`へ遷移する。device lost時はfade完了を待たず停止する。fade待機はplayback worker内だけで行い、
AppKit main threadをblockしない。

### 曲末とPhase 5への次曲遷移

有限曲は既存規則どおりmax count到達後にring bufferを排出し、`Finished`を通知してaudio deviceを閉じる。
Phase 4ではCoordinatorに「次の`CompiledSong`を返す」任意hookを用意し、2曲を渡した場合のsession切替を
headless testで固定する。Phase 4のeditorはqueueを登録しないため、通常はFinished表示で停止する。

folderからの曲選択、playlist生成、compile失敗skip、末尾から先頭へ戻るloop、最大時間／曲長割合によるskipは
Phase 5で実装する。この境界により、Phase 4の「次曲への遷移」はservice契約と安全なsession切替までとし、
user-facingなautomatic playerを先取りしない。

### Phase 4の実装順序と完了gate

| 段階 | 実施内容 | 完了gate |
|---|---|---|
| 4-0（完了） | resource値型、Coordinator、command状態、診断値の定義 | stale play intent、複数document、drop／refill単体試験 |
| 4-1（完了） | default PCM／voice、rhythm、外部ROMの明示path | 起動directory非依存、欠落resourceの明確なerror |
| 4-2（完了） | compile-and-play、transport、progress、shortcut | F5／F12、Esc、Control-F1とbutton／menuの結果が一致 |
| 4-3（完了） | device picker、hotplug、DeviceLost、Reconnect、format表示 | 切断時に無断切替せず、選択後に曲頭から再接続 |
| 4-4（完了） | 診断表示、fade-in／out、曲末hook | 反復操作でhangせず、sample不連続試験が成功 |
| 4-5（完了） | dummy／sanitizer回帰 | 全既存CTestとPhase 4追加試験がDebug／Release／sanitizerで成功 |
| 4-6（一部完了） | 実CoreAudio受入 | `System Default`でPCM曲を60分以上連続再生済み。内蔵speakerの聴感と物理device切断は未完了 |

**4-0完了日: 2026-09-26**

`ResourceConfiguration`を追加し、document directory、default PCM／voice、外部ROM、rhythm directoryと
外部ROM使用flagを非同期`CompileRequest`から`CompiledSong`へ値copyするcontractを実装した。この段階では
resourceの実読込と選択UIは実装せず、4-1へ残している。

`PlaybackCoordinator`をapplication共有serviceとして追加し、`PlaybackSession`の唯一のobserverになった。
token付き複数購読、application全体のplay intent世代、前要求cancel、Stopによる未完了intentの無効化、
別documentのstale compile結果破棄、active document close時停止を実装した。AppKitからの利用は4-2で行う。

`EditorCommandState`は`PlaybackState`を受け取り、Pause／Resume、Stop、FastForward、Reconnectを状態別に
有効化する。`Stopping`中のCompile & Playと重複Stop、`DeviceLost`以外のReconnectを禁止した。

audio診断へ`refill_events`を追加し、runtime生成frameと実際に破棄したframeをPlaybackSession側で計数するように
変更した。部分書込み後に再試行するframeをdropとして数えていた従来の意味を修正し、初回prefillはrefillへ
含めない。

`playback_coordinator_test`を追加し、stale play intent、2 documentの競合、複数observer、pause、active
document close、Stop後の遅延compileを検証した。`editor_command_test`、`compile_service_test`、
`audio_device_service_test`も新しいcontractを検証する。Release buildと全21 CTestは成功した。Debugおよび
sanitizer全構成は4-5のgateとして未実施である。

**4-1／4-2完了日: 2026-09-27**

default voiceはタグがない場合だけcompile前に読み込み、default PCMはMUBにPCMが埋め込まれていない場合だけ
playback／export runtimeへpreloadする。`#voice`／`#pcm`はdefaultより優先し、相対pathはdocument directoryで
解決する。未保存documentの相対resourceは拒否する。rhythm directoryは`CMucom::Init`からFMGENへ明示的に
渡し、CLIの`-r`も同じ経路に移した。外部ROMは選択directoryから8 fileを事前検査し、`CMucom`へ明示pathを
渡す。resource選択は計画どおりapplication実行中だけ保持する。

AppKit editorへCompile & Play、Pause／Resume、Stop、Fast、x2／x4／x6／x8／x10 popup、15 Hz更新の
progress／current／max／loop／driver／state／speed表示を追加した。Build／Playback menuとbuttonは同じ
document actionへ接続し、local event monitorからF5／F12、Esc、Control-F1押下／解放も同じactionへ送る。
Control-F1はkey-upとapplication非active化でx1へ戻る。編集中に本文が変化した場合はpending play intentを
世代ごと無効化し、古いrevisionを再生しない。window close時は購読解除とactive再生停止を行う。

`compile_service_test`へタグ優先、default resource、未保存相対path、不足ROM／rhythm一覧を追加し、
`playback_session_test`でPCM非埋込MUBへのdefault PCM preloadを実再生した。
`playback_coordinator_test`では編集相当のpending intent取消を追加した。Release buildと全21 CTestは成功した。
外部ROM実dataとGUIの実CoreAudio聴感は手動受入へ残す。device選択／切断／再接続は次の4-3で実装した。

**4-3完了日: 2026-09-27**

`AudioDeviceService`の出力識別子をSDL列挙index依存からdevice名基準へ変更し、`System Default`または
選択device名を再列挙ごとに解決する。AppKit editorへoutput device popup、Reconnect button、requested／
obtained format表示を追加した。`System Default`の選択はmacOSのsystem設定を書き換えず、次回open時に
system defaultを解決する意味とする。選択状態はapplication実行中の全documentで共有する。

AppKit main threadの0.2秒timerは`SDL_AUDIODEVICEADDED`／`SDL_AUDIODEVICEREMOVED`だけをevent queueから
pumpする。使用中instanceの削除では`MarkDeviceLost`を通知し、別deviceへ自動切替せず`DeviceLost`に留まる。
再列挙後に選択先が存在する場合だけReconnectを有効化し、保持中の同じ`CompiledSong`を新しい`SessionId`で
曲頭から再生する。paused中の切断も検出できるよう、playback workerは非再生状態でも50 ms以内にdevice診断を
確認する。

audio open結果はdevice名、requested／obtainedのsample rate、bit数、channel数、buffer frame数をsnapshotへ
保持してGUI表示する。exact formatでopenできない場合は変換を暗黙導入せず、requested formatとprobeで取得した
available／obtained formatを含む`UnsupportedFormat`を返す。

`audio_device_service_test`へSDL audio hotplug event、active instance切断、format snapshotを追加し、
`playback_coordinator_test`では自動切替されない`DeviceLost`、明示device選択、曲頭Reconnectと新sessionを
確認した。Release buildと全21 CTestは成功した。実物deviceの抜き差しとCoreAudio聴感は4-6へ残す。

**4-4完了日: 2026-09-27**

AppKit editorに`queued_frames`、`underruns`、`dropped_frames`、`refill_events`、`rendered_frames`と
active／device-lost状態を表示するaudio diagnostics行を追加した。値は15 Hzの既存snapshot更新経路だけで
読み、audio callbackからAppKitを直接呼ばない。

開始とresumeではSDL callbackの先頭256 frameを0から等倍まで線形fade-inする。pause、stop、別曲への
再初期化ではcallback側で現在値から256 frameを線形fade-outし、最大250 msだけplayback workerが完了を待って
からpause／flush／closeする。device lost時は待たない。有限曲は終了を検出したrender blockの末尾256 frameを
等倍から0へfade-outしてから`Draining`へ移る。1 callbackがfade長より大きい場合も残りをzero fillし、同じ
envelopeを複数callbackへ分割した結果と連続bufferへ適用した結果が一致する。

`PlaybackCoordinator::SetNextSongProvider`を追加した。hook未設定またはnull返却時は従来どおり`Finished`で
停止し、次の`CompiledSong`が返った場合だけ再生optionを引き継いで新しい`SessionId`へ移る。hook処理中に
Stopや別のplay intentが入った場合は世代とactive songの再照合で結果を破棄する。editorはhookを登録せず、
playlist UIはPhase 5へ残す。

`audio_fade_test`で開始値、終了値、単調性、隣接sample差、callbackをまたぐ適用、fade後のzero fillを確認した。
`playback_transport_stress_test`はSDL dummy上でPlay／Pause／Resume／Stopとdevice open／closeを100回反復する。
`playback_coordinator_test`では有限曲からloop曲への自動遷移とdocument／session切替を確認した。Release buildと
全23 CTestは成功した。Debugおよびsanitizer全構成は4-5、実CoreAudio聴感は4-6へ残す。

**4-5完了日: 2026-09-27**

Phase 4の追加試験を含む全23 CTestをRelease、Debug、ASan／UBSan、TSanの4構成で実行し、すべて成功した。
各構成で`MUCOM88Editor.app`、CLI、test executableのcompile／linkとad-hoc署名も成功した。

| 構成 | CMake option | CTest結果 |
|---|---|---|
| Release | `-DCMAKE_BUILD_TYPE=Release` | 23/23成功 |
| Debug | `-DCMAKE_BUILD_TYPE=Debug` | 23/23成功 |
| ASan／UBSan | Debug + `-DMUCOM88_ENABLE_ASAN_UBSAN=ON` | 23/23成功、sanitizer報告なし |
| TSan | Debug + `-DMUCOM88_ENABLE_TSAN=ON` | 23/23成功、data race報告なし |

SDL dummy上の`playback_transport_stress_test`は各構成でPlay／Pause／Resume／Stopとdevice open／closeを
100回完走した。4-4で追加したfade callback、fade完了待機、次曲hook、snapshot更新について、memory error、
undefined behavior、thread race、timeout、hangは検出されなかった。build directoryを手順どおりsource tree直下へ
作成しても差分扱いにならないよう、`build-asan/`と`build-tsan/`を`.gitignore`へ追加した。

4-5はdummy／sanitizer回帰の完了であり、実CoreAudio deviceや聴感を検証したものではない。内蔵speakerでの
60分PCM再生、click／音切れ／tempo、物理device切断は4-6の完了条件として残す。

**4-6一部実施日: 2026-09-27**

MacBook Air（Mac17,3、Apple M5、macOS 27.0 build 26A428）でRelease版`MUCOM88Editor.app`を起動し、
`package/sampl1.muc`を実CoreAudioの`System Default`へ出力した。requested／obtainedはいずれも
44.1 kHz、signed 16-bit、stereo、1024 framesである。

最初の実機確認では`underruns=0`のまま`dropped_frames`が増加した。1024 frame／44.1 kHzのcallback周期は
約23.2 msであるのに、ring buffer満杯時のproducer待機が20 ms固定だったため、callback直前にtimeoutし、
512 frame単位のrender blockを破棄していた。`AudioDeviceService::WriteFrames`の待機上限を250 msへ変更し、
cancel、device lost、shutdownでは従来どおりcondition variableで即時解除するようにした。

修正後は60分間連続して`Playing`とloop進行を維持し、60分時点で`queued_frames=16384`、
`underruns=0`、`dropped_frames=0`、`refill_events=0`、`rendered_frames=160259072`、loop 73を記録した。
その後Stopを実行し、`Idle`、`queued_frames=0`、各異常診断値0、`rendered_frames=163442176`を確認した。

常設回帰では100回のtransport stress後にも`underruns`と`dropped_frames`が0であることを追加検証した。
また、dummy callbackが20 ms以内に必ず実行されるという時刻依存を除去し、最大2秒のdeadline内で実際の
callback／underrunを待ってrefillを検証するようにした。Release、Debug、ASan／UBSan、TSanの各構成で
全23 CTest、計92件が成功し、sanitizer報告はない。

GUI状態、診断値、長時間継続、Stop後のdevice closeは確認済みである。一方、Computer Useは実音を聴取できず、
物理deviceの抜き差しも実行できないため、PCMの聴感、tempo、音切れ、click、終了noise、内蔵speaker endpoint、
実物device切断／再接続は手動受入として残す。したがって4-6およびPhase 4全体は一部完了とする。

### Phase 4で追加する常設試験

- 最新の編集snapshotだけが再生され、Stop後に遅延compile結果が再生されない
- document Aの遅延結果がdocument Bのactive sessionを上書きしない
- Pause／Resume、Stop／Play、device close／openをそれぞれ100回反復する
- x2、x4、x6、x8、x10でprogress進行率が倍率と一致し、解除後にx1へ戻る
- 1.7、1.5、EMの有限曲、loop曲、PCM曲で曲末規則が変わらない
- `#pcm`／`#voice`とdefault resourceの優先順位、空白／日本語を含むpath
- 合成した6種類のrhythm WAVを明示directoryから読み、rhythm channelが非無音になる
- stale device descriptor拒否、device lost mock、再列挙、明示再接続
- requested／obtained formatを含むerror
- underrun、実際に破棄したframe、refill eventの計数
- fade前後のsample不連続、終了時のbuffer排出
- window close／app終了中のcallback破棄とworker停止

実CoreAudio試験は通常のCTestへ含めず、`tests/manual/macos-gui-acceptance.md`へmacOS version、machine、
device、継続時間、操作回数、underrun、dropped frame、refill event、聴感結果を記録する。

完了条件: 実CoreAudio deviceで長時間再生し、操作、曲切替、終了にhangがなく、既知のclick、tempo変動、
PCM欠落がない。

## Phase 5: Home、player、monitor

- [x] 実装前contract test 64ケースの抽出とCTest登録
- [ ] folder sidebarとMUC／N88一覧
- [ ] 選択fileのmetadata inspector
- [ ] editorで開く、直接再生、MUB export
- [ ] playlist、連続再生、compile失敗skip、loop
- [ ] 最大演奏時間・曲長割合による自動skip
- [ ] Now Playingと曲切替表示
- [ ] 11 channelのmonitor view
- [ ] monitor更新頻度を制限し、audio threadをblockしない
- [ ] document、browser、playlist間で同じactive playback状態を共有する

### Phase 5の対象とWindows機能対応

Phase 5は`mucom88win.hsp`のHome／SMONと`aplayer.hsp`のautomatic playerをmacOS native UIへ移す。
Windowsと同じ座標、別process構成、HSP描画を再現するのではなく、次の利用目的を同等化する。

- 現在folderの子folderと`.muc`／`.n88`を選択、sort、再読込できる
- compileせずにtitle、author、composer、date、voice、PCM、commentを確認できる
- 選択曲をeditorで開く、直接再生する、MUBとして保存する
- folder内のMUCを順番に再生し、compile失敗をskipして末尾から先頭へ戻る
- 最大実時間またはmax count比率でloop曲を次へ進める
- activeな1曲についてNow PlayingとA～Kの11 channel状態を表示する
- editor、Home、player、monitorのどこから操作してもapplication内のactive playbackは1つである

`vplayer.hsp`の3D visualizerは既に`GUI-PLAYER-04`として標準版対象外である。再帰的library index、検索、
tag編集、MUB／WAV／VGM／S98の汎用export、設定永続化、filesystem監視もPhase 5へ混在させない。
MUB保存はHomeの単一用途だけを接続し、汎用export UIはPhase 6、folderやplayer設定の永続化はPhase 8で行う。

### metadataとlibraryの値型

runtimeの`CMucom::GetInfoBufferByName`やAppKit型をbrowserへ露出しない。次のplatform-neutralな値型を追加する。

```text
SongMetadata
  title, author, composer, date, voice, pcm, comment

LibraryEntry
  entry_id, absolute_path, display_name, DocumentKind
  is_directory, metadata, file_error

LibrarySnapshot
  scan_generation, root_directory, current_directory
  parent_available, directories[], songs[], scanning, error
```

`SongMetadata`は`CompiledSong`にも保持し、editor、direct play、playlistのどの経路でも同じNow Playing表示を
作れるようにする。表示用titleだけは空の場合にfile stemへfallbackし、modelへ`NO TITLE`という疑似tagを
書き込まない。

`MetadataService`は`DocumentService`のUTF-8／BOM／CP932／Shift_JIS decodeとMUC／N88判定を再利用し、
compileやprocess current directory変更を行わずtagを抽出する。tag規則は現runtimeに合わせる。

- 行頭が`#`の行だけを対象にし、ASCII小文字の`title`、`author`、`composer`、`date`、`voice`、`pcm`、
  `comment`を認識する
- tag名の後の空白を除いた残りを値とし、同名tagが複数ある場合はruntime同様に最初の値を採用する
- 未知tagは無視し、tag欠落はerrorにしない
- decode不能、NUL、read失敗はそのentryの`file_error`とし、folder全体のscanを失敗させない
- metadata取得のために巨大fileを無制限に読まず、16 MiBを超えるfileは一覧には残してpreviewだけをerrorにする

`LibraryService`は専用serial executorで`std::filesystem::directory_iterator`を実行する。初期版はWindows Homeと
同じく現在directoryの1階層だけを列挙し、再帰scanしない。子directoryを先、MUC／N88を後に分け、各groupを
ASCII case-insensitiveなfile名、同値時は元file名で安定sortする。extension比較はcase-insensitiveとし、
directory symlinkは表示できるが自動追跡しない。`.`で始まる項目は初期版では表示しない。

folder変更、Refresh、window closeごとに`scan_generation`を進め、旧generationの結果を破棄する。
AppKit main threadではfilesystem列挙、decode、metadata parseを行わない。Phase 5では選択folderをsession中だけ
保持し、security-scoped bookmarkやFSEventsは導入しない。

### playback ownerとapplication共有状態

現行`PlaybackCoordinator`は`DocumentId`だけでactive曲を識別するため、editor、direct play、playlistの
所有関係を表現できない。次の値をCoordinatorのrequestとsnapshotへ追加する。

```text
PlaybackOwner
  kind = Editor | Browser | Playlist
  token = application内で一意な64-bit値

NowPlayingInfo
  owner, document_id, revision, source_path, content_id, SongMetadata
```

`CompileAndPlay`と新設する`PlayCompiledSong`はownerを受け取り、play intent世代、active song、ownerを同時に
更新する。既存editorはwindow／document由来owner、Homeのdirect playは1操作ごとのBrowser owner、
automatic playerはplaylist generation由来ownerを渡す。Coordinatorのobserverは全windowへ同じimmutable
snapshotを配る。

Playlist再生中にeditorまたはHomeから別のPlayを実行した場合、新しいownerが通常のCoordinator arbitrationで
active playbackを取得する。`PlaylistService`はowner不一致を検出してqueueとprefetchをcancelするが、新しい
再生をStopしない。Playlist windowを閉じただけでは再生を止めず、明示Stop、別ownerのPlay、queue停止、app終了で
停止する。

### `PlaylistService`と次曲供給

`SetNextSongProvider`はcompile済み`CompiledSong`を同期で返す現在の契約を維持し、provider内でfile readや
compileを行うことを禁止する。`PlaylistService`はapplication共有serviceとして`LibraryService`、
`MucomCompileService`、`PlaybackCoordinator`を使用し、現在曲の再生中に次の有効曲を1曲だけ非同期prefetchする。

```text
PlaylistEntry
  entry_id, path, metadata
  state = Pending | Loading | Compiling | Ready | Playing | Failed | Skipped
  error

PlaylistSnapshot
  playlist_generation
  state = Idle | Starting | Playing | Advancing | Stopped | Failed
  entries
  current_index, ready_next_index, owner, policy, advance_reason

PlaylistPolicy
  automatic_advance, loop_folder
  maximum_play_seconds, maximum_count_percent
```

自動playlistはWindows `aplayer.hsp`に合わせてcurrent folderのMUCだけを対象とし、N88はHomeからのopen／direct
playに限定する。開始時のtable sort順をimmutable queueへcopyし、再scanで再生中queueを暗黙変更しない。
新しいfolderでStart Playlistを押した場合だけ新generationを作る。

prefetchが自然終了までに完了していれば`NextSongProvider`がready曲を返し、Coordinatorが新しい`SessionId`で
切り替える。未完了なら一度`Finished`に留まり、prefetch完了後に`PlayCompiledSong`で開始する。provider callbackは
playlist mutexを短時間取得するだけとし、compile、I/O、AppKit callbackを実行しない。

時間／比率threshold、Next、Previousではreadyな対象があれば直ちに`PlayCompiledSong`へ切り替える。未readyなら
現在曲をStopしてplaylistを`Advancing`にし、対象のload／compile完了後だけ再生する。Previousはcurrent indexを
1つ戻して既存prefetchをcancelし、先頭では`loop_folder`が有効な場合だけ末尾へwrapする。

compile失敗時はentryを`Failed`にして次候補をprefetchする。1回の周回でqueue要素数を超えて探索せず、全曲が
失敗した場合はaggregate errorを表示して`Failed`で停止する。Stop、folder変更、別ownerのPlayではload／compile
operationをcancelし、playlist generation、entry ID、content IDが一致しないcompletionを無視する。

### automatic advance policy

Windows defaultに合わせ、session defaultを`automatic_advance=true`、`loop_folder=true`、
`maximum_play_seconds=90`、`maximum_count_percent=150`とする。Phase 5ではUI変更を再起動後へ保存しない。

- `maximum_play_seconds`: `0`で無効、1～86400秒を許可する。`Playing`中の`steady_clock`時間だけを加算し、
  Pause、Preparing、Buffering、DeviceLost中は加算しない
- `maximum_count_percent`: `0`で無効、1～10000を許可する。`max_count > 0`のとき、64-bit計算で
  `absolute_interrupt_count * 100 >= max_count * percent`を判定する
- 両方有効なら先に達した条件で次曲へ進む。自然終了は両設定より優先して即座に次曲へ進む
- 早送り中も時間条件は実時間、比率条件は曲のinterrupt進行を基準にする
- Pause中、compile中、device lost中に自動skipしない。Reconnect後は同じ曲を曲頭から再生する現行規則に合わせ、
  そのentryの実時間も0から測り直す
- 無効範囲はclampせず`InvalidArgument`を返し、直前の有効policyを維持する

policy判定をAppKit timerへ依存させない。`PlaylistService`のworkerが50～100 ms間隔でCoordinatorのimmutable
snapshotを読み、threshold到達時だけ次曲commandを発行する。これによりHome／player windowを閉じてもqueueは
継続し、audio callbackとplayback workerをblockしない。

### macOS UI構成

`MucomAppDelegate`がapplication全体で各1個の`MucomHomeWindowController`と
`MucomPlayerWindowController`を所有する。現在の巨大な`mucom_editor.mm`へ全UIを追記せず、Home、player／monitor、
共通presentation controllerを別Objective-C++ fileへ分離してCMake targetへ追加する。

Home windowは`NSSplitViewController`を使う。

- 左: current directory、parent、子directoryを表示するsidebar
- 中央: MUC／N88の`NSTableView`。file名、kind、title、composerを表示する
- 右: title、author、composer、date、voice、PCM、commentとentry errorを表示するinspector
- toolbar: Choose Folder、Back、Refresh、Open in Editor、Play、Export MUB、Start Playlist

Open in Editorは`NSDocumentController`へURLを渡す。macOS版は複数document方式なので、編集中documentを置換せず
新規または既存windowを前面化する。したがってWindowsのsingle-document確認dialogを模倣せず、dirty documentを
失わないことを同等条件とする。Playはeditorを作らずBrowser ownerでcompile-and-playする。Export MUBは
`NSSavePanel`でdestinationを確定後、非同期compileと既存`ExportService`のMUB atomic saveを接続する。

Player windowは上部にNow Playing metadata、中央に11 channel table、下部に共有transport、playlist table、
Next／Previous／Stop、loop、90秒／150% policy control、compile失敗logを配置する。transportのPause／Resume、
Stop、speed、audio outputはeditorと同じCoordinator commandを使い、別の再生engineを作らない。

Window menuへHomeとPlayer / Sound Monitorを追加する。application起動時は従来どおりeditorを開き、Homeの自動表示は
行わない。Home／Playerのwindow frame永続化はPhase 8へ残す。

### monitor表示と更新頻度

channel tableはA～Kを固定行とし、Mute、Voice、Volume、Detune、Address、Key／Key On、LFO、Reverb、Pan、
Quantizeを列にする。上部にdriver、state、absolute interrupt、current、maximum、loop、speed、audio診断値を
表示する。Addressは4桁hex、Panは空欄／R／L／C、noteはC、C+、D、D+、E、F、F+、G、G+、A、A+、Bと
octaveへ整形する。値がないIdle／Preparing時は前曲の値を残さず`—`に戻す。

現在は各editor windowが15 Hz timerを持つため、document数に比例してCoordinatorをpollする。Phase 5では
macOS main thread上の`PlaybackPresentationController`へ1本化し、最大15 Hzで`Snapshot()`を1回だけ取得して
editor、Home、Playerへfan-outする。state／error／device lostはCoordinator observerで即時更新し、channel tableは
timer tickで最新snapshotだけを描画する。snapshot pointerと`SessionId`が同じで値が変わらない場合はtable reloadを
省略する。windowの購読解除はclose時、timer停止はapp終了時に行う。

audio callbackは従来どおりring buffer消費だけ、playback workerはimmutable `MonitorSnapshot`生成だけを行い、
AppKit dispatchや描画完了を待たない。表示を開閉しながら再生しても`underruns`／`dropped_frames`が増えないことを
実CoreAudio手動試験で確認する。

### concurrency、cancel、shutdown

- library scan、file decode、compile、MUB export、playlist prefetchはmain thread外で実行する
- callbackは`OperationId`、scan／playlist generation、entry ID、owner tokenを照合してからUI modelへ反映する
- Home selection変更はmetadata表示だけを変更し、active playbackを暗黙停止しない
- Home／Player deallocation前にUI subscriptionを解除する。service operationはowner／generationで無効化する
- `ApplicationServices`はPlaylistServiceをCoordinatorより先に停止し、provider解除、policy worker停止、
  pending load／compile cancel後にCoordinator、compiler、audioを破棄する
- provider、observer、completionを呼ぶときはCoordinator／playlist mutexを保持しない
- service実装は`chdir`せず、すべてabsolute pathとrequestのresource directoryを使用する

### Phase 5の実装順序と完了gate

| 段階 | 実施内容 | 完了gate |
|---|---|---|
| 5-0 | `SongMetadata`、`LibraryEntry`、owner、Now Playing contract | tag重複／欠落、4 encoding、stale scan、owner切替の単体試験 |
| 5-1 | `MetadataService`、`LibraryService`、Home一覧／inspector | MUC／N88だけを安定sortし、main threadをblockせずfolder移動できる |
| 5-2 | Open、direct play、MUB export action | dirty editorを失わずopenし、同じresource規則で直接再生／atomic MUB保存できる |
| 5-3 | `PlaylistService`、compile-ahead、skip／loop／policy | compile失敗、全失敗、自然終了、90秒、150%、Pause、別owner割込みを再現可能なfake clockで検証 |
| 5-4 | Player／Now Playing／11 channel monitor、15 Hz fan-out | A～K mapping、session切替時clear、複数window開閉でaudio workerをblockしない |
| 5-5 | 全構成回帰と実CoreAudio GUI受入 | Release／Debug／ASan+UBSan／TSan全CTest成功、実deviceでplaylistとmonitor操作時のdrop 0 |

5-0で値型とownershipを先に固定し、5-1／5-2で単曲操作を完成させてから5-3のautomatic playerへ進む。
playlistを先にAppKitだけで組まず、fake clock／fake compilerでpolicyと失敗skipを固定する。5-4では既存editorの
per-window timerも共通presentation controllerへ移し、monitor windowだけを追加して二重pollを残さない。

### Phase 5で追加・拡張する常設試験

- `metadata_service_test`: UTF-8／BOM／CP932／Shift_JIS、CRLF、最初の同名tag、欠落／未知tag、16 MiB上限
- `library_service_test`: directory先行sort、MUC／N88 filter、大文字extension、hidden、read error、scan cancel／世代
- `library_action_test`: dirty documentを破棄しないopen契約、direct playのowner／resource、MUB atomic export、cancel
- `playlist_service_test`: sort順、1曲／複数曲、compile失敗skip、全失敗停止、末尾loop、Next／Previous、stale completion
- `playlist_policy_test`: fake clockの90秒、150%、先着条件、Pause除外、fast forward、0無効、範囲外拒否
- `playback_coordinator_test`: Editor／Browser／Playlist owner arbitration、`PlayCompiledSong`、ready next song、Stop競合
- `monitor_snapshot_test`: 11 channel、note／pan表示model、SessionId切替、Idle clear、不変snapshot
- `playback_presentation_test`: 15 Hz上限、同一snapshot coalescing、複数subscriber解除、stale session破棄
- `app_service_lifetime_test`: playlist provider／workerをCoordinatorより先に停止し、callback残存とdeadlockがない

**Phase 5実装前contract test完了日: 2026-09-28**

metadata 10、library 12、playlist 12、policy 10、presentation 12、integration／lifetime 8の計64ケースを
`src/tests/phase5/README.md`へID付きで固定し、次の5 executable specificationをCTestへ登録した。

- `phase5_metadata_contract_test`
- `phase5_library_contract_test`
- `phase5_playlist_policy_contract_test`
- `phase5_presentation_contract_test`
- `phase5_integration_contract_test`

production headerが存在しない実装前段階では各executableが77を返し、CTestは`Skipped`として表示する。
既存testを無効化したり未実装をPASS扱いにはしない。対応する`editor/song_metadata.h`、
`editor/library_service.h`、`editor/playlist_service.h`、`editor/playback_presentation.h`が追加されると
preprocessor guardが外れてcontract本体がcompile／実行される。Phase 5の各段階は対応contractがskipのままでは
完了にできず、5-5では5件すべてがactiveかつ4 build構成で成功することを要求する。

導入直後にRelease、Debug、ASan／UBSan、TSanの4構成を再buildし、各構成で既存23件が成功、Phase 5の5件だけが
予定どおりSkippedとなった。通常build、CLI、`MUCOM88Editor.app`のlinkとad-hoc署名を維持し、sanitizer報告もない。

完了条件: `GUI-PLAY-08`、`GUI-HOME-01`～`06`、`GUI-MON-01`～`02`、`GUI-PLAYER-01`～`03`の
12項目について自動試験とmacOS GUI手動受入が揃い、editor、direct play、automatic playerの競合時もactive
playbackが1つに保たれる。全既存CTestを4構成で維持し、実CoreAudioでfolder 1周、compile失敗skip、
monitor windowの反復開閉を行って`underruns=0`、`dropped_frames=0`、hangなしを確認する。

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

次のsoftware実装対象はPhase 5の5-0である。`SongMetadata`、library値型、PlaybackOwner、Now Playing contractを
先に追加し、tag／encoding／stale scan／owner切替の単体試験を固定してからHome UIへ進む。Phase 4の4-6に残る
内蔵speaker聴感と物理device切断／再接続はrelease受入として並行管理し、Phase 5 source実装の先行条件にはしない。
Windows golden生成、VM導入、Windows CLI比較は先行条件にしない。

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
| 2026-09-24 | Phase 3完了 | save acknowledgement、encoding／混在改行／type、行番号、find／replace、diagnostic link、複数document、drop、recovery／backup、shortcut、MUC／N88 UTIを実装 | Release／Debug／ASan+UBSan／TSanで全20 CTest成功。実GUIで検索、行移動、compile error、複数window、crash recovery、N88 openを確認 |
| 2026-09-25 | Phase 3 Dark Mode／行番号表示修正 | `LineNumberRulerView`の幅を46ptへ固定し、ruler背景がeditor全体を覆ってMUC本文を隠す問題を修正。editor／messageへ動的system colorを適用。行番号の位置と表示領域をAppKitでtext viewからrulerへ変換し、本文と同じ境界でclipしてglyph baselineへ整列 | Dark Mode実GUIで`sampl1.muc`本文、行番号、status、compile transcriptを表示。scrollbarの上端／中間／下端／端数位置と上端復帰時に追従し、上下端の部分行で本文と行番号のclipとbaselineが一致することを確認 |
| 2026-09-25 | Phase 4計画具体化 | application単位Coordinator、stale compile抑止、transport／shortcut、resource解決、device hotplug／再接続、audio診断、click対策、Phase 5との次曲境界を確定 | 現行のPlaybackSession、AudioDeviceService、AppKit、CLI resource経路、Windows HSP操作、GUI受入仕様を照合。実装・試験は未着手 |
| 2026-09-26 | Phase 4 4-0完了 | ResourceConfiguration、application共有PlaybackCoordinator、play intent世代、状態別command、refill／drop診断契約を実装 | `playback_coordinator_test`を追加。Release buildと全21 CTest成功。GUI接続、resource実読込、実deviceは未着手 |
| 2026-09-27 | Phase 4 4-1／4-2完了 | default PCM／voice、rhythm、外部ROMを明示path化し、AppKitへcompile-and-play、transport、progress、速度、resource選択、F5／F12・Esc・Control-F1を接続 | タグ優先、未保存相対path、不足resource、PCM preload、pending intent取消を試験。Release buildと全21 CTest成功。device picker／hotplugは4-3へ継続 |
| 2026-09-27 | Phase 4 4-3完了 | device名基準のoutput選択、SDL hotplug、DeviceLost、明示Reconnect、requested／obtained format表示を実装 | SDL dummyでaudio event、切断時の非自動切替、選択後の曲頭再接続、新SessionIdを確認。Release buildと全21 CTest成功。実CoreAudio抜き差しは4-6へ継続 |
| 2026-09-27 | Phase 4 4-4完了 | audio診断表示、256 frame fade-in／out、自然終了fade、Phase 5向け次曲hookを実装 | 100回のtransport／device再初期化、sample連続性、2曲遷移を追加試験。Release buildと全23 CTest成功。sanitizerは4-5、実CoreAudio聴感は4-6へ継続 |
| 2026-09-27 | Phase 4 4-5完了 | Phase 4追加分を含むdummy audio／sanitizer回帰を実施し、sanitizer build directoryをignore対象化 | Release、Debug、ASan／UBSan、TSanの各構成で全23 CTest成功。memory／UB／data race／hang報告なし。実CoreAudio受入は4-6へ継続 |
| 2026-09-27 | Phase 4 4-6一部完了 | 実CoreAudioの60分連続再生で20 ms待機が1024-frame callback周期より短くdropする問題を検出し、250 msのbackpressure待機へ修正 | `System Default`で60分、loop 73、rendered 160259072、underrun／drop／refill 0。Stop後Idle／queue 0。4構成の全23 CTest成功。内蔵speaker聴感と物理hotplugは手動受入へ継続 |
| 2026-09-27 | Phase 5計画具体化 | metadata／library値型、playback owner、compile-ahead playlist、時間／比率policy、Home／Player／11 channel monitor、単一15 Hz presentation経路を確定 | Windows `mucom88win.hsp`／`aplayer.hsp`、既存Document／Compile／Playback／Export service、Phase 4次曲hookを照合。実装は未着手 |
| 2026-09-28 | Phase 5実装前test完了 | metadata、library、playlist、policy、presentation、integration／lifetimeの64ケースを抽出し、5 executable contractをCTestへ常設登録 | Release／Debug／ASan+UBSan／TSanで既存23件成功、Phase 5の5件は対応header未実装のため予定どおりSkipped。header追加時にcontract本体が自動有効化され、skip残存を各段階の未完了条件とする |
