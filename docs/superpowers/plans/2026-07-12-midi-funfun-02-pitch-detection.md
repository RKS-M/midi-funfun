# MIDI FUNFUN Milestone 2: ピッチ検出(YIN)・ノート化 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development (recommended) or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 設計スペック([2026-07-12-midi-funfun-milestone2-pitch-detection-design.md](../specs/2026-07-12-midi-funfun-milestone2-pitch-detection-design.md))記載のCoreクラス群(`Note`/`NoteSequence`、`YinPitchDetector`、`NoteSegmenter`、`OctaveErrorCorrector`、`PitchAnalyzer`)をTDDで実装し、`PluginProcessor`/`PluginEditor`に配線して、スタンドアロンで実際に「選択中テイクを解析→検出ノートが一覧パネルに表示される」まで動作する状態にする。

**Architecture:** 設計スペック通り。`Source/Core/Model/`(新規、GUI非依存)に`Note`/`NoteSequence`、`Source/Core/Pitch/`(新規、GUI非依存)に`YinPitchDetector`/`NoteSegmenter`/`OctaveErrorCorrector`/`PitchAnalyzer`を実装し`midi_funfun_core`に追加する。Plugin層は`PluginProcessor`が解析エントリポイント(`analyzeSelectedTake`)と3新規パラメータを持ち、`PluginEditor`にツールバー(解析ボタン・ノイズゲート感度スライダー・最小ノート長スライダー・デフォルトベロシティ入力)と最小限のノート一覧パネル(2つ目の`juce::ListBox`+専用`ListBoxModel`)を追加する。

## API確定(設計スペックの行間を実装レベルで確定する)

### YINアルゴリズムの具体的な数式(スペック§2.2は手順名のみなので、ここでtau探索範囲・CMNDF・補間の式を確定する)

- 積分窓長 `W = windowSize / 2`(既定1024サンプル)。差分関数は `d(tau) = Σ_{j=0}^{W-1} (x[frameStart+j] - x[frameStart+j+tau])^2`、`tau = 0..W`。これにより1フレームの解析に必要なサンプル数は `frameStart + windowSize <= numSamples`(= `W + W`)で足りる。`analyze()`のフレームループはこの条件を満たす間だけ回す(満たさなくなったら打ち切り、末尾の中途半端なフレームは生成しない)。
- CMNDF: `dPrime[0] = 1`。`runningSum`を0から開始し、`tau = 1..W`について `runningSum += d[tau]`、`dPrime[tau] = d[tau] * tau / runningSum`(`runningSum`が0に近い=無音に近い場合はゼロ除算を避けるため`dPrime[tau] = 1.0`とする)。
- 探索するtau範囲: `tauMinSamples = max(1, (int)(sampleRate / maxFrequencyHz))`、`tauMaxSamples = min(W, (int)(sampleRate / minFrequencyHz))`。`tauMinSamples > tauMaxSamples`になったら(極端なサンプルレートで理論上あり得るが通常のオーディオSRでは起きない)そのフレームは`voiced=false`。
- ダブ検出: `[tauMinSamples, tauMaxSamples]`の範囲で`dPrime`の最小値`globalMinVal`とその位置`globalMinTau`を求める。`globalMinVal < absoluteThreshold`なら、`tauMinSamples`から昇順に走査し、最初に`dPrime[tau] < absoluteThreshold`となった`tau`から、`dPrime[tau+1] < dPrime[tau]`である限り`tau`を進めて谷底(局所最小)を探し、それを`tauEstimate`とする(`tau+1`が範囲外になったら打ち切り)。`voiced = true`。`globalMinVal >= absoluteThreshold`なら`voiced = false`、`frequencyHz = 0.0`、`confidence = 1.0 - globalMinVal`(0未満にならないよう`std::max(0.0, ...)`でクランプ)。
- 放物線補間(`voiced == true`の場合のみ): `tauEstimate`が範囲の両端(`tauMinSamples`または`tauMaxSamples`)でなければ、`y0=dPrime[tauEstimate-1]`, `y1=dPrime[tauEstimate]`, `y2=dPrime[tauEstimate+1]`として `denom = y0 - 2*y1 + y2`。`std::abs(denom) > 1e-12`なら `offset = 0.5 * (y0 - y2) / denom` を`tauEstimate`に加算(`offset`は通常`[-0.5, 0.5]`程度に収まる)。両端の場合は`offset=0`。`tauRefined = tauEstimate + offset`。`frequencyHz = sampleRate / tauRefined`。`confidence = std::max(0.0, 1.0 - dPrime[tauEstimate])`(補間前の値を使う。実装を単純に保つための割り切り)。
- `rmsLevel`: 積分窓と同じ`[frameStart, frameStart+W)`のRMS(`sqrt(mean(x[j]^2))`)。

### `NoteSegmenter`のピッチ→MIDIノート番号変換

- `int quantizedPitch(double frequencyHz) { return (int) std::round(69.0 + 12.0 * std::log2(frequencyHz / 440.0)); }`。`frequencyHz <= 0`のフレームは無音/非ノート扱いなのでこの関数は呼ばない。

### `PitchAnalyzer`のtick変換

- サンプル位置→秒: `seconds = samplePos / sampleRate`。秒→tick: `tick = (juce::int64) std::round(seconds * (bpm / 60.0) * ticksPerQuarterNote)`。`samplePos`は`RawNoteSegment::startFrame * hopSize`(開始)、`lengthTicks`は`(startFrame+lengthFrames)*hopSize`から求めた終了tickとの差分(丸め誤差の蓄積を避けるため、開始・終了それぞれを独立にtick化してから引き算する)。

## Global Constraints

- コミットはこのマイルストーン全体で1回のみ(Task 10)。
- Core層の新規クラスは全てTDD(先に失敗するテストを書く)。
- 新規JUCEモジュールの追加は不要(`juce_core`/`juce_audio_basics`/`juce_dsp`の既存リンクで完結する見込み)。
- YINの許容誤差テストは±10セント、対象周波数はE2〜E5(約82Hz〜660Hz)をカバーする代表点(例: E2=82.41Hz, A3=220.00Hz, A4=440.00Hz, E5=659.25Hz)。

---

### Task 1: `Note` / `NoteSequence`

**Files:** Create `Source/Core/Model/Note.h`, `Source/Core/Model/NoteSequence.h`/`.cpp`, `Tests/NoteSequenceTests.cpp`. Modify `Source/Core/CMakeLists.txt`, `Tests/CMakeLists.txt`。

- [ ] `Note`(スペック§2.1の構造体そのまま)、`NoteSequence`(`clear`/`add`/`size`/`operator[]`/`getNotes`)を実装。
- [ ] テスト: 空の状態で`size()==0`、`add`後に`size`/`operator[]`/`getNotes().size()`が一致、`clear()`で空に戻る。

### Task 2: `YinPitchDetector`

**Files:** Create `Source/Core/Pitch/YinPitchDetector.h`/`.cpp`, `Tests/YinPitchDetectorTests.cpp`. Modify `Source/Core/CMakeLists.txt`, `Tests/CMakeLists.txt`。

- [ ] 上記「API確定」の数式通りに実装(`Settings`は既定値ごとスペック§2.2の`YinPitchDetector::Settings`のまま)。
- [ ] テスト: サンプルレート44100Hzの合成正弦波(E2=82.41Hz, A3=220.00Hz, A4=440.00Hz, E5=659.25Hz)をそれぞれ十分な長さ(例: 1.0秒)生成し、`analyze()`結果の各`voiced==true`フレームの`frequencyHz`が入力周波数から±10セント以内であることを検証(セント差 = `1200 * log2(detected/expected)`)。無音(全サンプル0.0)入力では全フレーム`voiced==false`であることを検証。
- [ ] `ctest --test-dir build -C Debug -R YinPitchDetector --output-on-failure` で確認。

### Task 3: `NoteSegmenter`

**Files:** Create `Source/Core/Pitch/NoteSegmenter.h`/`.cpp`, `Tests/NoteSegmenterTests.cpp`. Modify `Source/Core/CMakeLists.txt`, `Tests/CMakeLists.txt`。

- [ ] スペック§2.2の処理順序(ノイズゲート→半音量子化・区間結合→最小ノート長フィルタ)通りに実装。`PitchFrame`列を手動構築したテストフィクスチャ(`YinPitchDetector`を経由しない)で検証する。
- [ ] テスト: (a) 同一量子化ピッチが連続するフレーム列が1区間に結合される、(b) `rmsLevel`がしきい値未満のフレームで区間が分断される、(c) `voiced==false`のフレームで区間が分断される、(d) `lengthFrames * hopSize / sampleRate < minNoteLengthSeconds`の区間が結果から除去される境界値ケース。

### Task 4: `OctaveErrorCorrector`

**Files:** Create `Source/Core/Pitch/OctaveErrorCorrector.h`/`.cpp`, `Tests/OctaveErrorCorrectorTests.cpp`. Modify `Source/Core/CMakeLists.txt`, `Tests/CMakeLists.txt`。

- [ ] スペック§2.2の判定ロジック通りに実装(`std::vector<RawNoteSegment>`を破壊的に補正)。
- [ ] テスト: (a) 前後が同一ピッチで中央が+12(または-12)半音ずれている3ノート区間で中央が補正される、(b) 前後ピッチ自体が`neighborConsistencyToleranceSemitones`を超えて異なる(=文脈が不安定)場合は補正されない、(c) 先頭/末尾ノート(前後どちらかが欠ける)は補正対象外。

### Task 5: `TakeManager::getTake`追加

**Files:** Modify `Source/Core/Audio/TakeManager.h`/`.cpp`, `Tests/TakeManagerTests.cpp`.

- [ ] `const Take* getTake(int index) const`をスペック§2.4通りに追加(`lock`で排他、範囲外は`nullptr`)。
- [ ] テスト: 範囲内インデックスで対応する`Take`へのポインタが返り、範囲外(負・`getNumTakes()`以上)で`nullptr`が返ることを検証。

### Task 6: `PitchAnalyzer`

**Files:** Create `Source/Core/Pitch/PitchAnalyzer.h`/`.cpp`, `Tests/PitchAnalyzerTests.cpp`. Modify `Source/Core/CMakeLists.txt`, `Tests/CMakeLists.txt`。

- [ ] スペック§2.3の流れ(`YinPitchDetector::analyze` → `NoteSegmenter::segment` → `OctaveErrorCorrector::correct` → tick変換 → デフォルトベロシティ適用 → `NoteSequence`格納)通りに実装。tick変換は上記「API確定」の式に従う。
- [ ] テスト: 既知BPM・サンプルレートで、単一の合成正弦波(例: A4=440Hz、1秒)を含む`Take`を解析し、`NoteSequence`に1件(または妥当な件数)のノートが生成され、`pitch`がMIDIノート69(A4)、`startTick`/`lengthTicks`が既知の値から逆算した期待値の近傍であることを検証。

### Task 7: `PluginProcessor`への組み込み

**Files:** Modify `Source/Plugin/PluginProcessor.h`/`.cpp`。

- [ ] `analyzeSelectedTake()`(選択中`Take`が無ければ何もしない。あれば現在のUI設定値から`PitchAnalyzer::Settings`を都度構築し解析、結果を`analyzedNotes`へ格納)、`getAnalyzedNotes()`をスペック§3.1通りに追加。
- [ ] 新規`std::atomic`パラメータ: `noiseGateSensitivity`(0〜100、既定50)、`minNoteLengthMs`(既定60)、`defaultVelocity`(既定90)とその setter/getter。
- [ ] `noiseGateSensitivity`(%)→`NoteSegmenter::Settings::noiseGateRmsThreshold`への線形マッピング: `threshold = (sensitivity / 100.0) * 0.2`(0%→0.0=ゲートしない、100%→0.2。`YinPitchDetector`のRMSは概ね0〜1程度のオーディオ振幅を想定しており、0.2は十分強いゲート)。
- [ ] `cmake --build build --config Debug` でビルド成功を確認(GUIはまだ変更なし)。

### Task 8: ツールバーUI(解析ボタン・スライダー類)

**Files:** Modify `Source/Plugin/PluginEditor.h`/`.cpp`。

- [ ] 「解析」ボタン(`juce::TextButton`、押下で`processorRef.analyzeSelectedTake()`→ノート一覧`updateContent()`)、ノイズゲート感度スライダー(0〜100、既定50)、最小ノート長スライダー(0〜300ms、既定60)、デフォルトベロシティ入力(1〜127、既定90、既存`bpmSlider`と同じ`LinearHorizontal`+`TextBoxRight`スタイル)を既存ツールバー(`resized()`内、`settingsButton`の並びまたは2段目)に追加。
- [ ] Standaloneを実際に起動し、解析ボタン押下でクラッシュしないこと・スライダー操作で値が変わることを目視確認(録音済みテイクが無い状態でも解析ボタン押下が安全であることを含む)。

### Task 9: 最小限のノート一覧パネルUI + 動作確認

**Files:** Modify `Source/Plugin/PluginEditor.h`/`.cpp`。

- [ ] スペック§3.2通り、`NoteListBoxModel`(`PluginEditor`内プライベートネストクラス、`LevelMeter`と同じ配置方針で`juce::ListBoxModel`を実装)+2つ目の`juce::ListBox`を追加。各行は`ピッチ名 | 開始時刻(秒) | 長さ(秒)`(ピッチ名は`juce::MidiMessage::getMidiNoteName(pitch, true, true, 4)`、時刻はNoteの`startTick`/`lengthTicks`を解析時BPMで秒へ逆変換)。読み取り専用。
- [ ] `analyzeSelectedTake()`完了後にこのリストの`updateContent()`を呼ぶ(取消線: 解析ボタンのクリックハンドラ内で呼ぶのが自然)。
- [ ] Standaloneで実際にマイクから鼻歌を録音→テイク選択→解析ボタン押下→ノート一覧パネルに妥当なノート(音高・時刻・長さ)が表示されることを目視確認。

### Task 10: 最終確認・単一コミット・push

- [ ] `ctest --test-dir build -C Debug --output-on-failure` で全テスト(既存20件+本マイルストーン新規分)がパス。
- [ ] `cmake --build build --config Debug` でStandalone/VST3双方ビルド成功。
- [ ] `git add`(Milestone 2で変更・新規作成した全ファイル、本計画doc含む)→ `git commit`(Milestone 2の内容を要約)→ `git push origin main`。
- [ ] プロジェクトステータスのメモリファイルを更新(Milestone 2完了・コミットハッシュ・Milestone 3への再開ポイント)。
