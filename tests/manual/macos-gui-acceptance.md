# macOS GUI機能同等化 受入試験

## 目的

Windows GUI sourceから抽出した53機能について、Windows runtimeを使わず、macOS版で同じ利用目的を
達成できることを検証する。画面配置、内部API、生成fileのbyte一致は検査対象にしない。

release候補ごとにこの文書のcopyへ、`PASS`、`FAIL`、`BLOCKED`、`N/A`の結果と証跡を記録する。
`N/A`は`excluded`だけに使用する。`extension`は標準版releaseを妨げない。

## 実施記録

```text
app_version=
app_sha256=
git_commit=
macos_version=
macos_build=
machine_model=
soc=
sdl_version=
test_date=
tester=
```

## 証跡の規則

- 自動試験欄は追加予定のCTestまたはtest suite名であり、実装後は実際のtest名へ合わせる
- 手動項目には実施日、結果、短い観察記録を残す
- audio項目にはdevice、継続時間、underrun、dropped sampleを記録する
- network、更新、共有はtest doubleを基本とし、意図しない外部送信を行わない

### Phase 3開発時確認（2026-09-24～2026-09-25）

これはrelease候補の最終受入記録ではなく、Phase 3実装時の開発確認である。

| 対象 | 結果 | 証跡 |
|---|---|---|
| `GUI-EDIT-01`～`05`、`07`、`09` | PASS | Release／Debug／ASan+UBSan／TSanで全20 CTest成功。実GUIでruler、find／replace、Go to Line、diagnostic link、encoding menu、複数windowを確認 |
| `GUI-EDIT-06` | PARTIAL | MMLのdirty、Undo、close確認は実装済み。voice変更の統合はnative FM音色editorを実装するPhase 7で確認する |
| `GUI-EDIT-08` | PARTIAL | Phase 3のsave、compile、検索、行移動commandは実装済み。再生、stop、早送りshortcutはPhase 4で有効化する |
| document recovery | PASS | 未保存編集の5秒後にrecovery生成、process強制終了、次回起動の復元dialog、untitled documentへの内容復元を確認 |
| MUC／N88 UTI | PASS | `plutil`、ad-hoc署名を検証し、Launch Services経由で`.n88`を開いてN88-BASIC type判定を確認 |
| Dark Mode editor表示 | PASS | `sampl1.muc`で本文、46pt幅の行番号、status、compile transcriptを同時表示。行番号をAppKitのview座標変換で描画し、上端／中間／下端への縦scrollと上端への復帰時に本文と追従すること、上下端の部分行が本文と同じ境界でclipされbaselineが一致することを確認 |
| sandbox bookmark | N/A | 現在のappはsandboxを採用していない。採用時だけ文書外resource directoryをbookmark化する |
| Phase 4 shortcut | BLOCKED | F5／F12、Esc、Control-F1は再生・停止・早送りUIと同時にPhase 4で有効化する |

### Phase 4実CoreAudio開発時確認（2026-09-27）

これはrelease候補の最終受入記録ではなく、Phase 4 4-6の開発確認である。

```text
app=build/MUCOM88Editor.app (Release)
macos_version=27.0
macos_build=26A428
machine_model=MacBook Air (Mac17,3)
soc=Apple M5
device=System Default
requested_format=44100 Hz / signed 16-bit / 2 ch / 1024 frames
obtained_format=44100 Hz / signed 16-bit / 2 ch / 1024 frames
source=package/sampl1.muc
test_date=2026-09-27
```

| 対象 | 結果 | 証跡 |
|---|---|---|
| 実CoreAudio 60分連続再生 | PASS | 60分時点でPlaying、loop 73、queued 16384、rendered 160259072、underruns 0、dropped 0、refills 0。UIとloop進行も継続 |
| Stop／device close | PASS | Stop後にIdle、queued 0、underruns 0、dropped 0、refills 0、rendered 163442176 |
| backpressure回帰 | PASS | 20 ms待機を250 msへ修正。transport 100回後のunderrun／drop 0を常設化し、Release／Debug／ASan+UBSan／TSanで各23/23 CTest成功 |
| PCM、tempo、音切れ、click、終了noiseの聴感 | BLOCKED | GUI automationは実音を聴取できない。内蔵speakerを選んだ人間の聴取で判定する |
| 実物device切断／再接続 | BLOCKED | 物理操作は未実施。外部output deviceで`DeviceLost`、無断切替なし、明示`Reconnect`による曲頭再開を確認する |

初回の実機再生では1024 frame／44.1 kHzの約23.2 ms callback周期に対してring満杯時の待機が20 msしかなく、
callback直前にproducerがtimeoutして`dropped_frames`が増加した。修正後の上記60分runではdropを再現しなかった。
`System Default`が実際に内蔵speakerへ向いていたかはautomationから特定できないため、内蔵speaker受入は
`PASS`に含めない。

### Phase 5実CoreAudio GUI受入（2026-10-04）

```text
app_version=development build (523c72e + Phase 5 acceptance fixes)
app_sha256=e8b5b09506176c25dc5154674a892c18c416851f15ccda2f7f3fe18a18237091
git_commit=523c72e (working tree under test)
macos_version=27.0.1
macos_build=26A434
machine_model=MacBook Air (Mac17,3)
soc=Apple M5
sdl_version=2.32.72
test_date=2026-10-04
tester=Codex GUI automation / local CTest
device=System Default
requested_format=44100 Hz / signed 16-bit / 2 ch / 1024 frames
obtained_format=44100 Hz / signed 16-bit / 2 ch / 1024 frames
```

| 対象 | 結果 | 証跡 |
|---|---|---|
| `GUI-HOME-01`～`02` | PASS | `package`の3 folder／3 MUC、directory先行、metadata、Back、子folder移動、Refreshを実GUI確認。N88／4 encoding／entry単位errorはPhase 3 GUI証跡とPhase 5 contractで補完 |
| `GUI-HOME-03` | PASS | dirtyな`sampl1.muc`の一時編集を保持したまま`sampl2.muc`を別windowでopen。内容保持後にUndo |
| `GUI-HOME-04`、`GUI-HOME-06` | PASS | editorを増やさないdirect Playと3曲playlistをSystem Defaultで再生 |
| `GUI-HOME-05` | PASS | `sampl1.muc`から65,647-byte MUBを保存しmacOS CLIで再読込。save panel Cancelを実GUI、partial非生成をcontractで確認 |
| `GUI-PLAYER-01` | PASS | missing voiceの一時fixtureを`Failed`としてskipし、3曲目から1曲目へloop。全失敗の有界停止はcontractで確認。試験後fixture削除 |
| `GUI-PLAYER-02` | PASS | Now Playing、選択行、A～Kがsession切替へ追従。playlist中のBrowser PlayでplaylistだけStopped |
| `GUI-PLAYER-03` | PASS | 1秒／0%と0秒／1%を個別確認。Pause 3秒中はsession／count不変。Next／Previous／Stop成功 |
| `GUI-PLAY-08`、`GUI-MON-01`～`02` | PASS | monitor 20回開閉。A～Kとcount表示がactive sessionに一致し、underrun／drop／refill 0、hangなし |
| 全構成回帰 | PASS | Release／Debug／ASan+UBSan／TSanで各28 CTest成功、Skip 0。ad-hoc署名検証成功 |

受入中にPlayerのsplit pane collapse、Idle時の前曲count残留、複数playlist entryの`Playing`残留を検出し、
修正後に上記項目を再確認した。主観的な音質と物理device抜き差しはPhase 4の別受入として未完了であり、
このPhase 5結果には含めない。

## Editor・document

| ID | Scope | 予定自動試験 | 手動操作と期待結果 |
|---|---|---|---|
| `GUI-EDIT-01` | standard | `document_service_test`: UTF-8／CP932／Shift_JIS round trip | 日本語MUCを編集、保存、再openして文字と改行が失われない |
| `GUI-EDIT-02` | standard | `compile_service_test`: diagnosticの行、optional column、message | compile結果と全diagnosticを表示し、選択したerror行へ移動できる。compilerがcolumnを返さない場合は未設定とする |
| `GUI-EDIT-03` | standard | `editor_line_model_test`: 改行とUnicodeの論理行 | cursorと折返しに追従して正しい行番号を表示する |
| `GUI-EDIT-04` | standard | `document_service_test`: new/open/save/save-as | menu操作、上書き確認、title更新が正しい |
| `GUI-EDIT-05` | standard | `document_service_test`: MUC、N88、任意text判定 | filterが働き、CP932／Shift_JISの曖昧判定と未知encodingを明示確認する |
| `GUI-EDIT-06` | standard | `document_service_test`: dirty状態遷移 | MML／voice変更を表示し、変更を無断破棄しない |
| `GUI-EDIT-07` | standard | `document_service_test`: URLとtitle | open、save-as、rename後にtitleを更新する |
| `GUI-EDIT-08` | standard | `editor_command_test`: command dispatch | menuとkeyboardの双方でsave、compileを実行する。F5／F12、Esc等の再生commandはPhase 4で有効化する |
| `GUI-EDIT-09` | standard | `document_service_test`: close判断 | close／終了時に保存、破棄、取消が働く |

## Compile・再生

| ID | Scope | 予定自動試験 | 手動操作と期待結果 |
|---|---|---|---|
| `GUI-PLAY-01` | standard | `playback_session_test`: 最新snapshot | 未保存の最新内容をcompile・再生でき、UIが固まらない |
| `GUI-PLAY-02` | standard | `compile_service_test`: 複数error | 選択したdiagnostic行へ移動できる |
| `GUI-PLAY-03` | standard | `playback_session_test`: 状態遷移 | play、pause、resume、stop、Escの表示と状態が一致する |
| `GUI-PLAY-04` | standard | `playback_session_test`: fake clock | 早送りを開始・解除し、通常速度へ戻せる |
| `GUI-PLAY-05` | standard | `playback_session_test`: x2～x10 | 各倍率とprogress進行率が一致する |
| `GUI-PLAY-06` | excluded | なし | `N/A`。Windows sourceでも未使用のslow設定をrelease noteへ記載する |
| `GUI-PLAY-07` | standard | `playback_session_test`: progress | 再生、停止、曲切替で現在値と最大値が正しい |
| `GUI-PLAY-08` | standard | `playback_presentation_test`: 15 Hz fan-outと購読解除 | 再生中にmonitorを20回開閉してもUI、音声、診断値が乱れない |
| `GUI-PLAY-09` | standard | `compile_service_test`: driver選択 | MUCOM88 1.7／1.5／EMの使用中driverを確認できる |
| `GUI-PLAY-10` | standard | `resource_resolution_test`: PCM／voice | 異なる起動directoryでもdocument相対resourceを再生できる |
| `GUI-PLAY-11` | standard | `rhythm_resource_test`: 合成6 WAV | 指定したrhythm channelが非無音になる |

`rhythm_resource_test`はbuild directoryへ短い16-bit PCM WAVを、`2608_BD.WAV`、`2608_SD.WAV`、
`2608_TOP.WAV`、`2608_HH.WAV`、`2608_TOM.WAV`、`2608_RIM.WAV`の名前で生成する。

## Home・file browser

| ID | Scope | 予定自動試験 | 手動操作と期待結果 |
|---|---|---|---|
| `GUI-HOME-01` | standard | `library_service_test`: 非同期列挙、filter、安定sort、世代 | folderを選び、子folderとMUC／N88だけを一覧し、移動／Back／Refreshできる |
| `GUI-HOME-02` | standard | `metadata_service_test`: 4 encodingとtag規則 | 選択曲のtitle、author、composer、date、voice、PCM、commentをcompileせず表示する |
| `GUI-HOME-03` | standard | `library_action_test`: document open契約 | dirty editorを破棄せず、選択fileを新規または既存editor windowで開く |
| `GUI-HOME-04` | standard | `library_action_test`: Browser ownerとresource | editorを作らず選択MUCをcompile・再生し、active playbackをapplicationで共有する |
| `GUI-HOME-05` | standard | `library_action_test`: MUB atomic export／cancel | save panelの指定先へMUBを出力し、失敗／cancel時にpartial fileを残さない |
| `GUI-HOME-06` | standard | `playlist_service_test`: playlist生成 | main editorと競合せずautomatic playerを開始する |

## Sound monitor・player

| ID | Scope | 予定自動試験 | 手動操作と期待結果 |
|---|---|---|---|
| `GUI-MON-01` | standard | `monitor_snapshot_test`: A～K mappingと表示model | 11 channelのvoice、volume、detune、address、key／key-on、LFO、reverb、pan、quantizeを表示する |
| `GUI-MON-02` | standard | `monitor_snapshot_test`: count／SessionId | absolute interrupt、current、maximum、loop countが曲進行とsession切替に同期する |
| `GUI-PLAYER-01` | standard | `playlist_service_test`: skip／全失敗／末尾loop | compile失敗曲をskipして連続再生し、末尾から先頭へ戻り、全失敗時は停止理由を表示する |
| `GUI-PLAYER-02` | standard | `playlist_service_test`: owner／Now Playing snapshot | Now Playing、tag、channel詳細、選択行がactive曲と一致し、editor Play時はqueueが安全に終了する |
| `GUI-PLAYER-03` | standard | `playlist_policy_test`: fake clockとcount比率 | 90秒／150%の先着条件、Pause除外、0無効、無効値拒否を確認する |
| `GUI-PLAYER-04` | excluded | なし | `N/A`。未配布3D visualizerの除外理由をrelease noteへ記載する |

### Phase 5手動受入手順

Phase 5以後のrelease候補を再受入するときは、少なくとも次のfixture folderを一時directoryへ用意する。

- tagが揃ったUTF-8 MUC、tagなしMUC、CP932 MUC、N88を各1件
- file名の大文字／小文字と`.MUC`を含むsort確認用file
- compile成功する有限曲とloop曲、意図的なcompile error曲
- PCMを使う`sampl1.muc`と必要な相対resource

1. Homeでfixture folderを選び、directory先行、MUC／N88 filter、安定sort、Back／Refreshを確認する。
2. 各fileを選び、metadataとencoding errorが他entryへ波及しないことを確認する。
3. dirtyなeditorを残したまま別fileをOpen in Editorし、編集内容が失われないことを確認する。
4. Playでeditor windowが増えず、editor／Home／PlayerのtransportとNow Playingが同じsessionを示すことを確認する。
5. Export MUBで保存先を選び、成功fileをmacOS版で再読込する。cancel時はdestinationと一時fileが残らないことを確認する。
6. success、compile error、loopの順を含むplaylistを開始し、error skip、末尾loop、Next／Previous、Stopを確認する。
7. 90秒条件と150%条件を個別に短い試験値へ変更し、先に達した条件で次曲へ進むこと、Pause中は時間が進まないことを確認する。
8. monitorを20回開閉し、A～K、count、Now Playingがactive sessionと一致し、診断値のunderrun／dropが増えないことを確認する。
9. playlist再生中にeditorからCompile & Playし、playlistだけがcancelされeditor曲が継続することを確認する。

記録にはfolder内file数、再生順、skipしたfileとerror、policy値、使用device、monitor開閉回数、
`underruns`、`dropped_frames`、`refill_events`を残す。3D visualizerは実施せず`GUI-PLAYER-04=N/A`とする。

## Tool・export

| ID | Scope | 予定自動試験 | 手動操作と期待結果 |
|---|---|---|---|
| `GUI-TOOL-01` | standard | `phase6_text_transform_contract_test`: N88行番号 | preview後に適用でき、Undo一回で本文・選択範囲・dirty状態が戻る |
| `GUI-TOOL-02` | standard | `phase6_g_channel_contract_test`: G channel `q` | 他channel、tag、commentを変えず変換する |
| `GUI-TOOL-03` | standard | `phase6_metadata_tag_contract_test`: tag、encoding、compile | 欠落tagだけ追加し、既存・重複tagの規則を表示する |
| `GUI-TOOL-04` | standard | `phase6_voice_append_contract_test`: voice抽出 | 使用voiceだけを一度ずつ追記し、再compile・Undoできる |
| `GUI-TOOL-05` | standard | `phase6_pcm_bank_contract_test`: DATA／VOICE | sourceからPCM bankを生成し、macOS版で再読込する |
| `GUI-TOOL-06` | standard | `phase6_pcm_bank_contract_test`: list／WAV／ADPCM | 最大32 entryのbankを生成し、失敗fileとlist行を表示する |
| `GUI-TOOL-07` | standard | `phase6_text_transform_contract_test`: N88出力 | 開始行と増分を反映し、save panel Cancelではfileを作らない |
| `GUI-EXPORT-01` | standard | `phase6_export_operation_test`／`phase6_format_validator_contract_test`: RIFF | 指定秒数のWAVを保存し、progress・cancel・非無音を確認する |
| `GUI-EXPORT-02` | standard | `phase6_export_operation_test`／`phase6_format_validator_contract_test`: VGM／S98 | header、command、wait、終端が妥当な形式を保存する |

`GUI-TOOL-01`の操作入口はTools → Remove N88 Line Numbers…である。
番号付きsourceを開き、選択範囲を設定してpreviewを表示する。Original／Previewを確認し、
Cancelで本文・revision・dirtyが変わらないこと、Applyで変換できること、Undo一回で本文・
選択範囲・dirtyが戻り、Redoで再適用できることを確認する。保存済み／未保存の文書を両方確認する。
番号なし行やapostrophe欠落の入力はerrorを表示し、本文を変更しない。実GUI evidenceは未取得である。

`GUI-TOOL-02`の受入fixtureは、`G q1c ; q2`、`A q3d`、`G "q4" q5`、`#comment G q6`を含む
UTF-8 MUCと、同じ本文を行番号で包んだN88を用意する。ToolsのG channel変換入口からOriginal／Previewを開き、
Cancelで本文・選択範囲・dirty状態が不変、ApplyでGの`q1`と`q5`だけが`@`になり、Undo一回とRedo一回で
本文・選択範囲・dirty状態が戻ることを確認する。番号付きN88は行番号除去を先に行う。2 windowで別文書へ
誤適用しないこと、preview中の編集後にApplyが競合として拒否されること、変換後のcompile結果も記録する。
自動部分は`phase6_g_channel_contract_test`の`GCH-01`～`08`で検証する。

2026-10-06の実GUI確認: Releaseの`build/MUCOM88Editor.app`を起動し、未保存MUCへ上記4行を入力した。
Tools → Convert G Channel q to @…を開くとOriginalとPreviewが表示され、Previewは
`G @1c ; q2`、`A q3d`、`G "q4" @5`、`#comment G q6`だった。Cancelでは本文不変、
再度開いてApplyするとPreviewと同じ本文になった。Edit menuのUndo名は
`Convert G Channel q to @`で、1回のUndoで元の4行、1回のRedoで変換後の4行に戻った。
さらに非対象行`A q3d`を選択してApply／Undoし、Undo後に同じ文字列の選択が復元された。
保存済みMUC、dirty表示の復元、2 window、preview中の競合、N88からの連続操作、
実GUIでのcompileは未確認。従って手動受入全体は未完了である。

`GUI-TOOL-03`の実装前受入設計（2026-10-06）: 保存済みUTF-8 MUCに
`#mucom88 1.5`、既存の`#title Existing`、MML本文を置き、`composer`、`author`、
`voice`、`pcm`、`date`、`comment`は未設定にする。Toolsのtag追加画面では7項目を確認し、
既存titleの非上書きを明示する。未入力欄はtag行を作らず、入力した欄だけを既存の先頭tag blockの後へ
挿入する。Original／Previewを見てCancelすれば本文・選択・revision・dirtyが不変、Applyすれば
一回のUndo／Redoで全追加行を戻し／再適用できることを確認する。同じ操作の2回目はno-opにする。
`#TITLE`しかない別文書では小文字`#title`が新規追加されruntime metadataへ反映されること、
CP932および混在改行の文書では保存後も文字コードと元行の改行種別を保持することを確認する。
2 window間の誤適用とpreview後の編集競合を拒否し、番号付きN88では先に行番号を除去する。
最後に`sampl1.muc`からcomposerだけを除いたfixtureへ再追加し、macOS版でcompileする。
2026-10-06にGUI入口を実装し、TAG-03／06の不具合を修正した。専用testは全14 caseがPassする。

`GUI-TOOL-03`の詳細ケース（2026-10-06、下記に実施証拠を記録）:

| ID | 操作・fixture | 期待結果・記録する証拠 |
|---|---|---|
| `TAG-GUI-01` | 保存済みMUCで7欄を確認。titleは既存、composerだけ入力し、他欄は空欄 | 既存titleを変更せずcomposerだけ追加するpreview。date等を暗黙に生成しない。入力画面とpreviewを記録 |
| `TAG-GUI-02` | 保存済み／dirty文書で文字列を選択。入力画面でCancel、別試行でpreviewからCancel | 本文・選択・dirty不変、Undo履歴にtoolを追加しない。model revisionの確認はTAG-09が担当 |
| `TAG-GUI-03` | 保存済み文書に複数tagをApply、Undo一回、Redo一回 | Applyはdirty、Undoは元本文・元選択・clean、Redoは追加本文・dirty。menuのUndo名と各状態を記録 |
| `TAG-GUI-04` | 元からdirtyな文書でApply／Undo／Redo、追加後に同じ入力で再実行 | Undoでも元の編集を残してdirty。再実行は本文不変でUndo操作を増やさない |
| `TAG-GUI-05` | CRLF優勢の混在改行MUCとCP932日本語MUCでApply／Undo／Redo後にそれぞれ別pathへSave As | 各保存物のbytesを比較。Undo後は元encoding・BOM・全改行種別・末尾改行へ戻る。Redo後は追加行だけpreferred改行となる。元fixtureを上書きしない |
| `TAG-GUI-06` | `#TITLE Upper`と小文字tag重複を含む文書で入力 | 大文字行を保持し小文字titleを追加。既存小文字tagとその重複行は全て保持し、最初の値がruntimeへ反映される |
| `TAG-GUI-07` | 2文書を開き片方でpreview。別文書を編集。可能な経路で元文書のrevisionを変更してApply | 別文書へ適用しない。元文書の変更後はConflictで本文・設定を保持。modal sheetが元文書の編集を遮断する場合はその事実を記録し、競合はTAG-05／11で確認 |
| `TAG-GUI-08` | 改行を含む値をpaste、番号付きN88で起動、全欄空欄で起動 | 不正値を適用しない。N88は先に行番号除去を案内。全欄空欄は本文・dirty・Undo履歴を変更しない |
| `TAG-GUI-09` | `sampl1.muc`からcomposerだけ除いたcopyで入力→Apply→Save→再open→compile | 既存titleと再追加composerがmetadataに反映され、PCM内蔵MUBへcompile成功。保存物とcompile結果を記録 |

自動テストはTAG-01～14が4構成でPassする。実GUI確認は既存の起動中アプリを変更せず、
Release appの検証専用copyと一時fixtureで行った。

- TAG-GUI-01: 7欄を確認。既存titleはdisabledで値Existingを保持し、composerだけ入力すると
  他の空欄の行は追加されなかった。
- TAG-GUI-02（preview Cancel）: A cを選択したdirty文書でpreview Cancel後の本文・選択を確認した。
- TAG-GUI-03／04（一部）: dirty文書でApply → Undo一回 → Redo一回を行い、本文が往復し、
  Undoで元のA c選択が復元された。保存済み混在改行fixtureでもB dの選択復元を確認した。
- TAG-GUI-05（混在改行）: 元bytesは `#mucom88 1.5\r\n#title Existing\r\nA c\nB d\r`。
  composer C／author AをApply、Undo、Saveすると元bytesと完全一致した。Redo、Save後は
  `#mucom88 1.5\r\n#title Existing\r\n#composer C\r\n#author A\r\nA c\nB d\r`と完全一致した。
- 再実行のno-op: title／composer／authorがdisabled、他欄を空欄にしたpreviewはOriginalと一致した。
  Apply後にEdit → Undo Add Metadata Tagsを選ぶと、前回追加した2行が一回で消え、B d選択が戻った。

上記は基本GUI経路と混在改行保存の証拠である。dirty表示自体、CP932の実GUI保存、入力画面Cancel、
大文字tag／N88の実GUI操作、2文書競合、再open／GUI compileの残りの受入は未確認である。
GUI-TOOL-03全体を完了扱いにはしない。


`GUI-TOOL-07` N88-BASIC出力の詳細受入設計（実施証拠は下記）:

| ID | 操作 | 期待結果・証拠 |
|---|---|---|
| `N88-GUI-01` | 保存済みMUCで出力を開き、既定1000/10と任意7/3でpreview | 行番号・空行・末尾改行を反映。出力はnumbered textであることを表示し、元editor本文を変更しない |
| `N88-GUI-02` | dirty文書で選択を設定し、設定画面／previewでそれぞれCancel | 本文・選択・dirty・location・Undo履歴が変わらない。source revisionの非変更は自動N88-06が担当 |
| `N88-GUI-03` | previewからsave panelへ進みCancel | destinationと一時fileを作らない。既存destinationも変えず、元文書に保存済みの印を付けない |
| `N88-GUI-04` | 開始行負値、増分0／負値、最終行overflowを入力 | 検証errorを表示し、保存を開始しない。INT_MAX境界と末尾改行は自動N88-04も確認 |
| `N88-GUI-05` | 日本語／space pathへUTF-8 BOMまたはCP932で保存、再open | selected encoding・BOM・改行と開始行／増分を反映。元sourceのpath・dirty・Undo履歴を保つ |
| `N88-GUI-06` | CP932非対応文字の文書を既存destinationへ保存、書込不可pathも試す | 理由を表示し既存fileを保護、partialなし。元文書を保存済みにしない |
| `N88-GUI-07` | 2文書を開き片方をpreview、別windowを操作 | captured sourceだけを出力。他文書へApply／Saveしない。元sourceのrevisionが変わった場合は再previewを要求し古い結果を現在の出力として黙って保存しない |
| `N88-GUI-08` | 空文書、blank行だけ、no-final-newlineの文書を出力 | N88-03の境界規則と一致し、元選択・Undoを保持する |
| `N88-GUI-09` | sampl1.mucのcopyから出力→再open→行番号除去→compile | 元曲と同じmetadata、PCM内蔵MUBを得る。番号付きsourceへ再度出力する操作はGUIで案内・拒否し、二重番号を付けない |

2026-10-06にTools → Export N88-BASIC Source…とN88ExportServiceを実装した。
自動N88-01～15が4構成でPass。保存fixtureはproduction service経由で検証する。
検証専用Release appの新規文書に `A c\n\nB d` を入力し、B dを選択して実GUIを確認した。

- N88-GUI-01: 既定1000/10とUTF-8の設定欄を確認し、7/3でpreviewを表示した。
  表示は `7 'A c\n10 '\n13 'B d` と一致した。
- N88-GUI-03: save panelのCancel後も元本文とB d選択が不変だった。
- N88-GUI-05（一部）: 一時directoryへfixture.n88を保存し、保存bytesがpreviewと完全一致した。
  元文書の本文、名称未設定のlocation、B d選択を保持した。
- 元Undo履歴: 出力後にUndo一回で元のpasteが取り消され空文書へ戻った。
  出力自身はUndo操作を登録していない。Redo後に元本文へ戻した。
- N88-GUI-04（一部）: 増分0で整数入力errorを表示し、preview／save panelを開始しなかった。

CP932／BOMのGUI選択・保存、設定／preview Cancel、2 window、再open／GUI compile等の
残りのmatrixは未実施であり、GUI-TOOL-07全体の完了とはしない。

Phase 6の実装前contractは`src/tests/phase6/README.md`に記録する。save panel Cancelではoperationを開始せず、
処理中Cancelでは既存destinationを維持して一時fileを残さない。生成したMUBはmacOS版で再読込・再生する。

## Settings・連携・情報

| ID | Scope | 予定自動試験 | 手動操作と期待結果 |
|---|---|---|---|
| `GUI-SET-01` | standard | `settings_test`: user／resource | 再起動後も設定を維持しcompileへ反映する |
| `GUI-SET-02` | standard | `document_recovery_test`: recovery | 異常終了後に復旧し世代上限を守る |
| `GUI-SET-03` | standard | `localization_test`: ja／en | 言語変更後に主要UIの欠落keyがない |
| `GUI-SET-04` | standard | `settings_test`: window／font／色 | display変更後も安全な位置へ復元する |
| `GUI-SET-05` | standard | `settings_test`: schema migration | 破損値を安全なdefaultへ戻す |
| `GUI-FMEDIT-01` | standard | `voice_service_test`: 256 voice | 音色を編集、試聴、反映、保存する |
| `GUI-SCCI-01` | extension | `provider_contract_test`: fake real-chip | 未導入理由とsoftware代替を表示し、対応時だけ実機確認する |
| `GUI-DOTNET-01` | extension | `provider_contract_test`: fake external-driver | handshake、timeout、cancelと未導入表示を確認する |
| `GUI-UPD-01` | standard | `update_service_test`: 署名mock | 更新を無効化でき、失敗時も通常起動する |
| `GUI-WEB-01` | standard | `external_action_test`: HTTPS | Helpからdefault browserで文書を開く |
| `GUI-SHARE-01` | standard | `external_action_test`: share request | 明示操作時だけ共有しcancelできる |
| `GUI-ABOUT-01` | standard | `about_metadata_test`: metadata | app/core version、著作権、license、creditを表示する |

## 件数検査

repository top directoryで次を実行する。

```sh
test "$(rg -o 'GUI-[A-Z]+-[0-9][0-9]' tests/manual/macos-gui-acceptance.md | sort -u | wc -l | tr -d ' ')" = 53
test "$(rg -c '^\| `GUI-.*\| standard \|' tests/manual/macos-gui-acceptance.md)" = 49
test "$(rg -c '^\| `GUI-.*\| extension \|' tests/manual/macos-gui-acceptance.md)" = 2
test "$(rg -c '^\| `GUI-.*\| excluded \|' tests/manual/macos-gui-acceptance.md)" = 2
```

## Release判定

- 標準版49項目はすべて`PASS`が必要
- 除外2項目は`N/A`とし、release noteへ理由を記載する
- 拡張2項目は標準版では未提供でもよいが、UIが利用不可理由と代替を表示する
- 拡張機能を対応済みとして配布する場合だけ、対象providerと実環境で`PASS`を必要とする
