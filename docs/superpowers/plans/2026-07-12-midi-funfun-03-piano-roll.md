# MIDI FUNFUN Milestone 3: ピアノロール Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development (recommended) or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 設計スペック([2026-07-12-midi-funfun-milestone3-piano-roll-design.md](../specs/2026-07-12-midi-funfun-milestone3-piano-roll-design.md))記載のCoreクラス群(`Note`拡張/`NoteSequence`拡張・`Selection`・`EditCommands`各種・`GridSnap`・`PitchAnalyzer`戻り値変更・`WaveformPeakCache`・`PlaybackTransport`・`PianoVoice`/`PianoSound`)をTDDで実装し、新設`Source/UI/`配下(`PianoRollComponent`+ネスト`Canvas`・`PianoKeyboardComponent`・`PlaybackTransportBar`)とPluginレイヤーに配線して、スタンドアロンで実際に「録音→解析→ピアノロールでのノート表示・移動・リサイズ・削除・追加・選択・Undo/Redo・波形/ピッチカーブ表示・プレビュー再生・通し再生」まで一通り動作する状態にする。

**Architecture:** 設計スペック通り。`PluginProcessor`が編集中ノート列(`editedNotes`)・Undo履歴(`undoManager`)・直近解析結果(`lastAnalysisResult`)を所有し、`PluginEditor`/`Source/UI/`が選択状態・ズーム/スクロール位置を所有する(スペック§2.1)。ノート編集用`juce::UndoManager`はピアノロールのノート編集操作のみを管理し、Milestone 1由来のテイク管理とは配線上も完全に分離する(スペック§1.4)。UIレイヤーは新設の`Source/UI/`ディレクトリに切り出すが、CMakeターゲットとしては既存の`MidiFunfun`ターゲットへソース追加する形で1つにまとめる(新規ターゲットは立てない、スペック§6.2)。

**Tech Stack:** JUCE (C++20/CMake)、Catch2(Core層のTDD)、`juce_data_structures`(本マイルストーンで新規リンク、`UndoManager`/`UndoableAction`用)。

## API確定(設計スペックの行間を実装レベルで確定する)

設計スペックが名前・振る舞いのみを定め、正確な数値・アルゴリズムを確定していない箇所をここで固定する。以降のタスクはこれらの値をそのまま使う(タスク実装中に再検討しない)。

### 1. Canvas座標系・ピクセル/ズーム数値

`Source/UI/PianoRollComponent.h`の`Canvas`ネストクラスが使う座標変換とレイアウト定数を以下の通り確定する。

```cpp
// Canvas座標系:
//   x = 0 は tick = 0(Canvas左端。鍵盤は別コンポーネントでCanvasの外)。
//   y = 0 はCanvas上端 = ルーラー帯の開始。ルーラー帯の高さは rulerHeightPx。
//   ルーラー帯の下(y >= rulerHeightPx)がノートグリッド本体。

constexpr int rulerHeightPx = 24;
constexpr int keyboardWidthPx = 60;          // PianoKeyboardComponentの固定幅
constexpr int resizeHandleWidthPx = 6;       // ノート右端のリサイズハンドル判定幅
constexpr int minNoteWidthForResizePx = 12;  // これ未満の描画幅のノートはリサイズハンドルを持たない(移動のみ)

constexpr double defaultPixelsPerTick = 0.25;   // 四分音符(480 tick) = 120px
constexpr double minPixelsPerTick = 0.05;       // 四分音符 = 24px(最大縮小)
constexpr double maxPixelsPerTick = 1.0;        // 四分音符 = 480px(最大拡大)

constexpr double defaultPixelsPerSemitone = 16.0;
constexpr double minPixelsPerSemitone = 6.0;
constexpr double maxPixelsPerSemitone = 32.0;

// 座標変換(Canvasのprivateメソッドとして実装する):
int tickToX(juce::int64 tick) const;                 // (int) std::round(tick * pixelsPerTick)
juce::int64 xToTick(int x) const;                     // (int64) std::round(x / pixelsPerTick)
int pitchToY(int pitch) const;                        // rulerHeightPx + (int) std::round((highestVisiblePitch - pitch) * pixelsPerSemitone)
int yToPitch(int y) const;                            // highestVisiblePitch - (int) std::floor((y - rulerHeightPx) / pixelsPerSemitone)
```

- **重要(入れ子クラスのアクセス制御)**: `Canvas`はスペック§5.1の通り`PianoRollComponent`の`private`ネスト クラスである。そのため`minPixelsPerTick`/`maxPixelsPerTick`/`minPixelsPerSemitone`/`maxPixelsPerSemitone`(ズーム境界。`PluginEditor`がスライダーの範囲設定に必要、Task 18参照)は`Canvas`のメンバとしてではなく、`Source/UI/PianoRollComponent.h`の`midi_funfun::ui`名前空間スコープ(クラス宣言より前)に定数として置く。`Canvas`の各メソッドは同じ名前空間内なので修飾なしでそのまま参照できる。`rulerHeightPx`は`Canvas`内部でのみ使うため`Canvas`のメンバのままでよい(外部から`Canvas::`で参照する必要が無いため、入れ子クラスの`private`制約に抵触しない)。
- `highestVisiblePitch`/`lowestVisiblePitch`はCanvasのメンバ変数(自動フィット・手動ズームで更新、18節参照)。
- Canvas全体のサイズ(`setSize()`に渡す値): 幅 = `tickToX(timelineEndTick)`、高さ = `rulerHeightPx + (highestVisiblePitch - lowestVisiblePitch + 1) * pixelsPerSemitone`。
- `timelineEndTick`の決め方: `max(editedNotesの最終ノート終端tick, 解析元Takeの長さをtick換算した値, minTimelineTicks) + tailMarginTicks`。`minTimelineTicks = 8小節分 = 8 * 4 * ticksPerQuarterNote = 15360`。`tailMarginTicks = 1小節分 = 4 * ticksPerQuarterNote = 1920`。ノートが1つも無い・Takeも無い場合は`minTimelineTicks`のみを使う。
- ノートの矩形: `juce::Rectangle<int>(tickToX(note.startTick), pitchToY(note.pitch), tickToX(note.startTick + note.lengthTicks) - tickToX(note.startTick), (int) pixelsPerSemitone)`。

### 2. 自動フィット(縦方向)・ズーム境界

- 解析完了時(`editedNotes`が空でなくなった直後): `editedNotes`内の最小pitch `minP`・最大pitch `maxP`を求め、`autoFitMarginSemitones = 4`を上下に加える。結果の範囲`(maxP + 4) - (minP - 4) + 1`が`autoFitMinSpanSemitones = 24`(2オクターブ)未満なら、範囲の中心を保ったまま24半音まで対称に広げる。`highestVisiblePitch = min(127, maxP + 4 (+広げ分))`、`lowestVisiblePitch = max(0, minP - 4 (-広げ分))`。
- `editedNotes`が空(まだ一度も解析していない)場合の初期値: `highestVisiblePitch = 72`(C5)、`lowestVisiblePitch = 48`(C3)(2オクターブ、A4=69を含む中庸な初期範囲)。
- ズーム変更(縦・横とも)は`pixelsPerTick`/`pixelsPerSemitone`を`juce::jlimit(min, max, newValue)`でクランプしてから適用し、`Canvas::setSize()`し直す。

### 3. マウス操作の細則(スペック§5.3の表を実装レベルで確定)

- **クリック vs ドラッグの判定**: `mouseUp(e)`内で`e.mouseWasDraggedSinceMouseDown()`を使う。`false`ならクリック(選択+プレビュー、Undoなし)、`true`ならその`mouseDown`開始時に決定した`DragMode`に応じたActionをコミットする。
- **`mouseDown`時点での選択状態の決定**(ドラッグかクリックかが判明する前に、対象ノート集合を先に確定する):
  - ノート本体上でmouseDown、かつ`e.mods.isCtrlDown()`: そのノートidを`selection.toggle(id)`。
  - ノート本体上でmouseDown、かつ`e.mods.isShiftDown()`: `selection.addRange({id})`(既に選択済みなら変化なし)。
  - ノート本体上でmouseDown、修飾キーなし: そのidが**既に選択済みでなければ** `selection.set(id)`(選択を単一ノートに置き換える)。**既に選択済み**(複数選択の一部)なら選択はそのまま変更しない(→ドラッグで複数選択全体を動かせるようにするため)。
  - この時点で確定した選択集合が、後続の`MoveNotesAction`/`ResizeNoteAction`(リサイズは掴んだ1ノートのみ、スペック3.4節)の対象になる。
  - 空白部分でのmouseDown: 修飾キーに関わらず矩形マーキードラッグの開始として扱う(選択はまだ変更しない。確定はmouseUp時、5節参照)。
- **リサイズハンドルの判定**: ノート矩形の右端`resizeHandleWidthPx`px以内でのmouseDownをリサイズ開始とする。ただし、そのノートの描画幅が`minNoteWidthForResizePx`未満の場合はリサイズハンドルを認識せず、ノート全体を移動対象として扱う。
- **移動時のクランプ**: `newPitch`は`juce::jlimit(0, 127, ...)`、`newStartTick`は`std::max<juce::int64>(0, ...)`で常にクランプしてから`MoveNotesAction::Delta`を構築する(クランプはCanvas側の責務。`EditCommands`自体は渡された値をそのまま適用するのみで検証しない、スペック3.4節のシンプルな設計を維持する)。
- **リサイズ時のクランプ**: `newLengthTicks = std::max<juce::int64>(gridEnabled ? gridUnitTicks : 1, newLengthTicks)`(Grid ON時は最小1グリッド単位、OFF時は最小1 tick。ゼロ・負の長さを防ぐ)。
- **Grid ON/OFFの既定値**: `Canvas`の`gridEnabled`メンバは既定`true`。

### 4. マーキー(矩形)選択の確定

- 矩形とノート矩形の判定は`juce::Rectangle<int>::intersects()`(一部でも重なれば選択対象。全包含は要求しない — 一般的なDAWの挙動に合わせる)。
- マーキー確定(mouseUp時)は常に選択を**置き換える**(スペック本文・ブレスト記録に矩形選択と他選択操作の併用〈Shift+マーキーで追加、等〉についての言及が無いため、YAGNIで単純化する)。
- ドラッグ中はリアルタイムに交差ノートをハイライトプレビューする(スペック§1.1「矩形(マーキー)選択」)が、`selection`本体への反映は`mouseUp`時のみ(ドラッグ中は`Canvas`のローカル一時集合で描画するだけ)。

### 5. キーボードショートカット: キーコード確定

`Canvas::keyPressed(const juce::KeyPress& key)`で以下を判定する(`juce::ModifierKeys::commandModifier`はWindows/LinuxではCtrl、macOSではCmdに自動対応するJUCE標準の抽象化。本プロジェクトはWindows運用だが慣例通りこれを使う)。

```cpp
bool Canvas::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey)                                          { deleteSelected(); return true; }
    if (key == juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0))         { processor.getUndoManager().undo(); return true; }
    if (key == juce::KeyPress('y', juce::ModifierKeys::commandModifier, 0))         { processor.getUndoManager().redo(); return true; }
    if (key == juce::KeyPress('a', juce::ModifierKeys::commandModifier, 0))         { selection.selectAll(processor.getAnalyzedNotes()); repaint(); return true; }
    if (key == juce::KeyPress::upKey)                                              { nudge(1, 0); return true; }
    if (key == juce::KeyPress::downKey)                                            { nudge(-1, 0); return true; }
    if (key == juce::KeyPress::leftKey)                                            { nudge(0, -midi_funfun::core::gridUnitTicks); return true; }
    if (key == juce::KeyPress::rightKey)                                           { nudge(0, midi_funfun::core::gridUnitTicks); return true; }
    return false;
}
```

(`processor`は`Canvas`が保持する`MidiFunfunAudioProcessor&`メンバ。`selection`は`Canvas`が保持する`midi_funfun::core::Selection`メンバ。`sequence`という名前のメンバは存在しない — ノート列は常に`processor.getAnalyzedNotes()`〈読み取り〉/`processor.getEditedNotesForEditing()`〈EditCommands用の書き込み可能参照〉経由でアクセスする。)

`nudge(int semitoneDelta, juce::int64 tickDelta)`は選択中の全ノートについて`Delta{id, oldPitch, oldStartTick, jlimit(0,127,oldPitch+semitoneDelta), max<int64>(0,oldStartTick+tickDelta)}`を構築し、`processor.getUndoManager().perform(new midi_funfun::core::MoveNotesAction(processor.getEditedNotesForEditing(), deltas), "Nudge Notes")`する(スペック§1.3: ドラッグ移動と全く同じ`MoveNotesAction`を共有)。矢印キーの左右移動は`gridEnabled`の状態に関わらず常に`gridUnitTicks`固定(スペック§5.4に明記済み)。

`Canvas`は`setWantsKeyboardFocus(true)`をコンストラクタで呼び、`mouseDown()`冒頭で`grabKeyboardFocus()`する。

### 6. Undo transactionの単位

各コミット操作は`juce::UndoManager::perform(UndoableAction*, const juce::String& actionName)`(第2引数ありのオーバーロード。内部で`beginNewTransaction(actionName)`してから`perform()`する)を使う。これにより呼び出し1回=Undo 1ステップが保証される(手動での`beginNewTransaction()`呼び出しは不要)。使用する`actionName`文字列: `"Move Notes"`・`"Resize Note"`・`"Add Note"`・`"Delete Notes"`・`"Nudge Notes"`・`"Re-analyze"`。

### 7. `PitchAnalyzer::analyze()`再解析時の分岐(スペック§2.3の実装)

```cpp
if (editedNotes.size() == 0)
{
    editedNotes.replaceAll(lastAnalysisResult.notes.getNotes()); // Undoに積まない
}
else
{
    const auto oldNotes = editedNotes.getNotes();
    const auto newNotes = lastAnalysisResult.notes.getNotes();
    undoManager.perform(new midi_funfun::core::ReplaceAllNotesAction(editedNotes, oldNotes, newNotes), "Re-analyze");
}
```

`lastAnalysisResult`（`analyzedNotesBpm`・`analyzedTakeIndex`も含む、9節参照）は分岐に関わらず毎回上書きする(Undo対象外の生データ、スペック§1.4・§2.2)。

### 8. `PlaybackTransport`の内部アルゴリズム(スペックは公開APIのみを定義、内部スケジューリングをここで確定)

- `start(notes, bpm, sampleRate, startTick)`: `notes.getNotes()`の各ノートから NoteOn/NoteOff の2イベントを生成し、`{ samplePos, pitch, velocity, isNoteOn }`のリストとして保持する(`samplePos = round(tick / ticksPerQuarterNote * (60/bpm) * sampleRate)`)。イベント列を`samplePos`昇順、同一`samplePos`ではNoteOffを先(NoteOnを後)にして安定ソートする(重複ノートでの誤った順序を避ける)。`currentSamplePos`を`startTick`に対応するサンプル位置に設定し、そこ以降の最初のイベントを指す`nextEventIndex`を求める。`bpm`/`sampleRate`はメンバに保存し、以後の`getCurrentTick()`で使う。
- `advance(numSamples, sampleRate)`: `sampleRate`引数は`start()`に渡したものと同一である前提(呼び出し側の責務、Processorは常に`currentSampleRate`を渡す)。`[currentSamplePos, currentSamplePos + numSamples)`の範囲に入るイベントを`nextEventIndex`から順に取り出し、`sampleOffsetInBlock = (int)(event.samplePos - currentSamplePos)`(`event.samplePos`が`currentSamplePos`未満になることは無い、ループ不変条件)として`NoteEvent`を構築する。`currentSamplePos += numSamples`。ループ後に`nextEventIndex >= events.size()`(=すべてのイベントを消化済み)なら`state = State::Idle`にする。
- `getCurrentTick()`: `currentSamplePos`を保存済みの`bpm`/`sampleRate`で逆算する(`tick = round(samplePos / sampleRate * (bpm/60) * ticksPerQuarterNote)`)。

### 9. `PluginProcessor`の新規アクセサ確定

スペック§4.1が名前を挙げている箇所について、以下の正確なシグネチャで確定する:

```cpp
// 既存 getAnalyzedNotes() はそのまま名前を維持しつつ editedNotes を返すよう変更(スペック§4.1)
const midi_funfun::core::NoteSequence& getAnalyzedNotes() const { return editedNotes; }

// EditCommands各Actionのコンストラクタが要求する NoteSequence& を得るための新規アクセサ
midi_funfun::core::NoteSequence& getEditedNotesForEditing() { return editedNotes; }

juce::UndoManager& getUndoManager() { return undoManager; }

// lastAnalysisResult をまるごと返す(pitchFrames/hopSize/sampleRateの取得に使う。
// notesフィールドは解析直後のスナップショットであり編集を反映しないため、UI側は
// ノート表示には必ず getAnalyzedNotes()/getEditedNotesForEditing() を使うこと)。
const midi_funfun::core::PitchAnalysisResult& getLastAnalysisResult() const { return lastAnalysisResult; }

// 波形オーバーレイ用: 最後に解析されたテイクへの読み取り専用アクセス
const midi_funfun::core::Take* getWaveformSourceTake() const { return takeManager.getTake(analyzedTakeIndex); }

void triggerPreviewNote(int pitch, int velocity);
void startPlayback();
void stopPlayback();
midi_funfun::core::PlaybackTransport::State getPlaybackState() const { return playbackTransport.getState(); }
juce::int64 getPlaybackCurrentTick() const { return playbackTransport.getCurrentTick(); }
```

`analyzedTakeIndex`(新規`int`メンバ、既定`-1`)は`analyzeSelectedTake()`実行時に`takeManager.getSelectedTakeIndex()`の値へ毎回更新する(取消線: これにより、解析後にユーザーが一覧で別テイクを選び直しても波形オーバーレイは「実際に解析されたテイク」を表示し続ける)。

### 10. スレッド安全性の方針(既存コードの前例に合わせる)

- `PlaybackTransport::start()`/`stop()`・`editedNotes`へのUndo経由の変更は、既存の`RecordingTransport::startRecording()`/`stopRecording()`と同じ前例(状態遷移メソッドをGUIスレッドから直接呼ぶ、内部状態に`std::atomic`を使わない)に倣う。`PlaybackTransport::start()`が`notes`のスナップショットを同期的にコピーするため、コピー完了後は audio スレッドの`advance()`が触れるのは複製済みの内部データのみであり、`editedNotes`本体への並行アクセスは発生しない。
- 単音プレビュー再生のトリガーだけは、既存の`bpm`/`defaultVelocity`等と同じ形(GUIスレッドが値をセットし、audioスレッドが毎ブロック読む)の連続ポーリング用途のため、既存の`std::atomic`パターンを踏襲する: `std::atomic<int> pendingPreviewPitch { -1 }`・`std::atomic<int> pendingPreviewVelocity { 0 }`。`processBlock`側で`exchange(-1, ...)`して1回だけ消費する。

### 11. `PianoVoice`のエンベロープ・倍音構成の確定

スペック§3.9は「減衰する2〜3倍音構成の正弦波+シンプルなエンベロープ」とだけ述べており、正確な数値を以下で確定する:

- 倍音ゲイン(正規化前): 基音1.0・2倍音0.5・3倍音0.25(合計1.75で正規化してから合成波形の振幅とする)。
- Attack: 5ms、0→1へ線形。
- Decay: Attack完了後、ノート保持中は半減期0.5秒で指数減衰し続ける(ピアノの自然減衰を模す。`stopNote`が来なくても減衰し続ける)。
- Release: `stopNote(velocity, allowTailOff=true)`で現在のenvelopeLevelから80msかけて線形にゼロへ。`allowTailOff=false`は即座にゼロにして`clearCurrentNote()`する(このパスがTask 8のテストで検証する「即座に無音になる」経路)。
- 周波数: `juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber)`(既存コードが使うA4=440基準と一致させる、独自計算しない)。
- ボイス数: `juce::Synthesiser`に`PianoVoice`を16個登録(スペック§4.1「目安16」を16に確定)、`setNoteStealingEnabled(true)`を呼ぶ(全ボイス使用中でも新規ノートが無音落ちしないようにする)。
- `renderNextBlock`は出力バッファの**全チャンネル**に対して同一モノラル信号を**加算**(`addSample`、上書きしない)する。これは`processBlock`側で既存のモニタリング/クリック音ミックスと同じ「既存バッファへの加算」パターンに合わせるため(スペック§4.1)。

### 12. 単音プレビュー・通し再生の`processBlock`内アルゴリズム

`processBlock`の末尾(既存のクリック音ミックス`for`ループの後)に以下を追加する:

```cpp
// 単音プレビュー: GUIスレッドからの要求を1回だけ消費する
const int requestedPreviewPitch = pendingPreviewPitch.exchange(-1, std::memory_order_relaxed);
if (requestedPreviewPitch >= 0)
{
    if (activePreviewPitch >= 0)
        mainSynth.noteOff(1, activePreviewPitch, 1.0f, true);

    const int requestedVelocity = pendingPreviewVelocity.load(std::memory_order_relaxed);
    mainSynth.noteOn(1, requestedPreviewPitch, juce::jlimit(0.0f, 1.0f, (float) requestedVelocity / 127.0f));
    activePreviewPitch = requestedPreviewPitch;
    previewRemainingSamples = (int) std::round(0.4 * currentSampleRate); // 400ms、スペック§4.1
}

if (activePreviewPitch >= 0)
{
    previewRemainingSamples -= numSamples;
    if (previewRemainingSamples <= 0)
    {
        mainSynth.noteOff(1, activePreviewPitch, 1.0f, true);
        activePreviewPitch = -1;
    }
}

// 通し再生
juce::MidiBuffer emptyMidi; // noteOn/offを直接呼ぶ経路のため中身は使わない
const auto playbackEvents = playbackTransport.advance(numSamples, currentSampleRate);
int renderedUpTo = 0;
for (const auto& evt : playbackEvents)
{
    if (evt.sampleOffsetInBlock > renderedUpTo)
    {
        mainSynth.renderNextBlock(buffer, emptyMidi, renderedUpTo, evt.sampleOffsetInBlock - renderedUpTo);
        renderedUpTo = evt.sampleOffsetInBlock;
    }

    if (evt.isNoteOn)
        mainSynth.noteOn(1, evt.pitch, juce::jlimit(0.0f, 1.0f, (float) evt.velocity / 127.0f));
    else
        mainSynth.noteOff(1, evt.pitch, 1.0f, true);
}
mainSynth.renderNextBlock(buffer, emptyMidi, renderedUpTo, numSamples - renderedUpTo);
```

`stopPlayback()`は`playbackTransport.stop()`に加えて`mainSynth.allNotesOff(1, true)`(tail-off有り、急なクリックを避ける)を呼ぶ。

## Global Constraints

- コミットはこのマイルストーン全体で1回のみ(Task 21の最後)。
- Core層(`Source/Core/`配下)の新規クラスは全てTDD(先に失敗するテストを書く)。UI層(`Source/UI/`・`Source/Plugin/`)は既存方針(M1/M2)通り自動テスト対象外、Standaloneアプリの実機確認で検証する。
- 新規依存: `juce_data_structures`を`Source/Core/CMakeLists.txt`に追加(Task 3)。それ以外の新規JUCEモジュール追加は不要(`PianoVoice`等は既存の`juce_audio_basics`で足りる)。
- **文字化け防止(M1/M2で判明した既知の落とし穴)**: `juce::String::formatted`の`%s`にnarrow char*/`toRawUTF8()`を直接渡さない。文字列はすべて`juce::String`の連結で構築する。本マイルストーンで新規に文字列を組み立てる箇所(ツールバー・`PlaybackTransportBar`のラベル等)はすべてこの方針に従う。
- **明示的な`repaint()`(M2で判明した既知の落とし穴)**: JUCEのコンポーネントはデータが変わっただけでは自動再描画されない。`updateContent()`相当の処理をしてもrepaintは自動発火しない。本マイルストーンでは特に以下の3箇所で明示的な`repaint()`が必須(Task 16で集中的に扱うが、関連する他タスクでも都度呼ぶこと): (a) `undoManager`の`ChangeListener`コールバック内、(b) 再生ヘッド用`Timer`(既存Editorの30Hz Timerに相乗り、`playbackTransport.getCurrentTick()`が前回値と異なる場合のみ`repaint()`)、(c) ノート編集(ドラッグ確定・追加・削除)直後。
- 既存の`NoteListBoxModel`/`analysisStatusLabel`/`Source/Core/Model/NoteFormatting.h`はTask 20で削除する(新パネル実装完了と同じコミット内で削除、並存期間を作らない、スペック§5.9)。

---

### Task 1: `Note::id` + `NoteSequence`変更API

**Files:**
- Modify: `Source/Core/Model/Note.h`, `Source/Core/Model/NoteSequence.h`, `Source/Core/Model/NoteSequence.cpp`, `Tests/NoteSequenceTests.cpp`

**Interfaces:**
- Produces: `Note::id`(`juce::int64`、既定0)。`NoteSequence::add(Note) -> juce::int64`・`findById(juce::int64) -> Note*/const Note*`・`removeById(juce::int64) -> bool`・`replaceAll(const std::vector<Note>&)`。

- [ ] **Step 1: `Note`に`id`フィールドを追加**

`Source/Core/Model/Note.h`の`struct Note`に`juce::int64 id = 0;`を追加する(API確定なし、スペック§3.1そのまま)。

- [ ] **Step 2: 失敗するテストを書く(id採番・findById・removeById・replaceAll)**

`Tests/NoteSequenceTests.cpp`に以下を追記する:

```cpp
TEST_CASE("NoteSequence::add assigns increasing ids when note.id == 0", "[NoteSequence]")
{
    NoteSequence seq;
    const auto id1 = seq.add(Note { 60, 0, 480, 90 });
    const auto id2 = seq.add(Note { 64, 480, 240, 100 });

    REQUIRE(id1 != 0);
    REQUIRE(id2 != 0);
    REQUIRE(id1 != id2);
    REQUIRE(seq.findById(id1)->pitch == 60);
    REQUIRE(seq.findById(id2)->pitch == 64);
}

TEST_CASE("NoteSequence::add reuses a non-zero id and advances the counter past it", "[NoteSequence]")
{
    NoteSequence seq;
    Note explicitIdNote { 60, 0, 480, 90 };
    explicitIdNote.id = 500;
    REQUIRE(seq.add(explicitIdNote) == 500);

    const auto nextId = seq.add(Note { 64, 480, 240, 100 });
    REQUIRE(nextId > 500);
}

TEST_CASE("NoteSequence::findById returns nullptr for a missing id", "[NoteSequence]")
{
    NoteSequence seq;
    REQUIRE(seq.findById(999) == nullptr);
}

TEST_CASE("NoteSequence::removeById removes the matching note and returns true", "[NoteSequence]")
{
    NoteSequence seq;
    const auto id = seq.add(Note { 60, 0, 480, 90 });
    seq.add(Note { 64, 480, 240, 100 });

    REQUIRE(seq.removeById(id));
    REQUIRE(seq.size() == 1);
    REQUIRE(seq.findById(id) == nullptr);
}

TEST_CASE("NoteSequence::removeById returns false for a missing id", "[NoteSequence]")
{
    NoteSequence seq;
    seq.add(Note { 60, 0, 480, 90 });
    REQUIRE_FALSE(seq.removeById(999));
    REQUIRE(seq.size() == 1);
}

TEST_CASE("NoteSequence::replaceAll keeps ids from the replacement and advances the counter", "[NoteSequence]")
{
    NoteSequence seq;
    seq.add(Note { 60, 0, 480, 90 }); // id 1

    Note replacement { 72, 0, 240, 80 };
    replacement.id = 42;
    seq.replaceAll({ replacement });

    REQUIRE(seq.size() == 1);
    REQUIRE(seq.findById(42)->pitch == 72);

    const auto nextId = seq.add(Note { 64, 480, 240, 100 });
    REQUIRE(nextId > 42);
}
```

- [ ] **Step 3: テストが失敗することを確認**

Run: `cmake --build build --config Debug --target MidiFunfunTests` (ビルド失敗: `add`の戻り値型・`findById`/`removeById`/`replaceAll`が未定義)

- [ ] **Step 4: `NoteSequence.h`/`.cpp`を実装**

`Source/Core/Model/NoteSequence.h`:

```cpp
#pragma once

#include <vector>

#include "Note.h"

namespace midi_funfun::core
{
    class NoteSequence
    {
    public:
        void clear();

        /** note.id == 0 なら新規idを採番して追加し、そのidを返す。
         *  note.id != 0 ならそのidをそのまま使用して追加し、内部の採番カウンタを id 以上に更新する。 */
        juce::int64 add(Note note);

        int size() const;
        const Note& operator[](int index) const;
        const std::vector<Note>& getNotes() const;

        Note* findById(juce::int64 id);
        const Note* findById(juce::int64 id) const;
        bool removeById(juce::int64 id);
        void replaceAll(const std::vector<Note>& newNotes);

    private:
        std::vector<Note> notes;
        juce::int64 nextId = 1;
    };
}
```

`Source/Core/Model/NoteSequence.cpp`:

```cpp
#include "NoteSequence.h"

#include <algorithm>

namespace midi_funfun::core
{
    void NoteSequence::clear()
    {
        notes.clear();
    }

    juce::int64 NoteSequence::add(Note note)
    {
        if (note.id == 0)
            note.id = nextId++;
        else
            nextId = std::max(nextId, note.id + 1);

        notes.push_back(note);
        return note.id;
    }

    int NoteSequence::size() const
    {
        return (int) notes.size();
    }

    const Note& NoteSequence::operator[](int index) const
    {
        return notes[(size_t) index];
    }

    const std::vector<Note>& NoteSequence::getNotes() const
    {
        return notes;
    }

    Note* NoteSequence::findById(juce::int64 id)
    {
        for (auto& n : notes)
            if (n.id == id)
                return &n;
        return nullptr;
    }

    const Note* NoteSequence::findById(juce::int64 id) const
    {
        for (const auto& n : notes)
            if (n.id == id)
                return &n;
        return nullptr;
    }

    bool NoteSequence::removeById(juce::int64 id)
    {
        const auto it = std::find_if(notes.begin(), notes.end(), [id](const Note& n) { return n.id == id; });
        if (it == notes.end())
            return false;
        notes.erase(it);
        return true;
    }

    void NoteSequence::replaceAll(const std::vector<Note>& newNotes)
    {
        notes = newNotes;
        juce::int64 maxId = 0;
        for (const auto& n : notes)
            maxId = std::max(maxId, n.id);
        nextId = std::max(nextId, maxId + 1);
    }
}
```

- [ ] **Step 5: テストが通ることを確認**

Run: `cmake --build build --config Debug --target MidiFunfunTests && ctest --test-dir build -C Debug -R NoteSequence --output-on-failure`
Expected: PASS(既存3件+新規6件)

- [ ] **Step 6: コミットしない(Global Constraints通りTask 21でまとめて1コミット)**

---

### Task 2: `Selection`

**Files:**
- Create: `Source/Core/Model/Selection.h`, `Source/Core/Model/Selection.cpp`, `Tests/SelectionTests.cpp`
- Modify: `Source/Core/CMakeLists.txt`, `Tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `NoteSequence::getNotes() -> const std::vector<Note>&`(Task 1)。
- Produces: `Selection`クラス(`clear`/`set`/`toggle`/`addRange`/`selectAll`/`isSelected`/`isEmpty`/`getSelectedIds`/`pruneMissing`)。Task 12以降のCanvasマウス/キーボード処理が直接使う。

- [ ] **Step 1: 失敗するテストを書く**

`Tests/SelectionTests.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "Model/Selection.h"

using midi_funfun::core::Note;
using midi_funfun::core::NoteSequence;
using midi_funfun::core::Selection;

TEST_CASE("Selection starts empty", "[Selection]")
{
    Selection sel;
    REQUIRE(sel.isEmpty());
    REQUIRE(sel.getSelectedIds().empty());
}

TEST_CASE("Selection::set replaces the entire selection with a single id", "[Selection]")
{
    Selection sel;
    sel.set(1);
    sel.set(2);
    REQUIRE(sel.getSelectedIds() == std::set<juce::int64> { 2 });
}

TEST_CASE("Selection::toggle adds then removes an id", "[Selection]")
{
    Selection sel;
    sel.toggle(5);
    REQUIRE(sel.isSelected(5));
    sel.toggle(5);
    REQUIRE_FALSE(sel.isSelected(5));
}

TEST_CASE("Selection::addRange unions ids without clearing existing selection", "[Selection]")
{
    Selection sel;
    sel.set(1);
    sel.addRange({ 2, 3 });
    REQUIRE(sel.isSelected(1));
    REQUIRE(sel.isSelected(2));
    REQUIRE(sel.isSelected(3));
}

TEST_CASE("Selection::selectAll selects every id present in the sequence", "[Selection]")
{
    NoteSequence seq;
    const auto id1 = seq.add(Note { 60, 0, 480, 90 });
    const auto id2 = seq.add(Note { 64, 480, 240, 100 });

    Selection sel;
    sel.selectAll(seq);
    REQUIRE(sel.isSelected(id1));
    REQUIRE(sel.isSelected(id2));
}

TEST_CASE("Selection::clear empties the selection", "[Selection]")
{
    Selection sel;
    sel.set(1);
    sel.clear();
    REQUIRE(sel.isEmpty());
}

TEST_CASE("Selection::pruneMissing removes ids no longer present in the sequence", "[Selection]")
{
    NoteSequence seq;
    const auto id1 = seq.add(Note { 60, 0, 480, 90 });

    Selection sel;
    sel.set(id1);
    sel.addRange({ 999 }); // idが存在しないノート

    sel.pruneMissing(seq);
    REQUIRE(sel.isSelected(id1));
    REQUIRE_FALSE(sel.isSelected(999));
}
```

- [ ] **Step 2: `Tests/CMakeLists.txt`に`SelectionTests.cpp`を追加し、ビルド失敗を確認**

`add_executable(MidiFunfunTests ...)`のリストに`SelectionTests.cpp`を追加。

Run: `cmake --build build --config Debug --target MidiFunfunTests`
Expected: FAIL(`Model/Selection.h`が存在しない)

- [ ] **Step 3: `Selection.h`/`.cpp`を実装**

`Source/Core/Model/Selection.h`:

```cpp
#pragma once

#include <set>
#include <vector>

#include <juce_core/juce_core.h>

#include "NoteSequence.h"

namespace midi_funfun::core
{
    /** ピアノロールの選択状態。GUI非依存の純粋ロジック(スペック3.3節)。 */
    class Selection
    {
    public:
        void clear();
        void set(juce::int64 id);
        void toggle(juce::int64 id);
        void addRange(const std::vector<juce::int64>& ids);
        void selectAll(const NoteSequence& sequence);
        bool isSelected(juce::int64 id) const;
        bool isEmpty() const;
        const std::set<juce::int64>& getSelectedIds() const;
        void pruneMissing(const NoteSequence& sequence);

    private:
        std::set<juce::int64> selectedIds;
    };
}
```

`Source/Core/Model/Selection.cpp`:

```cpp
#include "Selection.h"

namespace midi_funfun::core
{
    void Selection::clear()
    {
        selectedIds.clear();
    }

    void Selection::set(juce::int64 id)
    {
        selectedIds.clear();
        selectedIds.insert(id);
    }

    void Selection::toggle(juce::int64 id)
    {
        if (selectedIds.count(id) > 0)
            selectedIds.erase(id);
        else
            selectedIds.insert(id);
    }

    void Selection::addRange(const std::vector<juce::int64>& ids)
    {
        for (const auto id : ids)
            selectedIds.insert(id);
    }

    void Selection::selectAll(const NoteSequence& sequence)
    {
        selectedIds.clear();
        for (const auto& note : sequence.getNotes())
            selectedIds.insert(note.id);
    }

    bool Selection::isSelected(juce::int64 id) const
    {
        return selectedIds.count(id) > 0;
    }

    bool Selection::isEmpty() const
    {
        return selectedIds.empty();
    }

    const std::set<juce::int64>& Selection::getSelectedIds() const
    {
        return selectedIds;
    }

    void Selection::pruneMissing(const NoteSequence& sequence)
    {
        std::set<juce::int64> stillPresent;
        for (const auto& note : sequence.getNotes())
            if (selectedIds.count(note.id) > 0)
                stillPresent.insert(note.id);
        selectedIds = std::move(stillPresent);
    }
}
```

- [ ] **Step 4: `Source/Core/CMakeLists.txt`に`Model/Selection.h`/`Model/Selection.cpp`を追加**

- [ ] **Step 5: テストが通ることを確認**

Run: `cmake --build build --config Debug --target MidiFunfunTests && ctest --test-dir build -C Debug -R Selection --output-on-failure`
Expected: PASS(7件)

---

### Task 3: `Model/EditCommands`各クラス + `juce_data_structures`リンク追加

**Files:**
- Create: `Source/Core/Model/EditCommands.h`, `Source/Core/Model/EditCommands.cpp`, `Tests/EditCommandsTests.cpp`
- Modify: `Source/Core/CMakeLists.txt`, `Tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `NoteSequence::add/findById/removeById/replaceAll`(Task 1)。
- Produces: `MoveNotesAction`・`ResizeNoteAction`・`AddNoteAction`・`DeleteNotesAction`・`ReplaceAllNotesAction`(全て`juce::UndoableAction`派生、Task 9のProcessor組み込み・Task 12〜16のUI層が`new`して`undoManager.perform(action, name)`する)。

- [ ] **Step 1: `Source/Core/CMakeLists.txt`に`juce::juce_data_structures`をリンク追加**

```cmake
target_link_libraries(midi_funfun_core PUBLIC
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_dsp
    juce::juce_data_structures
)
```

- [ ] **Step 2: 失敗するテストを書く**

`Tests/EditCommandsTests.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "Model/EditCommands.h"

using namespace midi_funfun::core;

TEST_CASE("MoveNotesAction perform/undo round-trips pitch and startTick", "[EditCommands]")
{
    NoteSequence seq;
    const auto id = seq.add(Note { 60, 0, 480, 90 });

    MoveNotesAction action(seq, { { id, 60, 0, 64, 240 } });
    REQUIRE(action.perform());
    REQUIRE(seq.findById(id)->pitch == 64);
    REQUIRE(seq.findById(id)->startTick == 240);

    REQUIRE(action.undo());
    REQUIRE(seq.findById(id)->pitch == 60);
    REQUIRE(seq.findById(id)->startTick == 0);
}

TEST_CASE("ResizeNoteAction perform/undo round-trips lengthTicks", "[EditCommands]")
{
    NoteSequence seq;
    const auto id = seq.add(Note { 60, 0, 480, 90 });

    ResizeNoteAction action(seq, id, 480, 960);
    REQUIRE(action.perform());
    REQUIRE(seq.findById(id)->lengthTicks == 960);

    REQUIRE(action.undo());
    REQUIRE(seq.findById(id)->lengthTicks == 480);
}

TEST_CASE("AddNoteAction perform assigns an id and undo removes it", "[EditCommands]")
{
    NoteSequence seq;
    AddNoteAction action(seq, Note { 67, 0, 240, 90 });

    REQUIRE(action.perform());
    REQUIRE(seq.size() == 1);

    REQUIRE(action.undo());
    REQUIRE(seq.size() == 0);
}

TEST_CASE("AddNoteAction redo (perform again) re-adds the same id", "[EditCommands]")
{
    NoteSequence seq;
    AddNoteAction action(seq, Note { 67, 0, 240, 90 });

    REQUIRE(action.perform());
    const auto id = seq.getNotes()[0].id;
    REQUIRE(action.undo());
    REQUIRE(action.perform());
    REQUIRE(seq.getNotes()[0].id == id);
}

TEST_CASE("DeleteNotesAction perform removes and undo restores with original id", "[EditCommands]")
{
    NoteSequence seq;
    const auto id = seq.add(Note { 60, 0, 480, 90 });
    Note snapshot = *seq.findById(id);

    DeleteNotesAction action(seq, { snapshot });
    REQUIRE(action.perform());
    REQUIRE(seq.size() == 0);

    REQUIRE(action.undo());
    REQUIRE(seq.size() == 1);
    REQUIRE(seq.findById(id) != nullptr);
    REQUIRE(seq.findById(id)->pitch == 60);
}

TEST_CASE("ReplaceAllNotesAction perform/undo round-trips the whole sequence", "[EditCommands]")
{
    NoteSequence seq;
    seq.add(Note { 60, 0, 480, 90 });
    const auto oldNotes = seq.getNotes();

    std::vector<Note> newNotes { Note { 72, 0, 240, 80 } };

    ReplaceAllNotesAction action(seq, oldNotes, newNotes);
    REQUIRE(action.perform());
    REQUIRE(seq.size() == 1);
    REQUIRE(seq.getNotes()[0].pitch == 72);

    REQUIRE(action.undo());
    REQUIRE(seq.size() == 1);
    REQUIRE(seq.getNotes()[0].pitch == 60);
}
```

- [ ] **Step 3: `Tests/CMakeLists.txt`に`EditCommandsTests.cpp`を追加し、失敗を確認**

Run: `cmake --build build --config Debug --target MidiFunfunTests`
Expected: FAIL(`Model/EditCommands.h`が存在しない)

- [ ] **Step 4: `EditCommands.h`/`.cpp`を実装**

`Source/Core/Model/EditCommands.h`(スペック§3.4のシグネチャそのまま):

```cpp
#pragma once

#include <vector>

#include <juce_data_structures/juce_data_structures.h>

#include "NoteSequence.h"

namespace midi_funfun::core
{
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

    class AddNoteAction final : public juce::UndoableAction
    {
    public:
        AddNoteAction(NoteSequence& sequenceIn, Note noteToAddIn);
        bool perform() override;
        bool undo() override;
    private:
        NoteSequence& sequence;
        Note noteToAdd;
    };

    class DeleteNotesAction final : public juce::UndoableAction
    {
    public:
        DeleteNotesAction(NoteSequence& sequenceIn, std::vector<Note> notesToDeleteIn);
        bool perform() override;
        bool undo() override;
    private:
        NoteSequence& sequence;
        std::vector<Note> notesToDelete;
    };

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
}
```

`Source/Core/Model/EditCommands.cpp`:

```cpp
#include "EditCommands.h"

namespace midi_funfun::core
{
    MoveNotesAction::MoveNotesAction(NoteSequence& sequenceIn, std::vector<Delta> deltasIn)
        : sequence(sequenceIn), deltas(std::move(deltasIn))
    {
    }

    bool MoveNotesAction::perform()
    {
        for (const auto& d : deltas)
            if (auto* note = sequence.findById(d.id))
            {
                note->pitch = d.newPitch;
                note->startTick = d.newStartTick;
            }
        return true;
    }

    bool MoveNotesAction::undo()
    {
        for (const auto& d : deltas)
            if (auto* note = sequence.findById(d.id))
            {
                note->pitch = d.oldPitch;
                note->startTick = d.oldStartTick;
            }
        return true;
    }

    ResizeNoteAction::ResizeNoteAction(NoteSequence& sequenceIn, juce::int64 idIn,
                                        juce::int64 oldLengthTicksIn, juce::int64 newLengthTicksIn)
        : sequence(sequenceIn), id(idIn), oldLengthTicks(oldLengthTicksIn), newLengthTicks(newLengthTicksIn)
    {
    }

    bool ResizeNoteAction::perform()
    {
        if (auto* note = sequence.findById(id))
            note->lengthTicks = newLengthTicks;
        return true;
    }

    bool ResizeNoteAction::undo()
    {
        if (auto* note = sequence.findById(id))
            note->lengthTicks = oldLengthTicks;
        return true;
    }

    AddNoteAction::AddNoteAction(NoteSequence& sequenceIn, Note noteToAddIn)
        : sequence(sequenceIn), noteToAdd(noteToAddIn)
    {
    }

    bool AddNoteAction::perform()
    {
        noteToAdd.id = sequence.add(noteToAdd);
        return true;
    }

    bool AddNoteAction::undo()
    {
        sequence.removeById(noteToAdd.id);
        return true;
    }

    DeleteNotesAction::DeleteNotesAction(NoteSequence& sequenceIn, std::vector<Note> notesToDeleteIn)
        : sequence(sequenceIn), notesToDelete(std::move(notesToDeleteIn))
    {
    }

    bool DeleteNotesAction::perform()
    {
        for (const auto& n : notesToDelete)
            sequence.removeById(n.id);
        return true;
    }

    bool DeleteNotesAction::undo()
    {
        for (const auto& n : notesToDelete)
            sequence.add(n);
        return true;
    }

    ReplaceAllNotesAction::ReplaceAllNotesAction(NoteSequence& sequenceIn, std::vector<Note> oldNotesIn, std::vector<Note> newNotesIn)
        : sequence(sequenceIn), oldNotes(std::move(oldNotesIn)), newNotes(std::move(newNotesIn))
    {
    }

    bool ReplaceAllNotesAction::perform()
    {
        sequence.replaceAll(newNotes);
        return true;
    }

    bool ReplaceAllNotesAction::undo()
    {
        sequence.replaceAll(oldNotes);
        return true;
    }
}
```

- [ ] **Step 5: `Source/Core/CMakeLists.txt`に`Model/EditCommands.h`/`.cpp`を追加**

- [ ] **Step 6: テストが通ることを確認**

Run: `cmake --build build --config Debug --target MidiFunfunTests && ctest --test-dir build -C Debug -R EditCommands --output-on-failure`
Expected: PASS(6件)

---

### Task 4: `GridSnap`

**Files:**
- Create: `Source/Core/Model/GridSnap.h`, `Source/Core/Model/GridSnap.cpp`, `Tests/GridSnapTests.cpp`
- Modify: `Source/Core/CMakeLists.txt`, `Tests/CMakeLists.txt`

**Interfaces:**
- Produces: `constexpr juce::int64 gridUnitTicks = 120`・`juce::int64 snapTickToGrid(juce::int64 tick)`。Task 12(ドラッグ)・Task 14(新規追加)・API確定5節(nudge)が使う。

- [ ] **Step 1: 失敗するテストを書く**

`Tests/GridSnapTests.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "Model/GridSnap.h"

using midi_funfun::core::gridUnitTicks;
using midi_funfun::core::snapTickToGrid;

TEST_CASE("snapTickToGrid snaps exactly onto grid boundaries", "[GridSnap]")
{
    REQUIRE(snapTickToGrid(0) == 0);
    REQUIRE(snapTickToGrid(gridUnitTicks) == gridUnitTicks);
    REQUIRE(snapTickToGrid(gridUnitTicks * 2) == gridUnitTicks * 2);
}

TEST_CASE("snapTickToGrid rounds to the nearest boundary", "[GridSnap]")
{
    REQUIRE(snapTickToGrid(gridUnitTicks / 2 - 1) == 0); // 59 -> 0
    REQUIRE(snapTickToGrid(gridUnitTicks / 2) == gridUnitTicks); // 60 -> 120 (ちょうど半分は切り上げ)
    REQUIRE(snapTickToGrid(gridUnitTicks + 1) == gridUnitTicks); // 121 -> 120
}

TEST_CASE("snapTickToGrid never returns a negative tick", "[GridSnap]")
{
    REQUIRE(snapTickToGrid(0) >= 0);
}
```

- [ ] **Step 2: `Tests/CMakeLists.txt`に`GridSnapTests.cpp`を追加し、失敗を確認**

Run: `cmake --build build --config Debug --target MidiFunfunTests`
Expected: FAIL(`Model/GridSnap.h`が存在しない)

- [ ] **Step 3: `GridSnap.h`/`.cpp`を実装**

`Source/Core/Model/GridSnap.h`:

```cpp
#pragma once

#include <juce_core/juce_core.h>

#include "Note.h"

namespace midi_funfun::core
{
    /** Grid ON時、ドラッグ移動・リサイズ・新規追加の位置をこの単位に丸める(スペック§3.5)。
     *  クオンタイズ(Milestone 4)とは独立した値であり、共有しない。 */
    constexpr juce::int64 gridUnitTicks = ticksPerQuarterNote / 4; // 1/16音符 = 120 ticks @ PPQ480

    juce::int64 snapTickToGrid(juce::int64 tick);
}
```

`Source/Core/Model/GridSnap.cpp`:

```cpp
#include "GridSnap.h"

#include <algorithm>

namespace midi_funfun::core
{
    juce::int64 snapTickToGrid(juce::int64 tick)
    {
        const juce::int64 snapped = ((tick + gridUnitTicks / 2) / gridUnitTicks) * gridUnitTicks;
        return std::max<juce::int64>(0, snapped);
    }
}
```

- [ ] **Step 4: `Source/Core/CMakeLists.txt`に`Model/GridSnap.h`/`.cpp`を追加**

- [ ] **Step 5: テストが通ることを確認**

Run: `cmake --build build --config Debug --target MidiFunfunTests && ctest --test-dir build -C Debug -R GridSnap --output-on-failure`
Expected: PASS(3件)

---

### Task 5: `PitchAnalyzer`の戻り値を`PitchAnalysisResult`へ変更

**Files:**
- Modify: `Source/Core/Pitch/PitchAnalyzer.h`, `Source/Core/Pitch/PitchAnalyzer.cpp`, `Tests/PitchAnalyzerTests.cpp`

**Interfaces:**
- Produces: `PitchAnalysisResult { NoteSequence notes; std::vector<PitchFrame> pitchFrames; int hopSize; double sampleRate; }`。`PitchAnalyzer::analyze(const Take&) -> PitchAnalysisResult`。Task 9(Processor)・Task 17(ピッチカーブ描画)が使う。

- [ ] **Step 1: 既存テストを新しい戻り値型に追随させる(失敗させる)**

`Tests/PitchAnalyzerTests.cpp`を以下へ書き換える:

```cpp
#include <cmath>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>

#include "Audio/Take.h"
#include "Pitch/PitchAnalyzer.h"
#include "Pitch/YinPitchDetector.h"

using midi_funfun::core::Note;
using midi_funfun::core::PitchAnalyzer;
using midi_funfun::core::Take;
using midi_funfun::core::ticksPerQuarterNote;
using midi_funfun::core::YinPitchDetector;

TEST_CASE("PitchAnalyzer converts a synthetic sine-wave take into a single expected note", "[PitchAnalyzer]")
{
    constexpr double sampleRate = 44100.0;
    constexpr double frequencyHz = 440.0; // A4 -> MIDI note 69
    constexpr double durationSeconds = 1.0;
    constexpr double bpm = 120.0;

    const int numSamples = (int) (durationSeconds * sampleRate);

    Take take;
    take.sampleRate = sampleRate;
    take.numSamplesRecorded = numSamples;
    take.buffer.setSize(1, numSamples);

    auto* writePtr = take.buffer.getWritePointer(0);
    const double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * frequencyHz / sampleRate;
    double phase = 0.0;
    for (int i = 0; i < numSamples; ++i)
    {
        writePtr[i] = (float) (0.5 * std::sin(phase));
        phase += phaseIncrement;
    }

    PitchAnalyzer::Settings settings;
    settings.bpm = bpm;
    settings.defaultVelocity = 100;

    PitchAnalyzer analyzer(settings);
    const auto result = analyzer.analyze(take);
    const auto& notes = result.notes;

    REQUIRE(notes.size() == 1);

    const Note& note = notes[0];
    REQUIRE(note.pitch == 69); // A4
    REQUIRE(note.velocity == 100);
    REQUIRE(note.startTick == 0);

    const YinPitchDetector::Settings yinDefaults;
    const int expectedFrameCount = (numSamples - yinDefaults.windowSize) / yinDefaults.hopSize + 1;
    const juce::int64 expectedEndSample = (juce::int64) expectedFrameCount * (juce::int64) yinDefaults.hopSize;
    const double expectedLengthSeconds = (double) expectedEndSample / sampleRate;
    const auto expectedLengthTicks = (juce::int64) std::round(expectedLengthSeconds * (bpm / 60.0) * (double) ticksPerQuarterNote);

    REQUIRE(note.lengthTicks == expectedLengthTicks);

    REQUIRE_FALSE(result.pitchFrames.empty());
    REQUIRE(result.hopSize == yinDefaults.hopSize);
    REQUIRE(result.sampleRate == sampleRate);
}

TEST_CASE("PitchAnalyzer returns an empty sequence for an empty take", "[PitchAnalyzer]")
{
    Take take;
    take.sampleRate = 44100.0;
    take.numSamplesRecorded = 0;
    take.buffer.setSize(1, 0);

    PitchAnalyzer analyzer;
    const auto result = analyzer.analyze(take);

    REQUIRE(result.notes.size() == 0);
}
```

- [ ] **Step 2: ビルドが失敗することを確認**

Run: `cmake --build build --config Debug --target MidiFunfunTests`
Expected: FAIL(`analyze()`が`NoteSequence`を返すため`result.notes`が存在しない)

- [ ] **Step 3: `PitchAnalyzer.h`/`.cpp`を変更**

`Source/Core/Pitch/PitchAnalyzer.h`:

```cpp
#pragma once

#include "../Audio/Take.h"
#include "../Model/NoteSequence.h"
#include "NoteSegmenter.h"
#include "OctaveErrorCorrector.h"
#include "YinPitchDetector.h"

namespace midi_funfun::core
{
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
        struct Settings
        {
            YinPitchDetector::Settings yin;
            NoteSegmenter::Settings segmenter;
            OctaveErrorCorrector::Settings octaveCorrection;
            double bpm = 120.0;
            int defaultVelocity = 90;
        };

        explicit PitchAnalyzer(Settings settingsIn = {});

        /** Take(モノラル録音バッファ)を解析し、tick単位・デフォルトベロシティ適用済みのノート列と
         *  半音量子化前の生ピッチ曲線を返す。 */
        PitchAnalysisResult analyze(const Take& take) const;

    private:
        Settings settings;
    };
}
```

`Source/Core/Pitch/PitchAnalyzer.cpp`(変更点: 戻り値の構築のみ、内部処理は既存のまま):

```cpp
#include "PitchAnalyzer.h"

#include <cmath>

namespace midi_funfun::core
{
    namespace
    {
        juce::int64 ticksFromSamplePos(juce::int64 samplePos, double sampleRate, double bpm)
        {
            const double seconds = (double) samplePos / sampleRate;
            return (juce::int64) std::round(seconds * (bpm / 60.0) * (double) ticksPerQuarterNote);
        }
    }

    PitchAnalyzer::PitchAnalyzer(Settings settingsIn) : settings(settingsIn)
    {
    }

    PitchAnalysisResult PitchAnalyzer::analyze(const Take& take) const
    {
        PitchAnalysisResult result;

        if (take.numSamplesRecorded <= 0 || take.sampleRate <= 0.0)
            return result;

        YinPitchDetector yin(settings.yin);
        auto frames = yin.analyze(take.buffer.getReadPointer(0), take.numSamplesRecorded, take.sampleRate);

        NoteSegmenter segmenter(settings.segmenter);
        auto rawSegments = segmenter.segment(frames, settings.yin.hopSize, take.sampleRate);

        OctaveErrorCorrector corrector(settings.octaveCorrection);
        corrector.correct(rawSegments);

        for (const auto& segment : rawSegments)
        {
            const juce::int64 startSamplePos = (juce::int64) segment.startFrame * (juce::int64) settings.yin.hopSize;
            const juce::int64 endSamplePos = (juce::int64) (segment.startFrame + segment.lengthFrames)
                                              * (juce::int64) settings.yin.hopSize;

            const juce::int64 startTick = ticksFromSamplePos(startSamplePos, take.sampleRate, settings.bpm);
            const juce::int64 endTick = ticksFromSamplePos(endSamplePos, take.sampleRate, settings.bpm);

            Note note;
            note.pitch = segment.pitch;
            note.startTick = startTick;
            note.lengthTicks = endTick - startTick;
            note.velocity = settings.defaultVelocity;

            result.notes.add(note);
        }

        result.pitchFrames = std::move(frames);
        result.hopSize = settings.yin.hopSize;
        result.sampleRate = take.sampleRate;

        return result;
    }
}
```

- [ ] **Step 4: テストが通ることを確認**

Run: `cmake --build build --config Debug --target MidiFunfunTests && ctest --test-dir build -C Debug -R PitchAnalyzer --output-on-failure`
Expected: PASS(2件)

---

### Task 6: `WaveformPeakCache`

**Files:**
- Create: `Source/Core/Audio/WaveformPeakCache.h`, `Source/Core/Audio/WaveformPeakCache.cpp`, `Tests/WaveformPeakCacheTests.cpp`
- Modify: `Source/Core/CMakeLists.txt`, `Tests/CMakeLists.txt`

**Interfaces:**
- Produces: `struct PeakPair { float minValue; float maxValue; }`・`WaveformPeakCache::build(const float*, int, int) -> std::vector<PeakPair>`。Task 17(波形描画)が使う。

- [ ] **Step 1: 失敗するテストを書く**

`Tests/WaveformPeakCacheTests.cpp`:

```cpp
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Audio/WaveformPeakCache.h"

using midi_funfun::core::WaveformPeakCache;

TEST_CASE("WaveformPeakCache::build finds the known min/max within each column", "[WaveformPeakCache]")
{
    // 8サンプル、幅2px -> 各列4サンプル。列0に既知の最大値、列1に既知の最小値を仕込む。
    std::vector<float> samples = { 0.1f, 0.9f, 0.2f, -0.3f, -0.8f, 0.1f, 0.05f, -0.05f };

    const auto peaks = WaveformPeakCache::build(samples.data(), (int) samples.size(), 2);

    REQUIRE(peaks.size() == 2);
    REQUIRE(peaks[0].maxValue == 0.9f);
    REQUIRE(peaks[0].minValue == -0.3f);
    REQUIRE(peaks[1].maxValue == 0.1f);
    REQUIRE(peaks[1].minValue == -0.8f);
}

TEST_CASE("WaveformPeakCache::build handles more pixels than samples", "[WaveformPeakCache]")
{
    std::vector<float> samples = { 0.5f, -0.5f };
    const auto peaks = WaveformPeakCache::build(samples.data(), (int) samples.size(), 5);

    REQUIRE(peaks.size() == 5);
    for (const auto& p : peaks)
        REQUIRE(p.minValue <= p.maxValue);
}

TEST_CASE("WaveformPeakCache::build returns an empty vector for zero samples or zero width", "[WaveformPeakCache]")
{
    std::vector<float> samples = { 0.1f, 0.2f };
    REQUIRE(WaveformPeakCache::build(samples.data(), 0, 4).empty());
    REQUIRE(WaveformPeakCache::build(samples.data(), (int) samples.size(), 0).empty());
}
```

- [ ] **Step 2: `Tests/CMakeLists.txt`に`WaveformPeakCacheTests.cpp`を追加し、失敗を確認**

Run: `cmake --build build --config Debug --target MidiFunfunTests`
Expected: FAIL(`Audio/WaveformPeakCache.h`が存在しない)

- [ ] **Step 3: `WaveformPeakCache.h`/`.cpp`を実装**

`Source/Core/Audio/WaveformPeakCache.h`:

```cpp
#pragma once

#include <vector>

namespace midi_funfun::core
{
    struct PeakPair { float minValue = 0.0f; float maxValue = 0.0f; };

    /** monoSamples[0, numSamples) を widthInPixels 列に間引き、列ごとのmin/maxを返す(スペック§3.7)。 */
    class WaveformPeakCache
    {
    public:
        static std::vector<PeakPair> build(const float* monoSamples, int numSamples, int widthInPixels);
    };
}
```

`Source/Core/Audio/WaveformPeakCache.cpp`:

```cpp
#include "WaveformPeakCache.h"

#include <algorithm>

namespace midi_funfun::core
{
    std::vector<PeakPair> WaveformPeakCache::build(const float* monoSamples, int numSamples, int widthInPixels)
    {
        std::vector<PeakPair> result;

        if (monoSamples == nullptr || numSamples <= 0 || widthInPixels <= 0)
            return result;

        result.reserve((size_t) widthInPixels);

        for (int col = 0; col < widthInPixels; ++col)
        {
            const int startSample = (int) (((juce::int64) col * numSamples) / widthInPixels);
            int endSample = (int) (((juce::int64) (col + 1) * numSamples) / widthInPixels);
            endSample = std::min(endSample, numSamples);

            if (startSample >= endSample)
            {
                const int single = std::min(startSample, numSamples - 1);
                result.push_back({ monoSamples[single], monoSamples[single] });
                continue;
            }

            float minV = monoSamples[startSample];
            float maxV = monoSamples[startSample];
            for (int i = startSample + 1; i < endSample; ++i)
            {
                minV = std::min(minV, monoSamples[i]);
                maxV = std::max(maxV, monoSamples[i]);
            }
            result.push_back({ minV, maxV });
        }

        return result;
    }
}
```

`juce::int64`を使うため、ファイル先頭に`#include <juce_core/juce_core.h>`を追加すること。

- [ ] **Step 4: `Source/Core/CMakeLists.txt`に`Audio/WaveformPeakCache.h`/`.cpp`を追加**

- [ ] **Step 5: テストが通ることを確認**

Run: `cmake --build build --config Debug --target MidiFunfunTests && ctest --test-dir build -C Debug -R WaveformPeakCache --output-on-failure`
Expected: PASS(3件)

---

### Task 7: `PlaybackTransport`

**Files:**
- Create: `Source/Core/Audio/PlaybackTransport.h`, `Source/Core/Audio/PlaybackTransport.cpp`, `Tests/PlaybackTransportTests.cpp`
- Modify: `Source/Core/CMakeLists.txt`, `Tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `NoteSequence::getNotes()`(Task 1)、`ticksPerQuarterNote`(既存`Note.h`)。
- Produces: `PlaybackTransport`(`start`/`stop`/`getState`/`getCurrentTick`/`advance`)、`PlaybackTransport::NoteEvent`。Task 9(Processor)が使う。

- [ ] **Step 1: 失敗するテストを書く**

`Tests/PlaybackTransportTests.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "Audio/PlaybackTransport.h"

using midi_funfun::core::Note;
using midi_funfun::core::NoteSequence;
using midi_funfun::core::PlaybackTransport;
using midi_funfun::core::ticksPerQuarterNote;

TEST_CASE("PlaybackTransport emits noteOn at the start of the first block", "[PlaybackTransport]")
{
    NoteSequence seq;
    seq.add(Note { 60, 0, ticksPerQuarterNote, 90 }); // 1拍分の長さ

    PlaybackTransport transport;
    // bpm=120, sampleRate=100 => 1拍 = 0.5秒 = 50サンプル
    transport.start(seq, 120.0, 100.0);
    REQUIRE(transport.getState() == PlaybackTransport::State::Playing);

    const auto events = transport.advance(10, 100.0);
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].isNoteOn);
    REQUIRE(events[0].pitch == 60);
    REQUIRE(events[0].sampleOffsetInBlock == 0);
}

TEST_CASE("PlaybackTransport emits noteOff at the correct sample offset within a later block", "[PlaybackTransport]")
{
    NoteSequence seq;
    seq.add(Note { 60, 0, ticksPerQuarterNote, 90 }); // ノートオフは50サンプル目

    PlaybackTransport transport;
    transport.start(seq, 120.0, 100.0);

    transport.advance(10, 100.0); // noteOn消化、currentSamplePos=10
    transport.advance(30, 100.0); // currentSamplePos=40、まだnoteOffは来ない(50)
    const auto events = transport.advance(20, 100.0); // [40,60) にnoteOff(50)が入る

    REQUIRE(events.size() == 1);
    REQUIRE_FALSE(events[0].isNoteOn);
    REQUIRE(events[0].pitch == 60);
    REQUIRE(events[0].sampleOffsetInBlock == 10); // 50 - 40
}

TEST_CASE("PlaybackTransport transitions to Idle once every event has been emitted", "[PlaybackTransport]")
{
    NoteSequence seq;
    seq.add(Note { 60, 0, ticksPerQuarterNote, 90 });

    PlaybackTransport transport;
    transport.start(seq, 120.0, 100.0);

    transport.advance(60, 100.0); // noteOn(0)・noteOff(50) 両方このブロックで消化
    REQUIRE(transport.getState() == PlaybackTransport::State::Idle);
}

TEST_CASE("PlaybackTransport::getCurrentTick reflects elapsed playback position", "[PlaybackTransport]")
{
    NoteSequence seq;
    seq.add(Note { 60, 0, ticksPerQuarterNote * 4, 90 });

    PlaybackTransport transport;
    transport.start(seq, 120.0, 100.0); // 1拍=50サンプル -> 1tick = 50/480秒相当

    transport.advance(50, 100.0); // 50サンプル進行 = ちょうど1拍 = ticksPerQuarterNote
    REQUIRE(transport.getCurrentTick() == ticksPerQuarterNote);
}

TEST_CASE("PlaybackTransport with no notes goes Idle immediately", "[PlaybackTransport]")
{
    NoteSequence seq;
    PlaybackTransport transport;
    transport.start(seq, 120.0, 100.0);

    const auto events = transport.advance(10, 100.0);
    REQUIRE(events.empty());
    REQUIRE(transport.getState() == PlaybackTransport::State::Idle);
}
```

- [ ] **Step 2: `Tests/CMakeLists.txt`に`PlaybackTransportTests.cpp`を追加し、失敗を確認**

Run: `cmake --build build --config Debug --target MidiFunfunTests`
Expected: FAIL(`Audio/PlaybackTransport.h`が存在しない)

- [ ] **Step 3: `PlaybackTransport.h`/`.cpp`を実装(API確定8節のアルゴリズムに従う)**

`Source/Core/Audio/PlaybackTransport.h`:

```cpp
#pragma once

#include <vector>

#include <juce_core/juce_core.h>

#include "../Model/NoteSequence.h"

namespace midi_funfun::core
{
    class PlaybackTransport
    {
    public:
        enum class State { Idle, Playing };

        struct NoteEvent
        {
            int sampleOffsetInBlock = 0;
            int pitch = 0;
            int velocity = 0;
            bool isNoteOn = false;
        };

        /** notesのスナップショットを取り、tick=startTickから再生を開始する。開始後にsequenceが
         *  変更されても、このインスタンスのイベント列には反映しない。 */
        void start(const NoteSequence& notes, double bpm, double sampleRate, juce::int64 startTick = 0);
        void stop();
        State getState() const { return state; }
        juce::int64 getCurrentTick() const;

        /** sampleRateはstart()に渡したものと同一である前提。 */
        std::vector<NoteEvent> advance(int numSamples, double sampleRate);

    private:
        struct ScheduledEvent
        {
            juce::int64 samplePos = 0;
            int pitch = 0;
            int velocity = 0;
            bool isNoteOn = false;
        };

        State state = State::Idle;
        std::vector<ScheduledEvent> events;
        size_t nextEventIndex = 0;
        juce::int64 currentSamplePos = 0;
        double storedBpm = 120.0;
        double storedSampleRate = 44100.0;
    };
}
```

`Source/Core/Audio/PlaybackTransport.cpp`:

```cpp
#include "PlaybackTransport.h"

#include <algorithm>
#include <cmath>

namespace midi_funfun::core
{
    namespace
    {
        juce::int64 ticksToSamples(juce::int64 tick, double bpm, double sampleRate)
        {
            return (juce::int64) std::round((double) tick / (double) ticksPerQuarterNote * (60.0 / bpm) * sampleRate);
        }

        juce::int64 samplesToTicks(juce::int64 samplePos, double bpm, double sampleRate)
        {
            return (juce::int64) std::round((double) samplePos / sampleRate * (bpm / 60.0) * (double) ticksPerQuarterNote);
        }
    }

    void PlaybackTransport::start(const NoteSequence& notes, double bpm, double sampleRate, juce::int64 startTick)
    {
        storedBpm = bpm;
        storedSampleRate = sampleRate;

        events.clear();
        for (const auto& note : notes.getNotes())
        {
            events.push_back({ ticksToSamples(note.startTick, bpm, sampleRate), note.pitch, note.velocity, true });
            events.push_back({ ticksToSamples(note.startTick + note.lengthTicks, bpm, sampleRate), note.pitch, note.velocity, false });
        }

        std::stable_sort(events.begin(), events.end(), [](const ScheduledEvent& a, const ScheduledEvent& b)
        {
            if (a.samplePos != b.samplePos)
                return a.samplePos < b.samplePos;
            return (a.isNoteOn ? 1 : 0) < (b.isNoteOn ? 1 : 0); // 同一tickではNote Offを先に
        });

        currentSamplePos = ticksToSamples(startTick, bpm, sampleRate);
        nextEventIndex = 0;
        while (nextEventIndex < events.size() && events[nextEventIndex].samplePos < currentSamplePos)
            ++nextEventIndex;

        state = events.empty() ? State::Idle : State::Playing;
    }

    void PlaybackTransport::stop()
    {
        state = State::Idle;
    }

    juce::int64 PlaybackTransport::getCurrentTick() const
    {
        return samplesToTicks(currentSamplePos, storedBpm, storedSampleRate);
    }

    std::vector<PlaybackTransport::NoteEvent> PlaybackTransport::advance(int numSamples, double /*sampleRate*/)
    {
        std::vector<NoteEvent> result;

        if (state != State::Playing)
            return result;

        const juce::int64 blockStart = currentSamplePos;
        const juce::int64 blockEnd = blockStart + numSamples;

        while (nextEventIndex < events.size() && events[nextEventIndex].samplePos < blockEnd)
        {
            const auto& evt = events[nextEventIndex];
            const juce::int64 clampedPos = std::max(evt.samplePos, blockStart);
            result.push_back({ (int) (clampedPos - blockStart), evt.pitch, evt.velocity, evt.isNoteOn });
            ++nextEventIndex;
        }

        currentSamplePos = blockEnd;

        if (nextEventIndex >= events.size())
            state = State::Idle;

        return result;
    }
}
```

- [ ] **Step 4: `Source/Core/CMakeLists.txt`に`Audio/PlaybackTransport.h`/`.cpp`を追加**

- [ ] **Step 5: テストが通ることを確認**

Run: `cmake --build build --config Debug --target MidiFunfunTests && ctest --test-dir build -C Debug -R PlaybackTransport --output-on-failure`
Expected: PASS(5件)

---

### Task 8: `PianoVoice`/`PianoSound`

**Files:**
- Create: `Source/Core/Audio/PianoVoice.h`, `Source/Core/Audio/PianoVoice.cpp`, `Tests/PianoVoiceTests.cpp`
- Modify: `Source/Core/CMakeLists.txt`, `Tests/CMakeLists.txt`

**Interfaces:**
- Produces: `PianoSound`・`PianoVoice`(`juce::SynthesiserSound`/`juce::SynthesiserVoice`派生)。Task 9のProcessorが`juce::Synthesiser`へ登録する。

- [ ] **Step 1: 失敗するテストを書く**

`Tests/PianoVoiceTests.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <juce_audio_basics/juce_audio_basics.h>

#include "Audio/PianoVoice.h"

using midi_funfun::core::PianoSound;
using midi_funfun::core::PianoVoice;

namespace
{
    bool bufferHasNonZeroSample(const juce::AudioBuffer<float>& buffer)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto* data = buffer.getReadPointer(ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                if (data[i] != 0.0f)
                    return true;
        }
        return false;
    }
}

TEST_CASE("PianoVoice produces non-zero output after startNote", "[PianoVoice]")
{
    PianoVoice voice;
    PianoSound sound;
    voice.setCurrentPlaybackSampleRate(44100.0);
    voice.startNote(69, 0.9f, &sound, 0);

    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    voice.renderNextBlock(buffer, 0, 512);

    REQUIRE(bufferHasNonZeroSample(buffer));
}

TEST_CASE("PianoVoice is silent after stopNote with allowTailOff=false", "[PianoVoice]")
{
    PianoVoice voice;
    PianoSound sound;
    voice.setCurrentPlaybackSampleRate(44100.0);
    voice.startNote(69, 0.9f, &sound, 0);

    juce::AudioBuffer<float> warmup(2, 256);
    warmup.clear();
    voice.renderNextBlock(warmup, 0, 256);

    voice.stopNote(1.0f, false);

    juce::AudioBuffer<float> afterStop(2, 512);
    afterStop.clear();
    voice.renderNextBlock(afterStop, 0, 512);

    REQUIRE_FALSE(bufferHasNonZeroSample(afterStop));
}

TEST_CASE("PianoVoice::canPlaySound only accepts PianoSound", "[PianoVoice]")
{
    PianoVoice voice;
    PianoSound sound;
    REQUIRE(voice.canPlaySound(&sound));
}
```

- [ ] **Step 2: `Tests/CMakeLists.txt`に`PianoVoiceTests.cpp`を追加し、失敗を確認**

Run: `cmake --build build --config Debug --target MidiFunfunTests`
Expected: FAIL(`Audio/PianoVoice.h`が存在しない)

- [ ] **Step 3: `PianoVoice.h`/`.cpp`を実装(API確定11節の数値に従う)**

`Source/Core/Audio/PianoVoice.h`:

```cpp
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace midi_funfun::core
{
    class PianoSound final : public juce::SynthesiserSound
    {
    public:
        bool appliesToNote(int) override { return true; }
        bool appliesToChannel(int) override { return true; }
    };

    /**
     * 要件4.6「シンプルな内蔵ピアノ風の音色」向けの最小構成ボイス(スペック§3.9)。
     * 数値の根拠はAPI確定11節を参照。
     */
    class PianoVoice final : public juce::SynthesiserVoice
    {
    public:
        bool canPlaySound(juce::SynthesiserSound* sound) override;
        void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int currentPitchWheelPosition) override;
        void stopNote(float velocity, bool allowTailOff) override;
        void pitchWheelMoved(int) override {}
        void controllerMoved(int, int) override {}
        void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override;

    private:
        enum class Stage { Idle, Attack, Decay, Release };

        double phase = 0.0;
        double phaseIncrement = 0.0;
        float velocityGain = 0.0f;
        float envelopeLevel = 0.0f;
        Stage stage = Stage::Idle;

        double attackPerSampleIncrement = 0.0;
        double decayPerSampleMultiplier = 0.0;
        double releasePerSampleDecrement = 0.0;
    };
}
```

`Source/Core/Audio/PianoVoice.cpp`:

```cpp
#include "PianoVoice.h"

#include <cmath>

namespace midi_funfun::core
{
    namespace
    {
        constexpr double attackTimeSeconds = 0.005;
        constexpr double decayHalfLifeSeconds = 0.5;
        constexpr double releaseTimeSeconds = 0.08;

        constexpr double harmonic1Gain = 1.0;
        constexpr double harmonic2Gain = 0.5;
        constexpr double harmonic3Gain = 0.25;
        constexpr double harmonicGainSum = harmonic1Gain + harmonic2Gain + harmonic3Gain;
    }

    bool PianoVoice::canPlaySound(juce::SynthesiserSound* sound)
    {
        return dynamic_cast<PianoSound*>(sound) != nullptr;
    }

    void PianoVoice::startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int)
    {
        phase = 0.0;
        const double frequencyHz = juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber);
        phaseIncrement = juce::MathConstants<double>::twoPi * frequencyHz / getSampleRate();

        velocityGain = velocity;
        envelopeLevel = 0.0f;
        stage = Stage::Attack;

        const double sr = getSampleRate();
        attackPerSampleIncrement = 1.0 / (attackTimeSeconds * sr);
        decayPerSampleMultiplier = std::pow(0.5, 1.0 / (decayHalfLifeSeconds * sr));
        releasePerSampleDecrement = 1.0 / (releaseTimeSeconds * sr);
    }

    void PianoVoice::stopNote(float, bool allowTailOff)
    {
        if (!allowTailOff)
        {
            envelopeLevel = 0.0f;
            stage = Stage::Idle;
            clearCurrentNote();
            return;
        }

        stage = Stage::Release;
    }

    void PianoVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
    {
        if (stage == Stage::Idle)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            switch (stage)
            {
                case Stage::Attack:
                    envelopeLevel += (float) attackPerSampleIncrement;
                    if (envelopeLevel >= 1.0f)
                    {
                        envelopeLevel = 1.0f;
                        stage = Stage::Decay;
                    }
                    break;
                case Stage::Decay:
                    envelopeLevel *= (float) decayPerSampleMultiplier;
                    break;
                case Stage::Release:
                    envelopeLevel -= (float) releasePerSampleDecrement;
                    if (envelopeLevel <= 0.0f)
                    {
                        envelopeLevel = 0.0f;
                        stage = Stage::Idle;
                    }
                    break;
                case Stage::Idle:
                    break;
            }

            const double h1 = std::sin(phase);
            const double h2 = std::sin(phase * 2.0);
            const double h3 = std::sin(phase * 3.0);
            const double harmonics = (harmonic1Gain * h1 + harmonic2Gain * h2 + harmonic3Gain * h3) / harmonicGainSum;

            const float sampleValue = velocityGain * envelopeLevel * (float) harmonics;

            for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch)
                outputBuffer.addSample(ch, startSample + i, sampleValue);

            phase += phaseIncrement;

            if (stage == Stage::Idle)
            {
                clearCurrentNote();
                break;
            }
        }
    }
}
```

- [ ] **Step 4: `Source/Core/CMakeLists.txt`に`Audio/PianoVoice.h`/`.cpp`を追加**

- [ ] **Step 5: テストが通ることを確認**

Run: `cmake --build build --config Debug --target MidiFunfunTests && ctest --test-dir build -C Debug -R PianoVoice --output-on-failure`
Expected: PASS(3件)

---

### Task 9: `PluginProcessor`への組み込み(editedNotes/undoManager/プレビュー/通し再生/シンセミックス) + 実機ビルド確認

**Files:** Modify `Source/Plugin/PluginProcessor.h`, `Source/Plugin/PluginProcessor.cpp`

このタスクはPlugin層の配線のみでUIはまだ変更しない(自動テストなし、既存方針通り。M1 Task 5・M2 Task 7と同じパターン)。

**Interfaces:**
- Consumes: Task 1-8で作った全Coreクラス。
- Produces: `getAnalyzedNotes()`(editedNotesを返すよう変更)・`getEditedNotesForEditing()`・`getUndoManager()`・`getLastAnalysisResult()`・`getWaveformSourceTake()`・`triggerPreviewNote(int,int)`・`startPlayback()`・`stopPlayback()`・`getPlaybackState()`・`getPlaybackCurrentTick()`。Task 10以降のUI層がこれらを直接呼ぶ。

- [ ] **Step 1: `PluginProcessor.h`のインクルードとメンバを追加**

新規インクルード(既存の末尾に追加):

```cpp
#include <juce_data_structures/juce_data_structures.h>

#include "Model/EditCommands.h"
#include "Audio/PlaybackTransport.h"
#include "Audio/PianoVoice.h"
```

`private:`セクションの既存メンバ群の末尾に追加:

```cpp
midi_funfun::core::PitchAnalysisResult lastAnalysisResult;
midi_funfun::core::NoteSequence editedNotes;
int analyzedTakeIndex = -1;
juce::UndoManager undoManager;

midi_funfun::core::PlaybackTransport playbackTransport;
juce::Synthesiser mainSynth;

std::atomic<int> pendingPreviewPitch { -1 };
std::atomic<int> pendingPreviewVelocity { 0 };
int activePreviewPitch = -1;
int previewRemainingSamples = 0;
```

既存の`midi_funfun::core::NoteSequence analyzedNotes;`メンバは削除する(`lastAnalysisResult.notes`が同じデータを持つため重複)。`analyzedNotesBpm`メンバはそのまま残す(既存の意味を維持)。

`public:`セクションに以下を追加(既存の`getAnalyzedNotes()`は下記の通り実装を変更、シグネチャは維持):

```cpp
const midi_funfun::core::NoteSequence& getAnalyzedNotes() const { return editedNotes; }
midi_funfun::core::NoteSequence& getEditedNotesForEditing() { return editedNotes; }
juce::UndoManager& getUndoManager() { return undoManager; }
const midi_funfun::core::PitchAnalysisResult& getLastAnalysisResult() const { return lastAnalysisResult; }
const midi_funfun::core::Take* getWaveformSourceTake() const { return takeManager.getTake(analyzedTakeIndex); }

/** ノートクリック時の単音プレビュー再生をリクエストする(400ms自動停止、メッセージスレッドから呼ぶ)。 */
void triggerPreviewNote(int pitch, int velocity);

void startPlayback();
void stopPlayback();
midi_funfun::core::PlaybackTransport::State getPlaybackState() const { return playbackTransport.getState(); }
juce::int64 getPlaybackCurrentTick() const { return playbackTransport.getCurrentTick(); }
```

- [ ] **Step 2: コンストラクタで`mainSynth`にボイス・音色を登録**

`PluginProcessor.cpp`のコンストラクタ本体に追加:

```cpp
MidiFunfunAudioProcessor::MidiFunfunAudioProcessor()
    : juce::AudioProcessor(BusesProperties()
                                .withInput("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    mainSynth.addSound(new midi_funfun::core::PianoSound());
    for (int i = 0; i < 16; ++i)
        mainSynth.addVoice(new midi_funfun::core::PianoVoice());
    mainSynth.setNoteStealingEnabled(true);
}
```

- [ ] **Step 3: `prepareToPlay`で`mainSynth`にサンプルレートを設定**

```cpp
void MidiFunfunAudioProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = sampleRate;
    peakLevelTracker.prepare(sampleRate);
    mainSynth.setCurrentPlaybackSampleRate(sampleRate);
}
```

- [ ] **Step 4: `analyzeSelectedTake()`をAPI確定7節の分岐に従って書き換え**

```cpp
void MidiFunfunAudioProcessor::analyzeSelectedTake()
{
    const int selectedIndex = takeManager.getSelectedTakeIndex();
    const auto* take = takeManager.getTake(selectedIndex);
    if (take == nullptr)
        return;

    midi_funfun::core::PitchAnalyzer::Settings settings;
    settings.segmenter.noiseGateRmsThreshold =
        midi_funfun::core::noiseGateSensitivityPercentToThreshold(noiseGateSensitivity.load(std::memory_order_relaxed));
    settings.segmenter.minNoteLengthSeconds = minNoteLengthMs.load(std::memory_order_relaxed) / 1000.0;
    settings.bpm = bpm.load(std::memory_order_relaxed);
    settings.defaultVelocity = defaultVelocity.load(std::memory_order_relaxed);

    midi_funfun::core::PitchAnalyzer analyzer(settings);
    lastAnalysisResult = analyzer.analyze(*take);
    analyzedNotesBpm = settings.bpm;
    analyzedTakeIndex = selectedIndex;

    if (editedNotes.size() == 0)
    {
        editedNotes.replaceAll(lastAnalysisResult.notes.getNotes());
    }
    else
    {
        const auto oldNotes = editedNotes.getNotes();
        const auto newNotes = lastAnalysisResult.notes.getNotes();
        undoManager.perform(new midi_funfun::core::ReplaceAllNotesAction(editedNotes, oldNotes, newNotes), "Re-analyze");
    }
}
```

- [ ] **Step 5: プレビュー再生・通し再生の制御メソッドを実装**

```cpp
void MidiFunfunAudioProcessor::triggerPreviewNote(int pitch, int velocity)
{
    pendingPreviewPitch.store(pitch, std::memory_order_relaxed);
    pendingPreviewVelocity.store(velocity, std::memory_order_relaxed);
}

void MidiFunfunAudioProcessor::startPlayback()
{
    playbackTransport.start(editedNotes, bpm.load(std::memory_order_relaxed), currentSampleRate);
}

void MidiFunfunAudioProcessor::stopPlayback()
{
    playbackTransport.stop();
    mainSynth.allNotesOff(1, true);
}
```

- [ ] **Step 6: `processBlock`末尾にプレビュー・通し再生のミキシングを追加(API確定12節通り)**

既存の`for (const int offset : advance.clickSampleOffsets) ...`ループの直後に追加:

```cpp
    // 単音プレビュー: GUIスレッドからの要求を1回だけ消費する(スペック§4.1、400ms固定)
    const int requestedPreviewPitch = pendingPreviewPitch.exchange(-1, std::memory_order_relaxed);
    if (requestedPreviewPitch >= 0)
    {
        if (activePreviewPitch >= 0)
            mainSynth.noteOff(1, activePreviewPitch, 1.0f, true);

        const int requestedVelocity = pendingPreviewVelocity.load(std::memory_order_relaxed);
        mainSynth.noteOn(1, requestedPreviewPitch, juce::jlimit(0.0f, 1.0f, (float) requestedVelocity / 127.0f));
        activePreviewPitch = requestedPreviewPitch;
        previewRemainingSamples = (int) std::round(0.4 * currentSampleRate);
    }

    if (activePreviewPitch >= 0)
    {
        previewRemainingSamples -= numSamples;
        if (previewRemainingSamples <= 0)
        {
            mainSynth.noteOff(1, activePreviewPitch, 1.0f, true);
            activePreviewPitch = -1;
        }
    }

    // 通し再生
    juce::MidiBuffer emptyMidi;
    const auto playbackEvents = playbackTransport.advance(numSamples, currentSampleRate);
    int renderedUpTo = 0;
    for (const auto& evt : playbackEvents)
    {
        if (evt.sampleOffsetInBlock > renderedUpTo)
        {
            mainSynth.renderNextBlock(buffer, emptyMidi, renderedUpTo, evt.sampleOffsetInBlock - renderedUpTo);
            renderedUpTo = evt.sampleOffsetInBlock;
        }

        if (evt.isNoteOn)
            mainSynth.noteOn(1, evt.pitch, juce::jlimit(0.0f, 1.0f, (float) evt.velocity / 127.0f));
        else
            mainSynth.noteOff(1, evt.pitch, 1.0f, true);
    }
    mainSynth.renderNextBlock(buffer, emptyMidi, renderedUpTo, numSamples - renderedUpTo);
}
```

(最後の`}`は既存の`processBlock`の閉じ括弧。挿入位置を正しく確認すること。)

- [ ] **Step 7: ビルド成功を確認**

Run: `cmake --build build --config Debug`
Expected: `MidiFunfun_Standalone`/`MidiFunfun_VST3`/`MidiFunfunTests`すべて成功。GUI(`PluginEditor`)はまだ変更していないため見た目は変わらない。

- [ ] **Step 8: 全体テストがまだ通ることを確認**

Run: `ctest --test-dir build -C Debug --output-on-failure`
Expected: PASS(既存+本マイルストーンTask 1-8で追加した全テスト)

---

### Task 10: `PianoKeyboardComponent`(UI、`Source/UI/`新設)

**Files:**
- Create: `Source/UI/PianoKeyboardComponent.h`, `Source/UI/PianoKeyboardComponent.cpp`
- Modify: `Source/Plugin/CMakeLists.txt`

自動テスト対象外(GUI描画)。Standalone起動での目視確認で検証する。

**Interfaces:**
- Produces: `PianoKeyboardComponent`。Task 11で`PianoRollComponent`が所有する。

- [ ] **Step 1: `Source/Plugin/CMakeLists.txt`を先に更新(空ファイルでもビルド対象に含めてから実装する)**

```cmake
target_sources(MidiFunfun PRIVATE
    PluginProcessor.h
    PluginProcessor.cpp
    PluginEditor.h
    PluginEditor.cpp
    ../UI/PianoKeyboardComponent.h
    ../UI/PianoKeyboardComponent.cpp
    ../UI/PianoRollComponent.h
    ../UI/PianoRollComponent.cpp
    ../UI/PlaybackTransportBar.h
    ../UI/PlaybackTransportBar.cpp
)

target_include_directories(MidiFunfun PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../UI)
```

(`PianoRollComponent.*`/`PlaybackTransportBar.*`はTask 11・19で作成する。ここでCMakeに一括で追加しておき、Task 11で空実装のスタブファイルを作ってビルドを通す。)

- [ ] **Step 2: `PianoRollComponent.h`/`.cpp`・`PlaybackTransportBar.h`/`.cpp`の最小スタブを作成(Task 11・19で本実装に差し替える前提の仮ファイル)**

`Source/UI/PianoRollComponent.h`:

```cpp
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace midi_funfun::ui
{
    class PianoRollComponent final : public juce::Component
    {
    public:
        PianoRollComponent() = default;
    };
}
```

`Source/UI/PianoRollComponent.cpp`: `#include "PianoRollComponent.h"`のみ。

`Source/UI/PlaybackTransportBar.h`:

```cpp
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace midi_funfun::ui
{
    class PlaybackTransportBar final : public juce::Component
    {
    public:
        PlaybackTransportBar() = default;
    };
}
```

`Source/UI/PlaybackTransportBar.cpp`: `#include "PlaybackTransportBar.h"`のみ。

- [ ] **Step 3: `PianoKeyboardComponent`を実装**

`Source/UI/PianoKeyboardComponent.h`:

```cpp
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace midi_funfun::ui
{
    /** ピアノロール左端の描画専用鍵盤(スペック§5.2)。クリックしても何も起きない。
     *  pixelsPerSemitone・表示pitch範囲を外部(PianoRollComponent)から受け取り、
     *  ノート行のピクセル位置と完全に一致させる。 */
    class PianoKeyboardComponent final : public juce::Component
    {
    public:
        static constexpr int rulerHeightPx = 24; // Canvasのルーラー帯と高さを揃える(API確定1節)

        void setVisibleRange(int lowestPitchIn, int highestPitchIn, double pixelsPerSemitoneIn);
        void paint(juce::Graphics& g) override;

    private:
        int lowestPitch = 48;
        int highestPitch = 72;
        double pixelsPerSemitone = 16.0;

        int pitchToY(int pitch) const;
        static bool isBlackKey(int pitch);
    };
}
```

`Source/UI/PianoKeyboardComponent.cpp`:

```cpp
#include "PianoKeyboardComponent.h"

namespace midi_funfun::ui
{
    void PianoKeyboardComponent::setVisibleRange(int lowestPitchIn, int highestPitchIn, double pixelsPerSemitoneIn)
    {
        lowestPitch = lowestPitchIn;
        highestPitch = highestPitchIn;
        pixelsPerSemitone = pixelsPerSemitoneIn;
        repaint();
    }

    int PianoKeyboardComponent::pitchToY(int pitch) const
    {
        return rulerHeightPx + (int) std::round((highestPitch - pitch) * pixelsPerSemitone);
    }

    bool PianoKeyboardComponent::isBlackKey(int pitch)
    {
        static const bool blackKeys[12] = { false, true, false, true, false, false, true, false, true, false, true, false };
        return blackKeys[((pitch % 12) + 12) % 12];
    }

    void PianoKeyboardComponent::paint(juce::Graphics& g)
    {
        g.fillAll(juce::Colours::white);

        for (int pitch = lowestPitch; pitch <= highestPitch; ++pitch)
        {
            const auto rowBounds = juce::Rectangle<int>(0, pitchToY(pitch), getWidth(), (int) pixelsPerSemitone);
            g.setColour(isBlackKey(pitch) ? juce::Colours::black : juce::Colours::white);
            g.fillRect(rowBounds);
            g.setColour(juce::Colours::grey);
            g.drawRect(rowBounds, 1);

            if (pitch % 12 == 0) // C音のみラベル表示(C4等)
            {
                g.setColour(isBlackKey(pitch) ? juce::Colours::white : juce::Colours::black);
                g.drawText(juce::MidiMessage::getMidiNoteName(pitch, true, true, 4),
                           rowBounds.reduced(2, 0), juce::Justification::centredLeft);
            }
        }
    }
}
```

- [ ] **Step 4: ビルド成功を確認**

Run: `cmake --build build --config Debug`
Expected: 成功(まだ`PluginEditor`には配線していないので画面上の変化はない)

---

### Task 11: `PianoRollComponent`/`Canvas`基本描画(グリッド・ノート・ルーラー) + `PluginEditor`への配線

**Files:**
- Modify: `Source/UI/PianoRollComponent.h`, `Source/UI/PianoRollComponent.cpp`, `Source/Plugin/PluginEditor.h`, `Source/Plugin/PluginEditor.cpp`

Task 10で作ったスタブを本実装に差し替える。マウス操作・キーボード操作はまだ実装しない(Task 12以降)。このタスクではノート・グリッド・ルーラーが正しい位置に描画され、`PluginEditor`内に表示されることまでを確認する。

**Interfaces:**
- Consumes: `MidiFunfunAudioProcessor::getAnalyzedNotes()`・`getEditedNotesForEditing()`(Task 9)、API確定1-2節の座標変換・自動フィット定数。
- Produces: `PianoRollComponent`(コンストラクタで`MidiFunfunAudioProcessor&`を受け取る)。`Canvas`は`PianoRollComponent`のprivateネストクラス(スペック§5.1)。Task 12以降がこのファイルへ追記していく。

- [ ] **Step 1: `PianoRollComponent.h`を本実装へ**

```cpp
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Plugin/PluginProcessor.h"
#include "PianoKeyboardComponent.h"

namespace midi_funfun::ui
{
    // ズーム境界(API確定1節)。名前空間スコープに置く理由: Canvasはスペック§5.1通り
    // PianoRollComponentのprivateネストクラスであり、外側のクラス(PluginEditor等)は
    // 入れ子クラスの名前自体を参照できない(C++の入れ子クラスのアクセス制御は非対称)。
    // PluginEditor側でスライダー範囲設定に使うため、Canvasのメンバにはせずここに置く。
    constexpr double minPixelsPerTick = 0.05;
    constexpr double maxPixelsPerTick = 1.0;
    constexpr double minPixelsPerSemitone = 6.0;
    constexpr double maxPixelsPerSemitone = 32.0;

    class PianoRollComponent final : public juce::Component
    {
    public:
        explicit PianoRollComponent(MidiFunfunAudioProcessor& processorIn);
        void resized() override;

        /** 解析完了直後に呼ぶ。縦方向の自動フィット(API確定2節)を行う。 */
        void fitVerticalRangeToNotes();

    private:
        class Canvas final : public juce::Component
        {
        public:
            explicit Canvas(MidiFunfunAudioProcessor& processorIn);
            void paint(juce::Graphics& g) override;

            int highestVisiblePitch = 72;
            int lowestVisiblePitch = 48;
            double pixelsPerTick = 0.25;
            double pixelsPerSemitone = 16.0;
            bool gridEnabled = true;

            static constexpr int rulerHeightPx = 24; // Canvas内部専用(外部からCanvas::では参照しない)

            int tickToX(juce::int64 tick) const;
            juce::int64 xToTick(int x) const;
            int pitchToY(int pitch) const;
            int yToPitch(int y) const;

            juce::int64 computeTimelineEndTick() const;
            void refreshSize();

        private:
            MidiFunfunAudioProcessor& processor;

            void paintGrid(juce::Graphics& g);
            void paintNotes(juce::Graphics& g);
            void paintRuler(juce::Graphics& g);
        };

        MidiFunfunAudioProcessor& processor;
        juce::Viewport rollViewport;
        Canvas canvas;
        PianoKeyboardComponent pianoKeyboard;
    };
}
```

- [ ] **Step 2: `PianoRollComponent.cpp`を本実装へ(座標変換・グリッド・ノート・ルーラー描画)**

```cpp
#include "PianoRollComponent.h"

#include <algorithm>
#include <cmath>

#include "../Core/Model/Note.h"
#include "../Core/Model/GridSnap.h"

namespace midi_funfun::ui
{
    namespace
    {
        constexpr juce::int64 minTimelineTicks = 8 * 4 * midi_funfun::core::ticksPerQuarterNote; // 8小節
        constexpr juce::int64 tailMarginTicks = 4 * midi_funfun::core::ticksPerQuarterNote;       // 1小節
        constexpr int autoFitMarginSemitones = 4;
        constexpr int autoFitMinSpanSemitones = 24;
    }

    PianoRollComponent::Canvas::Canvas(MidiFunfunAudioProcessor& processorIn) : processor(processorIn)
    {
    }

    int PianoRollComponent::Canvas::tickToX(juce::int64 tick) const
    {
        return (int) std::round((double) tick * pixelsPerTick);
    }

    juce::int64 PianoRollComponent::Canvas::xToTick(int x) const
    {
        return (juce::int64) std::round((double) x / pixelsPerTick);
    }

    int PianoRollComponent::Canvas::pitchToY(int pitch) const
    {
        return rulerHeightPx + (int) std::round((double) (highestVisiblePitch - pitch) * pixelsPerSemitone);
    }

    int PianoRollComponent::Canvas::yToPitch(int y) const
    {
        return highestVisiblePitch - (int) std::floor((double) (y - rulerHeightPx) / pixelsPerSemitone);
    }

    juce::int64 PianoRollComponent::Canvas::computeTimelineEndTick() const
    {
        juce::int64 endTick = minTimelineTicks;

        for (const auto& note : processor.getAnalyzedNotes().getNotes())
            endTick = std::max(endTick, note.startTick + note.lengthTicks);

        return endTick + tailMarginTicks;
    }

    void PianoRollComponent::Canvas::refreshSize()
    {
        const int width = tickToX(computeTimelineEndTick());
        const int height = rulerHeightPx + (int) std::round((double) (highestVisiblePitch - lowestVisiblePitch + 1) * pixelsPerSemitone);
        setSize(width, height);
    }

    void PianoRollComponent::Canvas::paintGrid(juce::Graphics& g)
    {
        g.setColour(juce::Colours::lightgrey);
        for (int pitch = lowestVisiblePitch; pitch <= highestVisiblePitch; ++pitch)
        {
            const int y = pitchToY(pitch);
            g.drawHorizontalLine(y, 0.0f, (float) getWidth());
        }

        const juce::int64 endTick = computeTimelineEndTick();
        for (juce::int64 tick = 0; tick <= endTick; tick += midi_funfun::core::ticksPerQuarterNote)
            g.drawVerticalLine(tickToX(tick), (float) rulerHeightPx, (float) getHeight());
    }

    void PianoRollComponent::Canvas::paintNotes(juce::Graphics& g)
    {
        for (const auto& note : processor.getAnalyzedNotes().getNotes())
        {
            const auto rect = juce::Rectangle<int>(tickToX(note.startTick), pitchToY(note.pitch),
                                                     std::max(1, tickToX(note.startTick + note.lengthTicks) - tickToX(note.startTick)),
                                                     (int) pixelsPerSemitone);
            g.setColour(juce::Colours::steelblue);
            g.fillRect(rect);
            g.setColour(juce::Colours::black);
            g.drawRect(rect, 1);
        }
    }

    void PianoRollComponent::Canvas::paintRuler(juce::Graphics& g)
    {
        g.setColour(juce::Colours::darkgrey);
        g.fillRect(0, 0, getWidth(), rulerHeightPx);

        const juce::int64 endTick = computeTimelineEndTick();
        int barNumber = 1;
        for (juce::int64 tick = 0; tick <= endTick; tick += 4 * midi_funfun::core::ticksPerQuarterNote, ++barNumber)
        {
            g.setColour(juce::Colours::white);
            g.drawText(juce::String(barNumber), tickToX(tick) + 2, 0, 40, rulerHeightPx, juce::Justification::centredLeft);
        }
    }

    void PianoRollComponent::Canvas::paint(juce::Graphics& g)
    {
        g.fillAll(juce::Colours::whitesmoke);
        paintGrid(g);
        paintNotes(g);
        paintRuler(g);
    }

    PianoRollComponent::PianoRollComponent(MidiFunfunAudioProcessor& processorIn)
        : processor(processorIn), canvas(processorIn)
    {
        canvas.refreshSize();
        rollViewport.setViewedComponent(&canvas, false);
        addAndMakeVisible(rollViewport);
        addAndMakeVisible(pianoKeyboard);
        pianoKeyboard.setVisibleRange(canvas.lowestVisiblePitch, canvas.highestVisiblePitch, canvas.pixelsPerSemitone);
    }

    void PianoRollComponent::resized()
    {
        auto area = getLocalBounds();
        pianoKeyboard.setBounds(area.removeFromLeft(PianoKeyboardComponent::rulerHeightPx == 0 ? 60 : 60)); // keyboardWidthPx = 60 (API確定1節)
        rollViewport.setBounds(area);
    }

    void PianoRollComponent::fitVerticalRangeToNotes()
    {
        const auto& notes = processor.getAnalyzedNotes().getNotes();
        if (notes.empty())
        {
            canvas.lowestVisiblePitch = 48;
            canvas.highestVisiblePitch = 72;
        }
        else
        {
            int minP = 127, maxP = 0;
            for (const auto& note : notes)
            {
                minP = std::min(minP, note.pitch);
                maxP = std::max(maxP, note.pitch);
            }

            int lo = std::max(0, minP - autoFitMarginSemitones);
            int hi = std::min(127, maxP + autoFitMarginSemitones);

            if (hi - lo + 1 < autoFitMinSpanSemitones)
            {
                const int deficit = autoFitMinSpanSemitones - (hi - lo + 1);
                lo = std::max(0, lo - deficit / 2);
                hi = std::min(127, hi + (deficit - deficit / 2));
            }

            canvas.lowestVisiblePitch = lo;
            canvas.highestVisiblePitch = hi;
        }

        pianoKeyboard.setVisibleRange(canvas.lowestVisiblePitch, canvas.highestVisiblePitch, canvas.pixelsPerSemitone);
        canvas.refreshSize();
        canvas.repaint();
    }
}
```

`keyboardWidthPx = 60`(API確定1節)は`PianoRollComponent::resized()`内に直接リテラルで書いてよい(`PianoKeyboardComponent`側は幅の意味を持たないため、幅の決定権は親の`resized()`が持つ)。上記コード例の`PianoKeyboardComponent::rulerHeightPx == 0 ? 60 : 60`は冗長なので、実装時は単純に`area.removeFromLeft(60)`と書くこと。

- [ ] **Step 3: `PluginEditor`に配線**

`PluginEditor.h`: `#include "../UI/PianoRollComponent.h"`を追加し、`private:`セクション末尾に`midi_funfun::ui::PianoRollComponent pianoRoll { processorRef };`を追加。

`PluginEditor.cpp`のコンストラクタに`addAndMakeVisible(pianoRoll);`を追加。`resized()`内、既存の`notesArea`(`noteListBox`が使っていた領域、Task 20で削除するまでは併存)とは別に、`takesArea`の下・画面下部に`pianoRoll`用の領域を確保する(暫定でよい。最終的なレイアウトはTask 19で`PlaybackTransportBar`を追加する際に確定する)。`analyzeButtonClicked()`の末尾に`pianoRoll.fitVerticalRangeToNotes();`を追加する。

- [ ] **Step 4: ビルド成功 + 実機確認**

Run: `cmake --build build --config Debug`

Standaloneを起動し、以下を目視確認する: 録音→解析→ピアノロール領域にグリッド線・ルーラー(小節番号)・検出ノート(青い矩形)が表示される。ノートの縦位置が鍵盤コンポーネントの対応する鍵と一致する。

---

### Task 12: マウス操作 - 移動・リサイズ(ドラッグ)

**Files:** Modify `Source/UI/PianoRollComponent.h`, `Source/UI/PianoRollComponent.cpp`

自動テスト対象外。Standalone実機確認で検証する。

**Interfaces:**
- Consumes: `MoveNotesAction`・`ResizeNoteAction`(Task 3)、`snapTickToGrid`/`gridUnitTicks`(Task 4)、`processor.getEditedNotesForEditing()`/`getUndoManager()`(Task 9)、API確定3節の細則。
- Produces: `Canvas::mouseDown/mouseDrag/mouseUp`(移動・リサイズの経路のみ。クリック選択・マーキー・ダブルクリックはTask 13・14)。`Canvas::selection`(`midi_funfun::core::Selection`)メンバ。

- [ ] **Step 1: `Canvas`にドラッグ状態・`Selection`メンバを追加**

`PianoRollComponent.h`の先頭(既存の`#include "PianoKeyboardComponent.h"`の下)に追加:

```cpp
#include "../Core/Model/Selection.h"
#include "../Core/Model/EditCommands.h"
```

(`EditCommands.h`は本タスクの`.cpp`実装が`MoveNotesAction`/`ResizeNoteAction`を`new`するために必要。`Selection.h`は下記`selection`メンバの型のために必要。)

`PianoRollComponent.h`の`Canvas`宣言に追加(`private:`セクション):

```cpp
enum class DragMode { None, MoveNotes, ResizeNote, Marquee };

struct DragState
{
    DragMode mode = DragMode::None;
    juce::Point<int> startPosition;
    juce::int64 grabbedNoteId = 0; // ResizeNote時: リサイズ対象のノートid
    std::vector<midi_funfun::core::Note> snapshotNotes; // ドラッグ開始時点の対象ノートのコピー(移動元)
};

midi_funfun::core::Selection selection;
DragState dragState;

void mouseDown(const juce::MouseEvent& e) override;
void mouseDrag(const juce::MouseEvent& e) override;
void mouseUp(const juce::MouseEvent& e) override;

juce::int64 hitTestNote(juce::Point<int> position, bool& onResizeHandle) const;
```

`Canvas`のコンストラクタは変更不要(既存のまま)。

- [ ] **Step 2: `hitTestNote`を実装(API確定3節のハンドル判定込み)**

`PianoRollComponent.cpp`に追加:

```cpp
juce::int64 PianoRollComponent::Canvas::hitTestNote(juce::Point<int> position, bool& onResizeHandle) const
{
    onResizeHandle = false;
    for (const auto& note : processor.getAnalyzedNotes().getNotes())
    {
        const int left = tickToX(note.startTick);
        const int right = tickToX(note.startTick + note.lengthTicks);
        const int top = pitchToY(note.pitch);
        const juce::Rectangle<int> rect(left, top, std::max(1, right - left), (int) pixelsPerSemitone);

        if (rect.contains(position))
        {
            constexpr int resizeHandleWidthPx = 6;
            constexpr int minNoteWidthForResizePx = 12;
            if (rect.getWidth() >= minNoteWidthForResizePx && position.x >= right - resizeHandleWidthPx)
                onResizeHandle = true;
            return note.id;
        }
    }
    return 0;
}
```

- [ ] **Step 3: `mouseDown`を実装(API確定3節: 選択決定 + ドラッグモード開始)**

```cpp
void PianoRollComponent::Canvas::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    bool onResizeHandle = false;
    const auto hitId = hitTestNote(e.getPosition(), onResizeHandle);

    if (hitId == 0)
    {
        dragState.mode = DragMode::Marquee;
        dragState.startPosition = e.getPosition();
        return;
    }

    if (e.mods.isCtrlDown())
        selection.toggle(hitId);
    else if (e.mods.isShiftDown())
        selection.addRange({ hitId });
    else if (!selection.isSelected(hitId))
        selection.set(hitId);
    // 既に選択済み・修飾キーなしの場合は選択を変更しない(複数選択を維持してドラッグするため)

    dragState.mode = onResizeHandle ? DragMode::ResizeNote : DragMode::MoveNotes;
    dragState.startPosition = e.getPosition();
    dragState.grabbedNoteId = hitId;
    dragState.snapshotNotes.clear();
    for (const auto id : selection.getSelectedIds())
        if (const auto* note = processor.getAnalyzedNotes().findById(id))
            dragState.snapshotNotes.push_back(*note);

    repaint();
}
```

- [ ] **Step 4: `mouseDrag`を実装(ローカル一時オフセットのみで再描画、`editedNotes`本体には触れない)**

```cpp
void PianoRollComponent::Canvas::mouseDrag(const juce::MouseEvent& e)
{
    if (dragState.mode == DragMode::None)
        return;

    // Marqueeのリアルタイムハイライトは repaint() のみで表現し、選択本体は変更しない。
    // MoveNotes/ResizeNoteのプレビューも同様に、このステップでは repaint() だけを呼び、
    // 実データの変更は mouseUp で1回だけ行う(スペック§5.3: 1ドラッグ=1 Undoステップ)。
    repaint();
}
```

(この時点でプレビュー描画〈ドラッグ中のノート位置を仮に動かして見せる〉を`paint()`に組み込みたい場合は、`dragState`の内容を見て`paintNotes()`内でオフセットを加味する形にしてよいが、`editedNotes`自体は変更しないこと。)

- [ ] **Step 5: `mouseUp`を実装(ドラッグ確定 or クリック判定への分岐)**

```cpp
void PianoRollComponent::Canvas::mouseUp(const juce::MouseEvent& e)
{
    if (dragState.mode == DragMode::MoveNotes && e.mouseWasDraggedSinceMouseDown())
    {
        const juce::int64 tickDelta = gridEnabled
            ? midi_funfun::core::snapTickToGrid(xToTick(e.getPosition().x)) - midi_funfun::core::snapTickToGrid(xToTick(dragState.startPosition.x))
            : xToTick(e.getPosition().x) - xToTick(dragState.startPosition.x);
        const int pitchDelta = yToPitch(e.getPosition().y) - yToPitch(dragState.startPosition.y);

        std::vector<midi_funfun::core::MoveNotesAction::Delta> deltas;
        for (const auto& snapshot : dragState.snapshotNotes)
        {
            const int newPitch = juce::jlimit(0, 127, snapshot.pitch + pitchDelta);
            const juce::int64 newStartTick = std::max<juce::int64>(0, snapshot.startTick + tickDelta);
            deltas.push_back({ snapshot.id, snapshot.pitch, snapshot.startTick, newPitch, newStartTick });
        }

        processor.getUndoManager().perform(
            new midi_funfun::core::MoveNotesAction(processor.getEditedNotesForEditing(), deltas), "Move Notes");
    }
    else if (dragState.mode == DragMode::ResizeNote && e.mouseWasDraggedSinceMouseDown())
    {
        if (const auto* note = processor.getAnalyzedNotes().findById(dragState.grabbedNoteId))
        {
            const juce::int64 rawNewEndTick = xToTick(e.getPosition().x);
            const juce::int64 snappedEndTick = gridEnabled ? midi_funfun::core::snapTickToGrid(rawNewEndTick) : rawNewEndTick;
            juce::int64 newLengthTicks = snappedEndTick - note->startTick;
            newLengthTicks = std::max<juce::int64>(gridEnabled ? midi_funfun::core::gridUnitTicks : 1, newLengthTicks);

            processor.getUndoManager().perform(
                new midi_funfun::core::ResizeNoteAction(processor.getEditedNotesForEditing(), note->id, note->lengthTicks, newLengthTicks),
                "Resize Note");
        }
    }
    else if (dragState.mode == DragMode::Marquee && e.mouseWasDraggedSinceMouseDown())
    {
        const auto marqueeRect = juce::Rectangle<int>(dragState.startPosition, e.getPosition());
        std::vector<juce::int64> hitIds;
        for (const auto& note : processor.getAnalyzedNotes().getNotes())
        {
            const int left = tickToX(note.startTick);
            const int right = tickToX(note.startTick + note.lengthTicks);
            const int top = pitchToY(note.pitch);
            const juce::Rectangle<int> noteRect(left, top, std::max(1, right - left), (int) pixelsPerSemitone);
            if (noteRect.intersects(marqueeRect))
                hitIds.push_back(note.id);
        }
        selection.clear();
        selection.addRange(hitIds);
    }
    // クリック(!mouseWasDraggedSinceMouseDown())の場合の選択+プレビューはTask 13で追加する。

    dragState = {};
    refreshSize();
    repaint();
}
```

- [ ] **Step 6: ビルド成功 + 実機確認**

Run: `cmake --build build --config Debug`

Standaloneで: ノートを縦にドラッグして音高が変わること、横にドラッグして開始位置が変わること、右端をドラッグして長さが変わること、`Ctrl+Z`で元に戻ること(キーボードショートカット自体はTask 15だが、`undoManager.undo()`を一時的に`PluginEditor`側のボタン等から呼んで確認してもよい)。

---

### Task 13: マウス操作 - クリック選択+プレビュー再生・マーキーの視覚フィードバック

**Files:** Modify `Source/UI/PianoRollComponent.h`, `Source/UI/PianoRollComponent.cpp`

自動テスト対象外。Standalone実機確認で検証する。

**Interfaces:**
- Consumes: `processor.triggerPreviewNote(int,int)`(Task 9)、`Canvas::selection`/`dragState`(Task 12)。
- Produces: `mouseUp`のクリック分岐(選択反映+プレビュー再生)。ドラッグ中のマーキー矩形描画。

- [ ] **Step 1: `mouseUp`にクリック(ドラッグなし)の分岐を追加**

Task 12の`mouseUp`内、コメント`// クリック(!mouseWasDraggedSinceMouseDown())の場合...`の箇所を以下へ置き換える:

```cpp
    else if (!e.mouseWasDraggedSinceMouseDown() && dragState.grabbedNoteId != 0)
    {
        if (const auto* note = processor.getAnalyzedNotes().findById(dragState.grabbedNoteId))
            processor.triggerPreviewNote(note->pitch, note->velocity);
    }
```

(`dragState.grabbedNoteId`は`mouseDown`でノート本体にヒットした場合のみ非0になる。空白部分でのクリック〈ドラッグなしのMarquee〉は何もしない。)

- [ ] **Step 2: マーキードラッグ中の矩形をリアルタイム描画**

`Canvas`に一時状態を追加(`private:`、`DragState`の下):

```cpp
juce::Point<int> lastDragPosition; // paint()でマーキー矩形を描くために使う
```

`mouseDrag`を以下へ更新:

```cpp
void PianoRollComponent::Canvas::mouseDrag(const juce::MouseEvent& e)
{
    if (dragState.mode == DragMode::None)
        return;

    lastDragPosition = e.getPosition();
    repaint();
}
```

`paint()`の末尾(`paintRuler(g);`の後)に追加:

```cpp
    if (dragState.mode == DragMode::Marquee)
    {
        g.setColour(juce::Colours::dodgerblue.withAlpha(0.3f));
        g.fillRect(juce::Rectangle<int>(dragState.startPosition, lastDragPosition));
        g.setColour(juce::Colours::dodgerblue);
        g.drawRect(juce::Rectangle<int>(dragState.startPosition, lastDragPosition), 1);
    }
```

- [ ] **Step 3: 選択中ノートのハイライト描画を`paintNotes`に追加**

`paintNotes()`内、`g.setColour(juce::Colours::steelblue);`の箇所を以下へ置き換える:

```cpp
        g.setColour(selection.isSelected(note.id) ? juce::Colours::orange : juce::Colours::steelblue);
```

- [ ] **Step 4: ビルド成功 + 実機確認**

Run: `cmake --build build --config Debug`

Standaloneで: ノートをクリック(ドラッグなし)すると選択色(オレンジ)になり、内蔵ピアノ音源で単音が約400ms鳴ること。Shift/Ctrlクリックでの複数選択、空白部分をドラッグして矩形が表示され、確定後に交差したノートが選択されること。

---

### Task 14: マウス操作 - ダブルクリックでの新規ノート追加

**Files:** Modify `Source/UI/PianoRollComponent.h`, `Source/UI/PianoRollComponent.cpp`

自動テスト対象外。Standalone実機確認で検証する。

**Interfaces:**
- Consumes: `AddNoteAction`(Task 3)、`gridUnitTicks`/`snapTickToGrid`(Task 4)、`processor.getDefaultVelocity()`(既存M2アクセサ)。
- Produces: `Canvas::mouseDoubleClick`。

- [ ] **Step 1: `Canvas`に`mouseDoubleClick`を宣言**

`PianoRollComponent.h`の`Canvas`宣言、`mouseUp`の下に追加:

```cpp
void mouseDoubleClick(const juce::MouseEvent& e) override;
```

- [ ] **Step 2: 実装(空白部分のダブルクリックのみ追加。ノート上のダブルクリックは何もしない)**

```cpp
void PianoRollComponent::Canvas::mouseDoubleClick(const juce::MouseEvent& e)
{
    bool onResizeHandle = false;
    if (hitTestNote(e.getPosition(), onResizeHandle) != 0)
        return; // 既存ノート上のダブルクリックは対象外(スペック§5.3: 空白部分のみ)

    const int pitch = juce::jlimit(0, 127, yToPitch(e.getPosition().y));
    const juce::int64 rawTick = xToTick(e.getPosition().x);
    const juce::int64 startTick = std::max<juce::int64>(0, gridEnabled ? midi_funfun::core::snapTickToGrid(rawTick) : rawTick);

    midi_funfun::core::Note newNote;
    newNote.pitch = pitch;
    newNote.startTick = startTick;
    newNote.lengthTicks = midi_funfun::core::gridUnitTicks; // 既定長=1/16音符(スペック§5.3、Grid ON/OFFに関わらず固定)
    newNote.velocity = processor.getDefaultVelocity();

    processor.getUndoManager().perform(
        new midi_funfun::core::AddNoteAction(processor.getEditedNotesForEditing(), newNote), "Add Note");

    refreshSize();
    repaint();
}
```

- [ ] **Step 3: ビルド成功 + 実機確認**

Run: `cmake --build build --config Debug`

Standaloneで: 空白部分をダブルクリックすると、その位置の音高・(Grid ON時はスナップ済みの)開始位置に1/16音符長の新規ノートが追加されること。既存ノート上のダブルクリックでは何も起きないこと。

---

### Task 15: キーボードショートカット(Delete/Ctrl+Z/Ctrl+Y/Ctrl+A) + 矢印キーnudge

**Files:** Modify `Source/UI/PianoRollComponent.h`, `Source/UI/PianoRollComponent.cpp`

自動テスト対象外。Standalone実機確認で検証する。

**Interfaces:**
- Consumes: `DeleteNotesAction`・`MoveNotesAction`(Task 3)、`gridUnitTicks`(Task 4)、API確定5節のキーコード。
- Produces: `Canvas::keyPressed`。

- [ ] **Step 1: `Canvas`に`keyPressed`と`nudge`/`deleteSelected`ヘルパーを宣言**

`PianoRollComponent.h`の`Canvas`宣言に追加:

```cpp
bool keyPressed(const juce::KeyPress& key) override;

private:
    void nudge(int semitoneDelta, juce::int64 tickDelta);
    void deleteSelected();
```

(`Canvas`のコンストラクタで`setWantsKeyboardFocus(true)`を呼ぶよう変更する。)

- [ ] **Step 2: 実装(API確定5節通り)**

```cpp
PianoRollComponent::Canvas::Canvas(MidiFunfunAudioProcessor& processorIn) : processor(processorIn)
{
    setWantsKeyboardFocus(true);
}

void PianoRollComponent::Canvas::nudge(int semitoneDelta, juce::int64 tickDelta)
{
    if (selection.isEmpty())
        return;

    std::vector<midi_funfun::core::MoveNotesAction::Delta> deltas;
    for (const auto id : selection.getSelectedIds())
    {
        const auto* note = processor.getAnalyzedNotes().findById(id);
        if (note == nullptr)
            continue;

        const int newPitch = juce::jlimit(0, 127, note->pitch + semitoneDelta);
        const juce::int64 newStartTick = std::max<juce::int64>(0, note->startTick + tickDelta);
        deltas.push_back({ id, note->pitch, note->startTick, newPitch, newStartTick });
    }

    if (!deltas.empty())
        processor.getUndoManager().perform(
            new midi_funfun::core::MoveNotesAction(processor.getEditedNotesForEditing(), deltas), "Nudge Notes");

    repaint();
}

void PianoRollComponent::Canvas::deleteSelected()
{
    if (selection.isEmpty())
        return;

    std::vector<midi_funfun::core::Note> snapshot;
    for (const auto id : selection.getSelectedIds())
        if (const auto* note = processor.getAnalyzedNotes().findById(id))
            snapshot.push_back(*note);

    processor.getUndoManager().perform(
        new midi_funfun::core::DeleteNotesAction(processor.getEditedNotesForEditing(), snapshot), "Delete Notes");

    refreshSize();
    repaint();
}

bool PianoRollComponent::Canvas::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey)                                        { deleteSelected(); return true; }
    if (key == juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0))       { processor.getUndoManager().undo(); return true; }
    if (key == juce::KeyPress('y', juce::ModifierKeys::commandModifier, 0))       { processor.getUndoManager().redo(); return true; }
    if (key == juce::KeyPress('a', juce::ModifierKeys::commandModifier, 0))       { selection.selectAll(processor.getAnalyzedNotes()); repaint(); return true; }
    if (key == juce::KeyPress::upKey)                                            { nudge(1, 0); return true; }
    if (key == juce::KeyPress::downKey)                                          { nudge(-1, 0); return true; }
    if (key == juce::KeyPress::leftKey)                                          { nudge(0, -midi_funfun::core::gridUnitTicks); return true; }
    if (key == juce::KeyPress::rightKey)                                         { nudge(0, midi_funfun::core::gridUnitTicks); return true; }
    return false;
}
```

- [ ] **Step 3: ビルド成功 + 実機確認**

Run: `cmake --build build --config Debug`

Standaloneで(ピアノロール上でクリックしてフォーカスを与えてから): `Delete`で選択ノート削除、`Ctrl+Z`/`Ctrl+Y`でUndo/Redo、`Ctrl+A`で全選択、矢印キーで選択ノートが半音/グリッド単位移動すること。矢印nudgeも`Ctrl+Z`一回で元に戻ること(ドラッグ移動と同じ`MoveNotesAction`を共有しているため)。

---

### Task 16: Undo/Redo後の再描画・選択整合性 + 再生ヘッドのTimerポーリング

**Files:** Modify `Source/UI/PianoRollComponent.h`, `Source/UI/PianoRollComponent.cpp`, `Source/Plugin/PluginEditor.h`, `Source/Plugin/PluginEditor.cpp`

自動テスト対象外。Standalone実機確認で検証する。M2で判明した「データが変わっただけでは自動再描画されない」落とし穴への対応(スペック§5.5、Global Constraints)。

**Interfaces:**
- Consumes: `juce::UndoManager`は`juce::ChangeBroadcaster`を継承(JUCE標準)。`processor.getPlaybackCurrentTick()`(Task 9)。既存`PluginEditor`の30Hz `Timer`(`startTimerHz(30)`、既存の`timerCallback()`)。
- Produces: `Canvas`が`juce::ChangeListener`を実装。`PianoRollComponent`に再生ヘッド位置を公開する`getLastKnownPlaybackTick()`は不要(`Canvas`が直接`processor`を参照できるため、`PluginEditor::timerCallback()`から`pianoRoll`経由でCanvasへポーリングを委譲する薄いメソッドを追加する)。

- [ ] **Step 1: `Canvas`を`juce::ChangeListener`にする**

`PianoRollComponent.h`の`Canvas`宣言を`class Canvas final : public juce::Component, private juce::ChangeListener`へ変更する。ファイル先頭に`#include <juce_events/juce_events.h>`を追加(`juce::ChangeListener`/`juce::ChangeBroadcaster`用)。

`Canvas`の`public:`セクション(`refreshSize();`の直後)に追加(`PianoRollComponent`側から`canvas.pollPlayhead()`を呼ぶため`public`が必須。入れ子クラスのアクセス制御は非対称であり、外側のクラスは入れ子クラスの`private`メンバを呼べない):

```cpp
void pollPlayhead(); // PluginEditorのTimerからPianoRollComponent経由で呼ばれる
```

`private:`セクションに追加(内部専用):

```cpp
void changeListenerCallback(juce::ChangeBroadcaster*) override;
juce::int64 lastKnownPlaybackTick = -1;
```

`PianoRollComponent`本体にも中継メソッドを追加:

```cpp
void pollPlayhead(); // canvas.pollPlayhead() を呼ぶだけ
```

- [ ] **Step 2: コンストラクタでリスナー登録し、`changeListenerCallback`を実装**

`Canvas`のコンストラクタ(Task 15で追加した`setWantsKeyboardFocus(true)`の後)に追加:

```cpp
    processor.getUndoManager().addChangeListener(this);
```

デストラクタを追加(登録解除、リークしたリスナー参照を防ぐ):

```cpp
~Canvas() override { processor.getUndoManager().removeChangeListener(this); }
```

(`PianoRollComponent.h`の`Canvas`宣言の`public:`セクション〈コンストラクタ宣言の直後〉に`~Canvas() override;`を追加する。`PianoRollComponent`が`Canvas canvas;`をメンバとして持つ以上、`PianoRollComponent`の暗黙のデストラクタが`canvas`のデストラクタを呼び出せる必要があり、これも`public`である必要がある。)

`changeListenerCallback`実装:

```cpp
void PianoRollComponent::Canvas::changeListenerCallback(juce::ChangeBroadcaster*)
{
    selection.pruneMissing(processor.getAnalyzedNotes());
    refreshSize();
    repaint();
}
```

- [ ] **Step 3: 再生ヘッドのポーリング + 描画**

```cpp
void PianoRollComponent::Canvas::pollPlayhead()
{
    if (processor.getPlaybackState() != midi_funfun::core::PlaybackTransport::State::Playing)
        return;

    const auto tick = processor.getPlaybackCurrentTick();
    if (tick != lastKnownPlaybackTick)
    {
        lastKnownPlaybackTick = tick;
        repaint();
    }
}

void PianoRollComponent::pollPlayhead()
{
    canvas.pollPlayhead();
}
```

`paint()`の末尾(マーキー矩形描画の後)に再生ヘッド線を追加:

```cpp
    if (processor.getPlaybackState() == midi_funfun::core::PlaybackTransport::State::Playing)
    {
        const int x = tickToX(processor.getPlaybackCurrentTick());
        g.setColour(juce::Colours::red);
        g.drawVerticalLine(x, (float) rulerHeightPx, (float) getHeight());
    }
```

- [ ] **Step 4: `PluginEditor`の既存30Hz Timerに相乗り**

`PluginEditor.cpp`の`timerCallback()`末尾に追加:

```cpp
    pianoRoll.pollPlayhead();
```

- [ ] **Step 5: ビルド成功 + 実機確認**

Run: `cmake --build build --config Debug`

Standaloneで: Undo/Redoを繰り返してもピアノロールの表示が即座に(次の操作を待たず)正しく更新されること。削除→Undoで復元されたノートが選択状態から漏れなく除去/復元されること(`pruneMissing`)。再生開始後、赤い再生ヘッド線がなめらかに動くこと(Task 19でPlay/Stopボタンを追加するまでは、一時的に`processor.startPlayback()`をどこかのボタンから呼んで確認してもよい)。

---

### Task 17: 波形・ピッチカーブのオーバーレイ描画

**Files:** Modify `Source/UI/PianoRollComponent.h`, `Source/UI/PianoRollComponent.cpp`

自動テスト対象外(描画)。`WaveformPeakCache::build`自体はTask 6で既にTDD済み。

**Interfaces:**
- Consumes: `WaveformPeakCache::build`(Task 6)、`processor.getWaveformSourceTake()`・`getLastAnalysisResult()`(Task 9)。
- Produces: `Canvas::paintWaveform`・`Canvas::paintPitchCurve`(スペック§5.7)、ズーム/スクロール変更時のみ再計算するキャッシュ。

- [ ] **Step 1: `Canvas`に波形キャッシュメンバとキャッシュ再計算メソッドを追加**

`PianoRollComponent.h`の`Canvas`宣言の`public:`セクション(`refreshSize();`の直後、Task 11参照)に追加(`PianoRollComponent`側から`canvas.rebuildWaveformCache()`を呼ぶため`public`にする必要がある。C++の入れ子クラスはアクセス制御が非対称であり、外側のクラス〈`PianoRollComponent`〉は入れ子クラス〈`Canvas`〉の`private`メンバを呼べない点に注意):

```cpp
void rebuildWaveformCache(); // ズーム/スクロール変更時・テイク変更時に呼ぶ(paint()内では呼ばない)
```

`Canvas`の`private:`セクション(`paintRuler(juce::Graphics& g);`の下)に追加(`paint()`内部からのみ呼ばれるため`private`のままでよい):

```cpp
std::vector<midi_funfun::core::PeakPair> waveformCache;

void paintWaveform(juce::Graphics& g);
void paintPitchCurve(juce::Graphics& g);
```

`#include "../Core/Audio/WaveformPeakCache.h"`を`PianoRollComponent.h`の先頭に追加。

- [ ] **Step 2: `rebuildWaveformCache`・`paintWaveform`・`paintPitchCurve`を実装**

```cpp
void PianoRollComponent::Canvas::rebuildWaveformCache()
{
    const auto* take = processor.getWaveformSourceTake();
    if (take == nullptr || take->numSamplesRecorded <= 0)
    {
        waveformCache.clear();
        return;
    }

    waveformCache = midi_funfun::core::WaveformPeakCache::build(
        take->buffer.getReadPointer(0), take->numSamplesRecorded, std::max(1, getWidth()));
}

void PianoRollComponent::Canvas::paintWaveform(juce::Graphics& g)
{
    if (waveformCache.empty())
        return;

    const float midY = (float) (rulerHeightPx + (getHeight() - rulerHeightPx) / 2);
    const float amplitudeScale = (float) (getHeight() - rulerHeightPx) * 0.5f;

    g.setColour(juce::Colours::grey.withAlpha(0.35f));
    for (int x = 0; x < (int) waveformCache.size() && x < getWidth(); ++x)
    {
        const auto& peak = waveformCache[(size_t) x];
        g.drawVerticalLine(x, midY - peak.maxValue * amplitudeScale, midY - peak.minValue * amplitudeScale);
    }
}

void PianoRollComponent::Canvas::paintPitchCurve(juce::Graphics& g)
{
    const auto& result = processor.getLastAnalysisResult();
    if (result.pitchFrames.empty() || result.sampleRate <= 0.0)
        return;

    juce::Path path;
    bool started = false;

    for (size_t i = 0; i < result.pitchFrames.size(); ++i)
    {
        const auto& frame = result.pitchFrames[i];
        if (!frame.voiced || frame.frequencyHz <= 0.0)
        {
            started = false;
            continue;
        }

        const double seconds = (double) (i * (size_t) result.hopSize) / result.sampleRate;
        const double bpm = processor.getAnalyzedNotesBpm();
        const juce::int64 tick = (juce::int64) std::round(seconds * (bpm / 60.0) * midi_funfun::core::ticksPerQuarterNote);
        const double fractionalPitch = 69.0 + 12.0 * std::log2(frame.frequencyHz / 440.0);

        const float x = (float) tickToX(tick);
        const float y = (float) (rulerHeightPx + (highestVisiblePitch - fractionalPitch) * pixelsPerSemitone);

        if (!started)
        {
            path.startNewSubPath(x, y);
            started = true;
        }
        else
        {
            path.lineTo(x, y);
        }
    }

    g.setColour(juce::Colours::darkorange.withAlpha(0.6f));
    g.strokePath(path, juce::PathStrokeType(1.5f));
}
```

`paint()`を以下の順序へ更新(波形はグリッドの背後、ノートの前に描く。ピッチカーブはノートの後ろでも前でもよいが、視認性のためノートの後に薄く重ねる):

```cpp
void PianoRollComponent::Canvas::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::whitesmoke);
    paintGrid(g);
    paintWaveform(g);
    paintNotes(g);
    paintPitchCurve(g);
    paintRuler(g);
    // (マーキー矩形・再生ヘッドはTask 13/16で追加済みの箇所のまま、この後に描画する)
```

- [ ] **Step 3: 解析完了時・ズーム変更時に`rebuildWaveformCache()`を呼ぶ**

`PianoRollComponent::fitVerticalRangeToNotes()`(Task 11、解析ボタン押下時に呼ばれる)の末尾に追加:

```cpp
    canvas.rebuildWaveformCache();
```

(Task 18で横ズーム変更ハンドラを追加する際にも同様に`rebuildWaveformCache()`を呼ぶよう指示する。)

- [ ] **Step 4: ビルド成功 + 実機確認**

Run: `cmake --build build --config Debug`

Standaloneで: 解析後、ノートグリッドの背後に元波形が半透明で表示されること、検出ノートに重ねてオレンジの生ピッチ曲線が表示されること(ノートの音高階段状の中心を曲線がなぞるように見えること)。

---

### Task 18: ズーム・スクロール(横スクロールはViewport標準機能、縦ズームはCtrl+ホイール、手動ズームスライダー)

**Files:** Modify `Source/UI/PianoRollComponent.h`, `Source/UI/PianoRollComponent.cpp`, `Source/Plugin/PluginEditor.h`, `Source/Plugin/PluginEditor.cpp`

自動テスト対象外。Standalone実機確認で検証する。

**Interfaces:**
- Consumes: API確定1-2節のズーム境界定数(`minPixelsPerTick`/`maxPixelsPerTick`/`minPixelsPerSemitone`/`maxPixelsPerSemitone`、Task 11で`midi_funfun::ui`名前空間スコープに既に定義済み、`Canvas`のメンバではない)。
- Produces: `PianoRollComponent::setHorizontalZoom(double)`・`setVerticalZoom(double)`(ツールバースライダーから呼ぶ)。`Canvas::mouseWheelMove`(Ctrl+ホイールでの縦ズーム)。`rollViewport`と`pianoKeyboard`の垂直スクロール同期。

- [ ] **Step 1: 水平スクロールをViewport標準機能へ委ねる(既存配線の確認のみ)**

Task 11で`rollViewport.setViewedComponent(&canvas, false)`済みのため、水平方向のスクロールバー・トラックパッド/Shift+ホイールでの横スクロールは追加実装不要(JUCE `juce::Viewport`の既定動作)。このステップは確認のみ: Standaloneで`Canvas`の幅がコンポーネント表示域より広い状態(ノートが多い、またはズームイン後)で横スクロールバーが機能することを目視確認する。

- [ ] **Step 2: `pianoKeyboard`を`rollViewport`の垂直スクロールに同期させる**

`PianoRollComponent.h`に`private juce::Viewport::Listener`を継承させ、宣言を追加:

```cpp
class PianoRollComponent final : public juce::Component, private juce::Viewport::Listener
{
    ...
private:
    void visibleAreaChanged(const juce::Rectangle<int>& newVisibleArea) override;
    ...
};
```

コンストラクタで`rollViewport.addListener(this);`を追加。実装:

```cpp
void PianoRollComponent::visibleAreaChanged(const juce::Rectangle<int>& newVisibleArea)
{
    pianoKeyboard.setTopLeftPosition(pianoKeyboard.getX(), -newVisibleArea.getY() + rollViewport.getY());
}
```

(鍵盤コンポーネント自体は`rollViewport`の外にあるため、垂直オフセットをここで手動同期する。水平方向はそもそも`pianoKeyboard`はスクロールしない固定表示。)

- [ ] **Step 3: `Canvas::mouseWheelMove`でCtrl+ホイールの縦ズームを実装**

`PianoRollComponent.h`の`Canvas`宣言に追加:

```cpp
void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
```

```cpp
void PianoRollComponent::Canvas::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (!e.mods.isCtrlDown())
        return; // Ctrl修飾なしは既定のスクロール(Viewportに委ねる)ため何もしない

    const double factor = wheel.deltaY > 0 ? 1.1 : (1.0 / 1.1);
    pixelsPerSemitone = juce::jlimit(minPixelsPerSemitone, maxPixelsPerSemitone, pixelsPerSemitone * factor);
    refreshSize();
    repaint();
}
```

- [ ] **Step 4: ツールバーにズームスライダーを追加(`PluginEditor`)**

`PluginEditor.h`に追加:

```cpp
juce::Label horizontalZoomLabel;
juce::Slider horizontalZoomSlider;
juce::Label verticalZoomLabel;
juce::Slider verticalZoomSlider;
```

`PianoRollComponent`に公開メソッドを追加(`PianoRollComponent.h`/`.cpp`):

```cpp
void setHorizontalZoom(double pixelsPerTick);
void setVerticalZoom(double pixelsPerSemitone);
```

```cpp
void PianoRollComponent::setHorizontalZoom(double pixelsPerTick)
{
    canvas.pixelsPerTick = juce::jlimit(minPixelsPerTick, maxPixelsPerTick, pixelsPerTick);
    canvas.refreshSize();
    canvas.rebuildWaveformCache();
    canvas.repaint();
}

void PianoRollComponent::setVerticalZoom(double pixelsPerSemitone)
{
    canvas.pixelsPerSemitone = juce::jlimit(minPixelsPerSemitone, maxPixelsPerSemitone, pixelsPerSemitone);
    canvas.refreshSize();
    pianoKeyboard.setVisibleRange(canvas.lowestVisiblePitch, canvas.highestVisiblePitch, canvas.pixelsPerSemitone);
    canvas.repaint();
}
```

(`minPixelsPerTick`等はTask 11で`midi_funfun::ui`名前空間スコープに定義済みの定数〈`Canvas`のメンバではない〉であり、`PianoRollComponent`の自身のメソッド内では修飾なしでそのまま参照できる。)

`PluginEditor.cpp`のコンストラクタに追加(既存の`noiseGateSlider`等と同じ`LinearHorizontal`+`TextBoxRight`スタイル):

```cpp
horizontalZoomLabel.setText("H-Zoom", juce::dontSendNotification);
addAndMakeVisible(horizontalZoomLabel);
horizontalZoomSlider.setSliderStyle(juce::Slider::LinearHorizontal);
horizontalZoomSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
horizontalZoomSlider.setRange(midi_funfun::ui::minPixelsPerTick, midi_funfun::ui::maxPixelsPerTick, 0.01);
horizontalZoomSlider.setValue(0.25, juce::dontSendNotification);
horizontalZoomSlider.onValueChange = [this] { pianoRoll.setHorizontalZoom(horizontalZoomSlider.getValue()); };
addAndMakeVisible(horizontalZoomSlider);

verticalZoomLabel.setText("V-Zoom", juce::dontSendNotification);
addAndMakeVisible(verticalZoomLabel);
verticalZoomSlider.setSliderStyle(juce::Slider::LinearHorizontal);
verticalZoomSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
verticalZoomSlider.setRange(midi_funfun::ui::minPixelsPerSemitone, midi_funfun::ui::maxPixelsPerSemitone, 1.0);
verticalZoomSlider.setValue(16.0, juce::dontSendNotification);
verticalZoomSlider.onValueChange = [this] { pianoRoll.setVerticalZoom(verticalZoomSlider.getValue()); };
addAndMakeVisible(verticalZoomSlider);
```

(`midi_funfun::ui::minPixelsPerTick`等はTask 11で`Source/UI/PianoRollComponent.h`の名前空間スコープに定義済みの定数であり、`Canvas`を経由せず直接参照できる — `Canvas`はスペック§5.1通り`private`ネストクラスのままでよい。`resized()`内の配置はツールバー2段目〈`analysisToolbar`〉の空きスペース、または3段目を新設して収める。)

- [ ] **Step 5: ビルド成功 + 実機確認**

Run: `cmake --build build --config Debug`

Standaloneで: H-Zoom/V-Zoomスライダーでピアノロールの拡大縮小ができ、鍵盤の行位置がノート行とズレないこと。Ctrl+ホイールでも縦ズームができること。ズーム後、波形オーバーレイが正しい列幅で再計算されること(粗くなったりズレたりしない)。

---

### Task 19: `PlaybackTransportBar`(Play/Stop)

**Files:** Modify `Source/UI/PlaybackTransportBar.h`, `Source/UI/PlaybackTransportBar.cpp`, `Source/Plugin/PluginEditor.h`, `Source/Plugin/PluginEditor.cpp`

自動テスト対象外。Standalone実機確認で検証する。再生ヘッド描画・Timerポーリング自体はTask 16で実装済み。本タスクはPlay/Stopボタンの追加のみ。

**Interfaces:**
- Consumes: `processor.startPlayback()`/`stopPlayback()`/`getPlaybackState()`(Task 9)。
- Produces: `PlaybackTransportBar`(コンストラクタで`MidiFunfunAudioProcessor&`を受け取る)。

- [ ] **Step 1: `PlaybackTransportBar.h`を本実装へ**

```cpp
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Plugin/PluginProcessor.h"

namespace midi_funfun::ui
{
    /** Play/Stopのみのシンプルなトランスポートバー(スペック§5.8。一時停止・シークは実装しない)。 */
    class PlaybackTransportBar final : public juce::Component, private juce::Timer
    {
    public:
        explicit PlaybackTransportBar(MidiFunfunAudioProcessor& processorIn);
        void resized() override;

    private:
        void timerCallback() override;
        void playStopClicked();
        void updateAppearance();

        MidiFunfunAudioProcessor& processor;
        juce::TextButton playStopButton;
    };
}
```

- [ ] **Step 2: `PlaybackTransportBar.cpp`を本実装へ**

```cpp
#include "PlaybackTransportBar.h"

namespace midi_funfun::ui
{
    PlaybackTransportBar::PlaybackTransportBar(MidiFunfunAudioProcessor& processorIn) : processor(processorIn)
    {
        playStopButton.onClick = [this] { playStopClicked(); };
        addAndMakeVisible(playStopButton);
        updateAppearance();
        startTimerHz(15); // ボタン見た目の状態(再生中/停止中)の追随のみ。再生ヘッド自体はPianoRollComponent側の30Hz Timer(Task 16)。
    }

    void PlaybackTransportBar::resized()
    {
        playStopButton.setBounds(getLocalBounds());
    }

    void PlaybackTransportBar::timerCallback()
    {
        updateAppearance();
    }

    void PlaybackTransportBar::playStopClicked()
    {
        if (processor.getPlaybackState() == midi_funfun::core::PlaybackTransport::State::Playing)
            processor.stopPlayback();
        else
            processor.startPlayback();

        updateAppearance();
    }

    void PlaybackTransportBar::updateAppearance()
    {
        const bool isPlaying = processor.getPlaybackState() == midi_funfun::core::PlaybackTransport::State::Playing;
        playStopButton.setButtonText(isPlaying ? "Stop" : "Play");
        playStopButton.setColour(juce::TextButton::buttonColourId, isPlaying ? juce::Colours::red : juce::Colours::darkgreen);
    }
}
```

- [ ] **Step 3: `PluginEditor`に配線**

`PluginEditor.h`: `#include "../UI/PlaybackTransportBar.h"`を追加し、`private:`セクション末尾に`midi_funfun::ui::PlaybackTransportBar playbackTransportBar { processorRef };`を追加。

`PluginEditor.cpp`のコンストラクタに`addAndMakeVisible(playbackTransportBar);`を追加。`resized()`内、ピアノロール領域の上または下に幅80px程度の領域を確保して`playbackTransportBar.setBounds(...)`する。

- [ ] **Step 4: ビルド成功 + 実機確認**

Run: `cmake --build build --config Debug`

Standaloneで: 「Play」ボタン押下で内蔵ピアノ音源によるノート列の通し再生が始まり、再生ヘッドが動くこと。「Stop」で停止し、次回「Play」で先頭(tick=0)から再度再生されること(位置記憶なし、スペック§5.8)。

---

### Task 20: Milestone 2暫定パネルの削除(スペック§5.9)

**Files:**
- Modify: `Source/Plugin/PluginEditor.h`, `Source/Plugin/PluginEditor.cpp`, `Source/Core/CMakeLists.txt`, `Tests/CMakeLists.txt`
- Delete: `Source/Core/Model/NoteFormatting.h`, `Tests/NoteFormattingTests.cpp`

新パネル(Task 11〜19のピアノロール)の実装が完了した今、M2の仮UIを削除する。新旧を並存させない(スペック§5.9)。

- [ ] **Step 1: `PluginEditor.h`から`NoteListBoxModel`関連を削除**

削除対象: `private juce::ListBoxModel`の継承(`class MidiFunfunAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer, private juce::ListBoxModel`から`private juce::ListBoxModel`を除く。ただし`takeListBox`が同じ`ListBoxModel`実装〈`getNumRows`/`paintListBoxItem`/`selectedRowsChanged`〉を使っているため、**削除してよいのは`NoteListBoxModel`ネストクラスとそのメンバのみ**であり、`MidiFunfunAudioProcessorEditor`自身の`juce::ListBoxModel`継承〈テイク一覧用〉は残すこと)。

具体的に削除する行:
- `class NoteListBoxModel final : public juce::ListBoxModel { ... };`(ネストクラス全体)
- `juce::TextButton analyzeButton;`以降の解析関連UIメンバ(`noiseGateLabel`/`noiseGateSlider`/`minNoteLengthLabel`/`minNoteLengthSlider`/`defaultVelocityLabel`/`defaultVelocitySlider`)は**残す**(ピアノロールの前段としての解析設定UIは引き続き必要)。削除するのは`analysisStatusLabel`・`NoteListBoxModel noteListBoxModel { processorRef };`・`juce::ListBox noteListBox { "Notes", &noteListBoxModel };`のみ。
- `#include "Model/NoteFormatting.h"`を`PluginEditor.h`から削除。

- [ ] **Step 2: `PluginEditor.cpp`から関連実装を削除**

削除対象:
- `MidiFunfunAudioProcessorEditor::NoteListBoxModel::getNumRows()`/`paintListBoxItem()`の定義。
- `ticksToSeconds`ヘルパー(`NoteListBoxModel::paintListBoxItem`専用だった場合は削除。他の箇所で使われていないか確認してから削除すること)。
- コンストラクタ内の`noteListBox.setRowHeight(20); addAndMakeVisible(noteListBox);`・`analysisStatusLabel`関連の初期化行。
- `resized()`内の`notesArea`/`noteListBox`/`analysisStatusLabel`のレイアウト行(この領域は既にTask 11で`pianoRoll`に置き換わっているはずなので、重複したレイアウトコードが残っていないか確認する)。
- `analyzeButtonClicked()`内の`noteListBox.updateContent(); noteListBox.repaint(); analysisStatusLabel.setText(...)`(Task 11で追加した`pianoRoll.fitVerticalRangeToNotes();`はそのまま残す)。

- [ ] **Step 3: `Source/Core/Model/NoteFormatting.h`と対応テストを削除**

`Source/Core/Model/NoteFormatting.h`を削除。`Tests/NoteFormattingTests.cpp`を削除。

`Source/Core/CMakeLists.txt`の`add_library`ソース一覧から`Model/NoteFormatting.h`を削除。

`Tests/CMakeLists.txt`の`add_executable`ソース一覧から`NoteFormattingTests.cpp`を削除。

- [ ] **Step 4: ビルド + 全テスト成功を確認**

Run: `cmake --build build --config Debug && ctest --test-dir build -C Debug --output-on-failure`
Expected: ビルド成功(`NoteFormatting.h`/`NoteListBoxModel`への参照が残っていないこと)、全テストPASS(`NoteFormattingTests`が一覧から消えていること)。

- [ ] **Step 5: Standalone実機確認**

Run: Standaloneを起動し、旧ノート一覧パネルが画面上に存在しない(ピアノロールのみが表示される)ことを確認する。

---

### Task 21: 最終確認・単一コミット・push

**Files:** なし(検証とgit操作のみ)

- [ ] **Step 1: 全テストパスを確認**

Run: `ctest --test-dir build -C Debug --output-on-failure`
Expected: 全テストPASS。本マイルストーン開始時点の既存テストは40件(`grep -rc "TEST_CASE" Tests/*.cpp`で確認可能)。そこから`NoteFormattingTests.cpp`の2件がTask 20で削除され、Task 1-8で新規テストが追加される(`NoteSequence`+6件・`Selection`+7件・`EditCommands`+6件・`GridSnap`+3件・`PitchAnalyzer`は既存2件を型変更に追随させるのみで件数増減なし・`WaveformPeakCache`+3件・`PlaybackTransport`+5件・`PianoVoice`+3件)。合計で40 - 2 + 33 = 71件がPASSする見込み(実装中に個別テストの分割等で多少前後しても構わない。重要なのは全件PASSであること)。

- [ ] **Step 2: Standalone/VST3双方ビルド成功を確認**

Run: `cmake --build build --config Debug`
Expected: 成功。

- [ ] **Step 3: Standaloneで一通りの手動シナリオを実行**

録音→解析→ピアノロールでノートを移動・リサイズ・削除・追加・選択(単一/複数/矩形)→Undo/Redo→矢印キーnudge→波形/ピッチカーブオーバーレイの表示確認→ズーム/スクロール操作→ノートクリックでの単音プレビュー→Play/Stopでの通し再生→再解析(2回目の「解析」ボタン押下)でのUndo経由の置き換え確認、を一通り実施し、クラッシュ・明らかな不具合が無いことを確認する。

- [ ] **Step 4: 単一コミット・push**

```bash
git add Source/Core/Model/Note.h \
        Source/Core/Model/NoteSequence.h Source/Core/Model/NoteSequence.cpp \
        Source/Core/Model/Selection.h Source/Core/Model/Selection.cpp \
        Source/Core/Model/EditCommands.h Source/Core/Model/EditCommands.cpp \
        Source/Core/Model/GridSnap.h Source/Core/Model/GridSnap.cpp \
        Source/Core/Pitch/PitchAnalyzer.h Source/Core/Pitch/PitchAnalyzer.cpp \
        Source/Core/Audio/WaveformPeakCache.h Source/Core/Audio/WaveformPeakCache.cpp \
        Source/Core/Audio/PlaybackTransport.h Source/Core/Audio/PlaybackTransport.cpp \
        Source/Core/Audio/PianoVoice.h Source/Core/Audio/PianoVoice.cpp \
        Source/Core/CMakeLists.txt \
        Source/Plugin/PluginProcessor.h Source/Plugin/PluginProcessor.cpp \
        Source/Plugin/PluginEditor.h Source/Plugin/PluginEditor.cpp \
        Source/Plugin/CMakeLists.txt \
        Source/UI/PianoRollComponent.h Source/UI/PianoRollComponent.cpp \
        Source/UI/PianoKeyboardComponent.h Source/UI/PianoKeyboardComponent.cpp \
        Source/UI/PlaybackTransportBar.h Source/UI/PlaybackTransportBar.cpp \
        Tests/CMakeLists.txt \
        Tests/NoteSequenceTests.cpp Tests/SelectionTests.cpp Tests/EditCommandsTests.cpp \
        Tests/GridSnapTests.cpp Tests/PitchAnalyzerTests.cpp Tests/WaveformPeakCacheTests.cpp \
        Tests/PlaybackTransportTests.cpp Tests/PianoVoiceTests.cpp \
        docs/superpowers/plans/2026-07-12-midi-funfun-03-piano-roll.md

git rm Source/Core/Model/NoteFormatting.h Tests/NoteFormattingTests.cpp

git commit -m "$(cat <<'EOF'
Add Milestone 3 piano roll (edit/select/undo-redo, waveform & pitch overlay, preview playback)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"

git push origin main
```

- [ ] **Step 5: プロジェクトステータスのメモリファイルを更新**

`project_midi_funfun_status.md`(ユーザーのメモリファイル)を更新し、Milestone 3完了・コミットハッシュ・Milestone 4(クオンタイズ)への再開ポイントを記録する。

