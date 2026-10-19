/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2024
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

#pragma once

namespace tracktion::inline engine {

struct InputProvider;
class TrackMuteState;

/**
    Node for processing a plugin.
*/
class PluginNode final  : public tracktion::graph::Node,
                          public TracktionEngineNode
{
public:
    //==============================================================================
    /** Creates a PluginNode to process a plugin on a Track.
        @param const TrackMuteState*    The optional TrackMuteState to use
        @param ProcessState             The ProcessState for playhead access and time mapping
        @param rendering                Should be true if this is an offline render
        @param balanceLatency           If set to true, this creates a copy of the dry input and
                                        delays it by the plugin's latency and uses this when the
                                        plugin is bypassed to avoid changes in latency
        @param maxNumChannelsToUse      Limits the maximum number of channels to use, set this
                                        to -1 to disable limitations

    */
    PluginNode (std::unique_ptr<Node> input,
                tracktion::engine::Plugin::Ptr,
                double sampleRateToUse, int blockSizeToUse,
                const TrackMuteState*,
                ProcessState&,
                bool rendering, bool balanceLatency,
                int maxNumChannelsToUse);

    /** Destructor. */
    ~PluginNode() override;

    //==============================================================================
    Plugin& getPlugin()                                 { return *plugin; }

    tracktion::graph::NodeProperties getNodeProperties() override;
    std::vector<Node*> getDirectInputNodes() override   { return { input.get() }; }
    bool isReadyToProcess() override                    { return input->hasProcessed(); }
    void prepareToPlay (const tracktion::graph::PlaybackInitialisationInfo&) override;
    void prefetchBlock (juce::Range<int64_t>) override;
    void preProcess (choc::buffer::FrameCount, juce::Range<int64_t>) override;
    void process (ProcessContext&) override;

private:
    //==============================================================================
    std::unique_ptr<Node> input;
    tracktion::engine::Plugin::Ptr plugin;

    const TrackMuteState* trackMuteState = nullptr;
    tracktion::graph::PlayHeadState& playHeadState;
    bool isRendering = false;

    bool isInitialised = false;
    double sampleRate = 44100.0;
    int latencyNumSamples = 0, maxNumChannels = -1;
    tracktion::engine::MidiMessageArray midiMessageArray;
    int subBlockSizeToUse = -1;
    bool balanceLatency = true, canProcessBypassed = false;
    // a bypassed latent plug-in that isn't hosted (ours / Tracktion's) isn't run at all - its output would be replaced by
    // the delayed dry anyway; when it comes back it is fed `warmupSamplesLeft` (its latency) of real input while the
    // delayed dry still plays, so nothing stale from before the bypass is heard
    bool skippedWhileBypassed = false;
    int warmupSamplesLeft = 0;
    TimeDuration automationAdjustmentTime;

    std::shared_ptr<tracktion::graph::LatencyProcessor> latencyProcessor;

    /** Plugin::levelMatch: the output brought to the input's K-weighted loudness */
    struct KFilter
    {
        double b[2][3] {}, a[2][3] {}, z[2][2] {};   // two biquads (shelf, high-pass), transposed DF II
        void prepare (double sr);
        double run (double x);
        void clear() { for (auto& s : z) s[0] = s[1] = 0; }
    };
    std::array<KFilter, 2> kIn, kOut;
    double lmIn = 0, lmOut = 0, lmGain = 1;
    bool lmActive = false;
    double kPower (choc::buffer::ChannelArrayView<float>, std::array<KFilter, 2>&);
    std::optional<NodeProperties> cachedNodeProperties;
    bool isPrepared = false, canUseSourceBuffers = false;

    //==============================================================================
    void initialisePlugin (double sampleRateToUse, int blockSizeToUse);
    PluginRenderContext getPluginRenderContext (TimeRange, juce::AudioBuffer<float>&);
    void replaceLatencyProcessorIfPossible (NodeGraph*);
};

} // namespace tracktion::inline engine
