# MIDI FUNFUN Milestone 1: 録音機能 設計

日付: 2026-07-10
対象要件: [docs/requirements.md](../../requirements.md) v2 §4.1
前提: [アーキテクチャ設計](2026-07-10-midi-funfun-architecture-design.md)、Milestone 0(雛形)完了・コミット済み(`3663d59`)

この設計は要件4.1(録音機能)を満たすための実装アーキテクチャを確定するもの。ベロシティのデフォルト値設定(4.7)とプレビュー再生(4.6)は別マイルストーン(2, 3)で扱うため本設計のスコープ外。

## 1. スコープ

- マイク入力キャプチャ(モノラル前提)
- 入力レベルメーター(ピークホールド表示)
- 録音中のモニタリング(入力を出力へパススルー)
- BPM入力に基づくメトロノーム/カウントイン(カウントイン拍数はUIで設定可能、0で無効)
- 複数テイクの録音・比較・選択・削除
- 1テイク最大5分での自動停止
- 全テイク合算512MBのメモリ上限
- 入力デバイス・サンプルレート・バッファサイズの設定画面(自前の「設定」ボタン)

## 2. Coreレイヤー(`Source/Core/Audio/`、GUI非依存)

### `PeakLevelTracker`
オーディオスレッドから`std::atomic<float>`で現在ピークとピークホールド値を更新するロックフリークラス。GUIスレッドは`Timer`でポーリングして読む。
- `void pushBlock(const float* samples, int numSamples)` — オーディオスレッドから呼ぶ
- `float getCurrentLevel() const` / `float getPeakHoldLevel() const` — GUIスレッドから呼ぶ
- ピークホールドは一定時間(既定1.5秒相当のブロック数)減衰しない仕様とし、以降は緩やかに減衰する

### `Take` / `TakeManager`
`Take`はモノラル`juce::AudioBuffer<float>`とサンプルレートを保持する値オブジェクト。`TakeManager`が`Take`の集合・選択中インデックス・メモリ予算を管理する。
- 新規テイク開始時、既存テイク合計 + 見込み最大サイズ(5分ぶん)が512MBを超える場合は開始を拒否する(要件4.1の「警告を表示して録音を開始しない」に対応するブール戻り値を返す)
- 1テイクの録音中、サンプル数が5分相当(`sampleRate * 60 * 5`)に達したら自動停止を要求するフラグを返す
- テイクの追加・削除・選択・一覧取得のAPIを持つ

### `Metronome`
BPM・サンプルレート・現在の再生サンプル位置から、メトロノームクリックとカウントインのタイミングを計算する純粋ロジック。
- `void start(double bpm, double sampleRate, int countInBeats)` — カウントイン拍数0なら即座に「本番開始」
- `Result processBlock(int numSamples)` — このブロック内で鳴らすべきクリック位置(オフセットのリスト)と、カウントインが完了して本番録音に移行すべきかどうかを返す
- 拍子は4/4固定(要件に拍子指定なし)とする

### `RecordingTransport`
`Metronome`と`TakeManager`を用いて Idle → カウントイン中 → 録音中 → (自動停止 or 手動停止) → Idle の状態遷移を管理する。
- `bool startRecording(bool metronomeEnabled, int countInBeats, double bpm, double sampleRate)` — `TakeManager`のメモリ予算チェックに失敗したら`false`
- `Advance advance(int numSamples)` — 毎ブロック呼ばれ、「このブロックをテイクに追記すべきか」「クリックをどこに鳴らすか」「自動停止すべきか」を返す構造体を返す
- `void stopRecording()` — 手動停止(録音ボタン再クリック)

## 3. Pluginレイヤー(`Source/Plugin/`)

- `PluginProcessor`が`PeakLevelTracker` / `TakeManager` / `Metronome` / `RecordingTransport`のインスタンスを所有する。
- `processBlock`: 入力バッファ(先頭チャンネル、モノラル前提)を`PeakLevelTracker`に渡す → `RecordingTransport::advance()`を呼ぶ → 結果に応じて現在テイクへサンプルを追記し、クリック音を出力にミックスし、入力を出力へパススルーする(モニタリング)。
- Editor(UI)にツールバー要素を追加する:
  - BPM入力(数値editable)
  - メトロノームON/OFFトグル
  - カウントイン拍数(0〜8の数値入力、既定4)
  - 録音ボタン(Idle/カウントイン中/録音中で見た目が変わる)
  - レベルメーター(`PeakLevelTracker`を`Timer`でポーリングして描画、ピークホールド表示)
  - テイク一覧(`juce::ListBox` + `ListBoxModel`、選択・削除操作)
  - 「設定」ボタン: `processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone` のときのみ表示。押下時、`juce::StandalonePluginHolder::getInstance()->deviceManager` から取得した`AudioDeviceManager`を使って`juce::AudioDeviceSelectorComponent`をダイアログ表示する(VST3では録音非保証のため非表示)。

## 4. テスト戦略

Catch2でCore層を検証する(GUI/オーディオデバイス非依存、ヘッドレス):
- `PeakLevelTracker`: 既知振幅ブロックでの現在値更新、ピークホールドの保持と減衰
- `TakeManager`: 512MB上限到達時の新規テイク拒否、5分相当での自動停止フラグ、テイク削除による予算解放
- `Metronome`: BPM/サンプルレートに対するクリック位置の正しさ、カウントイン拍数消化後の「本番開始」報告、カウントイン0拍時の即時開始
- `RecordingTransport`: Idle→カウントイン→録音→自動停止の状態遷移、手動停止

GUI・実デバイスI/O・実際のマイク入力・テイク一覧操作・設定ダイアログは自動テスト対象外とし、Standaloneアプリを実際にビルド・起動して手動確認する。

## 5. タスク分割(全体で1コミット、Milestone 0と同じ方針)

1. `PeakLevelTracker`(Core、TDD)
2. `Take` / `TakeManager`(Core、TDD)
3. `Metronome`(Core、TDD)
4. `RecordingTransport`(Core、TDD)
5. `PluginProcessor::processBlock`への組み込み(録音・クリックミックス・モニタリングパススルー)+ 実機ビルドで動作確認
6. ツールバーUI(BPM・メトロノームトグル・カウントイン拍数・録音ボタン・レベルメーター)+ 動作確認
7. テイク一覧UI(選択・削除)+ 設定ボタン(AudioDeviceSelectorComponent)+ 動作確認
8. 最終確認・単一コミット・push

## 6. スコープ外の再確認

- ベロシティのデフォルト値設定(要件4.7)→ Milestone 2(ピッチ検出時にノートへ適用)
- 内蔵ピアノ音でのプレビュー再生(要件4.6)→ Milestone 3(ピアノロール)
- アプリ再起動をまたいだ録音データの永続化 → 要件7でMVPスコープ外と明記済み、本設計でも対象外
