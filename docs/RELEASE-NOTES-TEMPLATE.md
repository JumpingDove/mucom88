# MUCOM88 for macOS Release Notes Template

releaseごとにcopyし、`<...>`を実値へ置換する。該当しない項目も削除せず`該当なし`と記載する。

## Release情報

- Version: `<version>`
- Release date: `<YYYY-MM-DD>`
- Git commit: `<commit>`
- App SHA-256: `<sha256>`
- CLI SHA-256: `<sha256>`

## 対応環境

- macOS: `26.0以降`
- Architecture: `arm64`
- 検証機種／SoC: `<machine / SoC>`
- SDL version: `<version>`

Intel Mac、Rosetta、Universal Binaryは対応対象ではない。

## 利用できる機能

標準版49機能のうち受入済みの機能IDと、利用者が達成できる結果を記載する。

- `<GUI-...>`: `<結果>`
- 受入記録: `<tests/manual/macos-gui-acceptance.mdのcopyまたはartifact>`

## file formatと互換性

macOS版によるMML compile、MUBの生成・再読込・再生、WAV／VGM／S98の構造的妥当性を検証する。
次は保証しない。

- macOS版が生成したMUB、VGM、S98等をWindows版が読み込めること
- Windows版とmacOS版の生成file、PCM sample、終了code、messageがbyte単位で一致すること
- Windows DLL/plugin ABIまたはHSP内部APIとの互換性

Windows上で確認した場合も、任意の参考結果として環境と結果を記載し、保証範囲を推測で拡大しない。

## Windows版との意図的な相違

- DirectSound／WinMMの代わりにSDL2／CoreAudio／CoreMIDIを使う
- 画面配置、menu、shortcut表記は複製せずmacOS native操作を採用する
- 未使用のslow再生設定を対象外とする（`GUI-PLAY-06`）
- 未配布3D visualizerを対象外とする（`GUI-PLAYER-04`）
- FM音色editorをnative model/APIで接続し、Windows plugin ABIを使わない
- このrelease固有の相違: `<該当なし、または理由と代替>`

## 拡張profile

### 実chip provider (`GUI-SCCI-01`)

- 状態: `<未提供 / experimental / supported>`
- Provider／version: `<値>`
- 検証device／firmware: `<値>`
- 代替: `Software YM2608`
- 制限: `<値>`

### 外部driver provider (`GUI-DOTNET-01`)

- 状態: `<未提供 / experimental / supported>`
- Provider／version: `<値>`
- 検証環境: `<値>`
- 標準版での代替: `<値>`
- 制限: `<値>`

`supported`はprovider契約の自動試験と実環境の手動受入を満たした場合だけ使用する。

## 既知の制限

- Text encoding: `<値>`
- Audio device／buffer: `<値>`
- PCM／voice resource: `<値>`
- YM2608 rhythm WAV: `<値>`
- File access／sandbox: `<値>`
- その他: `<値または該当なし>`

## 検証結果

### 自動試験

- Build configuration: `<command / preset>`
- CTest: `<passed>/<total>、artifact>`
- Sanitizer: `<結果または未実施理由>`
- MUB round trip: `<結果>`
- WAV／VGM／S98 parser: `<結果>`

### 手動受入

- 実施日／環境: `<date / macOS build / machine / audio device>`
- Standard: `<PASS>/49、FAIL <n>、BLOCKED <n>`
- Excluded: `2 N/A`（理由は意図的な相違に記載）
- Extension: `<結果。標準版releaseを妨げない>`
- 証跡: `<path / URL>`

## 配布とsecurity

- Code signing: `<Developer ID / ad-hoc / unsigned>`
- Hardened Runtime: `<enabled / disabled>`
- Notarization／stapling: `<結果>`
- Update署名検証: `<結果または更新機構なし>`
- Network送信: `<用途または該当なし>`

## Licenseとcredit

- MUCOM88／OpenMucom88: `<license / credit>`
- SDL2: `<version / license>`
- その他の同梱library／data: `<名称、version、license、入手元>`
- 再配布sample／rhythm／voice data: `<権利と配布条件>`

## Release前確認

- [ ] `<...>`をすべて実値または`該当なし`へ置換した
- [ ] 標準版49項目の受入結果を添付した
- [ ] 除外2項目と理由を記載した
- [ ] 拡張profileを未検証のまま`supported`と表記していない
- [ ] Windowsで未検証のfile互換性を保証していない
- [ ] app／CLIのversion、署名、hashを配布物から取得した
- [ ] third-party licenseと再配布条件を確認した
