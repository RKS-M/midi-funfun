# MIDI FUNFUN アーキテクチャ設計

日付: 2026-07-10
対象要件: [docs/requirements.md](../../requirements.md) v2

この設計は要件を作り直すものではなく、`docs/requirements.md` を満たすための実装アーキテクチャを確定するもの。機能要件そのものについては要件書を正とする。

## 1. 技術スタック・ビルド方針

| 項目 | 決定内容 |
|---|---|
| フレームワーク | JUCE 8.0.14(タグ固定)。CMake `FetchContent` で取得し、リポジトリには含めない |
| ビルドシステム | CMake(最小構成、Projucerは使わない) |
| C++標準 | C++20 |
| OS優先度 | Windows 11 最優先。Windows固有APIを避け、macOS移植を妨げない設計 |
| ビルド形式 | `juce_add_plugin(... FORMATS Standalone VST3 ...)` により、単一の AudioProcessor/Editor コードベースからスタンドアロンとVST3の両方を生成する |
| JUCEライセンス | GPLv3(個人利用・非商用) |
| テストフレームワーク | Catch2 v3.15.2(タグ固定)を `FetchContent` で取得 |
| CI | 対象外(ローカルビルドのみ。将来必要になれば追加検討) |

### プラグインメタ情報(仮値・変更容易)

- COMPANY_NAME: `RKSM`
- PRODUCT_NAME: `MIDI FUNFUN`
- BUNDLE_ID: `com.rksm.midifunfun`
- PLUGIN_MANUFACTURER_CODE: `Rksm`
- PLUGIN_CODE: `Mfun`

## 2. モジュール構成

```
midi_funfun/
├── CMakeLists.txt
├── .gitignore
├── docs/
├── Source/
│   ├── Plugin/     # PluginProcessor / PluginEditor(JUCE Audio/GUI層。両ラッパーの実体)
│   ├── UI/         # ToolBar, LevelMeter, PianoRollComponent, LookAndFeel(青空配色)
│   └── Core/       # midi_funfun_core ライブラリの実体(GUI非依存の純粋ロジック)
│       ├── Audio/    # Recorder, TakeManager, Metronome
│       ├── Pitch/    # YinPitchDetector, NoteSegmenter, OctaveErrorCorrector
│       ├── Model/    # Note, NoteSequence, EditCommands(UndoableAction群)
│       ├── Quantize/ # Quantizer
│       ├── Scale/    # ScaleDefinitions, ScaleCorrector
│       └── Export/   # MidiFileWriter, TempFileManager
└── Tests/          # Catch2実行ファイル。midi_funfun_core のみをリンク
```

依存の向きは `Plugin/UI → Core` の一方向のみ。Core は上位層を一切知らない。

### レイヤーの責務分担

- **Core**: `juce_core` / `juce_audio_basics` / `juce_dsp` にのみ依存する、GUIやリアルタイムデバイスI/Oを持たない純粋ロジック層。合成音声バッファなどをテストから直接注入できる。
- **Plugin**: `AudioProcessor` / `AudioProcessorEditor` の実装。`AudioDeviceManager` を介した実デバイスI/O(スタンドアロン時はJUCE標準の "Audio/MIDI Settings" ダイアログを利用)、ドラッグ&ドロップ、状態保存(空実装)など、JUCEのホスト/デバイス統合に関わる部分。
- **UI**: ピアノロールや各種コントロールなど、見た目・操作に関わる `Component` 群。Core が生成したデータ(ノート列・ピッチカーブ)を描画・編集するが、ロジック自体はCoreの関数/クラスを呼び出す。

## 3. データモデルとUndo/Redo

- ノートは `struct Note { int pitch; int64 startTick; int64 lengthTicks; int velocity; }` のようなプレーンな構造体とし、`NoteSequence`(実体は `std::vector<Note>` ラッパー)で保持する。
- 編集操作(移動・リサイズ・削除・追加・クオンタイズ適用・スケール補正適用)は、それぞれ `juce::UndoableAction` のサブクラスとして実装し、`juce::UndoManager` に積む。`Ctrl+Z` / `Ctrl+Y` はJUCE標準のUndoManager機構にそのままマッピングする。
- `ValueTree` は採用しない。永続化はMVP対象外(要件7)であり、プレーンな構造体の方がユニットテストしやすく、C++/JUCE経験が浅い開発者にも理解しやすいため。

## 4. テスト戦略

- `Tests/` ターゲットは `midi_funfun_core` + Catch2 のみをリンクするコンソール実行ファイル。GUIモジュール・オーディオデバイスモジュールには一切依存せず、ヘッドレスで高速に実行できる。
- テスト対象の例:
  - YINピッチ検出: 既知周波数の合成正弦波に対する検出精度(セント単位の許容誤差)
  - オクターブエラー補正: 孤立したオクターブジャンプが補正されること
  - ノイズゲート/無音検出: 閾値境界での挙動
  - 最小ノート長フィルタ
  - クオンタイズ: 強度0/50/100%での位置計算
  - スケール補正: 各スケール種類での最近接音判定
  - MIDI書き出し: SMFヘッダ・PPQ(480)・チャンネル固定(1)・テンポメタイベントのバイト列検証
- GUI(ピアノロールの描画・マウス操作等)は自動テスト対象外とし、実アプリでの手動確認とする。

## 5. 機能単位の実装順序とコミット粒度

各項目はそれぞれ1コミットとする(詳細タスクへの分解は別途 `writing-plans` で作成する実装計画による)。

0. **雛形**: `.gitignore` + CMake雛形(空のProcessor/Editor、Standalone+VST3ビルド、Testsターゲットの雛形1本)。要件アプリ全体の前提。
1. **録音**: マイク入力キャプチャ、レベルメーター(ピークホールド)、モニタリング、BPM入力+メトロノーム/カウントイン、複数テイク管理、5分自動停止、512MBメモリ上限。
2. **ピッチ検出(YIN)**: 「解析」ボタンによるオフラインバッチ処理、ノイズゲート、最小ノート長フィルタ、オクターブエラー補正、感度スライダー。ベロシティのデフォルト値設定もここで生成ノートに適用する。
3. **ピアノロール**: ノート表示・編集(移動/リサイズ/削除/追加)、選択操作、キーボードショートカット、Gridスナップ切替、Undo/Redo、波形オーバーレイ、連続ピッチカーブ表示、縦方向自動フィット・ズーム、再生ヘッド、ノートクリック単音プレビュー、内蔵ピアノ音でのプレビュー再生。
4. **クオンタイズ**: 強度指定(0〜100%)、デフォルト(開始位置のみ)、右クリックメニュー(開始+長さ)。
5. **スケール補正**: ルート/スケール種類選択、選択ノートのみ適用、最近接音への補正(固定100%)。
6. **MIDI書き出し**: SMF書き出し(PPQ480/チャンネル1固定/BPMテンポ埋め込み)、ドラッグ&ドロップ(スタンドアロン)、名前を付けて保存、一時ファイルのクリーンアップ。
7. **VST3最低要件・UI仕上げ**: `getStateInformation`/`setStateInformation` の安全な空実装、青空配色のUI最終調整、ツールバー・画面構成の統合。

## 6. スコープ外の再確認

要件書 7章のスコープ外事項(永続化、ポリフォニック対応、スケール補正強度調整、信頼度可視化、ベロシティ連動、キー/スケール自動推定)は、本設計でも対象としない。
