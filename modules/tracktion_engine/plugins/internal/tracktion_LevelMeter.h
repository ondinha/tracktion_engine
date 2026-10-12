/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2024
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

namespace tracktion::inline engine {

class LevelMeterPlugin  : public Plugin,
                          private juce::Timer
{
public:
    LevelMeterPlugin (PluginCreationInfo);
    ~LevelMeterPlugin() override;

    static juce::ValueTree create();

    //==============================================================================
    static const char* getPluginName()              { return NEEDS_TRANS("Level Meter"); }
    static const char* xmlTypeName;

    juce::String getName() const override               { return TRANS("Level Meter"); }
    juce::String getPluginType() override               { return xmlTypeName; }
    juce::String getShortName (int) override            { return "Meter"; }
    juce::String getTooltip() override                  { return TRANS("Level meter plugin") + "$levelmeterplugin"; }
    bool canBeDisabled() override                       { return false; }
    bool shouldMeasureCpuUsage() const noexcept final   { return false; }

    int getNumOutputChannelsGivenInputs (int numInputChannels) override     { return numInputChannels; }
    BusLayout getBusses() const override   { return BusLayout::singlePassThrough(); }

    juce::String getSelectableDescription() override                        { return TRANS("Level Meter Plugin"); }
    void initialise (const PluginInitialisationInfo&) override;
    void initialiseWithoutStopping (const PluginInitialisationInfo&) override;
    void deinitialise() override;
    void applyToBuffer (const PluginRenderContext&) override;
    void timerCallback() override;

    void restorePluginStateFromValueTree (const juce::ValueTree&) override;

    //==============================================================================
    LevelMeasurer measurer;

    juce::CachedValue<bool> showMidiActivity;

    /** Receives every block the meter sees (the track's output), e.g. for an analyser.
        Called on the audio thread: must be realtime-safe. The owner keeps it alive for
        a while after clearing the pointer, as a block may still be in progress.
    */
    struct AudioTap
    {
        virtual ~AudioTap() = default;
        virtual void tapBlock (const juce::AudioBuffer<float>&, int startSample, int numSamples) = 0;
    };

    /** Set or clear (nullptr) the tap; nothing is done while it's null. */
    std::atomic<AudioTap*> audioTap { nullptr };

private:
    int controllerTrack = -1;
    LevelMeasurer::Client controllerLevelClient;
    std::vector<float> controllerGainsBuffer;

   #if TRACKTION_UNIT_TESTS
    friend struct LevelMeterPluginTestAccess;
   #endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMeterPlugin)
};

} // namespace tracktion::inline engine
