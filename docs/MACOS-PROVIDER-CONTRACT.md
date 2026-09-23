# macOS外部provider契約

## 目的

標準版のsoftware YM2608再生から、`GUI-SCCI-01`（実chip）と`GUI-DOTNET-01`（外部driver）を
分離する境界を定義する。Windows DLL ABI、HSP内部API、raw pointerは公開しない。providerがなくても
標準版のcompile、software再生、MUB／WAV／VGM／S98出力を利用可能にする。

初版は`provider_api_version = 1`とする。実装時に型名やtransportを変更しても、ここで定める状態、能力、
失敗時の挙動、UI表示を維持する。

## 共通契約

### 所有権とthread

- `CMucom`、`mucomvm`、AppKit object等の内部pointerをproviderへ渡さない
- commandとeventは値型またはimmutable snapshotとする
- provider呼出しは専用serial queueで順序付け、audio callback threadをblockしない
- providerはAppKitを呼ばず、状態変更をeventとしてapplicationへ返す
- `connect`、`stop`、`reset`、`disconnect`は冪等にする
- 終了時はrequest受付を止め、処理をcancelし、`stop`、`reset`、`disconnect`の順に有限時間で終了する

### 状態

| 状態 | 意味 | UIの扱い |
|---|---|---|
| `unavailable` | provider未導入、または対象なし | 理由とsoftware代替を表示 |
| `ready` | provider認識済み、未接続 | 接続操作を許可 |
| `connected` | request受付可能 | provider名とdeviceを表示 |
| `disconnected` | 接続後に対象を喪失 | 再生停止後、再接続またはsoftware再生を選択 |
| `incompatible` | API versionまたは必須能力が不一致 | 必要versionと検出versionを表示 |
| `error` | 回復不能な初期化／通信／実行失敗 | 操作、理由、診断情報の場所を表示 |

状態eventは`state`、`provider_id`、`provider_version`、`reason_code`、localize可能な
`message_key`を持つ。表示文をprovider側で任意に組み立てない。

### versionと能力

初期化時に次を交換する。

```text
provider_api_version
provider_id
provider_version
display_name
capabilities[]
devices[]
```

未知のoptional能力は無視してよい。API version不一致または必須能力不足なら`incompatible`とし、処理を
開始しない。

### 配布と信頼境界

- 任意pathのunsigned dylibを標準版processへloadしない
- process内providerはappと同一teamで署名し、bundle内の固定位置からだけloadする
- 第三者driverはhelper processまたはXPC serviceに分離し、明示許可した実行fileだけを使う
- helperへ必要最小限のfile、device、network権限だけを与える
- crash、timeout、protocol違反をapplication processのcrashへ波及させない

## RealChipProvider

必須操作は次のとおり。

```text
enumerateDevices() -> DeviceDescriptor[]
connect(device_id) -> Result
reset() -> Result
writeRegister(chip, port, address, value, timestamp) -> Result
stop() -> Result
disconnect() -> Result
```

ADPCM転送能力を表明する場合は、`beginAdpcmTransfer`、`writeAdpcmChunk`、
`commitAdpcmTransfer`、`cancelAdpcmTransfer`も提供する。能力はchip種別、register write、
timestamp／wait、ADPCM転送を個別に表明し、applicationは未表明の能力を推測しない。

再生開始前に利用不能ならprovider再生を開始せずsoftware YM2608を選択可能にする。再生中の切断では送信を
停止し、可能ならresetしてsessionを終了する。無断で途中からsoftware音源へ切り替えず、利用者が選択した
場合だけ曲頭から再開する。再接続時も自動再生しない。

## ExternalDriverProvider

外部driverは別processで起動し、UTF-8 JSON Linesのversion付きcontrol protocolを用いる。最初に`hello`で
protocol versionと能力を確認し、基本commandを`compile`、`play`、`pause`、`resume`、`stop`、
`status`、`shutdown`とする。各requestに一意の`request_id`を付ける。

大きなsource／artifactはJSONへ埋め込まず、applicationが作成したprivate作業directoryのfileとして渡す。
pathはそのdirectory内へ正規化し、symlinkによる範囲外参照を拒否する。

- operationごとに有限timeoutを設ける
- cancel後も終了しないprocessは診断情報を保存して終了できる設計にする
- exit status、signal、最後に成功した`request_id`を記録する
- malformed JSON、重複response、未知の必須fieldはprotocol errorとしてsessionを終了する
- 標準エラーにはsize上限を設けて診断logへ保存する

## 非対応時の表示

設定画面と実行時の双方で、機能、理由、代替を同時に表示する。単にcontrolをdisableしない。

```text
実チップ出力: 利用不可
理由: 対応providerがインストールされていません
代替: Software YM2608
```

外部driverも同じ形式にする。未導入、version不一致、権限不足、device未接続、process起動失敗を区別する。

## 受入試験とrelease判定

fake providerで、全状態、合法／不正な遷移、version一致／不一致、初期化失敗、再生中切断、timeout、
cancel、異常終了、冪等cleanupを自動試験する。外部driverではhandshake、request ID、malformed response、
作業directory外path拒否も試験する。

provider境界と非対応表示は標準版へ実装する。provider本体と実hardware対応は拡張profileであり、標準版
releaseを妨げない。「対応済み」と表明する場合だけ、fake provider試験に加え、対象hardwareまたはdriverで
`tests/manual/macos-gui-acceptance.md`の該当項目を`PASS`にする。
