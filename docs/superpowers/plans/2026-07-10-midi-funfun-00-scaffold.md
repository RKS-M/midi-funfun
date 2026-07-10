# MIDI FUNFUN 雛形(Milestone 0) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** JUCE(C++)+CMakeによるMIDI FUNFUNプロジェクトの雛形を構築し、スタンドアロン/VST3の両方が空の状態でビルドでき、Core層のユニットテストが `ctest` で通る状態にする。

**Architecture:** ルート`CMakeLists.txt`がJUCE 8.0.14とCatch2 v3.15.2を`FetchContent`で取得し、`Source/Core`(GUI非依存の静的ライブラリ `midi_funfun_core`)、`Source/Plugin`(`juce_add_plugin`によるStandalone+VST3ターゲット)、`Tests`(Catch2実行ファイル、`midi_funfun_core`のみリンク)の3つのサブディレクトリを束ねる。

**Tech Stack:** JUCE 8.0.14(FetchContent、gitに本体は含めない)、CMake 3.22+、C++20、Catch2 v3.15.2(FetchContent)、Windows 11 + MSVC(Visual Studio 2022 Build Tools)。

## Global Constraints

- JUCEバージョンは `8.0.14` に固定し、`GIT_TAG 8.0.14` で取得する(タグはJUCE公式リポジトリのタグ名そのまま、`v`prefixなし)。
- Catch2バージョンは `v3.15.2` に固定して取得する。
- JUCE本体・Catch2本体はリポジトリにコミットしない(`.gitignore`でビルドディレクトリごと除外)。
- C++標準は20(`CMAKE_CXX_STANDARD 20`)。
- ビルド形式は `juce_add_plugin(... FORMATS Standalone VST3 ...)` の1コードベースから両方生成する。
- プラグインメタ情報: `COMPANY_NAME "RKSM"` / `PRODUCT_NAME "MIDI FUNFUN"` / `BUNDLE_ID "com.rksm.midifunfun"` / `PLUGIN_MANUFACTURER_CODE Rksm` / `PLUGIN_CODE Mfun`。
- `midi_funfun_core` は `juce_core` / `juce_audio_basics` / `juce_dsp` にのみリンクする(GUI・オーディオデバイスモジュールは持たない)。
- **コミットはこのマイルストーン全体で1回のみ**(Task 4の最後)。Task 1〜3の途中ではコミットしない。
- 開発機はWindows 11。CMakeとVisual Studio 2022 Build Tools(C++ワークロード)がインストール済みであることを前提とする(本計画の実行前提としてインストール済み)。

---

### Task 1: `.gitignore` とルートCMakeLists雛形(JUCE/Catch2のFetchContentのみ)

**Files:**
- Create: `.gitignore`
- Create: `CMakeLists.txt`

**Interfaces:**
- Consumes: なし(最初のタスク)
- Produces: `FetchContent`で取得された `juce::*` ターゲット群と `Catch2::Catch2WithMain` ターゲット。Task 2/3 はこれらのターゲットをリンクして使う。CMakeモジュールパスに `${catch2_SOURCE_DIR}/extras` が追加され、`include(Catch)` で `catch_discover_tests` が使えるようになる。

- [ ] **Step 1: `.gitignore` を作成する**

```gitignore
# Build output (CMakeのout-of-sourceビルド先。FetchContentで取得したJUCE/Catch2本体もこの中に入る)
/build/
/out/
cmake-build-*/

# CMakeが誤ってソース直下に生成した場合の安全策
CMakeCache.txt
CMakeFiles/
cmake_install.cmake
CTestTestfile.cmake
_deps/

# Visual Studio
.vs/
*.sln
*.vcxproj
*.vcxproj.filters
*.vcxproj.user
*.user

# macOS(将来のmacOS移植時用)
.DS_Store
```

- [ ] **Step 2: ルート `CMakeLists.txt` を作成する(JUCE/Catch2の取得のみ、サブディレクトリはまだ追加しない)**

```cmake
cmake_minimum_required(VERSION 3.22)

project(MidiFunfun VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.14
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.15.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Catch2)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

enable_testing()
```

- [ ] **Step 3: CMake configureを実行し、JUCEとCatch2の取得・設定が成功することを確認する**

Run: `cmake -B build -S .`

Expected: コマンドが正常終了し(exit code 0)、出力の最後付近に `-- Build files have been written to: <...>/build` が表示される。初回はJUCEリポジトリのクローンが走るため数分かかる場合がある。エラー(`CMake Error`)が出ないこと。

---

### Task 2: `midi_funfun_core` ライブラリと `Tests` ターゲット(TDD)

**Files:**
- Create: `Source/Core/CMakeLists.txt`
- Create: `Source/Core/Version.h`
- Create: `Source/Core/Version.cpp`
- Create: `Tests/CMakeLists.txt`
- Test: `Tests/CoreVersionTests.cpp`
- Modify: `CMakeLists.txt`(`add_subdirectory` を追加)

**Interfaces:**
- Consumes: Task 1で用意された `juce::juce_core` / `juce::juce_audio_basics` / `juce::juce_dsp` / `Catch2::Catch2WithMain` ターゲット、および `include(Catch)` による `catch_discover_tests`。
- Produces: `midi_funfun_core` という名前のCMakeライブラリターゲット(以降のマイルストーンのCoreロジックは全てここに実装を追加していく)。`midi_funfun::core::libraryVersionString()` 関数(`Source/Core/Version.h` で宣言)。

- [ ] **Step 1: 失敗するテストを先に書く — `Tests/CoreVersionTests.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <string>

#include "Version.h"

TEST_CASE("libraryVersionString returns the expected version", "[core][version]")
{
    REQUIRE(std::string(midi_funfun::core::libraryVersionString()) == "0.1.0");
}
```

- [ ] **Step 2: `Tests/CMakeLists.txt` を作成する**

```cmake
add_executable(MidiFunfunTests
    CoreVersionTests.cpp
)

target_link_libraries(MidiFunfunTests PRIVATE
    midi_funfun_core
    Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(MidiFunfunTests)
```

- [ ] **Step 3: `Source/Core/CMakeLists.txt` を作成する**

```cmake
add_library(midi_funfun_core STATIC
    Version.h
    Version.cpp
)

target_include_directories(midi_funfun_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(midi_funfun_core PUBLIC
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_dsp
)

target_compile_features(midi_funfun_core PUBLIC cxx_std_20)
```

- [ ] **Step 4: `Source/Core/Version.h` を作成する(まだ実装しない、宣言のみ)**

```cpp
#pragma once

namespace midi_funfun::core
{
    /** MIDI FUNFUN core ライブラリの現在のバージョン文字列("0.1.0"のようなドット区切り)を返す。 */
    const char* libraryVersionString();
}
```

- [ ] **Step 5: ルート `CMakeLists.txt` の末尾に `add_subdirectory` を追加する**

`enable_testing()` の行の直後に以下を追記する:

```cmake
add_subdirectory(Source/Core)
add_subdirectory(Tests)
```

- [ ] **Step 6: `Source/Core/Version.cpp` はまだ作らず、configureとビルドを行いテストがリンクエラー/未定義参照で失敗することを確認する**

Run: `cmake -B build -S . && cmake --build build --config Debug --target MidiFunfunTests`

Expected: リンクエラー(`unresolved external symbol` あるいは `undefined reference`)で `midi_funfun::core::libraryVersionString` が見つからず失敗する。

- [ ] **Step 7: `Source/Core/Version.cpp` を実装する**

```cpp
#include "Version.h"

namespace midi_funfun::core
{
    const char* libraryVersionString()
    {
        return "0.1.0";
    }
}
```

- [ ] **Step 8: ビルドしてテストを実行し、パスすることを確認する**

Run: `cmake --build build --config Debug --target MidiFunfunTests && ctest --test-dir build -C Debug --output-on-failure`

Expected: ビルド成功、`ctest` の出力に `100% tests passed, 0 tests failed out of 1` が表示される。

---

### Task 3: Pluginターゲット(空のProcessor/Editor、Standalone+VST3ビルド)

**Files:**
- Create: `Source/Plugin/CMakeLists.txt`
- Create: `Source/Plugin/PluginProcessor.h`
- Create: `Source/Plugin/PluginProcessor.cpp`
- Create: `Source/Plugin/PluginEditor.h`
- Create: `Source/Plugin/PluginEditor.cpp`
- Modify: `CMakeLists.txt`(`add_subdirectory(Source/Plugin)` を追加)

**Interfaces:**
- Consumes: `midi_funfun_core`(Task 2)、JUCEの `juce_add_plugin` マクロ、`juce::juce_audio_utils` / `juce::juce_dsp` モジュール。
- Produces: `MidiFunfunAudioProcessor`(`juce::AudioProcessor`派生)、`MidiFunfunAudioProcessorEditor`(`juce::AudioProcessorEditor`派生)。以降のマイルストーン(録音・ピアノロール等)はこの2クラスにメンバー・UIコンポーネントを追加していく。

- [ ] **Step 1: `Source/Plugin/PluginProcessor.h` を作成する**

```cpp
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class MidiFunfunAudioProcessor final : public juce::AudioProcessor
{
public:
    MidiFunfunAudioProcessor();
    ~MidiFunfunAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiFunfunAudioProcessor)
};
```

- [ ] **Step 2: `Source/Plugin/PluginProcessor.cpp` を作成する**

```cpp
#include "PluginProcessor.h"
#include "PluginEditor.h"

MidiFunfunAudioProcessor::MidiFunfunAudioProcessor()
    : juce::AudioProcessor(BusesProperties()
                                .withInput("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void MidiFunfunAudioProcessor::prepareToPlay(double, int)
{
}

void MidiFunfunAudioProcessor::releaseResources()
{
}

void MidiFunfunAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
}

juce::AudioProcessorEditor* MidiFunfunAudioProcessor::createEditor()
{
    return new MidiFunfunAudioProcessorEditor(*this);
}

bool MidiFunfunAudioProcessor::hasEditor() const
{
    return true;
}

const juce::String MidiFunfunAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool MidiFunfunAudioProcessor::acceptsMidi() const
{
    return false;
}

bool MidiFunfunAudioProcessor::producesMidi() const
{
    return false;
}

bool MidiFunfunAudioProcessor::isMidiEffect() const
{
    return false;
}

double MidiFunfunAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int MidiFunfunAudioProcessor::getNumPrograms()
{
    return 1;
}

int MidiFunfunAudioProcessor::getCurrentProgram()
{
    return 0;
}

void MidiFunfunAudioProcessor::setCurrentProgram(int)
{
}

const juce::String MidiFunfunAudioProcessor::getProgramName(int)
{
    return {};
}

void MidiFunfunAudioProcessor::changeProgramName(int, const juce::String&)
{
}

void MidiFunfunAudioProcessor::getStateInformation(juce::MemoryBlock&)
{
    // MVP時点では永続化する状態を持たない。ホストからの保存要求に対して
    // クラッシュしない安全な空実装(要件6.1)。Milestone 7で再確認する。
}

void MidiFunfunAudioProcessor::setStateInformation(const void*, int)
{
    // 復元すべき状態がなくても安全に無視する(要件6.1)。
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MidiFunfunAudioProcessor();
}
```

- [ ] **Step 3: `Source/Plugin/PluginEditor.h` を作成する**

```cpp
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

class MidiFunfunAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit MidiFunfunAudioProcessorEditor(MidiFunfunAudioProcessor&);
    ~MidiFunfunAudioProcessorEditor() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    MidiFunfunAudioProcessor& processorRef;
    juce::Label titleLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiFunfunAudioProcessorEditor)
};
```

- [ ] **Step 4: `Source/Plugin/PluginEditor.cpp` を作成する**

```cpp
#include "PluginEditor.h"

namespace
{
    // 「青空」を意識した水色系の背景色(要件5章 UI/デザイン要件)。Milestone 7で最終調整する。
    const juce::Colour skyBlueBackground { 0xff8ecae6 };
}

MidiFunfunAudioProcessorEditor::MidiFunfunAudioProcessorEditor(MidiFunfunAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p)
{
    titleLabel.setText("MIDI FUNFUN", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setFont(juce::Font(juce::FontOptions().withHeight(24.0f)));
    addAndMakeVisible(titleLabel);

    setSize(600, 400);
}

void MidiFunfunAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(skyBlueBackground);
}

void MidiFunfunAudioProcessorEditor::resized()
{
    titleLabel.setBounds(getLocalBounds());
}
```

- [ ] **Step 5: `Source/Plugin/CMakeLists.txt` を作成する**

```cmake
juce_add_plugin(MidiFunfun
    COMPANY_NAME "RKSM"
    PRODUCT_NAME "MIDI FUNFUN"
    BUNDLE_ID "com.rksm.midifunfun"
    PLUGIN_MANUFACTURER_CODE Rksm
    PLUGIN_CODE Mfun
    FORMATS Standalone VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES "Tools"
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(MidiFunfun PRIVATE
    PluginProcessor.h
    PluginProcessor.cpp
    PluginEditor.h
    PluginEditor.cpp
)

target_include_directories(MidiFunfun PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(MidiFunfun PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(MidiFunfun PRIVATE
    midi_funfun_core
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
```

- [ ] **Step 6: ルート `CMakeLists.txt` に `add_subdirectory(Source/Plugin)` を追加する**

`add_subdirectory(Source/Core)` の直後に追記する:

```cmake
add_subdirectory(Source/Plugin)
```

- [ ] **Step 7: フルビルドを実行し、Standalone実行ファイルとVST3の両方が生成されることを確認する**

Run: `cmake -B build -S . && cmake --build build --config Debug`

Expected: ビルドが成功し、`build/Source/Plugin/MidiFunfun_artefacts/Debug/Standalone/MIDI FUNFUN.exe` と `build/Source/Plugin/MidiFunfun_artefacts/Debug/VST3/MIDI FUNFUN.vst3` が生成される(正確な出力先パスはJUCEのCMakeバージョンにより多少異なる場合があるため、`MidiFunfun_artefacts` 配下を確認する)。

- [ ] **Step 8: Standaloneアプリを手動起動し、ウィンドウが表示されることを目視確認する**

Run: `& "build/Source/Plugin/MidiFunfun_artefacts/Debug/Standalone/MIDI FUNFUN.exe"`(PowerShellで実行。実際のパスはStep 7で確認したものに合わせる)

Expected: 水色背景に「MIDI FUNFUN」というタイトルが中央表示されたウィンドウが起動する。クラッシュしないこと。ウィンドウを閉じて終了する。

---

### Task 4: 最終確認とコミット

**Files:**
- (新規ファイルなし。Task 1〜3で作成した全ファイルをステージしてコミットする)

**Interfaces:**
- Consumes: Task 1〜3で作成した全ファイル
- Produces: Milestone 0 の単一コミット(以降のマイルストーンはこの状態を土台にする)

- [ ] **Step 1: `git status` で意図した変更のみが含まれることを確認する**

Run: `git status`

Expected: `.gitignore`, `CMakeLists.txt`, `Source/Core/*`, `Source/Plugin/*`, `Tests/*` が untracked として表示される。`build/` は `.gitignore` により表示されないこと。

- [ ] **Step 2: テストとビルドを再実行し、最終確認する**

Run: `ctest --test-dir build -C Debug --output-on-failure`

Expected: `100% tests passed, 0 tests failed out of 1`

- [ ] **Step 3: ステージしてコミットする**

```bash
git add .gitignore CMakeLists.txt Source/Core Source/Plugin Tests
git commit -m "Add JUCE/CMake project scaffold (Standalone + VST3, Core lib, Catch2 tests)

Sets up the CMake build fetching JUCE 8.0.14 and Catch2 v3.15.2 via
FetchContent (JUCE itself is not committed to the repo), an empty
midi_funfun_core static library with a Catch2-tested version function,
and an empty MidiFunfunAudioProcessor/Editor building both Standalone
and VST3 targets from one codebase, per the architecture design.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
```

- [ ] **Step 4: GitHubにpushする**

Run: `git push origin main`

Expected: pushが成功し、`main -> main` の更新が表示される。
