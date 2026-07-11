#include "TakeManager.h"

#include <algorithm>

namespace midi_funfun::core
{
    size_t TakeManager::estimatedMaxTakeBytes(double sampleRate) const
    {
        return static_cast<size_t>(sampleRate * maxTakeSeconds) * sizeof(float);
    }

    size_t TakeManager::getTotalBytesUsedUnlocked() const
    {
        size_t total = 0;
        for (const auto& take : takes)
            total += static_cast<size_t>(take.numSamplesRecorded) * sizeof(float);
        return total;
    }

    bool TakeManager::canStartNewTake(double sampleRate) const
    {
        const juce::ScopedLock sl(lock);
        return getTotalBytesUsedUnlocked() + estimatedMaxTakeBytes(sampleRate) <= maxTotalBytes;
    }

    bool TakeManager::startNewTake(double sampleRate)
    {
        const juce::ScopedLock sl(lock);

        if (getTotalBytesUsedUnlocked() + estimatedMaxTakeBytes(sampleRate) > maxTotalBytes)
            return false;

        Take take;
        take.sampleRate = sampleRate;
        take.buffer.setSize(1, 0);
        take.numSamplesRecorded = 0;

        takes.push_back(std::move(take));
        recordingIndex = static_cast<int>(takes.size()) - 1;
        selectedIndex = recordingIndex;
        return true;
    }

    bool TakeManager::appendToCurrentTake(const float* samples, int numSamples)
    {
        const juce::ScopedLock sl(lock);

        if (recordingIndex < 0)
            return false;

        auto& take = takes[static_cast<size_t>(recordingIndex)];

        const int needed = take.numSamplesRecorded + numSamples;
        if (needed > take.buffer.getNumSamples())
        {
            const int newCapacity = std::max(needed, std::max(1, take.buffer.getNumSamples()) * 2);
            take.buffer.setSize(1, newCapacity, true, true, true);
        }

        take.buffer.copyFrom(0, take.numSamplesRecorded, samples, numSamples);
        take.numSamplesRecorded = needed;

        const int maxSamples = static_cast<int>(take.sampleRate * maxTakeSeconds);
        return take.numSamplesRecorded >= maxSamples;
    }

    void TakeManager::finishRecording()
    {
        const juce::ScopedLock sl(lock);
        recordingIndex = -1;
    }

    void TakeManager::deleteTake(int index)
    {
        const juce::ScopedLock sl(lock);

        if (index < 0 || index >= static_cast<int>(takes.size()))
            return;

        takes.erase(takes.begin() + index);

        if (recordingIndex == index)
            recordingIndex = -1;
        else if (recordingIndex > index)
            --recordingIndex;

        if (takes.empty())
            selectedIndex = -1;
        else if (selectedIndex >= static_cast<int>(takes.size()))
            selectedIndex = static_cast<int>(takes.size()) - 1;
        else if (selectedIndex > index)
            --selectedIndex;
    }

    void TakeManager::selectTake(int index)
    {
        const juce::ScopedLock sl(lock);
        if (index >= 0 && index < static_cast<int>(takes.size()))
            selectedIndex = index;
    }

    int TakeManager::getSelectedTakeIndex() const
    {
        const juce::ScopedLock sl(lock);
        return selectedIndex;
    }

    int TakeManager::getNumTakes() const
    {
        const juce::ScopedLock sl(lock);
        return static_cast<int>(takes.size());
    }

    double TakeManager::getTakeLengthSeconds(int index) const
    {
        const juce::ScopedLock sl(lock);
        if (index < 0 || index >= static_cast<int>(takes.size()))
            return 0.0;

        const auto& take = takes[static_cast<size_t>(index)];
        return take.sampleRate > 0.0 ? static_cast<double>(take.numSamplesRecorded) / take.sampleRate : 0.0;
    }

    size_t TakeManager::getTotalBytesUsed() const
    {
        const juce::ScopedLock sl(lock);
        return getTotalBytesUsedUnlocked();
    }
}
