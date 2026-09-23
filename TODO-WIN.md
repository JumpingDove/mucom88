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
- Windows比較はApple Silicon上のWindows 11 ARM64 VMだけで実施し、x64 Windows環境を
  完了条件にしない
- Windows ARM VM上のx86 emulation結果は機能比較基準として採用するが、x64/WOW64固有の
  性能、timing、driver互換性を証明するものとして扱わない

## 同等化レベル

| レベル | 対象 | 現状 |
|---|---|---|
| Level 1 | CLIのコンパイル、再生、各種ファイル出力 | 一部達成。厳密なWindows比較が未完了 |
| Level 2 | 通常のGUI編集・再生ワークフロー | 文書・compile service基礎を一部実装。GUIは未着手 |
| Level 3 | FM音色エディタ、PCMツール、MIDI、モニター | 未着手または部分実装 |
| Level 4 | プラグイン、実チップ、外部ドライバ | 未着手。user-mode機能を優先し、実チップは条件付き |

### macOS初期版の編集機能スコープ

macOS初期版ではMML文書編集とFM音色エディタ／pluginを分離する。通常の曲作成に必要な
`#voice`、既定の`voice.dat`、MML内の`@n:{...}`音色定義は対応する一方、V.EDIT、音色parameterの
GUI編集、再生中voice更新、編集した`voice.dat`の保存、pluginからのMML更新は初期版の完了条件に
含めない。

`voice.dat`はcompile時の読み取り専用resourceとする。macOS文書の保存はMML textだけを対象とし、
Windows editor互換APIの`SaveEditorMML()`や一時音色fileを経由しない。利用者は外部`voice.dat`の
参照またはMML内音色定義でFM音色を指定できる。対話的FM音色編集と汎用plugin機構は初期版完成後、
需要と安全なAPI境界を確認して採否を再判断する。

## Phase 0: 互換性基準の確定

- [x] 比較対象とするWindows版のversion、配布物、driverを固定する
- [x] Windows GUIの全機能を操作単位で一覧化し、必須・任意・廃止候補へ分類する
- [x] Windows CLIとmacOS CLIで共通に使用するsample、PCM、voice、ROMを固定する
- [ ] Windows ARM VMでMUB、WAV、VGM、S98の機能比較goldenを生成・保存する
- [ ] Windows ARM VMで終了code、標準出力、標準errorの期待値を記録する
- [ ] GUI、user-mode plugin、実chip代替方針を含む受入試験表を作成する
- [ ] Windows固有機能について、同等動作、macOS向け代替、非対応のいずれかを決定する
- [x] 初期MML editorからFM音色editorとpluginを分離し、`voice.dat`を読み取り専用入力とする

### 固定済みWindows比較対象

比較対象は外部から取得した最新版ではなく、このrepositoryの次のsource/package snapshotとする。
将来packageが更新された場合も既存のgoldenを暗黙に置換せず、新しい比較対象としてversion、
hash、変更理由をこの節へ追加する。

| 項目 | 固定値 |
|---|---|
| repository commit | `535a65c472ac67abe5f9a852092ee755e4c202c6` |
| Windows製品version | MUCOM88 Windows `0.70` |
| MUCOM88 core表記 | OpenMucom88 `1.7d` |
| package directory | `package/` |
| binary architecture | PE32 / Intel 80386 (x86) |
| host | Apple Silicon Mac。model、SoC、macOS versionを実行時に記録する |
| 基準OS | Windows 11 ARM64 24H2 VM。Wineは使用しない |
| 実行方式 | Windows on Armのx86 user-mode emulationでPE32/x86 binaryを実行する |
| hypervisor | 使用製品とversionを初回実行時に固定し、golden metadataへ記録する |
| 基準locale | 日本語system locale、console code page 932 |
| 基本比較 | software音源を使用し、plugin、SCCI2、network更新確認を無効化する |

Windowsの正確なedition、OS build番号、適用patch、host Mac、hypervisor、割当CPU/memory、
console code pageはgolden生成時のmetadataへ必ず記録する。Windows、macOSまたはhypervisorの
更新後に結果が変化した場合は、旧goldenを上書きせず差分を調査する。

#### Apple Silicon-only比較方針

このprojectでは、Windows x64実機やIntel/AMD hostを必須にせず、Apple Silicon上の
Windows 11 ARM64 VMで得た結果をWindows版の**機能比較基準**として採用する。
packageのWindows componentはPE32/x86であり、VM上ではx86 user-mode emulationを経由して
実行される。この制約を隠さず、証跡を次の3区分で管理する。

| 証跡区分 | Apple Siliconだけで確定する対象 | 扱い |
|---|---|---|
| `A: deterministic` | file hash、CLI終了code、stdout/stderr、MUB、offline WAV/VGM/S98 | Phase 0/1の正式な機能比較基準にできる |
| `B: functional` | GUI操作、file I/O、software音源、仮想audio、x86 plugin/FM editor | 対象VMでの機能合格。物理x64の性能保証には使わない |
| `C: unavailable` | x64/WOW64固有挙動、物理DirectSound latency、ARM64 driverのないSCCI/USB実chip | Apple Silicon-only計画の完了条件から除外し、未検証制約として公開する |

将来x64 Windowsの結果が任意に提供された場合は追加比較してよいが、Phase 0完了やmacOS版の
開発開始を待たせない。差分が見つかった場合もWindows ARM基準を無断で置換せず、環境差として
原因を記録してからbaseline更新を判断する。

#### Windows ARM VM準備

- [ ] Apple Silicon上にWindows 11 ARM64 24H2 VMを作成する
- [ ] Windows licenseを有効化し、Windows Update適用後にsnapshotを作成する
- [ ] 日本語system localeを設定し、UTF-8 beta optionを無効、console code pageを932にする
- [ ] 仮想audio deviceを有効にする
- [ ] repositoryをVM内のNTFS volume上へ配置し、shared folder上では試験しない
- [ ] 基準commitをcheckoutし、Windows binaryとfixtureのSHA-256を再確認する
- [ ] host/guest/hypervisor/CPU/memory/locale情報を`environment.txt`相当へ保存する
- [ ] GUI初回起動はnetworkを無効にし、update確認、SCCI2、pluginを基本比較用に無効化する
- [ ] 準備完了状態のsnapshot名と作成日を記録する

VMの最小metadata:

```text
host_arch=arm64
host_soc=<Apple SoC>
host_macos=<version/build>
hypervisor=<name/version>
guest_os=Windows 11 ARM64 24H2
guest_build=<build>
process_arch=x86
execution=x86-emulated
locale=ja-JP
code_page=932
```

#### Windows ARM VMで進める順序

1. binary/fixture hashとVM metadataを保存する
2. CLI正常系・異常系を各2回実行し、終了codeとraw stdout/stderrを取得する
3. driver別にMUBを各2回生成し、byte単位の決定性を確認する
4. 固定時間のWAV/VGM/S98を各2回生成し、file全体とdata領域を比較する
5. GUIの必須33項目をsoftware音源で実行する
6. x86 plugin、FM editor、MucomDotNETは基本比較と分離して条件付き試験する
7. DirectSound再生は仮想audioで機能確認し、latency/click評価はVM限定結果と明記する
8. SCCI2実chipはbinary/config画面の起動確認までとし、hardware動作を合格条件にしない

生成物は将来のx64結果と混同しないよう、次の識別子を含む場所で管理する。

```text
tests/reference/windows-arm64-vm/mucom88-win-0.70/
```

実際にtest directoryを追加するときは、入力fixtureを複製せずmanifestから参照し、出力、command、
raw log、hash、metadataを同じcase IDで関連付ける。

技術前提の確認先（VM作成時に最新版を再確認する）:

- Microsoft「How emulation works on Arm」:
  `https://learn.microsoft.com/windows/arm/apps-on-arm-x86-emulation`
- Microsoft「Add Arm support to your Windows app」:
  `https://learn.microsoft.com/windows/arm/add-arm-support`
- Microsoft「Options for using Windows 11 with Mac computers with Apple silicon」:
  `https://support.microsoft.com/windows/experience/platform-variants/options-for-using-windows-11-with-mac-computers-with-apple-m1-m2-and-m3-chips`

#### 基本比較バイナリ

| ID | 役割 | repository相対path | size (byte) | SHA-256 |
|---|---|---|---:|---|
| `win-cli` | Windows CLI基準 | `package/mucom88.exe` | 329,728 | `f46ea25e733cf176d2a2c54fbc9a2451660c2a94f862f96d2c984dbdfff9876d` |
| `win-gui` | Windows GUI基準 | `package/mucom88win.exe` | 387,483 | `e5acbd489307efa1690719bdd78f5b5981b96aa4d5cc9b816b05ca90cb1d7da6` |
| `win-bridge` | GUIとcoreのbridge | `package/hspmucom.dll` | 323,584 | `48a2c2dffe0e5fdafd09c05a089522a77a9a1678f12f990a33848edb2d0b2c58` |
| `win-fmgen` | FM音源module | `package/fmgenmodule.dll` | 202,752 | `0eff833903ba40980dde57af3999b653d631ef5bfcd7bb4ba98cf541c363eb02` |
| `win-hsp-ext` | Windows GUI runtime拡張 | `package/hspext.dll` | 367,616 | `98e1eeb7aec402a0f2ce44dc558f9d68e1aaaecf8a3c01e425d01d4e38a6c16d` |

#### Driver条件

基本比較では次の3種類を必須とする。driverを指定しないcaseと、MMLの`#driver` tagで選択する
caseに加え、CLIでは`-f`による強制指定も比較する。

| 優先度 | driver指定 | 内容 | 比較方針 |
|---|---|---|---|
| 必須・primary | `mucom88` | MUCOM88 1.7、default | 全fixtureで比較する |
| 必須 | `mucom88E` | MUCOM88 1.5、PSG hardware envelope対応 | 対応fixtureで比較する |
| 必須 | `mucom88EM` | MUCOM88 1.7、拡張memory版 | 大容量入力を含めて比較する |
| 拡張 | `mucomDotNET` | 外部.NET driver | Phase 8で実装方針確定後に比較する |

`sampl1.muc`～`sampl3.muc`の`#mucom88 1.5`はMML/compiler側の指定であり、
`#driver mucom88E`とは同一視しない。driver未指定のdefault caseと各driverを明示したcaseを
混同せず、goldenの名前には実際に選択されたdriver IDを含める。

#### Windows固有機能の拡張比較バイナリ

次のcomponentは基本的なCLI生成物のgoldenには使用せず、対応Phaseの受入試験でのみ使用する。

| ID | 役割 | repository相対path | size (byte) | SHA-256 |
|---|---|---|---:|---|
| `win-fm-editor-plugin` | FM editor plugin | `package/muplug_fmeditor.dll` | 922,112 | `0edab4ea321909e6d22fdbe91f0137b73f2d14e588413824a268bb7a4a588a04` |
| `win-fm-editor-app` | FM editor単体版 | `package/FmToneEditor.exe` | 1,136,128 | `57c8a4115a34ecc013f36a4218b13b0a75f883ec32897a1a53ef23e1e13be736` |
| `win-scci2` | SCCI2 provider | `package/scci2.dll` | 1,406,464 | `1f1a5d725fa2860bbb2bdbea4542e8ce9933a4b9e45294ce7b350b1f54c59571` |
| `win-scci2-config` | SCCI2設定 | `package/scci2config.exe` | 13,312 | `e30d1a7e128422055eb7fcafe29ff4e4ae4fca724e988d32aedeceffe3000c64` |
| `win-auto-player` | 自動player | `package/aplayer.exe` | 316,326 | `b0bb3327cb8811b41a50d02829a8c7095e3427e8443fb985c41bfcff05e4eed8` |
| `win-updater` | update確認 | `package/updcheck.exe` | 288,405 | `f6c59eaa0f2b99d06e74c0e63b4b8bc6bdf0cd284d2a7bda0158d508137a74c7` |

`package/index.html`には`Ver0.60 (MUCOM88 Ver1.7c)`という古い記載が残っているが、比較対象の
version表記には使用しない。`hspplugin/mucom88win.hsp`の`APP_VER 0.70`、
`package/history.txt`の最新履歴`ver0.70`、`package/readme.txt`の`OpenMucom88 Ver.1.7d`を
採用し、最終的なartifact同一性は上表のsizeとSHA-256で判定する。

hashの再確認command:

```sh
shasum -a 256 \
  package/mucom88.exe package/mucom88win.exe package/hspmucom.dll \
  package/fmgenmodule.dll package/hspext.dll \
  package/muplug_fmeditor.dll package/FmToneEditor.exe \
  package/scci2.dll package/scci2config.exe \
  package/aplayer.exe package/updcheck.exe
```

### Windows GUI機能一覧

分類はmacOS版の初期実装順を決めるための暫定区分である。

- **必須**: Windows GUIの主要な編集・compile・再生workflowを同等化するために必要
- **任意**: 完全同等化には有用だが、基本workflow完成後に独立して追加できる
- **廃止候補**: Windows固有、現在のmacOS UXで代替可能、または実装上有効に動作していない

廃止候補を実際に非対応と決定するには、別項目「Windows固有機能について、同等動作、macOS向け
代替、非対応のいずれかを決定する」で合意する。この一覧化完了だけでは非対応を確定しない。

#### Editor・document

| ID | 分類 | Windows版の機能 | macOSでの提供方針 | 受入確認 |
|---|---|---|---|---|
| `GUI-EDIT-01` | 必須 | 複数行MML editor | native text editor。内部UTF-8とlegacy encoding境界を明示する | 日本語を含むMUCを無損失で開き、編集、保存できる |
| `GUI-EDIT-02` | 必須 | compile結果・error message表示pane | documentごとのmessage/diagnostic pane | 成功結果とerror行・内容を表示する |
| `GUI-EDIT-03` | 必須 | cursor位置に対応するMML行番号表示 | status表示またはeditor gutter | cursor移動に追従して論理行を表示する |
| `GUI-EDIT-04` | 必須 | New、Load、Save、Save As、上書き確認 | macOS Document操作へ対応付ける | 新規・読込・保存・別名保存を往復できる |
| `GUI-EDIT-05` | 必須 | MUC、N88-BASIC source、任意textの読込 | open panelのtype filterと安全なencoding判定 | `.muc`と`.n88`を開ける。未知textは明示確認する |
| `GUI-EDIT-06` | 必須 | MMLまたは編集中voiceの変更検出 | 初期版はMML textだけをdocument dirty stateに反映。voice編集は別機能として延期する | load、新規、終了時に未保存のMML変更を破棄しない |
| `GUI-EDIT-07` | 必須 | Window titleへ編集中file名を表示 | document window titleへ反映する | file切替・保存後にtitleが更新される |
| `GUI-EDIT-08` | 必須 | F1、Ctrl+S、F5/F12、Esc、Ctrl+F1 shortcut | macOS標準shortcutを主とし、互換shortcutも可能な範囲で維持する | menuとkeyboardの両方から同じ操作ができる |
| `GUI-EDIT-09` | 必須 | 終了時の未保存確認と一時file破棄 | document close/app termination処理へ統合する | cancel、破棄、保存の各経路でdata lossや残骸がない |

根拠: `hspplugin/mucom88win.hsp`の`*main_editor`、`*menuload`、`*file_save`、
`*check_modify`、`*cmdkey`、`*byebye`。

#### Compile・再生

| ID | 分類 | Windows版の機能 | macOSでの提供方針 | 受入確認 |
|---|---|---|---|---|
| `GUI-PLAY-01` | 必須 | 編集中MMLのcompileと即時再生 | core APIのcompile/playを非同期実行する | F5相当操作で最新textが再生される |
| `GUI-PLAY-02` | 必須 | Compile errorの抽出と該当行表示 | 構造化diagnosticとしてeditorへ返す | error行へ移動でき、messageを確認できる |
| `GUI-PLAY-03` | 必須 | StopとEscによる停止・再開toggle | transportのstop/pause/resumeとして明示する | 再開位置と状態表示が一貫する |
| `GUI-PLAY-04` | 必須 | Ctrl+F1を押している間の早送り | press-and-holdまたはtoggle操作として提供する | release後に通常速度へ戻る |
| `GUI-PLAY-05` | 必須 | 早送り倍率x2、x4、x6、x8、x10 | 再生設定として提供する | 各倍率で進行量が期待値になる |
| `GUI-PLAY-06` | 廃止候補 | スロー再生倍率x2～x10の設定欄 | 要求がなければ削除。採用時は意味を再設計する | 現Windows sourceでは`slowfw`を保存するだけで再生処理から未参照 |
| `GUI-PLAY-07` | 必須 | 再生位置と最大countによるprogress bar | transport progress表示 | 再生中に単調増加し、reset/曲切替で更新される |
| `GUI-PLAY-08` | 必須 | Shift付き再生でsound monitorへ遷移 | 再生とmonitor表示を独立操作にしてよい | 再生開始後にmonitorを表示できる |
| `GUI-PLAY-09` | 必須 | `#driver`に基づくMUCOM88 1.7/1.5/EM選択 | core APIのdriver選択結果を表示する | tag/default/明示指定で期待driverが使用される |
| `GUI-PLAY-10` | 必須 | 標準voice・PCMとMML tag指定dataの読込 | document相対pathと設定defaultを使い分ける | PCM/voiceあり曲が起動directoryに依存せず鳴る |
| `GUI-PLAY-11` | 必須 | YM2608 rhythm WAVの暗黙利用 | 設定またはresource directoryを明示する | 6種のrhythm sampleを指定した曲が再生できる |

根拠: `hspplugin/mucom88win.hsp`の`*to_mmlcomp`、`*to_keyff`、`*mmlcomp`、
`*m_play`、`*m_stopesc`、`putmusicbar`。

#### Home・file browser

| ID | 分類 | Windows版の機能 | macOSでの提供方針 | 受入確認 |
|---|---|---|---|---|
| `GUI-HOME-01` | 必須 | directory移動とMUC/N88 file一覧 | sidebar、open panelまたはfolder browser | folderを移動し対象fileを列挙できる |
| `GUI-HOME-02` | 必須 | 選択fileのtitle、author、composer、date、voice、PCM、comment表示 | inspectorまたはpreviewへ表示する | 選択変更時にtag表示が更新される |
| `GUI-HOME-03` | 必須 | 選択fileをeditorで開く | document openへ統合する | 未保存確認後に対象documentが開く |
| `GUI-HOME-04` | 必須 | 選択MUCを直接compile・再生 | browserのPlay actionとして提供する | editorへ開かず再生できる |
| `GUI-HOME-05` | 必須 | 選択MUCからMUB作成 | Export MUB actionとして提供する | 入力と同名のMUBまたは指定先へ正常出力する |
| `GUI-HOME-06` | 任意 | Playerボタンから自動playerを起動 | app内playlist/playerへ統合する | main editorと競合せず連続再生できる |

根拠: `hspplugin/mucom88win.hsp`の`*menu_home`、`*build_mylist`、`*list_change`、
`*to_sellist`、`*to_selplay`、`*to_selmub`、`*to_vplayer`。

#### Sound monitor・補助player

| ID | 分類 | Windows版の機能 | macOSでの提供方針 | 受入確認 |
|---|---|---|---|---|
| `GUI-MON-01` | 必須 | A～K channelのmute flag、voice、volume、detune、address、key、LFO、reverb、pan、quantize表示 | snapshot APIを使用するmonitor view | 再生中に全11 channelが更新される |
| `GUI-MON-02` | 必須 | interrupt count/current countと最大値の表示 | transport/debug statusへ表示する | 曲進行と同期する |
| `GUI-PLAYER-01` | 任意 | folder内MUCの自動連続再生 | playlistとしてapp内へ統合する | compile失敗曲をskipし、末尾から先頭へ戻る |
| `GUI-PLAYER-02` | 任意 | title/tag、note履歴、channel詳細のplayer表示 | Now Playing画面で必要部分を再現する | 曲切替時に表示内容が更新される |
| `GUI-PLAYER-03` | 任意 | 最大演奏時間・曲長割合による自動skip | playlist optionとして提供する | 指定秒数または割合で次曲へ進む |
| `GUI-PLAYER-04` | 廃止候補 | `vplayer.hsp`の6種類の3D visual effect | 要求がある場合だけ別visualizerとして再設計する | Windows配布物に`vplayer.exe`はなくmain GUIからも直接使用されない |

根拠: `hspplugin/mucom88win.hsp`の`*to_smon`、`hspplugin/aplayer.hsp`、
`hspplugin/vplayer.hsp`。Sound monitorのmute値は表示のみで、GUIからchannel muteを操作する
機能は確認できないため、操作機能としては登録しない。

#### Tool・export

| ID | 分類 | Windows版の機能 | macOSでの提供方針 | 受入確認 |
|---|---|---|---|---|
| `GUI-TOOL-01` | 任意 | N88行番号の除去 | text transformとしてpreview付きで提供する | comment/textを壊さず対象行だけ変換する |
| `GUI-TOOL-02` | 任意 | G channelの`q`を`@`へ変換 | text transformとして提供する | G channelだけが変換される |
| `GUI-TOOL-03` | 任意 | title/composer/author/voice/PCM/date/comment tag追加 | document metadata actionとして提供する | 重複規則を決め、期待tagを追加する |
| `GUI-TOOL-04` | 任意 | 使用中FM voice定義をMML末尾へ追加 | compile結果からvoiceを抽出して追記する | 使用voiceだけが再compile可能な形式で追加される |
| `GUI-TOOL-05` | 任意 | PC-8801 `DATA`と`VOICE._n`群からPCM bank作成 | portable `pcmtool`へ集約する | Windows出力とtable/dataが一致する |
| `GUI-TOOL-06` | 任意 | listとWAV/ADPCM fileからPCM bank作成 | portable `pcmtool`とfile pickerを使用する | 最大32 entryとWAV変換の結果が一致する |
| `GUI-TOOL-07` | 任意 | MMLを指定開始行・増分のN88-BASIC sourceへ出力 | text exportとして提供する | 各行へ正しい行番号とcomment記号を付ける |
| `GUI-EXPORT-01` | 必須 | `#time`または指定秒数によるWAV出力 | coreのoffline renderをsave panelから実行する | 指定時間・formatでWindows基準と一致する |
| `GUI-EXPORT-02` | 必須 | VGM/S98出力 | core exportをsave panelから実行する | 拡張子に応じて正しい形式を生成する |

根拠: `hspplugin/mucom88win.hsp`の`*menu_tool`、`*to_toolexec`、`*to_vaddexec`、
`*to_toolexec2`、`*exec_pcmtool`、`*exec_pcmtool2`、`*to_wavout`、`*to_vgmout`。

#### Settings・外部連携・情報

| ID | 分類 | Windows版の機能 | macOSでの提供方針 | 受入確認 |
|---|---|---|---|---|
| `GUI-SET-01` | 必須 | user名、標準voice、標準PCMの設定 | app preferencesへ保存する | 再起動後も維持されtag/data選択へ反映される |
| `GUI-SET-02` | 必須 | 一定時間保存と世代backup | native autosave/recoveryを優先し、世代数の扱いを決める | 異常終了後に復旧でき、世代上限を超えない |
| `GUI-SET-03` | 必須 | 日本語・英語UI切替 | localization resourceとして実装する | 再起動後も選択言語が維持される |
| `GUI-SET-04` | 任意 | window size/position、editor font、文字色、背景色 | macOS window restorationとeditor appearanceへ対応付ける | 複数displayを含め安全な位置へ復元する |
| `GUI-SET-05` | 必須 | file directory、各option、UUID等の永続化 | `UserDefaults`等へschema/version付きで保存する | 設定破損時にdefaultへ安全に戻る |
| `GUI-FMEDIT-01` | 任意 | V.EDITとFM editor plugin連携、再生中voice更新 | 初期版は非対応。Phase 5で採用する場合もMML editorから分離した内蔵moduleまたは別appとする | 採用時は起動、編集、反映、保存、終了を安全に実行できる |
| `GUI-SCCI-01` | 任意 | SCCI2使用切替、software FM mute、設定utility起動 | Windows ARM VMでは画面・設定まで。macOS実chipはPhase 7で採否を決める | 非採用時は制約を表示。採用時だけ対応実機で再生・切断を確認する |
| `GUI-DOTNET-01` | 任意 | MucomDotNET folder指定、外部compile/player起動 | Phase 8で採否を決定する | 採用時はspaceを含むpathでもcompile・再生できる |
| `GUI-UPD-01` | 廃止候補 | 起動時に`updcheck.exe`を実行 | 署名済みappの更新機構を採用する場合だけ別方式で実装する | 任意に無効化でき、起動を妨げない |
| `GUI-WEB-01` | 任意 | MUCOM88 Wikiをbrowserで開く | Help menuの公式documentation link | default browserでHTTPS URLを開く |
| `GUI-SHARE-01` | 廃止候補 | Twitterの`#mucom88`検索を開く | 固定service依存のため原則削除。必要なら汎用共有へ置換する | 採用時も曲dataを無断送信しない |
| `GUI-ABOUT-01` | 必須 | version、著作権、credit表示 | standard About panelとlicense表示 | app/core versionと配布物のcreditが一致する |

根拠: `hspplugin/mucom88win.hsp`の`*menu_option`、`*menu_web`、`*menu_share`、
`*menu_about`と`hspplugin/mod_mucom88.as`の設定load/save処理。

#### Inventory結果

- 必須: 33項目
- 任意: 16項目
- 廃止候補: 4項目
- 合計: 53項目

一覧のsource範囲はWindows main GUI、auto player、未配布のvisual player prototypeである。
FM音色editor内部、plugin API、SCCI2実chip、MucomDotNET内部機能はそれぞれPhase 5～8で
別途一覧化・受入条件化する。

### 固定済み共通テストデータ

Windows/macOS間の基本比較では、repositoryに追跡されている `package/` 配下の次の6点を
入力fixtureとして使用する。比較時は作業directoryへcopyせず、原本を入力として使用するか、
copy後にSHA-256が一致することを確認する。sizeとhashが異なるfileは同名でも別fixtureとして扱う。

| ID | 用途 | repository相対path | size (byte) | SHA-256 |
|---|---|---|---:|---|
| `muc-sample-1` | PCM・voice使用曲 | `package/sampl1.muc` | 1,940 | `8194fd26cee9be5f60bc57b9c6c819ce89f774bee7f6640a2ba0e21b47f898e4` |
| `muc-sample-2` | voice使用曲 | `package/sampl2.muc` | 3,968 | `0e09a4a2a475408be30e65cc28c37ace52a7d591a8b7f49e00f36ba43afed409` |
| `muc-sample-3` | PSG中心・voice使用曲 | `package/sampl3.muc` | 1,283 | `ff1ae3b5e3dbe8a66a165ecd286d4f71f5c3d7e36ba6f92d2c84d61506e21617` |
| `mub-reference` | 既存MUB読込 | `package/mucom88.mub` | 1,309 | `aae62a128a1197fb66a30a3bc905b21bc177a280e4b245bc878718d0dadc0787` |
| `pcm-default` | ADPCM bank | `package/mucompcm.bin` | 63,640 | `29e3a31a38388eaa7cf93fb00af85e806f393a9ea5e26342f6996c8ab4af0609` |
| `voice-default` | FM voice bank | `package/voice.dat` | 8,192 | `5a1c7121804d3e486949357d122792cb2d9cb33d18e481ae0a1a283a367c5a0f` |

fixtureの役割は次のように固定する。

- `sampl1.muc`は同directoryの`mucompcm.bin`と`voice.dat`を使用するPCMありの標準caseとする
- `sampl2.muc`は`voice.dat`を使用し、外部PCMを要求しない標準caseとする
- `sampl3.muc`は`voice.dat`を使用するPSG中心の標準caseとする
- `package/mucom88.mub`は既存MUBの読込互換性確認にのみ使用する。root直下および`src/`の
  同名fileは内容とhashが異なるためfixtureに含めない
- 基本比較では外部ROMを使用しない。Windows版の説明書でも通常動作にPC-8801 ROMは不要と
  されており、repository内に配布可能なROM fixtureは存在しない
- 基本比較ではYM2608 rhythm WAVを使用しない。repositoryに同梱されていないため、入手元と
  再配布条件を確認できた場合に拡張fixtureとして別途固定する
- 外部ROMおよびrhythm WAVの有無を検証する試験では、基本fixtureを暗黙に変更せず、専用ID、
  出典、license、size、SHA-256をこの節へ追加する

hashの再確認command:

```sh
shasum -a 256 \
  package/sampl1.muc package/sampl2.muc package/sampl3.muc \
  package/mucom88.mub package/mucompcm.bin package/voice.dat
```

この表は入力fixtureの同一性を固定するものであり、Windows版から生成するMUB、WAV、VGM、S98の
golden値ではない。生成結果は別のPhase 0項目でWindows ARM VMと実行条件を固定して登録する。

### 完了条件

- 全機能に比較元、macOSでの提供方針、検証方法が割り当てられている
- golden生成に使用したApple Silicon host、Windows ARM VM、hypervisor、localeを第三者が再現できる
- `C: unavailable`に分類したx64固有・実chip項目が、未検証制約として受入表とrelease noteへ
  引き継がれている

## Phase 1: CLIと生成物の同等化

### 現在利用可能な機能

- [x] MUCのコンパイルとMUB読込の基本経路がmacOSで動作する
- [x] MUCOM88 1.7、1.5、EM driverを選択できる
- [x] SDL2でリアルタイム音声を出力できる
- [x] WAV、VGM、S98のoffline出力経路が存在する
- [x] PCM、voice、tag、外部ROM、rhythm dataを扱うCLI経路が存在する
- [x] Ctrl-Cによる通常終了経路が存在する
- [x] `miniplay`のMUC compileをSTEP modeで実行し、compile中に再生用とは別のaudio deviceと
  timerを起動しない

上記は機能経路の存在を示すもので、Windows版との完全一致を保証するものではない。

### 未完了作業

- [ ] 同じMUCから生成したMUBをWindows ARM VM基準とbyte単位で比較する
- [ ] WAVのsample数、format、PCM dataをWindows ARM VM基準と比較する
- [ ] VGMとS98のheader、command列、時間、dataをWindows ARM VM基準と比較する
- [ ] MUCOM88 1.7、1.5、EMの各driverで同じ試験を実行する
- [ ] PCM内蔵・外部PCM・PCMなしのMUBを比較する
- [ ] 外部ROMとrhythm WAVを使用する入力を比較する
- [ ] CP932、Shift_JIS、UTF-8、日本語pathとtagの互換性を確認する
- [ ] 正常系・異常系の終了codeとmessageをWindows ARM VM基準へ合わせる
- [ ] `pcmtool`をmacOS buildへ統合し、Windows ARM VMの変換結果と比較する
- [ ] `miniplay`を正式なCMake targetにし、入出力pathと終了条件を整理する
- [ ] CLIのdriver、PCM、voice、export optionを利用者向けに文書化する

`miniplay`のcompile/play option分離と終了時のthread停止は実装済みだが、CMake target化、
曲末尾での自動終了、実deviceでの長時間再生は未完了である。このため上記のCMake target・終了条件
項目は完了扱いにしない。

### 完了条件

- 代表fixtureについてMUBとoffline出力が合意した規則でWindows ARM VM基準と一致する
- 一致させないfieldがある場合は、差分理由と許容条件が文書化されている
- 自動試験で退行を検出できる
- x64 Windowsで未検証であることを互換性情報から確認できる

関連するCI・自動試験の詳細は `TODO.md` で管理する。

## Phase 2: コアAPIとアプリケーション境界

Windows GUIは `hspplugin/hspmucom.cpp` を経由してコアを操作する。macOS GUIから
`CMucom`、`mucomvm`や内部bufferを直接操作せずに済む、platform非依存APIを用意する。

- [-] 初期化、終了、resetのAPIを定義する
- [-] MUC compile、文字列compile、MUB読込のAPIを定義する
- [-] 再生、停止、fade、早送り、低速再生、音量のAPIを定義する
- [-] PCM、voice、tag、UUID、driver optionのAPIを定義する
- [-] compile結果、error、現在行、再生状態を取得できるようにする
- [ ] channel状態と音源状態をsnapshotとして取得できるようにする
- [-] MML text更新、保存要求、editor要求をUI非依存のeventへ整理する
- [ ] voice取得、更新、保存、dumpのAPIを定義する（初期版は読み取り専用voice参照だけを使用）
- [ ] WAV、VGM、S98出力をGUIから安全に実行できるAPIを定義する
- [-] object寿命、thread、callback、memory所有権を明文化する
- [-] C++例外や内部pointerがAPI境界を越えないようにする
- [-] API単体試験を追加する

実装済みの基礎境界:

- `MmlDocument`がMML text、保存済みsnapshot、path、dirty状態を所有し、保存時にvoice dataへ触れない
- `MucomCompileService`が所有権を持つMML snapshotを受け取り、legacy compilerへ渡す可変bufferを内部生成する
- compile結果はstatus、message、行番号付きdiagnosticとして返し、`CMucom`や内部bufferのpointerを公開しない
- `#voice`と`#pcm`の相対pathは文書のresource directoryから解決し、process-wide `chdir()`を使用しない
- editor serviceでは一時音色fileを読まず、`voice.dat`を読み取り専用として扱う
- serviceは同時呼出しを許可せず、GUI側の直列workerから使用する。非同期job管理とcancelは未実装
- `MucomModule`はVM初期化optionとcompiler optionを分離し、compile用VMをSTEP modeで動作させる
- `AudioSdl`は音声生成用threadを所有し、終了要求後にthreadをjoinしてからSDL audio deviceと
  subsystemを破棄する

上記のthread所有権は現在`miniplay`の`AudioSdl`に限定される。将来のGUI transport、
`OsDependentSdl`、非同期compile jobを含む共通のlifetime規約は引き続き未完了である。

### 完了条件

- CLIとmacOS GUIが同じコアAPIを利用する
- HSP bridgeが提供していた必須操作にmacOS側の対応APIがある
- UI threadとaudio threadの責務が明確で、終了時の競合がない

## Phase 3: macOS GUI

Windows HSP画面を直接移植せず、SwiftUI/AppKitなどmacOSで保守可能な構成として実装する。

### Documentとeditor

- [-] 新規作成、開く、保存、別名保存を実装する
- [ ] 未保存変更の確認、autosave、crash後の復旧を実装する
- [ ] MUC/MUBのFinder関連付け、drag and drop、最近使ったfileを実装する
- [ ] MML editor、行番号、検索、compile error行への移動を実装する
- [ ] 日本語入力、CP932/UTF-8変換、macOSのUnicode正規化を検証する
- [ ] sandboxを採用する場合はsecurity-scoped bookmarkで外部dataを保持する

`MmlDocument`によるbyte保持の読込、保存、別名保存、dirty判定は実装済み。現時点では
AppKit/SwiftUIのwindow、encoding自動判定、CP932への再変換、autosave、Undoとの接続が未実装である。
macOS UIは`CMucom::Editor*`およびpluginのtext更新commandを使用せず、このdocument modelから
immutableなcompile requestを作成する。

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

- [x] `miniplay`の周期的な音声生成処理を終了時にjoin可能なthreadとして所有し、解放済みobjectへの
  timer callbackを防止する
- [-] `sampl1.muc`の起動とCtrl-C停止をSDL dummy deviceで反復検証する（60回連続成功。実device、
  長時間、曲切替は未検証）
- [ ] `WaitSendingAudio()`にdrainまたはflushの明確な意味を定義して実装する
- [ ] backendのread量、write量、pool量、総sample数などを取得できるようにする
- [ ] 出力deviceを列挙・選択できるようにする
- [ ] default device変更、切断、再接続を処理する
- [ ] pause、resume、stop、再初期化を繰り返してもhangしないことを確認する
- [ ] requested/obtained audio formatが異なる場合の変換またはerror処理を実装する
- [ ] latencyとbuffer設定を公開する必要性を判断する
- [ ] 長時間再生、PCM再生、曲切替、終了時のclick noiseを試験する
- [ ] underrun、dropped sample、再充填回数を診断情報として取得できるようにする
- [ ] `AudioSdl`の10 ms生成threadについて、高負荷時のscheduler遅延、underrun、tempo、終了待ち時間を
  長時間試験する
- [ ] SDL2で要件を満たせない場合にのみCoreAudio native backendを検討する

### 完了条件

- Windows版と同じ利用場面で途切れ、click noise、末尾欠落が発生しない
- device変更や切断から安全に復旧するか、利用者へ明確なerrorを通知する
- 自動試験と実device聴感試験の両方を通過する

## Phase 5: FM音色エディタとMIDI（初期版後の任意機能）

現在のFM音色エディタはWin32 window、GDI、WinMM MIDI、Windows timerへ依存している。
初期macOS版では本Phaseを実施しなくてもLevel 2を完了できる。採用する場合もMML文書保存と
`voice.dat`保存を再結合せず、独立したmoduleまたは別appとして実装する。

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

## Phase 6: Plugin（初期版では非対応）

既存のWindows plugin ABIはDLL、`__stdcall`、Win32 handle、C++ objectの生pointerを含むため、
既存DLLをmacOSで直接利用することは対象外とする。
macOS初期版はplugin fileを探索・loadせず、plugin通知やplugin起点のMML変更も行わない。
本Phaseは初期release後に具体的なuse caseが確認できた場合だけ着手する。

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
また、Windows ARM VMはx86 user-mode binaryを実行できても、x86/x64 kernel driverを
emulateできない。Apple Silicon-only方針ではWindows SCCI2実機動作を比較基準にせず、
macOSから直接利用可能な公開protocolと検証用hardwareを確保できた場合だけ実装対象にする。

- [ ] 実chip機能を採用するか、Apple Silicon-only初期releaseでは非対応とするか決定する
- [ ] 採用する場合、対応対象とするSCCI/G.I.M.I.C等のhardwareを決定する
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

- 採用する場合、対応対象として明記したhardwareで曲を再生できる
- 採用する場合、software音源と実chip出力を安全に切り替え、途中切断を処理できる
- 非対応とする場合、Windows ARM VMでは検証不能である理由、影響、software音源による代替を
  release noteへ明記する

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

- [ ] Windows ARM VM/macOS比較matrixの全必須項目を実行する
- [ ] sample 1～3とPCM使用曲をCLI・GUIの両方で再生する
- [ ] compile、MUB、WAV、VGM、S98の結果を比較する
- [ ] 連続再生、停止、曲切替、device切替、終了を長時間試験する
- [ ] 日本語MML、tag、path、voice名を試験する
- [ ] FM editor、CoreMIDI、pluginは採用した場合のみ、実chip機能は採用した場合のみ実機試験する
- [ ] arm64とx86_64またはUniversal Binaryを試験する
- [ ] 既知の非互換、未対応機能、回避策をrelease noteへ記載する

## 優先順位

1. Phase 0～1: Windows ARM VMとの機能比較基準とCLI出力互換
2. Phase 2: GUIから利用できる安定したコアAPI
3. Phase 3～4: 通常の編集・再生を行えるmacOS app
4. Phase 3内のPCM toolとmonitor、およびPhase 9～10: 署名済み配布物と受入試験
5. Phase 5～8: FM音色editor、MIDI、plugin、実chip、MucomDotNET。初期版完成後に需要と外部仕様に応じて着手する

## 実施履歴

| 日付 | 状態 | 内容 | 検証・証跡 |
|---|---|---|---|
| 2026-09-21 | 文書作成 | Windows版との機能差分を整理し、段階別TODOと完了条件を作成 | repositoryのWindows/SDL実装、HSP GUI、plugin ABI、FM editorを比較 |
| 2026-09-21 | Phase 0一部完了 | Windows/macOS共通の入力fixture 6点をsizeとSHA-256で固定。外部ROMとrhythm WAVは基本比較で不使用と決定 | `package/sampl1.muc`～`sampl3.muc`、`package/mucom88.mub`、`package/mucompcm.bin`、`package/voice.dat`を`shasum -a 256`で確認 |
| 2026-09-21 | Phase 0一部完了 | Windows比較対象をMUCOM88 Windows 0.70 / OpenMucom88 1.7dの同梱x86 binaryへ固定。基本driver 3種と拡張componentを分離 | commit、PE architecture、size、SHA-256、source内version定義を確認 |
| 2026-09-21 | Phase 0一部完了 | Windows GUIを操作単位で53項目に分解し、必須・任意・廃止候補へ分類 | `mucom88win.hsp`、`mod_mucom88.as`、`aplayer.hsp`、`vplayer.hsp`、`package/readme.txt`を照合 |
| 2026-09-21 | 方針変更 | Windows比較をApple Silicon上のWindows 11 ARM64 VMだけで進める方式へ変更。x86 emulation結果を機能比較基準とし、x64固有性能とARM64 driverのない実chipを完了条件から除外 | Windows componentがPE32/x86であること、Windows on Armのuser-mode emulation範囲、projectのSCCI2依存を確認 |
| 2026-09-21 | Phase 2/3一部実装 | macOS初期MML editorからFM音色editor/pluginを分離。text-onlyの`MmlDocument`、snapshot compile用`MucomCompileService`、文書相対resource解決、voice read-only modeを追加 | `editor_core_test`で`sampl1.muc`の文書相対voice/PCM compile・再生開始・停止、MML保存前後の`voice.dat`不変、旧一時音色fileの無視、dirty状態、NUL拒否を確認 |
| 2026-09-23 | Phase 1/2/4一部完了 | `MucomModule`のVM optionとcompiler optionを分離し、compile時の不要なSDL audio/timer初期化を廃止。`AudioSdl`の生成処理をjoin可能なthreadへ移し、停止後にaudio deviceを破棄する順序へ変更 | SDL dummy deviceで`sampl1.muc`の起動・Ctrl-C停止を60回連続実行し、crash/hang 0件。生成MUBは65,647 byte、SHA-256 `52116e8284f0e29d0de050b984926d6ca9da60e88cb1cb1b46d4ab586f31e309`で全回一致。`make mini test all`、`dep_test`、CTest 1件が成功。実deviceは試験環境でdefault deviceを取得できず未検証 |
