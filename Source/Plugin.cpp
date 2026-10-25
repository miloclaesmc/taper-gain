#include <juce_audio_processors/juce_audio_processors.h>
#include <limits>
#include <cmath>

// ---------------------------------------------------------------------------
//  TAPER TABLE  (edit these numbers to retune the curve)
//  pos = how far up the fader / automation lane (0 = bottom, 1 = top)
//  db  = gain at that position
//  Measured from Logic's channel fader scale. Must stay in ascending order.
// ---------------------------------------------------------------------------
namespace
{
    struct CurvePoint { float pos; float db; };

    constexpr CurvePoint kCurve[] =
    {
        { 0.0812f, -40.0f }, { 0.1239f, -30.0f }, { 0.2094f, -20.0f }, { 0.2927f, -15.0f },
        { 0.3996f, -10.0f }, { 0.4850f,  -6.0f }, { 0.6132f,  -3.0f }, { 0.7415f,   0.0f },
        { 0.8718f,   3.0f }, { 1.0000f,   6.0f }
    };

    constexpr int   kNumPoints  = (int) (sizeof (kCurve) / sizeof (kCurve[0]));
    constexpr float kUnityPos   = 0.7415f;   // position that equals 0 dB
    constexpr float kSilencePos = 0.002f;    // at/below this = silence (-inf)
    constexpr float kFloorDb    = -80.0f;    // dB reached just above silence

    float posToDb (float pos)
    {
        pos = juce::jlimit (0.0f, 1.0f, pos);

        if (pos <= kSilencePos)
            return -std::numeric_limits<float>::infinity();

        if (pos < kCurve[0].pos)
        {
            const float t = pos / kCurve[0].pos;
            return kFloorDb + t * (kCurve[0].db - kFloorDb);
        }

        for (int i = 1; i < kNumPoints; ++i)
        {
            if (pos <= kCurve[i].pos)
            {
                const float t = (pos - kCurve[i - 1].pos) / (kCurve[i].pos - kCurve[i - 1].pos);
                return kCurve[i - 1].db + t * (kCurve[i].db - kCurve[i - 1].db);
            }
        }
        return kCurve[kNumPoints - 1].db;
    }

    float dbToPos (float db)
    {
        if (db <= kFloorDb)
            return 0.0f;

        if (db < kCurve[0].db)
        {
            const float t = (db - kFloorDb) / (kCurve[0].db - kFloorDb);
            return t * kCurve[0].pos;
        }

        for (int i = 1; i < kNumPoints; ++i)
        {
            if (db <= kCurve[i].db)
            {
                const float t = (db - kCurve[i - 1].db) / (kCurve[i].db - kCurve[i - 1].db);
                return kCurve[i - 1].pos + t * (kCurve[i].pos - kCurve[i - 1].pos);
            }
        }
        return 1.0f;
    }
}

// ---------------------------------------------------------------------------
class TaperGainProcessor : public juce::AudioProcessor
{
public:
    TaperGainProcessor()
        : AudioProcessor (BusesProperties()
                              .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
        auto attributes = juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int)
            {
                const float db = posToDb (v);
                if (std::isinf (db))
                    return juce::String ("-inf dB");
                return juce::String (db > 0.05f ? "+" : "") + juce::String (db, 1) + " dB";
            })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                const auto s = text.trim().toLowerCase();
                if (s.contains ("inf"))
                    return 0.0f;
                return dbToPos (s.retainCharacters ("0123456789.-+").getFloatValue());
            });

        gain = new juce::AudioParameterFloat (juce::ParameterID { "gain", 1 }, "Gain",
                                              juce::NormalisableRange<float> (0.0f, 1.0f),
                                              kUnityPos, attributes);
        addParameter (gain);
    }

    const juce::String getName() const override              { return "Taper Gain"; }
    bool acceptsMidi() const override                        { return false; }
    bool producesMidi() const override                       { return false; }
    double getTailLengthSeconds() const override             { return 0.0; }
    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto& out = layouts.getMainOutputChannelSet();
        if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
            return false;
        return out == layouts.getMainInputChannelSet();
    }

    void prepareToPlay (double sampleRate, int) override
    {
        smoothed.reset (sampleRate, 0.01);
        smoothed.setCurrentAndTargetValue (targetGain());
    }

    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        smoothed.setTargetValue (targetGain());

        const int numSamples  = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        float* const* data = buffer.getArrayOfWritePointers();

        for (int i = 0; i < numSamples; ++i)
        {
            const float g = smoothed.getNextValue();
            for (int c = 0; c < numChannels; ++c)
                data[c][i] *= g;
        }
    }

    bool hasEditor() const override                          { return true; }
    juce::AudioProcessorEditor* createEditor() override      { return new juce::GenericAudioProcessorEditor (*this); }

    void getStateInformation (juce::MemoryBlock& destData) override
    {
        juce::MemoryOutputStream stream (destData, true);
        stream.writeFloat (gain->get());
    }

    void setStateInformation (const void* data, int sizeInBytes) override
    {
        if (sizeInBytes >= (int) sizeof (float))
        {
            juce::MemoryInputStream stream (data, (size_t) sizeInBytes, false);
            *gain = stream.readFloat();
        }
    }

private:
    float targetGain() const
    {
        const float db = posToDb (gain->get());
        return std::isinf (db) ? 0.0f : juce::Decibels::decibelsToGain (db);
    }

    juce::AudioParameterFloat* gain = nullptr;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TaperGainProcessor)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TaperGainProcessor();
}
