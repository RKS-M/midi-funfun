#include "PluginEditor.h"

#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

using midi_funfun::core::RecordingTransport;

namespace
{
    // 「青空」を意識した水色系の背景色(要件5章 UI/デザイン要件)。Milestone 7で最終調整する。
    const juce::Colour skyBlueBackground { 0xff8ecae6 };

    juce::String formatMinutesSeconds(double seconds)
    {
        const int totalSeconds = (int) seconds;
        return juce::String::formatted("%d:%02d", totalSeconds / 60, totalSeconds % 60);
    }

    double ticksToSeconds(juce::int64 ticks, double bpm)
    {
        return (double) ticks / ((bpm / 60.0) * (double) midi_funfun::core::ticksPerQuarterNote);
    }
}

int MidiFunfunAudioProcessorEditor::NoteListBoxModel::getNumRows()
{
    return processor.getAnalyzedNotes().size();
}

void MidiFunfunAudioProcessorEditor::NoteListBoxModel::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowIsSelected)
        g.fillAll(juce::Colours::lightblue);

    const auto& note = processor.getAnalyzedNotes()[rowNumber];
    const double bpm = processor.getAnalyzedNotesBpm();
    const double startSeconds = ticksToSeconds(note.startTick, bpm);
    const double lengthSeconds = ticksToSeconds(note.lengthTicks, bpm);

    const auto text = midi_funfun::core::formatNoteListRow(note.pitch, startSeconds, lengthSeconds);

    g.setColour(juce::Colours::black);
    g.drawText(text, 4, 0, width - 8, height, juce::Justification::centredLeft);
}

void MidiFunfunAudioProcessorEditor::LevelMeter::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillRect(bounds);

    const float level = juce::jlimit(0.0f, 1.0f, tracker.getCurrentLevel());
    const float peak = juce::jlimit(0.0f, 1.0f, tracker.getPeakHoldLevel());

    g.setColour(juce::Colours::limegreen);
    g.fillRect(bounds.withWidth(bounds.getWidth() * level));

    g.setColour(juce::Colours::red);
    g.fillRect(juce::Rectangle<float>(bounds.getWidth() * peak - 1.0f, bounds.getY(), 2.0f, bounds.getHeight()));
}

MidiFunfunAudioProcessorEditor::MidiFunfunAudioProcessorEditor(MidiFunfunAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p), levelMeter(p.getPeakLevelTracker())
{
    titleLabel.setText("MIDI FUNFUN", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setFont(juce::Font(juce::FontOptions().withHeight(24.0f)));
    addAndMakeVisible(titleLabel);

    bpmLabel.setText("BPM", juce::dontSendNotification);
    addAndMakeVisible(bpmLabel);

    bpmSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    bpmSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    bpmSlider.setRange(20.0, 300.0, 1.0);
    bpmSlider.setValue(processorRef.getBpm(), juce::dontSendNotification);
    bpmSlider.onValueChange = [this] { processorRef.setBpm(bpmSlider.getValue()); };
    addAndMakeVisible(bpmSlider);

    metronomeToggle.setButtonText("Metronome");
    metronomeToggle.setToggleState(processorRef.getMetronomeEnabled(), juce::dontSendNotification);
    metronomeToggle.onClick = [this] { processorRef.setMetronomeEnabled(metronomeToggle.getToggleState()); };
    addAndMakeVisible(metronomeToggle);

    // 既定でOFF: 入力を出力へパススルーするとスピーカー環境ではフィードバックループの
    // 恐れがあるため、モニタリングはユーザーの明示的なオプトインとする。
    monitoringToggle.setButtonText("Monitoring");
    monitoringToggle.setToggleState(processorRef.getMonitoringEnabled(), juce::dontSendNotification);
    monitoringToggle.onClick = [this] { processorRef.setMonitoringEnabled(monitoringToggle.getToggleState()); };
    addAndMakeVisible(monitoringToggle);

    countInLabel.setText("Count-in", juce::dontSendNotification);
    addAndMakeVisible(countInLabel);

    countInSlider.setSliderStyle(juce::Slider::IncDecButtons);
    countInSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 20);
    countInSlider.setRange(0.0, 8.0, 1.0);
    countInSlider.setValue(processorRef.getCountInBeats(), juce::dontSendNotification);
    countInSlider.onValueChange = [this] { processorRef.setCountInBeats((int) countInSlider.getValue()); };
    addAndMakeVisible(countInSlider);

    recordButton.onClick = [this] { recordButtonClicked(); };
    addAndMakeVisible(recordButton);
    updateRecordButtonAppearance();

    addAndMakeVisible(levelMeter);

    takeListBox.setModel(this);
    takeListBox.setRowHeight(22);
    addAndMakeVisible(takeListBox);

    deleteTakeButton.setButtonText("Delete Take");
    deleteTakeButton.onClick = [this] { deleteButtonClicked(); };
    addAndMakeVisible(deleteTakeButton);

    settingsButton.setButtonText("Settings");
    settingsButton.onClick = [this] { settingsButtonClicked(); };
    settingsButton.setVisible(processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone);
    addAndMakeVisible(settingsButton);

    analyzeButton.setButtonText("Analyze");
    analyzeButton.onClick = [this] { analyzeButtonClicked(); };
    addAndMakeVisible(analyzeButton);

    noiseGateLabel.setText("Noise Gate", juce::dontSendNotification);
    addAndMakeVisible(noiseGateLabel);

    noiseGateSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    noiseGateSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    noiseGateSlider.setRange(0.0, 100.0, 1.0);
    noiseGateSlider.setValue(processorRef.getNoiseGateSensitivity(), juce::dontSendNotification);
    noiseGateSlider.onValueChange = [this] { processorRef.setNoiseGateSensitivity(noiseGateSlider.getValue()); };
    addAndMakeVisible(noiseGateSlider);

    minNoteLengthLabel.setText("Min Note Len", juce::dontSendNotification);
    addAndMakeVisible(minNoteLengthLabel);

    minNoteLengthSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    minNoteLengthSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    minNoteLengthSlider.setRange(0.0, 300.0, 1.0);
    minNoteLengthSlider.setValue(processorRef.getMinNoteLengthMs(), juce::dontSendNotification);
    minNoteLengthSlider.onValueChange = [this] { processorRef.setMinNoteLengthMs(minNoteLengthSlider.getValue()); };
    addAndMakeVisible(minNoteLengthSlider);

    defaultVelocityLabel.setText("Velocity", juce::dontSendNotification);
    addAndMakeVisible(defaultVelocityLabel);

    defaultVelocitySlider.setSliderStyle(juce::Slider::LinearHorizontal);
    defaultVelocitySlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    defaultVelocitySlider.setRange(1.0, 127.0, 1.0);
    defaultVelocitySlider.setValue(processorRef.getDefaultVelocity(), juce::dontSendNotification);
    defaultVelocitySlider.onValueChange = [this] { processorRef.setDefaultVelocity((int) defaultVelocitySlider.getValue()); };
    addAndMakeVisible(defaultVelocitySlider);

    noteListBox.setRowHeight(20);
    addAndMakeVisible(noteListBox);

    analysisStatusLabel.setJustificationType(juce::Justification::centredLeft);
    analysisStatusLabel.setColour(juce::Label::textColourId, juce::Colours::darkred);
    addAndMakeVisible(analysisStatusLabel);

#if JucePlugin_Build_Standalone
    // JUCEのStandaloneラッパーは入出力チャンネルを持つプロセッサ(=フィードバックの可能性)を
    // 検出すると入力を既定でミュートする安全機能を持つ。モニタリングは既定OFFで
    // フィードバックの実害が無いため、起動時に自動でミュート解除する。
    if (auto* holder = juce::StandalonePluginHolder::getInstance())
        holder->getMuteInputValue().setValue(false);
#endif

    setSize(980, 540);
    startTimerHz(30);
}

void MidiFunfunAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(skyBlueBackground);
}

void MidiFunfunAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(10);

    titleLabel.setBounds(area.removeFromTop(30));
    area.removeFromTop(6);

    auto toolbar = area.removeFromTop(40);
    bpmLabel.setBounds(toolbar.removeFromLeft(40));
    bpmSlider.setBounds(toolbar.removeFromLeft(160));
    toolbar.removeFromLeft(10);
    metronomeToggle.setBounds(toolbar.removeFromLeft(110));
    toolbar.removeFromLeft(10);
    countInLabel.setBounds(toolbar.removeFromLeft(70));
    countInSlider.setBounds(toolbar.removeFromLeft(110));
    toolbar.removeFromLeft(10);
    monitoringToggle.setBounds(toolbar.removeFromLeft(110));
    toolbar.removeFromLeft(10);
    recordButton.setBounds(toolbar.removeFromLeft(100));
    toolbar.removeFromLeft(10);
    settingsButton.setBounds(toolbar.removeFromLeft(90));

    area.removeFromTop(6);
    auto analysisToolbar = area.removeFromTop(40);
    analyzeButton.setBounds(analysisToolbar.removeFromLeft(90));
    analysisToolbar.removeFromLeft(10);
    noiseGateLabel.setBounds(analysisToolbar.removeFromLeft(80));
    noiseGateSlider.setBounds(analysisToolbar.removeFromLeft(150));
    analysisToolbar.removeFromLeft(10);
    minNoteLengthLabel.setBounds(analysisToolbar.removeFromLeft(90));
    minNoteLengthSlider.setBounds(analysisToolbar.removeFromLeft(150));
    analysisToolbar.removeFromLeft(10);
    defaultVelocityLabel.setBounds(analysisToolbar.removeFromLeft(60));
    defaultVelocitySlider.setBounds(analysisToolbar.removeFromLeft(150));

    area.removeFromTop(10);
    levelMeter.setBounds(area.removeFromTop(24));

    area.removeFromTop(10);

    auto notesArea = area.removeFromRight(area.getWidth() / 2);
    area.removeFromRight(10);
    auto takesArea = area;

    auto deleteButtonArea = takesArea.removeFromBottom(30);
    deleteTakeButton.setBounds(deleteButtonArea.removeFromLeft(120));
    takesArea.removeFromBottom(6);
    takeListBox.setBounds(takesArea);

    analysisStatusLabel.setBounds(notesArea.removeFromTop(20));
    noteListBox.setBounds(notesArea);
}

int MidiFunfunAudioProcessorEditor::getNumRows()
{
    return processorRef.getTakeManager().getNumTakes();
}

void MidiFunfunAudioProcessorEditor::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowIsSelected)
        g.fillAll(juce::Colours::lightblue);

    g.setColour(juce::Colours::black);
    const auto length = processorRef.getTakeManager().getTakeLengthSeconds(rowNumber);
    g.drawText("Take " + juce::String(rowNumber + 1) + "  (" + formatMinutesSeconds(length) + ")",
               4, 0, width - 8, height, juce::Justification::centredLeft);
}

void MidiFunfunAudioProcessorEditor::selectedRowsChanged(int lastRowSelected)
{
    if (lastRowSelected >= 0)
        processorRef.getTakeManager().selectTake(lastRowSelected);
}

void MidiFunfunAudioProcessorEditor::timerCallback()
{
    levelMeter.repaint();
    updateRecordButtonAppearance();

    takeListBox.updateContent();
    takeListBox.repaint();

    const int selected = processorRef.getTakeManager().getSelectedTakeIndex();
    if (selected != takeListBox.getSelectedRow())
        takeListBox.selectRow(selected, true, true);
}

void MidiFunfunAudioProcessorEditor::recordButtonClicked()
{
    if (processorRef.getRecordingState() == RecordingTransport::State::Idle)
    {
        if (!processorRef.startRecording())
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                     "MIDI FUNFUN",
                                                     "録音を開始できません(メモリ上限に達しています)。"
                                                     "不要なテイクを削除してから再度お試しください。");
        }
    }
    else
    {
        processorRef.stopRecording();
    }

    updateRecordButtonAppearance();
}

void MidiFunfunAudioProcessorEditor::deleteButtonClicked()
{
    const int selected = processorRef.getTakeManager().getSelectedTakeIndex();
    if (selected >= 0)
        processorRef.getTakeManager().deleteTake(selected);

    takeListBox.updateContent();
}

void MidiFunfunAudioProcessorEditor::analyzeButtonClicked()
{
    processorRef.analyzeSelectedTake();
    noteListBox.updateContent();
    noteListBox.repaint();

    analysisStatusLabel.setText(processorRef.getAnalyzedNotes().size() == 0
                                     ? "ノートが検出されませんでした(Noise Gateを下げて再度お試しください)"
                                     : "",
                                 juce::dontSendNotification);
}

void MidiFunfunAudioProcessorEditor::settingsButtonClicked()
{
#if JucePlugin_Build_Standalone
    auto* holder = juce::StandalonePluginHolder::getInstance();
    if (holder == nullptr)
        return;

    auto* selector = new juce::AudioDeviceSelectorComponent(holder->deviceManager,
                                                              0, 2,
                                                              0, 2,
                                                              false, false, false, false);
    selector->setSize(500, 400);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(selector);
    options.dialogTitle = "Audio Settings";
    options.dialogBackgroundColour = juce::Colours::lightgrey;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
#endif
}

void MidiFunfunAudioProcessorEditor::updateRecordButtonAppearance()
{
    switch (processorRef.getRecordingState())
    {
        case RecordingTransport::State::Idle:
            recordButton.setButtonText("Rec");
            recordButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred);
            break;
        case RecordingTransport::State::CountIn:
            recordButton.setButtonText("Count-in...");
            recordButton.setColour(juce::TextButton::buttonColourId, juce::Colours::orange);
            break;
        case RecordingTransport::State::Recording:
            recordButton.setButtonText("Stop");
            recordButton.setColour(juce::TextButton::buttonColourId, juce::Colours::red);
            break;
    }
}
