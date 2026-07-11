# MIDI FUNFUN Milestone 2: ピッチ検出(YIN)・ノート化 設計

日付: 2026-07-12
対象要件: [docs/requirements.md](../../requirements.md) v2 §4.2(ピッチ検出・MIDI変換)、§4.7(ベロシティ設定、生成ノートへの適用部分)
前提: [アーキテクチャ設計](2026-07-10-midi-funfun-architecture-design.md)、Milestone 0(雛形)完了・コミット済み(`3663d59`)、Milestone 1(録音)完了・コミット済み(`9afc9d9`、追加修正 `524fd52`)

この設計は要件4.2(ピッチ検出・MIDI変換)と、4.7のうち「解析で生成されるノートへのデフォルトベロシティ適用」を満たすための実装アーキテクチャを確定するもの。ピアノロールでのノート表示・編集(要件4.3、波形/連続ピッチカーブ表示含む)はMilestone 3のスコープであり、本設計の対象外。

## 1. スコープ

- YINアルゴリズムの自前実装によるオフラインピッチ検出(第1段階のみ。pYIN/Viterbi平滑化は対象外 — 詳細は6章)
- フレーム単位のピッチ曲線を半音単位に量子化し、ノート区間へセグメント化
- ノイズゲート/無音検出によるブレスノイズ・無音区間の除外(感度はUIスライダーで調整可能)
- 最小ノート長フィルタによる、ピッチのチラつきに起因する短すぎるノートの除去(閾値はUIスライダーで調整可能)
- オクターブエラー補正(孤立した単発オクターブジャンプを周辺音高に合わせて補正。しきい値は内部固定)
- 「解析」ボタン(選択中テイクに対する手動トリガー、オフラインバッチ処理)
- デフォルトベロシティ設定(数値入力、既定値90)を解析生成ノートへ適用
- 検出結果を目視確認するための**最小限のノート一覧パネル**(読み取り専用、ピアノロールの仮置き。Milestone 3で本実装のピアノロールに置き換え)

対象外(6章で再確認):ピアノロール本体(表示・編集・選択・Undo/Redo・波形/連続ピッチカーブ表示・プレビュー再生)、pYIN/Viterbi平滑化、クオンタイズ、スケール補正、MIDI書き出し。

## 2. Coreレイヤー

### 2.1 `Source/Core/Model/`(新規)

ノートのデータモデル。GUI非依存のプレーンな構造体。アーキテクチャ設計3章の型定義に従う。

```cpp
// Note.h
namespace midi_funfun::core
{
    // 要件4.8で確定しているPPQ(480)をここで先取りして使用する。
    // MIDIファイル書き出し自体はMilestone 6のスコープだが、ノートの時間表現は
    // 本マイルストーンで tick 単位に統一しておく。
    constexpr int ticksPerQuarterNote = 480;

    struct Note
    {
        int pitch = 0;              // MIDIノート番号(0-127)
        juce::int64 startTick = 0;
        juce::int64 lengthTicks = 0;
        int velocity = 90;           // 要件4.7の既定値
    };
}

// NoteSequence.h
namespace midi_funfun::core
{
    /** std::vector<Note> の薄いラッパー。Milestone 3でUndo/Redo対応の編集操作が
     *  この上に載る想定だが、本マイルストーンでは読み取り専用の入れ物として使う。 */
    class NoteSequence
    {
    public:
        void clear();
        void add(const Note& note);
        int size() const;
        const Note& operator[](int index) const;
        const std::vector<Note>& getNotes() const;

    private:
        std::vector<Note> notes;
    };
}
```

### 2.2 `Source/Core/Pitch/`(新規)

#### `YinPitchDetector`

自前実装のYINアルゴリズム。モノラル音声バッファ全体を固定フレーム長・固定ホップ長で走査し、フレームごとのピッチ推定値を返す。

```cpp
struct PitchFrame
{
    double frequencyHz = 0.0; // 0 = unvoiced(有意なピッチなし)
    double confidence = 0.0;  // 1 - CMNDF最小値(周期性の確からしさ、0〜1)
    double rmsLevel = 0.0;    // フレームのRMS振幅(線形、0〜1程度)。NoteSegmenterのノイズゲートで使用
    bool voiced = false;      // YINの絶対しきい値内で明確な周期が見つかったか
};

class YinPitchDetector
{
public:
    struct Settings
    {
        int windowSize = 2048;          // 約46ms @ 44.1kHz
        int hopSize = 512;               // 約11.6ms @ 44.1kHz(フレームレート ~86fps)
        double absoluteThreshold = 0.15; // YIN内部のCMNDFディップしきい値(内部固定、ユーザー非公開)
        double minFrequencyHz = 70.0;    // 探索範囲下限(E2 ~82Hzに余裕を持たせる)
        double maxFrequencyHz = 1000.0;  // 探索範囲上限
    };

    explicit YinPitchDetector(Settings settingsIn = {});

    /** monoSamples全体をhopSizeごとに走査し、フレーム単位のPitchFrame列を返す。 */
    std::vector<PitchFrame> analyze(const float* monoSamples, int numSamples, double sampleRate) const;
};
```

- 差分関数→累積平均正規化差分関数(CMNDF)→絶対しきい値によるダブ検出→放物線補間によるサブサンプル精度化、という古典的なYINの手順をそのまま実装する。
- `windowSize`/`hopSize`/探索周波数レンジは、ボーカル・鼻歌の想定音域(おおよそE2〜E5、82Hz〜660Hz)を十分にカバーする固定値とし、UIには公開しない(要件のスライダーは4.2で言及される「感度・しきい値」= ノイズゲートと最小ノート長のみを指す。YIN内部のCMNDFしきい値や窓長はDSP実装の詳細であり、要件はここへのUI公開を求めていない)。
- `rmsLevel`はフレームごとの単純RMSで、ノイズゲート判定用に`NoteSegmenter`へ渡す。YIN自体の`confidence`は将来的な信頼度可視化(要件7でMVPスコープ外と明記)や第2段階のpYIN導入時の材料として保持するが、本マイルストーンではノイズゲート判定には使わない(ノイズゲートは振幅ベースのRMSしきい値のみで行う。理由: ユーザーにとって「感度」は直感的には音量ベースの方が理解しやすく、要件4.2の文言「ノイズゲート/無音検出」も振幅ベースの一般的な意味と整合するため)。

#### `NoteSegmenter`

フレーム単位のピッチ曲線を、半音量子化・ノイズゲート・最小ノート長フィルタを経てノート区間列に変換する。

```cpp
struct RawNoteSegment
{
    int pitch = 0;       // MIDIノート番号(半音量子化済み)
    int startFrame = 0;  // PitchFrame列内のインデックス
    int lengthFrames = 0;
};

class NoteSegmenter
{
public:
    struct Settings
    {
        double noiseGateRmsThreshold = 0.02; // これ未満のRMSは無音/ノイズ扱い(UIの「ノイズゲート感度」スライダーから設定)
        double minNoteLengthSeconds = 0.06;  // これ未満の長さのノート区間は除去(UIの「最小ノート長」スライダーから設定)
    };

    explicit NoteSegmenter(Settings settingsIn = {});

    std::vector<RawNoteSegment> segment(const std::vector<PitchFrame>& frames, int hopSize, double sampleRate) const;
};
```

処理順序:
1. 各フレームについて、`rmsLevel < noiseGateRmsThreshold` または `voiced == false` なら「無音/非ノート」フレームとして扱う。
2. 有効な連続フレームを、周波数をMIDIノート番号へ丸めた値(`69 + 12 * log2(freq / 440)` を最近接整数に丸める)でグルーピングする。同一ピッチが連続する限り1つの区間として結合する(ピッチが1フレームだけ隣接半音へ揺れて戻るような瞬間的なチラつきは、次段の最小ノート長フィルタで吸収する想定であり、本段では単純な「直前フレームと同じ量子化ピッチか」の連続判定のみを行う)。
3. `lengthFrames * hopSize / sampleRate < minNoteLengthSeconds` の区間を除去する。

#### `OctaveErrorCorrector`

孤立した単発オクターブジャンプを検出し、周辺音高に合わせて補正する。しきい値は要件通り内部固定。

```cpp
class OctaveErrorCorrector
{
public:
    struct Settings
    {
        int octaveToleranceSemitones = 1;             // 「ちょうど1オクターブ差」とみなす許容幅(12±1半音)
        int neighborConsistencyToleranceSemitones = 1; // 前後のノートが「安定した文脈」とみなせる一致許容幅
    };

    explicit OctaveErrorCorrector(Settings settingsIn = {});

    /** segments を破壊的に補正する。 */
    void correct(std::vector<RawNoteSegment>& segments) const;
};
```

判定ロジック: 各ノート区間 `i`(先頭・末尾を除く。前後どちらか片方でも欠ける区間は判定対象外)について、
- 直前ノート `i-1` と直後ノート `i` の音高差が `neighborConsistencyToleranceSemitones` 以内(=前後が安定した文脈を形成している)、かつ
- ノート `i` の音高が、その安定文脈から `12 ± octaveToleranceSemitones` 半音離れている

場合、ノート `i` の音高を文脈側(前後ノート)に合わせて ±12 半音シフトする。タイミング・長さ・ベロシティは変更しない。

### 2.3 `PitchAnalyzer`(オーケストレーション、`Source/Core/Pitch/`)

上記3クラスと tick 変換・デフォルトベロシティ適用をまとめる、解析の唯一のエントリポイント。`RecordingTransport`が録音の状態遷移をまとめる役割に対応する、解析側のオーケストレータ。

```cpp
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

    /** Take(モノラル録音バッファ)を解析し、tick単位・デフォルトベロシティ適用済みのNoteSequenceを返す。 */
    NoteSequence analyze(const Take& take) const;
};
```

`analyze()`内部の流れ: `YinPitchDetector::analyze` → `NoteSegmenter::segment` → `OctaveErrorCorrector::correct` → 各`RawNoteSegment`のフレーム位置(`startFrame`/`lengthFrames`、hopSize基準)をサンプル位置へ変換 → `tick = samplePos / sampleRate * (bpm / 60) * ticksPerQuarterNote` で tick 化 → `velocity = settings.defaultVelocity` を設定 → `NoteSequence`に格納。

`Take`は`Source/Core/Audio/Take.h`のものをそのまま使う(Core内での参照であり、アーキテクチャのレイヤー分離である「Plugin/UI → Core」の一方向性には抵触しない。Core内でAudio→Pitchの依存が生じるが、これはCoreサブモジュール間の依存であり許容する)。

### 2.4 `TakeManager`への追加(既存ファイルの拡張)

解析はGUIスレッド(「解析」ボタン押下)から選択中テイクの生データを読む必要があるが、現状の`TakeManager`にはテイクの長さ・件数のみを返すAPIしかなく、`Take`本体(バッファ)へのアクセス手段がない。以下を追加する。

```cpp
/** 指定インデックスのTakeへの参照を返す。範囲外ならnullptr。解析(GUIスレッド)用の読み取り専用アクセス。 */
const Take* getTake(int index) const;
```

録音中のテイク(`recordingIndex`)を解析対象に選ぶことは通常のUIフロー上あり得ない(録音停止後にテイクを選択して解析する)想定だが、念のため`getTake`は`lock`で排他した上でスナップショット的に参照を返す(呼び出し側は解析中に別テイクの録音が始まらないことを前提とする — 本アプリはシングルユーザーの手動操作フローであり、実運用上「解析ボタンを押しながら同時に録音ボタンも押す」は想定しない)。

## 3. Pluginレイヤー

### 3.1 `PluginProcessor`

- `PitchAnalyzer`のインスタンスは保持せず、「解析」実行のたびに現在のUI設定値(ノイズゲート感度・最小ノート長・BPM・デフォルトベロシティ)から`PitchAnalyzer::Settings`を組み立てて都度構築する(ステートレスな使い捨てオブジェクトとして扱う。理由: 設定値はUIスライダーからいつでも変更されうるため、都度最新値で構築する方が単純で状態不整合が起きにくい)。
- 新規メソッド:
  ```cpp
  /** 現在選択中のTakeを解析し、結果をanalyzedNotesへ格納する。選択中テイクが無ければ何もしない。 */
  void analyzeSelectedTake();
  const midi_funfun::core::NoteSequence& getAnalyzedNotes() const { return analyzedNotes; }
  ```
- 新規パラメータ(BPM等と同じ`std::atomic`パターンでGUIスレッドから読み書き):
  - `noiseGateSensitivity`(0〜100の%値。内部でRMSしきい値へ線形マッピング。0% → ほぼゲートしない、100% → 強くゲートする)
  - `minNoteLengthMs`(0〜300msを想定。既定60ms)
  - `defaultVelocity`(1〜127、既定90。要件4.7)
- `analyzeSelectedTake()`はメッセージスレッド上で同期的に実行する。5分テイクの解析でもYIN自体はオフライン用途としては軽量な処理であり、MVPでは非同期化しない。体感で許容できないほど遅い場合はバックグラウンドスレッド化を将来の改善候補とする(要件のリアルタイム制約は無いため、同期実行はMVPとして許容できる簡略化と位置づける)。

### 3.2 `PluginEditor`

ツールバーに以下を追加する(既存の録音系コントロールと並置):

- 「解析」ボタン(`juce::TextButton`)。押下で`processorRef.analyzeSelectedTake()`を呼び、結果をノート一覧パネルへ反映する。
- ノイズゲート感度スライダー(`juce::Slider`、0〜100%、既定50%)
- 最小ノート長スライダー(`juce::Slider`、0〜300ms、既定60ms)
- デフォルトベロシティ入力(`juce::Slider`、既存のBPM入力と同じ「エディット可能なテキストボックス付きスライダー」スタイル、1〜127、既定90)

**最小限のノート一覧パネル**(本マイルストーンの仮UI、Milestone 3で本実装のピアノロールに置き換え):

- 既存のテイク一覧(`juce::ListBox` + `ListBoxModel`)と同じパターンで、`juce::ListBox` + 専用の`ListBoxModel`実装(`PluginEditor`内のプライベートネストクラス、`LevelMeter`と同じ配置方針)を追加する。
- 各行は `ピッチ名 | 開始時刻(秒) | 長さ(秒)` を表示する。例: `C4   0.83s   0.41s`
- ピッチ名は`juce::MidiMessage::getMidiNoteName(pitch, true, true, 4)`相当(シャープ表記、オクターブ番号付き、中央Cをオクターブ4とする一般的な表記=MIDIノート60がC4)で生成する。
- 開始時刻・長さは`Note`の`startTick`/`lengthTicks`を、解析時のBPMを使って秒へ逆変換して表示する(tick→秒の往復変換が正しく行われていることの目視確認も兼ねる)。
- 読み取り専用。クリックしても選択・編集・プレビュー再生は行わない(それらはMilestone 3のピアノロールで実装)。
- 「解析」ボタン押下→`analyzeSelectedTake()`完了後に`updateContent()`を呼んで再描画する(テイク一覧の更新パターンを踏襲)。

## 4. テスト戦略

`Tests/`ターゲット(`midi_funfun_core` + Catch2、ヘッドレス)に以下を追加する:

- **`YinPitchDetector`**: 既知周波数の合成正弦波(ボーカル・鼻歌の想定音域をカバーする代表的な周波数、おおよそE2〜E5=82Hz〜660Hzの範囲で複数点)を入力し、検出周波数が**±10セント**以内であることを検証する。無音・極小振幅入力に対しては`voiced == false`となることも検証する。
  - ±10セントを許容誤差とする根拠: 半音は100セントであり、±10セントは「正しい音として認識される範囲」に十分収まりつつ、実装バグを検出できる意味のある基準として妥当。クリーンな合成音に対する自前YIN実装は実運用上これより高精度を狙えるはずだが、テストの許容誤差としては余裕を持たせる。
- **`NoteSegmenter`**: 既知のピッチ曲線(フレーム列)を与え、半音量子化・区間結合が正しく行われること、ノイズゲート閾値未満のフレームが除外されること、最小ノート長未満の区間が除去されることを境界値付近で検証する。
- **`OctaveErrorCorrector`**: 前後が安定した文脈の中に孤立したオクターブジャンプがあるケースで補正されること、前後の文脈自体が不安定(実際のメロディとしてのオクターブ移動が複数ノートにまたがる)ケースでは補正されないこと、先頭・末尾ノート(片側に文脈が無い)は補正対象外であることを検証する。
- **`PitchAnalyzer`**: 合成音声(単純な正弦波を複数音つないだもの)を`Take`相当の入力として与え、期待されるノート数・ピッチ・tick位置(既知のBPM・サンプルレートから逆算した期待値)が得られることを統合的に検証する。
- **`Note` / `NoteSequence`**: 生成・追加・イテレートの基本動作(自明だが他クラスのテストのアサーションに使うため最小限は用意する)。
- **`TakeManager::getTake`**: 既存の`TakeManagerTests.cpp`に、範囲内/範囲外インデックスでの参照取得を追加する。

GUI(ノート一覧パネルの描画、ボタン操作)は自動テスト対象外とし、Standaloneアプリを実際にビルド・起動して手動確認する(マイクで実際に鼻歌を録音→解析→パネルに妥当なノートが表示されることを目視確認)。

## 5. タスク分割(全体で1コミット、Milestone 0/1と同じ方針)

1. `Note` / `NoteSequence`(Model、TDD)
2. `YinPitchDetector`(Core/Pitch、TDD、合成正弦波での精度検証)
3. `NoteSegmenter`(Core/Pitch、TDD)
4. `OctaveErrorCorrector`(Core/Pitch、TDD)
5. `TakeManager::getTake`追加(既存ファイル拡張、TDD)
6. `PitchAnalyzer`オーケストレーション(Core/Pitch、TDD、統合的な合成音声テスト)
7. `PluginProcessor`への組み込み(`analyzeSelectedTake`、新規パラメータ、アクセサ)
8. ツールバーUI(解析ボタン・ノイズゲート感度スライダー・最小ノート長スライダー・デフォルトベロシティ入力)+ 実機ビルドで動作確認
9. 最小限のノート一覧パネルUI + 実際の録音→解析での動作確認
10. 最終確認・単一コミット・push

## 6. スコープ外の再確認

- **pYIN/Viterbi平滑化(要件4.2の第2段階)**: 本マイルストーンでは実装しない。要件本文が「ピッチのふらつきが実用上問題になった場合に限り」と条件付きで第2段階を位置づけており、その要否は第1段階(素のYIN)を実際の録音で試してみるまで判断できない。将来、実際の鼻歌/ボーカル録音での解析結果に実用上看過できないピッチのふらつきが見られた場合の、**将来検討事項の候補**として明記しておく(要件から削除するものではない)。
- **ピアノロール本体**(要件4.3: ノート表示・編集・選択・Undo/Redo・波形オーバーレイ・連続ピッチカーブ表示・声域自動フィット・再生ヘッド・ノートクリックプレビュー): Milestone 3のスコープ。本マイルストーンの「最小限のノート一覧パネル」は暫定の目視確認手段であり、Milestone 3実装時に削除・置き換えを行う。
- **クオンタイズ**(要件4.4)・**スケール補正**(要件4.5): それぞれMilestone 4・5のスコープ。
- **MIDI書き出し**(要件4.8): Milestone 6のスコープ。本マイルストーンで`Note`/`NoteSequence`をtick単位(PPQ 480)で表現するのは、後続マイルストーンでの再設計を避けるための先取りであり、実際のSMFファイル生成自体は行わない。
- 要件書7章のスコープ外事項(永続化、ポリフォニック対応、スケール補正強度調整、信頼度可視化、ベロシティ連動、キー/スケール自動推定)は、本設計でも対象としない。`PitchFrame::confidence`を保持するのは将来のpYIN導入・信頼度可視化の下地としてであり、本マイルストーンでUIに公開するものではない。
