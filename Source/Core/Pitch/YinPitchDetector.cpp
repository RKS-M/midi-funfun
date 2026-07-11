#include "YinPitchDetector.h"

#include <algorithm>
#include <cmath>

namespace midi_funfun::core
{
    namespace
    {
        double computeRms(const float* samples, int start, int length)
        {
            double sumSq = 0.0;
            for (int i = 0; i < length; ++i)
            {
                const double s = (double) samples[start + i];
                sumSq += s * s;
            }
            return std::sqrt(sumSq / (double) length);
        }
    }

    YinPitchDetector::YinPitchDetector(Settings settingsIn) : settings(settingsIn)
    {
    }

    std::vector<PitchFrame> YinPitchDetector::analyze(const float* monoSamples, int numSamples, double sampleRate) const
    {
        std::vector<PitchFrame> frames;

        // 積分窓長W = windowSize/2。差分関数d(tau)はtau=0..Wで計算するため、
        // 1フレームの解析には[frameStart, frameStart+windowSize)が必要になる。
        const int windowLength = settings.windowSize / 2;
        if (windowLength <= 1 || numSamples < settings.windowSize)
            return frames;

        std::vector<double> d((size_t) windowLength + 1, 0.0);
        std::vector<double> dPrime((size_t) windowLength + 1, 0.0);

        const int tauMinSamples = std::max(1, (int) (sampleRate / settings.maxFrequencyHz));
        const int tauMaxSamples = std::min(windowLength, (int) (sampleRate / settings.minFrequencyHz));

        int frameStart = 0;
        while (frameStart + settings.windowSize <= numSamples)
        {
            for (int tau = 0; tau <= windowLength; ++tau)
            {
                double sum = 0.0;
                for (int j = 0; j < windowLength; ++j)
                {
                    const double diff = (double) monoSamples[frameStart + j] - (double) monoSamples[frameStart + j + tau];
                    sum += diff * diff;
                }
                d[(size_t) tau] = sum;
            }

            dPrime[0] = 1.0;
            double runningSum = 0.0;
            for (int tau = 1; tau <= windowLength; ++tau)
            {
                runningSum += d[(size_t) tau];
                dPrime[(size_t) tau] = (runningSum > 1e-12) ? (d[(size_t) tau] * tau / runningSum) : 1.0;
            }

            PitchFrame frame;
            frame.rmsLevel = computeRms(monoSamples, frameStart, windowLength);

            if (tauMinSamples <= tauMaxSamples)
            {
                int globalMinTau = tauMinSamples;
                double globalMinVal = dPrime[(size_t) tauMinSamples];
                for (int tau = tauMinSamples + 1; tau <= tauMaxSamples; ++tau)
                {
                    if (dPrime[(size_t) tau] < globalMinVal)
                    {
                        globalMinVal = dPrime[(size_t) tau];
                        globalMinTau = tau;
                    }
                }

                if (globalMinVal < settings.absoluteThreshold)
                {
                    int tauEstimate = globalMinTau;
                    for (int tau = tauMinSamples; tau <= tauMaxSamples; ++tau)
                    {
                        if (dPrime[(size_t) tau] < settings.absoluteThreshold)
                        {
                            tauEstimate = tau;
                            while (tauEstimate + 1 <= tauMaxSamples
                                   && dPrime[(size_t) (tauEstimate + 1)] < dPrime[(size_t) tauEstimate])
                                ++tauEstimate;
                            break;
                        }
                    }

                    double tauRefined = (double) tauEstimate;
                    if (tauEstimate > tauMinSamples && tauEstimate < tauMaxSamples)
                    {
                        const double y0 = dPrime[(size_t) (tauEstimate - 1)];
                        const double y1 = dPrime[(size_t) tauEstimate];
                        const double y2 = dPrime[(size_t) (tauEstimate + 1)];
                        const double denom = y0 - 2.0 * y1 + y2;
                        if (std::abs(denom) > 1e-12)
                            tauRefined += 0.5 * (y0 - y2) / denom;
                    }

                    frame.voiced = true;
                    frame.frequencyHz = sampleRate / tauRefined;
                    frame.confidence = std::max(0.0, 1.0 - dPrime[(size_t) tauEstimate]);
                }
                else
                {
                    frame.voiced = false;
                    frame.frequencyHz = 0.0;
                    frame.confidence = std::max(0.0, 1.0 - globalMinVal);
                }
            }
            else
            {
                frame.voiced = false;
                frame.frequencyHz = 0.0;
                frame.confidence = 0.0;
            }

            frames.push_back(frame);
            frameStart += settings.hopSize;
        }

        return frames;
    }
}
