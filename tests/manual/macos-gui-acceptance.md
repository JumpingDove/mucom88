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

## Editor・document

| ID | Scope | 予定自動試験 | 手動操作と期待結果 |
|---|---|---|---|
| `GUI-EDIT-01` | standard | `document_service_test`: UTF-8／CP932／Shift_JIS round trip | 日本語MUCを編集、保存、再openして文字と改行が失われない |
| `GUI-EDIT-02` | standard | `compile_service_test`: diagnosticの行、column、message | compile結果を表示し、error行へ移動できる |
| `GUI-EDIT-03` | standard | `editor_line_model_test`: 改行とUnicodeの論理行 | cursorと折返しに追従して正しい行番号を表示する |
| `GUI-EDIT-04` | standard | `document_service_test`: new/open/save/save-as | menu操作、上書き確認、title更新が正しい |
| `GUI-EDIT-05` | standard | `document_service_test`: MUC、N88、任意text判定 | filterが働き、未知encodingを明示確認する |
| `GUI-EDIT-06` | standard | `document_service_test`: dirty状態遷移 | MML／voice変更を表示し、変更を無断破棄しない |
| `GUI-EDIT-07` | standard | `document_service_test`: URLとtitle | open、save-as、rename後にtitleを更新する |
| `GUI-EDIT-08` | standard | `editor_command_test`: command dispatch | menuとkeyboardの双方でsave、compile、stopを実行する |
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
| `GUI-PLAY-08` | standard | `monitor_snapshot_test`: 購読 | 再生中にmonitorを開閉しても音切れしない |
| `GUI-PLAY-09` | standard | `compile_service_test`: driver選択 | MUCOM88 1.7／1.5／EMの使用中driverを確認できる |
| `GUI-PLAY-10` | standard | `resource_resolution_test`: PCM／voice | 異なる起動directoryでもdocument相対resourceを再生できる |
| `GUI-PLAY-11` | standard | `rhythm_resource_test`: 合成6 WAV | 指定したrhythm channelが非無音になる |

`rhythm_resource_test`はbuild directoryへ短い16-bit PCM WAVを、`2608_BD.WAV`、`2608_SD.WAV`、
`2608_TOP.WAV`、`2608_HH.WAV`、`2608_TOM.WAV`、`2608_RIM.WAV`の名前で生成する。

## Home・file browser

| ID | Scope | 予定自動試験 | 手動操作と期待結果 |
|---|---|---|---|
| `GUI-HOME-01` | standard | `browser_service_test`: 列挙とsort | folderを選びMUC／N88だけを一覧する |
| `GUI-HOME-02` | standard | `metadata_service_test`: tag | 選択曲のtitle、author、composer、date、voice、PCM、commentを表示する |
| `GUI-HOME-03` | standard | `browser_action_test`: open | 未保存確認後に選択fileをeditorで開く |
| `GUI-HOME-04` | standard | `browser_action_test`: direct play | editorを開かず選択MUCをcompile・再生する |
| `GUI-HOME-05` | standard | `browser_action_test`: MUB export | save panelの指定先へMUBを出力する |
| `GUI-HOME-06` | standard | `playlist_service_test`: playlist生成 | main editorと競合せずautomatic playerを開始する |

## Sound monitor・player

| ID | Scope | 予定自動試験 | 手動操作と期待結果 |
|---|---|---|---|
| `GUI-MON-01` | standard | `monitor_snapshot_test`: A～K mapping | 11 channelのvoice、volume、detune、address、key、LFO、reverb、pan、quantizeを表示する |
| `GUI-MON-02` | standard | `monitor_snapshot_test`: count | interrupt、current、maximum countが曲進行と同期する |
| `GUI-PLAYER-01` | standard | `playlist_service_test`: skip／loop | 失敗曲をskipして連続再生し末尾から先頭へ戻る |
| `GUI-PLAYER-02` | standard | `playlist_service_test`: 表示snapshot | Now Playing、tag、channel詳細が選択曲と一致する |
| `GUI-PLAYER-03` | standard | `playlist_service_test`: 時間／割合 | 指定条件で次曲へ進み無効値を拒否する |
| `GUI-PLAYER-04` | excluded | なし | `N/A`。未配布3D visualizerの除外理由をrelease noteへ記載する |

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
