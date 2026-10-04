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
| `GUI-TOOL-01` | standard | `text_transform_test`: N88行番号 | preview後に適用できUndoで戻せる |
| `GUI-TOOL-02` | standard | `text_transform_test`: G channel `q` | 他channelとcommentを変えず変換する |
| `GUI-TOOL-03` | standard | `text_transform_test`: tag | metadata tagを追加し重複規則を表示する |
| `GUI-TOOL-04` | standard | `voice_service_test`: voice抽出 | 使用voiceだけを追記して再compileする |
| `GUI-TOOL-05` | standard | `pcmtool_test`: DATA／VOICE | sourceからPCM bankを生成・再読込する |
| `GUI-TOOL-06` | standard | `pcmtool_test`: list／WAV／ADPCM | 最大32 entryのbankを生成し失敗fileを特定する |
| `GUI-TOOL-07` | standard | `text_transform_test`: N88出力 | 開始行と増分を反映しsave panelへ出力する |
| `GUI-EXPORT-01` | standard | `export_format_test`: RIFF | 指定秒数のWAVを保存しcancel時に一時fileを残さない |
| `GUI-EXPORT-02` | standard | `export_format_test`: VGM／S98 | header、command、wait、終端が妥当な形式を保存する |

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
