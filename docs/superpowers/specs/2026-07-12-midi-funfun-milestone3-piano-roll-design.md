# MIDI FUNFUN Milestone 3: ピアノロール(表示・編集・選択・Undo/Redo・波形/ピッチカーブ・プレビュー再生) 設計

日付: 2026-07-12
対象要件: [docs/requirements.md](../../requirements.md) v2 §4.3(ピアノロール編集)、§4.6(プレビュー再生)
前提: [アーキテクチャ設計](2026-07-10-midi-funfun-architecture-design.md)、Milestone 0完了・コミット済み(`3663d59`)、Milestone 1(録音)完了・コミット済み(`9afc9d9`、追加修正`524fd52`)、Milestone 2(ピッチ検出)完了・コミット済み(`cd0e5bd`、追加修正`d8d8d93`・`2d1863b`)

この設計は要件4.3(ピアノロール編集)と4.6(プレビュー再生)を満たすための実装アーキテクチャを確定するもの。要件本文はv2で確定済みであり、本設計はそれを作り直すものではない。M0-M2までと異なり、本マイルストーンは初めて本格的なGUI編集(ドラッグ・矩形選択・Undo/Redo・キーボード操作)とオーディオスケジューリング(プレビュー再生)を扱うため、規模はM0-M2それぞれより明確に大きい。

## 1. スコープ

### 1.1 対象(要件4.3・4.6)

- ノート表示(音高×時間のグリッド上にノートブロックを描画)
- 編集操作: 音高移動(縦ドラッグ)・タイミング移動(横ドラッグ)・長さリサイズ(右端ハンドルの横ドラッグ)・削除・新規手動追加
- 選択操作: 単一クリック・Shift/Ctrlクリックでの複数選択・矩形(マーキー)選択・全選択
- キーボードショートカット: `Delete`(選択ノート削除)・`Ctrl+Z`(Undo)・`Ctrl+Y`(Redo)・`Ctrl+A`(全選択)
- Gridボタン(ON/OFF)によるドラッグ・リサイズ・新規追加時のスナップ切り替え
- 上記編集操作すべてのUndo/Redo対応
- 元録音波形のピアノロール背後への半透明オーバーレイ表示
- 連続ピッチカーブ(半音量子化前の生の音程曲線)のオーバーレイ表示
- 声域に応じた縦方向表示範囲の自動フィット、手動ズーム(縦・横)
- 再生ヘッド(現在位置)表示
- ノートクリックによる単音プレビュー再生
- 内蔵ピアノ風シンセによるMIDIノートの通し再生(プレビュー再生)

### 1.2 対象外(後続マイルストーンへ)

- クオンタイズ本体(強度指定・右クリックメニュー) → Milestone 4
- スケール補正本体 → Milestone 5
- MIDI書き出し → Milestone 6
- 元録音音声そのものの同期再生(後述1.4 Q1参照。要件4.6は編集後MIDIの内蔵音源再生のみを求めている)

### 1.3 ユーザー承認済みのスコープ追加事項

要件書の文言には明記がないが、ブレスト時のチェックポイントで以下を明示的に承認済み:

- **矢印キーによるノートのnudge移動**: `↑`/`↓` = 選択ノートを半音移動、`←`/`→` = 選択ノートをグリッド1単位分移動。要件4.3のショートカット一覧(Delete/Ctrl+Z/Ctrl+Y/Ctrl+A)には無い追加操作。nudgeは3.4節の`MoveNotesAction`をドラッグ移動と全く同じ形で構築する(専用のActionクラスは作らない)。これによりQ4の「nudgeもドラッグ移動と同じUndo粒度にする」という要求を、コード上の重複なく満たす。

### 1.4 Undo/Redoのスコープに関する明確化

ブレスト時にユーザーから明示的な指摘があった点: **「録音した音を消すところまではUndoの機能に含めなくていい。そこはdeleteで対応する」**。

これに従い、本マイルストーンで導入する`juce::UndoManager`(3.4節)が管理するのは**ピアノロール上のノート編集操作のみ**(移動・リサイズ・追加・削除・全置換〈再解析、2.3節〉)であり、Milestone 1由来のテイク管理(`TakeManager`によるテイクの録音・選択・削除)は対象外とする。テイク削除は既存どおり即時実行・Undo不可のままとし、ノート編集用の`UndoManager`とは配線上も完全に分離する(共有しない・同じスタックに積まない)。この分離は既存コードの設計を変更するものではなく、「新設する`UndoManager`の適用範囲をノート編集に限定する」という確認事項である。

## 2. 全体データフロー・所有関係

本マイルストーンは複数レイヤーにまたがる新しい状態(編集中ノート列・選択状態・Undo履歴・再生位置)を導入するため、「何がどこに存在し、いつ生存し、いつ破棄されるか」を先に確定する。

### 2.1 所有関係の原則

- **`PluginProcessor`が「データ」を所有する**: 編集中ノート列(`editedNotes`)・Undo履歴(`undoManager`)・直近の解析結果(`lastAnalysisResult`、ノート+ピッチ曲線)。理由: VST3ホストはプラグインウィンドウ(Editor)をユーザー操作で自由に閉じ・再度開くことができ、Editorのライフサイクルは`AudioProcessor`より短い。編集内容とUndo履歴をEditor側に置くと、ウィンドウを閉じただけで編集が消えるという致命的な不具合になる。既存の`analyzedNotes`もこの理由でProcessor所有だった(M2設計)ため、本マイルストーンもその前例を踏襲する。
- **`PluginEditor`(および新設`Source/UI/`コンポーネント群)が「表示状態」を所有する**: 選択状態(`Selection`)・ズーム/スクロール位置。これらはウィンドウを閉じて開き直せば失われて構わない一時的なビュー状態であり、一般的なDAW/エディタでも保持されない。

### 2.2 データの実体

```
PluginProcessor
├── midi_funfun::core::PitchAnalysisResult lastAnalysisResult;  // 解析直後の生データ(notes + ピッチ曲線)。編集はしない
├── midi_funfun::core::NoteSequence        editedNotes;          // ピアノロールが表示・編集する対象
└── juce::UndoManager                      undoManager;          // editedNotesに対する編集操作のみを積む

PluginEditor / Source/UI/PianoRollComponent
├── midi_funfun::core::Selection selection;   // 選択中ノートのid集合(一時状態)
└── ズーム率・スクロール位置(pixelsPerTick, pixelsPerSemitone, スクロールオフセット)
```

### 2.3 「解析」ボタン再押下時の扱い

要件のワークフロー(§3)は「解析→編集」を1回想定した記述だが、実際の操作では「編集後にもう一度解析をやり直したくなる」ことは起こり得る(例: ノイズゲート感度を変えて再解析したい)。この挙動を未定義のままにしないため、以下で確定する:

- `editedNotes`が空(=このセッションでまだ解析結果を取り込んでいない)場合: `lastAnalysisResult.notes`をそのまま`editedNotes`へコピーする。Undoに積まない(元に戻す対象となる「前の状態」が存在しないため)。
- `editedNotes`が空でない(=既に編集中、または過去の解析結果が入っている)場合: 新しい解析結果への全置換を`ReplaceAllNotesAction`(3.4節)として`undoManager`に積む。これにより、「解析をもう一度押したら編集が消えた」と思ってもユーザーは`Ctrl+Z`で編集後の状態へ戻せる。1.4節の「Undoスコープはノート編集操作のみ」という制約とも矛盾しない(これはテイク削除ではなく、ノート列そのものへの編集操作の一種として扱う)。

テイク一覧で別のテイクを選び直しただけでは`editedNotes`は変化しない(要件4.2「解析は自動実行ではなく手動実行」を踏襲し、解析ボタンを押すまで何も起きない)。

## 3. Coreレイヤーの変更(`Source/Core/`)

### 3.1 `Model/Note.h`(既存拡張): 安定id の追加

選択状態やUndo/Redoでノートを指し示すために、**ベクタ内インデックスではなく安定した識別子**が必要(削除・復元・並べ替えでインデックスはずれるため)。

```cpp
struct Note
{
    int pitch = 0;
    juce::int64 startTick = 0;
    juce::int64 lengthTicks = 0;
    int velocity = 90;
    juce::int64 id = 0;   // 0 = 未採番。NoteSequence::add()が採番する。選択・Undo/Redoでの同一性判定に使う
};
```

### 3.2 `Model/NoteSequence.h/.cpp`(既存拡張): 変更API

```cpp
class NoteSequence
{
public:
    void clear();

    /** note.id == 0 なら新規idを採番して追加し、そのidを返す。
     *  note.id != 0 ならそのidをそのまま使用して追加し(Undoによる復元・再解析時の同一id維持に使用)、
     *  内部の採番カウンタを id 以上に更新する。 */
    juce::int64 add(Note note);

    int size() const;
    const Note& operator[](int index) const;
    const std::vector<Note>& getNotes() const;

    // --- Milestone 3 追加: 編集コマンドから使う変更API ---
    Note* findById(juce::int64 id);
    const Note* findById(juce::int64 id) const;
    bool removeById(juce::int64 id);
    /** idを保持したまま全体を置き換える(Undo実行時の完全復元、2.3節の再解析全置換に使用)。 */
    void replaceAll(const std::vector<Note>& newNotes);

private:
    std::vector<Note> notes;
    juce::int64 nextId = 1;
};
```

`add()`のシグネチャが`void add(const Note&)`から`juce::int64 add(Note)`へ変わるため、既存の呼び出し元(`PitchAnalyzer::analyze`、`Tests/NoteSequenceTests.cpp`)は実装フェーズで追随が必要(戻り値を無視すればビルド互換)。

### 3.3 `Model/Selection.h/.cpp`(新規): 選択状態

GUI非依存の純粋ロジック。実体はEditor側が1つ所有する。

```cpp
class Selection
{
public:
    void clear();
    void set(juce::int64 id);                              // 単一選択(既存選択をクリアしてから選択)
    void toggle(juce::int64 id);                            // Ctrlクリック用
    void addRange(const std::vector<juce::int64>& ids);     // Shiftクリック・マーキー確定用(和集合)
    void selectAll(const NoteSequence& sequence);
    bool isSelected(juce::int64 id) const;
    bool isEmpty() const;
    const std::set<juce::int64>& getSelectedIds() const;
    /** sequenceに存在しなくなったidを選択集合から取り除く(Undo/Redoでノートが消えた後の整合性維持用)。 */
    void pruneMissing(const NoteSequence& sequence);

private:
    std::set<juce::int64> selectedIds;
};
```

### 3.4 `Model/EditCommands.h/.cpp`(新規): Undoableな編集操作

アーキテクチャ設計§3で確定済みの方針(`juce::UndoableAction`のサブクラス群を`juce::UndoManager`に積む、コマンドパターン)をそのまま実装する。各アクションは`NoteSequence&`への参照を持ち、`perform()`/`undo()`で変更・復元する。

```cpp
/** 選択中の1つ以上のノートを移動する(音高・開始位置いずれか、または両方)。
 *  ドラッグ移動確定時、および矢印キーnudge確定時の両方で使う(1.3節)。 */
class MoveNotesAction final : public juce::UndoableAction
{
public:
    struct Delta { juce::int64 id; int oldPitch; juce::int64 oldStartTick; int newPitch; juce::int64 newStartTick; };
    MoveNotesAction(NoteSequence& sequenceIn, std::vector<Delta> deltasIn);
    bool perform() override;
    bool undo() override;
private:
    NoteSequence& sequence;
    std::vector<Delta> deltas;
};

/** 単一ノートの長さリサイズ(右端ハンドルドラッグ確定時)。複数選択中でもリサイズ操作自体は
 *  ハンドルを掴んだ1ノートのみに適用する(一般的なピアノロールの挙動に合わせる)。 */
class ResizeNoteAction final : public juce::UndoableAction
{
public:
    ResizeNoteAction(NoteSequence& sequenceIn, juce::int64 idIn, juce::int64 oldLengthTicksIn, juce::int64 newLengthTicksIn);
    bool perform() override;
    bool undo() override;
private:
    NoteSequence& sequence;
    juce::int64 id, oldLengthTicks, newLengthTicks;
};

/** 新規ノートの手動追加。 */
class AddNoteAction final : public juce::UndoableAction
{
public:
    AddNoteAction(NoteSequence& sequenceIn, Note noteToAddIn);
    bool perform() override;   // 内部でsequence.add()し、採番されたidをnoteに反映して保持
    bool undo() override;      // 採番済みidでremoveById
private:
    NoteSequence& sequence;
    Note noteToAdd;
};

/** 選択中ノート群の削除。 */
class DeleteNotesAction final : public juce::UndoableAction
{
public:
    DeleteNotesAction(NoteSequence& sequenceIn, std::vector<Note> notesToDeleteIn); // 呼び出し側が削除前にスナップショットを渡す
    bool perform() override;   // 各idをremoveById
    bool undo() override;      // 元のNoteをid付きでadd()し直す(順序復元は保証しない。描画は時間順に依存しないため実害なし)
private:
    NoteSequence& sequence;
    std::vector<Note> notesToDelete;
};

/** ノート列全体の置き換え(2.3節: 再解析時)。 */
class ReplaceAllNotesAction final : public juce::UndoableAction
{
public:
    ReplaceAllNotesAction(NoteSequence& sequenceIn, std::vector<Note> oldNotesIn, std::vector<Note> newNotesIn);
    bool perform() override;
    bool undo() override;
private:
    NoteSequence& sequence;
    std::vector<Note> oldNotes, newNotes;
};
```

クオンタイズ(M4)・スケール補正(M5)は、複数ノートの音高/開始位置を一括で書き換える操作であり、実装時には`MoveNotesAction`と同型の一括変更アクション(または汎用化した派生)を再利用できる見込みだが、その詳細設計はM4/M5の設計時に行う(本マイルストーンでは先取りしない)。

**依存関係の追加**: `juce::UndoableAction`/`juce::UndoManager`は`juce_data_structures`モジュールに属する。現在`midi_funfun_core`は`juce_core`/`juce_audio_basics`/`juce_dsp`のみをリンクしており、`juce_data_structures`は未リンクのため、`Source/Core/CMakeLists.txt`の`target_link_libraries`に追加する(6章)。

### 3.5 `Model/GridSnap.h/.cpp`(新規): ドラッグ時のグリッドスナップ

要件4.3の「Gridボタン」は「編集ドラッグ時のスナップ挙動」の制御であり、Milestone 4の「クオンタイズ」(既存ノート一括のグリッド吸着)とは別機能と要件文に明記されている。本マイルストーンでは前者のみを実装する。

```cpp
namespace midi_funfun::core
{
    /** Grid ON時、ドラッグ移動・リサイズ・新規追加の位置をこの関数で丸める。
     *  グリッド単位は1/16音符固定(120 tick @ PPQ480)。この単位をM4のクオンタイズ機能と
     *  共有するかどうかはM4設計時に再検討する(要件は両機能を別物と位置づけているため、
     *  本マイルストーンでは値を共有させず独立させておく)。 */
    constexpr juce::int64 gridUnitTicks = ticksPerQuarterNote / 4; // 1/16音符

    juce::int64 snapTickToGrid(juce::int64 tick);
}
```

音高方向のスナップ(縦ドラッグ)は常に半音単位(`Note::pitch`が`int`である以上、Grid ON/OFFに関わらずそもそも半音未満には丸まらない)。Grid ON/OFFが効くのは時間軸(横ドラッグ・リサイズ・新規追加のtick位置)のみである。

### 3.6 `Pitch/PitchAnalyzer.h/.cpp`(既存変更): ピッチ曲線を捨てずに返す

現状の`PitchAnalyzer::analyze()`は`YinPitchDetector::analyze()`が返すフレーム単位のピッチ曲線を内部で使った後に捨てており、`NoteSequence`だけを返している。ピッチカーブ表示(要件4.3)のためにこれをGUI側へ渡す必要がある。

```cpp
struct PitchAnalysisResult
{
    NoteSequence notes;
    std::vector<PitchFrame> pitchFrames; // 半音量子化前の生ピッチ曲線。frames[i]の時刻 = i * hopSize / sampleRate
    int hopSize = 0;
    double sampleRate = 0.0;
};

class PitchAnalyzer
{
public:
    // Settingsは変更なし
    explicit PitchAnalyzer(Settings settingsIn = {});

    /** 戻り値をNoteSequenceからPitchAnalysisResultへ変更(ノート+生ピッチ曲線)。 */
    PitchAnalysisResult analyze(const Take& take) const;
};
```

内部処理(YIN→NoteSegmenter→OctaveErrorCorrector→tick変換)は変更なし。`YinPitchDetector::analyze`が返す`frames`を捨てずに`PitchAnalysisResult::pitchFrames`へそのまま格納するだけの変更。既存の`Tests/PitchAnalyzerTests.cpp`は戻り値の型変更に伴い`result.notes.size()`のような形へ実装フェーズで追随が必要。

### 3.7 `Audio/WaveformPeakCache.h/.cpp`(新規): 波形オーバーレイ用の間引き

録音バッファは既に全体がメモリ上にあり(`Take::buffer`)、解析もオフラインのため、ファイルストリーミング前提の`juce::AudioThumbnail`(バックグラウンドスレッド・ハッシュキャッシュ等を持つ、ディスク上ファイル向けの重量級クラス)を使う理由がない。素朴な同期的min/max間引きで十分であり、JUCE経験の浅い開発者にも理解しやすいコードになる。

```cpp
struct PeakPair { float minValue = 0.0f; float maxValue = 0.0f; };

class WaveformPeakCache
{
public:
    /** monoSamples[0, numSamples) を widthInPixels 列に間引き、列ごとのmin/maxを返す。 */
    static std::vector<PeakPair> build(const float* monoSamples, int numSamples, int widthInPixels);
};
```

UI側(3.9節参照)はこれを水平ズーム/スクロール範囲が変わった時だけ呼び直し、結果をキャッシュして`paint()`のたびには再計算しない(毎フレーム全サンプル走査を避けるため)。

### 3.8 `Audio/PlaybackTransport.h/.cpp`(新規): 通し再生のスケジューリング

`RecordingTransport`(M1)が録音側の状態遷移をまとめる役割に対応する、再生側のオーケストレータ。命名・責務分担のパターンをそのまま踏襲する。

```cpp
class PlaybackTransport
{
public:
    enum class State { Idle, Playing };

    /** notesのスナップショットを取り、tick=0(またはstartTick)から再生を開始する。
     *  開始後にeditedNotesが編集されても、この再生インスタンスのイベント列には反映しない
     *  (再生中の編集は次回の再生開始時から反映される、という単純化)。 */
    void start(const NoteSequence& notes, double bpm, double sampleRate, juce::int64 startTick = 0);
    void stop();
    State getState() const;
    juce::int64 getCurrentTick() const; // 再生ヘッド描画用。GUIスレッドがTimerでポーリングする

    struct NoteEvent
    {
        int sampleOffsetInBlock = 0;
        int pitch = 0;
        int velocity = 0;
        bool isNoteOn = false;
    };

    /** このオーディオブロックで発火すべきnoteOn/noteOffイベント群を返し、内部の再生位置をnumSamples進める。
     *  最後のノートの終端を過ぎたら自動的にStateをIdleへ遷移する。 */
    std::vector<NoteEvent> advance(int numSamples, double sampleRate);
};
```

単音プレビュー再生(ノートクリック)は`PlaybackTransport`を使わない別経路とする(下記4.1節の`PreviewNote`)。理由: シーケンス全体の進行管理は不要で、「1音を短時間鳴らして自動的に止める」という単純な要求のためだけに`PlaybackTransport`の状態機械を巻き込むと過剰設計になる。

### 3.9 `Audio/PianoVoice.h/.cpp`(新規): 内蔵音源

要件4.6「シンプルな内蔵ピアノ風の音色(リアルな音質は求めない)」を満たす最小構成として、`juce::Synthesiser`のフレームワーク(`juce_audio_basics`に属し、Coreは既にリンク済み)を使う。減衰する2〜3倍音構成の正弦波 + シンプルなAmplitude Envelope(Attack短め・Release短め)程度の軽量実装とし、サンプル音源やIR等の外部アセットは使わない(要件に忠実、依存も増やさない)。

```cpp
class PianoSound final : public juce::SynthesiserSound
{
public:
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

class PianoVoice final : public juce::SynthesiserVoice
{
public:
    bool canPlaySound(juce::SynthesiserSound*) override;
    void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int) override;
    void stopNote(float velocity, bool allowTailOff) override;
    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}
    void renderNextBlock(juce::AudioBuffer<float>&, int startSample, int numSamples) override;
    // 内部: 現在の周波数・簡易envelopeの位相
};
```

## 4. Pluginレイヤーの変更(`Source/Plugin/`)

### 4.1 `PluginProcessor`

- 新規メンバ: `lastAnalysisResult`(2.2節)・`editedNotes`・`undoManager`・`PlaybackTransport playbackTransport`・`juce::Synthesiser mainSynth`(`PianoVoice`を複数体、目安16、+`PianoSound`)。
- `analyzeSelectedTake()`: 内部で`PitchAnalyzer::analyze()`を呼んだ後、2.3節の分岐(初回コピー or `ReplaceAllNotesAction`経由の置換)を行う。既存の`getAnalyzedNotes()`は`editedNotes`を返すよう置き換え、新規に`getAnalyzedPitchCurve()`(`lastAnalysisResult.pitchFrames`+`hopSize`+`sampleRate`)・`getUndoManager()`・`getWaveformSourceTake()`相当のアクセサを追加する。
- 単音プレビュー再生: `struct PreviewNote { int pitch = -1; int remainingSamples = 0; }`をメンバに1つ持ち、ノートクリック時に`mainSynth.noteOn(1, pitch, velocityAsFloat)`し`remainingSamples`を約400ms相当のサンプル数にセットする。`processBlock`の先頭で`remainingSamples`を減算し、0を跨いだブロックで`mainSynth.noteOff(...)`する(400msという長さは「音高確認が目的」という要件4.6の記述に基づく暫定固定値。ノート本来の長さではなく固定短時間とすることで、二重クリック時の重複ノートオフ処理などの複雑化を避ける)。
- 通し再生: `startPlayback()`/`stopPlayback()`を追加し、`playbackTransport.start(editedNotes, bpm, sampleRate)`を呼ぶ。`processBlock`内で`playbackTransport.advance(numSamples, sampleRate)`が返す`NoteEvent`列を`mainSynth.noteOn/noteOff`へ渡す。
- `processBlock`拡張: 上記の単音プレビュー・通し再生いずれの経路も、既存の録音/メトロノーム処理と同じ`processBlock`内で行う。`mainSynth.renderNextBlock`の出力を既存の出力バッファへミックスする(録音時のモニタリング/クリック音ミックスと同様のパターン)。

### 4.2 Undo履歴の深さ

`undoManager`は`juce::UndoManager`のデフォルトコンストラクタ(`maxNumberOfUnitsToKeep = 30000`)をそのまま使い、上限を独自に縮めない。ノート編集操作(移動・リサイズ・追加・削除)はデータ量として軽量であり、1セッション中に実質上限へ到達することは想定しにくいため、意図的な上限設定は行わない(実質無制限)。

### 4.3 テイク管理との分離の再確認(1.4節)

`undoManager`・`editedNotes`は`TakeManager`と一切連携しない。テイクの削除は既存どおり`TakeManager`のAPIを直接呼ぶ即時操作のままとし、ノート編集用Undoスタックに影響しない(そもそも別インスタンスであり、混線しようがない設計だが、実装時にも「同じUndoManagerを使い回さない」ことを明示的に確認する)。

## 5. UIレイヤーの新設(`Source/UI/`)

アーキテクチャ設計(§2)が当初から予定していた`Source/UI/`ディレクトリを、本マイルストーンで初めて実際に起こす。現状すべてのGUIコードは`Source/Plugin/PluginEditor.cpp`に同居しているが、ピアノロールの複雑さをそこに追加し続けると単一ファイルが肥大化するため、独立したコンポーネント群として切り出す。既存の`LevelMeter`等の小さな補助コンポーネントは`PluginEditor.cpp`内ネストのままで構わない(変更しない)。

### 5.1 `PianoRollComponent`(新規、`Source/UI/PianoRollComponent.h/.cpp`)

画面中央のピアノロール全体を統括するコンポーネント。内部構成:

- `juce::Viewport rollViewport` — 水平・横スクロールをJUCE標準機構に任せる(独自スクロール実装をしない)。
- `Canvas`(`PianoRollComponent`のprivateネストクラス、`juce::Component`) — `rollViewport`の中身。グリッド・波形・ピッチカーブ・ノート・再生ヘッド・時間ルーラー(上端帯)をすべて描画し、マウス操作(ドラッグ移動・リサイズ・矩形選択・ダブルクリック追加・クリック選択+プレビュー)を処理する。実サイズは`全体tick数 × pixelsPerTick`(幅)・`表示pitch範囲 × pixelsPerSemitone`(高さ)で、ズーム変更のたびに`setSize()`し直す。
- `PianoKeyboardComponent pianoKeyboard`(5.2節) — 左端に固定表示。`rollViewport`の垂直スクロール位置と`Viewport::Listener::visibleAreaChanged`で同期する。

ルーラーを独立コンポーネントに分けず`Canvas`の上端帯として描画するのは、ルーラーと本体が常に同じ水平スクロール量で動くため、独立コンポーネント+リスナー配線を増やすメリットが薄いという判断(過剰分割を避ける)。

`Canvas`の実装は「1つの視覚的単位(ピアノロールを描いて操作する)」という観点でまとまった責務だが、`paint()`一つに全部書くと肥大化するため、`paintGrid`/`paintWaveform`/`paintPitchCurve`/`paintNotes`/`paintPlayhead`のような小さなprivateメソッドに分割し、マウスドラッグの状態(`enum class DragMode { None, MoveNotes, ResizeNote, Marquee }`+ドラッグ開始時のスナップショット)も専用のprivate構造体にまとめる。

### 5.2 `PianoKeyboardComponent`(新規、`Source/UI/PianoKeyboardComponent.h/.cpp`)

左側の鍵盤。**描画専用**(クリックしても何も起きない)。`juce::MidiKeyboardComponent`(JUCE標準)は採用しない — その理由: `MidiKeyboardComponent`は自身のスクロール・ズームモデルを内部に持ち、外部のピクセル単位ズーム(ピアノロール側の`pixelsPerSemitone`)と正確に同期させる作りになっていない。ピアノロールのノート行とピクセル単位で完全に一致させる必要があるため、`pixelsPerSemitone`と表示pitch範囲を外部から受け取って自前で鍵盤を描くだけの軽量コンポーネントにする方が単純で確実。要件4.3が求めるのは「音高移動をノートクリックで確認できる」ことであり、鍵盤自体のクリック再生は要件にない機能追加のため実装しない(スコープを広げない)。

### 5.3 マウス操作の状態機械(`Canvas`内)

| 開始条件 | 動作 | 確定タイミング | Undoアクション |
|---|---|---|---|
| ノート本体上でmouseDown→ドラッグ | 音高・開始位置の移動(Grid ONなら開始位置のみスナップ) | mouseUp | `MoveNotesAction` |
| ノート右端ハンドル(数px)上でmouseDown→ドラッグ | 長さリサイズ(Grid ONならスナップ) | mouseUp | `ResizeNoteAction` |
| ノート本体上でクリック(ドラッグなし) | 選択(Shift/Ctrl修飾で追加/トグル)+単音プレビュー再生 | mouseUp | なし(選択は非Undo) |
| 空白部分でmouseDown→ドラッグ | 矩形マーキー選択(ドラッグ中リアルタイムに交差ノートをプレビュー選択) | mouseUp | なし |
| 空白部分でダブルクリック | 新規ノート追加(クリック位置の音高・Grid ON時はスナップ済み開始位置、既定長=1/16音符) | mouseUp相当(ダブルクリック時点) | `AddNoteAction` |

ドラッグ中は`editedNotes`本体・`undoManager`に一切触れず、`Canvas`のローカルな一時オフセットのみで再描画する(mouseUp時に初めて1回のActionとしてコミットする)。これにより「1ドラッグジェスチャ=1 Undoステップ」(ブレスト時のQ2確認事項)を自然に満たす。

### 5.4 キーボードショートカット・nudge

`Canvas::keyPressed()`で処理する(`Canvas`は`setWantsKeyboardFocus(true)`、mouseDown時に`grabKeyboardFocus()`)。

- `Delete` → 選択中ノートのスナップショットを取り`DeleteNotesAction`
- `Ctrl+Z` / `Ctrl+Y` → `undoManager.undo()` / `undoManager.redo()`
- `Ctrl+A` → `selection.selectAll(editedNotes)`
- `↑`/`↓`(矢印キー、1.3節) → 選択中ノート全体を±1半音移動する`MoveNotesAction`を構築・実行
- `←`/`→`(矢印キー、1.3節) → 選択中ノート全体を±1グリッド単位(`gridUnitTicks`、Grid ON/OFFに関わらず固定単位。矢印キーでのnudgeは「明示的に1段階動かす」操作のため、Gridトグルの影響を受けない)移動する`MoveNotesAction`を構築・実行

### 5.5 Undo/Redo後の再描画・選択整合性(JUCEの既知の落とし穴への対応)

M2で判明した既知の落とし穴: JUCEのコンポーネントはデータが変わっただけでは自動再描画されない(`ListBox`等も明示的な`updateContent()`/`repaint()`が必要)。本マイルストーンは再生ヘッドや波形など「時間経過やUndo操作で変わるが、マウス/キー入力そのものではない」状態が多く、同じ落とし穴を踏みやすい。対応方針:

- `juce::UndoManager`は`juce::ChangeBroadcaster`を継承している。`PianoRollComponent`(または`Canvas`)を`juce::ChangeListener`として`undoManager`に登録し、`changeListenerCallback()`内で`selection.pruneMissing(editedNotes)`(存在しなくなったノートのidを選択から除去)と`repaint()`を明示的に呼ぶ。
- 再生ヘッドは`juce::Timer`(既存の`PluginEditor`のパターンを踏襲、目安30〜60Hz)で`playbackTransport.getCurrentTick()`をポーリングし、前回値と異なれば`repaint()`する。
- 波形・ピッチカーブは3.7節の通りズーム/スクロール変更時のみ再計算し、そのタイミングで明示的に`repaint()`する(`paint()`内で毎回計算しない)。

### 5.6 ズーム・スクロール

- 水平(時間軸): `pixelsPerTick`。マウスホイール(横スクロール、Shift+ホイールまたはトラックパッド水平スクロール)は`rollViewport`標準機能に任せる。ズーム変更はツールバーに配置する既存パターンのスライダー(`juce::Slider`、`PluginEditor`側に追加)で行う。
- 垂直(音高軸): `pixelsPerSemitone`。解析完了時、`editedNotes`内の最低〜最高pitchに上下マージン(数半音)を加えた範囲へ自動フィットする(要件4.3「声域(2オクターブ程度)に応じて自動フィット」)。手動ズームはCanvas上でのマウスホイール(縦スクロール)+ Ctrl修飾、または同様のツールバースライダーで行う。
- ズーム変更時は`Canvas::setSize()`し直し、3.7節のwaveformキャッシュを再計算、`pianoKeyboard`のサイズも追随させる。

### 5.7 波形・ピッチカーブの描画

要件どおり、ノートグリッドの**背後**に半透明で重ねる(別レーンにはしない、Melodyneスタイル)。

- 波形: `WaveformPeakCache::build()`(3.7節)の結果を、Canvas幅の各列について`minValue`〜`maxValue`の縦線として半透明色で描画する。選択中テイク(解析対象になったテイク)の`Take::buffer`を参照する。
- ピッチカーブ: `lastAnalysisResult.pitchFrames`の`voiced`なフレームを、`(フレーム時刻をtick換算→x, frequencyHzをMIDIノート番号相当の実数値に変換→y)`の折れ線として薄く描画する。半音量子化前の生の値をそのまま使う(要件4.3「検出がどこを拾っているかを視覚的に確認できるように」)。

いずれもCanvasの`paint()`内で、キャッシュ済みデータを使って描くのみとし、`paint()`自体でサンプル/フレーム列の重い走査をしない(5.5節の再描画方針と整合)。

### 5.8 プレビュー再生UI

要件5章の画面構成(「下部: 再生トランスポート、プレビュー音源表示」)に対応する、新規`PlaybackTransportBar`コンポーネント(`Source/UI/PlaybackTransportBar.h/.cpp`、または`PluginEditor`下部領域に直接配置する程度の小規模ならそちらでも可 — 実装時、他のツールバー要素との統一感を見て判断してよい)。

- Play/Stopボタン(一時停止という概念は導入しない。MVPとして単純化: 停止時は常に先頭または最後に止めた位置から、という区別も設けず、停止=位置0に戻るものとする。要件4.6・5章にPause/Loop/位置記憶についての言及はなく、シンプルな仕様で十分)
- 再生中/停止中の見た目切り替え(既存の録音ボタンのパターンを踏襲)

再生ヘッドのシーク(ルーラー上クリックで位置移動)は要件に明記がないため、本マイルストーンでは実装しない(YAGNI。将来必要になれば追加は容易)。

### 5.9 Milestone 2暫定パネルの削除

M2で「あくまで目視確認用の仮UI、M3で本実装のピアノロールに置き換え」と明記されていた`NoteListBoxModel`(`PluginEditor.h`内ネストクラス)を、本マイルストーンの実装で完全に削除する。具体的には:

- `PluginEditor.h`/`.cpp`から`NoteListBoxModel`クラス定義・`noteListBoxModel`/`noteListBox`/`analysisStatusLabel`メンバと関連する`resized()`/コンストラクタ内の配線をすべて削除する。
- `Source/Core/Model/NoteFormatting.h`(`formatNoteListRow`)はこのパネル専用のフォーマッタであり、他に利用箇所がないため削除する(死んだコードを残さない)。ピッチ名表示が新たに必要になった箇所(例: 将来的なツールバー上の状態表示)が出てきた場合は、その時点で必要な形の関数を新設すればよい(現時点で使途のない汎用ユーティリティを先回りして残さない)。
- 新旧UIが並存する期間を作らない(新パネルの実装完了と同じコミットで削除する。実装順序は8章参照)。

新旧を並存させない理由は、ブレスト時にユーザーへ確認済みのとおり「置き換え対象が並んでいると紛らわしい」ため。

## 6. CMakeの変更

### 6.1 `Source/Core/CMakeLists.txt`

`target_link_libraries`に`juce::juce_data_structures`を追加(3.4節、`UndoManager`/`UndoableAction`用)。`add_library`のソース一覧に新規ファイル(`Model/Selection.h/.cpp`・`Model/EditCommands.h/.cpp`・`Model/GridSnap.h/.cpp`・`Audio/WaveformPeakCache.h/.cpp`・`Audio/PlaybackTransport.h/.cpp`・`Audio/PianoVoice.h/.cpp`)を追加し、`Model/NoteFormatting.h`(5.9節)を削除する。

### 6.2 `Source/Plugin/CMakeLists.txt`

新規に作る`Source/UI/`配下のファイル(`PianoRollComponent.h/.cpp`・`PianoKeyboardComponent.h/.cpp`・`PlaybackTransportBar.h/.cpp`)を、新規の静的ライブラリターゲットとしてではなく**既存の`MidiFunfun`ターゲットへの追加ソースとして**`target_sources`に加える(`../UI/PianoRollComponent.cpp`のような相対パス)。`target_include_directories`に`${CMAKE_CURRENT_SOURCE_DIR}/../UI`を追加する。

新規ターゲットを立てない理由: `Source/UI/`はGUIコンポーネント群であり`Source/Plugin/`と同様にJUCEのGUIモジュールに依存する。両者を分離しても独立してビルド・テストされる単位にはならず(GUIは自動テスト対象外、6.1節・7章参照)、複雑さが増すだけである。物理的なディレクトリ分割(アーキテクチャ設計が当初意図した`Plugin/`と`UI/`の区別)は維持しつつ、CMakeターゲットとしては1つのプラグイン本体にまとめる。

## 7. テスト戦略

`Tests/`ターゲット(`midi_funfun_core` + Catch2、ヘッドレス)に以下を追加する:

- **`Note`/`NoteSequence`**: id採番の一意性・`add(note)`でid!=0を渡した場合の再利用と採番カウンタ更新・`findById`/`removeById`/`replaceAll`の境界値。
- **`Selection`**: 単一選択・トグル・範囲追加・全選択・`pruneMissing`で消えたidが除去されること。
- **`EditCommands`各クラス**: `perform()`→`undo()`で元の状態(pitch/startTick/lengthTicks/id/velocity)に厳密に戻ること、`redo()`(デフォルト実装が`perform()`を再呼び出しすること)で再度同じ結果になること。`DeleteNotesAction`の`undo()`で削除前のidがそのまま復元されること。
- **`GridSnap::snapTickToGrid`**: グリッド境界・境界直前直後の丸め方向。
- **`PitchAnalyzer`**: 既存の統合テストを`PitchAnalysisResult`型に追随させ、`pitchFrames`が空でないこと・`hopSize`/`sampleRate`が設定と一致することを追加検証する。
- **`WaveformPeakCache::build`**: 既知の合成波形(既知の最大振幅を持つ区間を含む)に対し、対応する列のmin/maxが期待どおりであることを検証する。列幅よりサンプル数が少ない/多い境界ケースも確認する。
- **`PlaybackTransport`**: 既知のNoteSequence(複数ノート、ブロック境界をまたぐノート開始/終了を含む)に対し、`advance()`が正しいサンプルオフセットでnoteOn/noteOffを返すこと、最後のノート終端後に`State::Idle`へ遷移すること。
- **`PianoVoice`**: `startNote`後に`renderNextBlock`が非ゼロの出力を生成すること、`stopNote(velocity, allowTailOff=false)`後は無音になること。

GUI(`Canvas`の描画・マウスジェスチャ・キーボードショートカットの実オペレーション・ズーム/スクロールUI・鍵盤描画)は既存方針どおり自動テスト対象外とし、Standaloneアプリを実際にビルド・起動して手動確認する(録音→解析→ピアノロールでの移動/リサイズ/削除/追加/選択/Undo/Redo/波形・ピッチカーブ表示/プレビュー再生を一通り操作)。

## 8. タスク分割(概略、詳細な実装計画は着手時に別途作成)

ロードマップの方針(各マイルストーン1コミット)を踏襲する。以下は`writing-plans`スキルによる詳細タスク分解の前段としての概略順序であり、本設計スペックの一部として参考に示すのみ(詳細なbite-sizedタスクへの分解は別途行う)。

1. `Note`(idフィールド)・`NoteSequence`(変更API)(Core、TDD)
2. `Selection`(Core、TDD)
3. `Model/EditCommands`各クラス(Core、TDD、`juce_data_structures`のCMake追加込み)
4. `GridSnap`(Core、TDD)
5. `PitchAnalyzer`の戻り値変更(`PitchAnalysisResult`)+ 既存テスト追随(Core、TDD)
6. `WaveformPeakCache`(Core、TDD)
7. `PlaybackTransport`(Core、TDD)
8. `PianoVoice`/`PianoSound`(Core、TDD)
9. `PluginProcessor`への組み込み(editedNotes/undoManager/2.3節の解析分岐/プレビュー再生/通し再生/`processBlock`でのシンセミックス)+ 実機ビルドで動作確認
10. `PianoKeyboardComponent`(UI)+ 動作確認
11. `PianoRollComponent`/`Canvas`基本描画(グリッド・ノート・ルーラー)+ 動作確認
12. マウス操作(移動・リサイズ・クリック選択+プレビュー・マーキー選択・ダブルクリック追加)+ 動作確認
13. キーボードショートカット(Delete/Ctrl+Z/Ctrl+Y/Ctrl+A/矢印nudge)+ 動作確認
14. 波形・ピッチカーブのオーバーレイ描画+ 動作確認
15. ズーム・スクロール(自動フィット・手動ズーム)+ 動作確認
16. `PlaybackTransportBar`(Play/Stop)+ 再生ヘッド描画+ 動作確認
17. Milestone 2暫定パネルの削除(5.9節)
18. 最終確認・単一コミット・push

## 9. スコープ外の再確認

- **クオンタイズ本体**(要件4.4: 強度指定・右クリックメニュー): Milestone 4のスコープ。本マイルストーンの`GridSnap`(3.5節)はドラッグ編集時のスナップのみであり、既存ノート一括のクオンタイズ処理そのものではない。
- **スケール補正**(要件4.5): Milestone 5のスコープ。
- **MIDI書き出し**(要件4.8): Milestone 6のスコープ。
- **元録音音声の同期再生**: 要件4.6は「内蔵ピアノ風音色でのMIDIノート再生」のみを求めており、元の録音音声をプレビュー再生と同期させる機能は要件に含まれない(ブレスト時のQ1でユーザーが明示的に選択しなかった拡張)。将来的にMelodyneのような「元の声と補正後MIDIを聴き比べる」機能が欲しくなった場合の将来検討事項の候補として記録しておく(要件からの削除ではなく、単に要件に含まれていない旨の確認)。
- **再生ヘッドのシーク操作**(ルーラークリックでの位置移動): 要件に明記なし、本マイルストーンでは実装しない(5.8節)。
- **鍵盤(`PianoKeyboardComponent`)のクリック再生**: 要件が求めるのは「ノートクリックでのプレビュー」であり、鍵盤自体のクリック再生は要件にない機能追加のため実装しない(5.2節)。
- 要件書7章のスコープ外事項(永続化、ポリフォニック対応、スケール補正強度調整、信頼度可視化、ベロシティ連動、キー/スケール自動推定)は、本設計でも対象としない。
