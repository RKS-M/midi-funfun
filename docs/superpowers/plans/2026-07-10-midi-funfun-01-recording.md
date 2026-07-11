# MIDI FUNFUN Milestone 1: 録音機能 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development (recommended) or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 設計スペック([2026-07-10-midi-funfun-milestone1-recording-design.md](../specs/2026-07-10-midi-funfun-milestone1-recording-design.md))記載のCoreクラス4つ(`PeakLevelTracker`/`Take`+`TakeManager`/`Metronome`/`RecordingTransport`)をTDDで実装し、`PluginProcessor`/Editorに配線して、スタンドアロンで実際にマイク録音・メトロノーム・複数テイク管理・レベルメーター・設定ダイアログが動作する状態にする。

**Architecture:** 設計スペック通り。Core層は`Source/Core/Audio/`配下にGUI非依存で実装し、`midi_funfun_core`に追加する(既存の`juce_core`/`juce_audio_basics`/`juce_dsp`リンクのみで足り、新規JUCEモジュール追加は不要)。Plugin層は`PluginProcessor`が4クラスのインスタンスを所有し`processBlock`で結線、`PluginEditor`にツールバー・レベルメーター・テイク一覧・設定ボタンを追加する。

## API確定(設計スペックの行間を実装レベルで確定する)

- `RecordingTransport::advance(int numSamples)` は設計スペック通りオーディオを触らず、`{ State state; bool shouldAppendToTake; std::vector<int> clickSampleOffsets; }` を返す。実際のテイクへのサンプル追記(`TakeManager::appendToCurrentTake`)と、その戻り値(5分到達フラグ)を受けての `RecordingTransport::stopRecording()` 呼び出しは `PluginProcessor::processBlock` 側が行う(スペック§3の記述通り「結果に応じて現在テイクへサンプルを追記し」はPluginProcessorの責務)。
- `PeakLevelTracker` はサンプルレートを知らないと1.5秒のホールド時間をブロック数から逆算できないため、`prepare(double sampleRate)` を追加する(スペックに明記はないが§2の「1.5秒相当のブロック数」を実現するのに必要な最小限の拡張)。
- `Metronome`/`RecordingTransport`のカウントイン→本番録音の切り替えはブロック粒度で行う(サンプル単位の途中切り替えはしない)。数百サンプル程度のレイテンシはホビー用途で許容する、という判断。

## Global Constraints

- コミットはこのマイルストーン全体で1回のみ(Task 8の最後)。
- Core層の新規クラスは全てTDD(先に失敗するテストを書く)。
- 新規JUCEモジュールの追加は不要(既存リンクで完結する見込み。実装中に必要になった場合のみ追加し、その旨を最終報告に明記する)。

---

### Task 1: `PeakLevelTracker`

**Files:** Create `Source/Core/Audio/PeakLevelTracker.h`/`.cpp`, `Tests/PeakLevelTrackerTests.cpp`. Modify `Source/Core/CMakeLists.txt`, `Tests/CMakeLists.txt`。

- [ ] ロックフリー(`std::atomic<float>`)で `pushBlock(const float*, int)` / `getCurrentLevel()` / `getPeakHoldLevel()` / `prepare(double sampleRate)` を実装。
- [ ] テスト: 既知振幅ブロックでcurrentLevel更新、ピークホールドが1.5秒相当は保持されること、その後緩やかに減衰すること、新しいより大きいピークが来たら即座に更新されること。

### Task 2: `Take` / `TakeManager`

**Files:** Create `Source/Core/Audio/Take.h`, `TakeManager.h`/`.cpp`, `Tests/TakeManagerTests.cpp`.

- [ ] `Take`: `juce::AudioBuffer<float>` + `double sampleRate` の値オブジェクト。
- [ ] `TakeManager`: `canStartNewTake(sampleRate)`, `startNewTake(sampleRate)`, `appendToCurrentTake(const float*, int) -> bool(5分到達)`, `deleteTake(index)`, `selectTake(index)`, `getSelectedTakeIndex()`, `getNumTakes()`, `getTake(index)`, `getTotalBytesUsed()`.
- [ ] テスト: 512MB上限到達時の新規テイク拒否、5分相当到達でtrue、テイク削除でバイト予算が解放されること。

### Task 3: `Metronome`

**Files:** Create `Source/Core/Audio/Metronome.h`/`.cpp`, `Tests/MetronomeTests.cpp`.

- [ ] `start(bpm, sampleRate, countInBeats)`, `processBlock(numSamples) -> { clickSampleOffsets, bool countInJustCompleted }`。カウントイン0拍なら`start()`直後から本番扱い。
- [ ] テスト: 既知BPM/サンプルレートでのクリック位置、カウントイン消化後の`countInJustCompleted`が一度だけtrue、カウントイン0拍での即時開始。

### Task 4: `RecordingTransport`

**Files:** Create `Source/Core/Audio/RecordingTransport.h`/`.cpp`, `Tests/RecordingTransportTests.cpp`.

- [ ] Idle→CountIn→Recording→Idle の状態機械。`startRecording(metronomeEnabled, countInBeats, bpm, sampleRate) -> bool`(`TakeManager`予算チェック失敗でfalse)、`advance(numSamples) -> Advance`、`stopRecording()`。
- [ ] テスト: メモリ予算超過時の`startRecording`失敗、カウントイン→録音への遷移、手動停止、メトロノーム無効時はカウントインをスキップして即録音。

### Task 5: `PluginProcessor::processBlock`への組み込み + 実機ビルド確認

**Files:** Modify `Source/Plugin/PluginProcessor.h`/`.cpp`。

- [ ] 4つのCoreクラスをメンバーとして所有。`prepareToPlay`で`PeakLevelTracker::prepare`呼び出し。`processBlock`: 入力(先頭チャンネル)を`PeakLevelTracker`へ→`RecordingTransport::advance()`→`shouldAppendToTake`なら`TakeManager::appendToCurrentTake`(戻り値trueなら`RecordingTransport::stopRecording()`)→クリック音を出力にミックス→入力を出力へパススルー。
- [ ] `cmake --build build --config Debug` でビルド成功を確認(GUIはまだ空のまま、Task 6以降で追加)。

### Task 6: ツールバーUI

**Files:** Modify `Source/Plugin/PluginEditor.h`/`.cpp`。

- [ ] BPM数値入力、メトロノームON/OFFトグル、カウントイン拍数(0〜8、既定4)、録音ボタン(Idle/カウントイン中/録音中で表示変化)、レベルメーター(`Timer`ポーリング、ピークホールド表示)。
- [ ] Standaloneを実際に起動し、録音ボタン押下でカウントイン→録音→レベルメーター反応を目視確認。

### Task 7: テイク一覧UI + 設定ボタン

**Files:** Modify `Source/Plugin/PluginEditor.h`/`.cpp`。

- [ ] `juce::ListBox`+`ListBoxModel`でテイク一覧(選択・削除)。「設定」ボタンは`processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone`の時のみ表示し、`juce::StandalonePluginHolder::getInstance()->deviceManager`を使った`AudioDeviceSelectorComponent`をダイアログ表示。
- [ ] Standaloneで複数回録音→一覧に複数テイクが並ぶこと、削除、設定ダイアログでのデバイス切替を目視確認。

### Task 8: 最終確認・単一コミット・push

- [ ] `ctest --test-dir build -C Debug --output-on-failure` で全テストパス。
- [ ] `cmake --build build --config Debug` でStandalone/VST3双方ビルド成功。
- [ ] `git add`(Milestone 1で変更・新規作成した全ファイル)→ `git commit`(Milestone 1の内容を要約)→ `git push origin main`。
