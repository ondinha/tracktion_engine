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

namespace
{
    static bool shouldUseFineGrainAutomation (Plugin& p)
    {
        if (! p.isAutomationNeeded())
            return false;

        if (p.engine.getPluginManager().canUseFineGrainAutomation)
            return p.engine.getPluginManager().canUseFineGrainAutomation (p);

        return true;
    }
}

//==============================================================================
PluginNode::PluginNode (std::unique_ptr<Node> inputNode,
                        tracktion::engine::Plugin::Ptr pluginToProcess,
                        double sampleRateToUse, int blockSizeToUse,
                        const TrackMuteState* trackMuteStateToUse,
                        ProcessState& processStateToUse,
                        bool rendering, bool canBalanceLatency,
                        int maxNumChannelsToUse)
    : TracktionEngineNode (processStateToUse),
      input (std::move (inputNode)),
      plugin (std::move (pluginToProcess)),
      trackMuteState (trackMuteStateToUse),
      playHeadState (processStateToUse.playHeadState),
      isRendering (rendering),
      maxNumChannels (maxNumChannelsToUse),
      balanceLatency (canBalanceLatency)
{
    jassert (input != nullptr);
    jassert (plugin != nullptr);
    initialisePlugin (sampleRateToUse, blockSizeToUse);
}

PluginNode::~PluginNode()
{
    if (isInitialised && ! plugin->baseClassNeedsInitialising())
        plugin->baseClassDeinitialise();
}

//==============================================================================
tracktion::graph::NodeProperties PluginNode::getNodeProperties()
{
    if (cachedNodeProperties)
        return *cachedNodeProperties;

    auto props = input->getNodeProperties();

    // Use the actual input channel count — channel conversion (e.g. mono→stereo) is handled
    // upstream by createNodeForPlugin inserting a ChannelRemappingNode when needed
    props.numberOfChannels = std::max (props.numberOfChannels, plugin->getNumOutputChannelsGivenInputs (props.numberOfChannels));

    // Ensure audio plugins always get at least their declared minimum channel count,
    // even when the input node reports 0 channels (e.g. unconnected rack plugin).
    // Only truly MIDI-only plugins (no audio in, no audio out) are excluded.
    if (plugin->takesAudioInput() || plugin->producesAudioWhenNoAudioInput())
    {
        const auto pluginBusses = plugin->getBusses();

        if (! pluginBusses.inputs.empty())
            props.numberOfChannels = std::max (props.numberOfChannels,
                                               pluginBusses.inputs.front().getNumChannels());

        if (! pluginBusses.outputs.empty())
        {
            int totalOutputChans = 0;

            for (const auto& bus : pluginBusses.outputs)
                totalOutputChans += bus.getNumChannels();

            props.numberOfChannels = std::max (props.numberOfChannels, totalOutputChans);
        }
    }

    if (maxNumChannels > 0)
        props.numberOfChannels = std::min (props.numberOfChannels, maxNumChannels);

    props.hasAudio = props.hasAudio || plugin->producesAudioWhenNoAudioInput();
    props.hasMidi  = props.hasMidi || plugin->takesMidiInput();
    props.latencyNumSamples = std::max (0, props.latencyNumSamples + latencyNumSamples);

    if (props.nodeID != 0)
        hash_combine (props.nodeID, (size_t) plugin->itemID.getRawID());
    else
        props.nodeID = (size_t) plugin->itemID.getRawID();

    if (isPrepared)
        cachedNodeProperties = props;

    return props;
}

void PluginNode::prepareToPlay (const tracktion::graph::PlaybackInitialisationInfo& info)
{
    juce::ignoreUnused (info);
    jassert (sampleRate == info.sampleRate);
    jassert (! isPrepared); // Is this being called multiple times?

    auto props = getNodeProperties();

    if (props.latencyNumSamples > 0)
        automationAdjustmentTime = TimeDuration::fromSamples (-props.latencyNumSamples, sampleRate);

    if (shouldUseFineGrainAutomation (*plugin))
        subBlockSizeToUse = std::max (128, 128 * juce::roundToInt (info.sampleRate / 44100.0));

    // any plug-in with latency, not only external ones: a bypassed internal plug-in (e.g. an STFT effect) would
    // otherwise pass its input through undelayed while the graph still compensates for its latency - its track
    // then plays early by that latency
    canProcessBypassed = balanceLatency && latencyNumSamples > 0;
    for (auto* k : { &kIn, &kOut }) for (auto& f : *k) f.prepare (info.sampleRate);

    if (canProcessBypassed)
    {
        replaceLatencyProcessorIfPossible (info.nodeGraphToReplace);

        if (! latencyProcessor)
        {
            latencyProcessor = std::make_shared<tracktion::graph::LatencyProcessor>();
            latencyProcessor->setLatencyNumSamples (latencyNumSamples);
            latencyProcessor->prepareToPlay (info.sampleRate, info.blockSize, props.numberOfChannels);
        }
    }

    isPrepared = true;

    if (info.enableNodeMemorySharing && input->numOutputNodes == 1 && input->canShareOutputBuffer())
    {
        const auto inputNumChannels = input->getNodeProperties().numberOfChannels;
        const auto desiredNumChannels = props.numberOfChannels;

        if (inputNumChannels >= desiredNumChannels)
        {
            canUseSourceBuffers = true;
            setOptimisations ({ tracktion::graph::ClearBuffers::no,
                                tracktion::graph::AllocateAudioBuffer::no });
        }
    }
}

void PluginNode::prefetchBlock (juce::Range<int64_t>)
{
    plugin->prepareForNextBlock (getEditTimeRange().getStart());
}

void PluginNode::preProcess (choc::buffer::FrameCount, juce::Range<int64_t>)
{
    if (canUseSourceBuffers)
        setBufferViewToUse (input.get(), input->getProcessedOutput().audio);
}

void PluginNode::process (ProcessContext& pc)
{
    auto inputBuffers = input->getProcessedOutput();
    auto& inputAudioBlock = inputBuffers.audio;
    const bool matching = plugin->levelMatch.load (std::memory_order_relaxed);
    const double blockInPower = matching ? kPower (inputAudioBlock, kIn) : 0.0;

    auto& outputBuffers = pc.buffers;
    auto outputAudioView = outputBuffers.audio;
    const auto blockNumSamples = inputAudioBlock.getNumFrames();
    jassert (inputAudioBlock.getNumFrames() == outputAudioView.getNumFrames());

    const auto numInputChannelsToCopy = std::min (inputAudioBlock.getNumChannels(),
                                                  outputAudioView.getNumChannels());

    if (latencyProcessor)
    {
        if (numInputChannelsToCopy > 0)
            latencyProcessor->writeAudio (inputAudioBlock.getFirstChannels (numInputChannelsToCopy));

        latencyProcessor->writeMIDI (inputBuffers.midi);
    }

    // Copy the inputs to the outputs, then process using the
    // output buffers as that will be the correct size
    if (numInputChannelsToCopy > 0)
        tracktion::graph::copyIfNotAliased (outputAudioView.getFirstChannels (numInputChannelsToCopy),
                                            inputAudioBlock.getFirstChannels (numInputChannelsToCopy));

    // Init block
    auto subBlockSize = subBlockSizeToUse < 0 ? blockNumSamples
                                              : (choc::buffer::FrameCount) subBlockSizeToUse;

    choc::buffer::FrameCount numSamplesDone = 0;
    auto numSamplesLeft = blockNumSamples;

    bool shouldProcessPlugin = canProcessBypassed || plugin->isEnabled();
    const bool enabledNow = plugin->isEnabled();

    // bypassed and latent: only a hosted plug-in gets its bypass processing, ours / Tracktion's would compute a block
    // the delayed dry overwrites (a disabled STFT effect cost as much as an enabled one)
    if (canProcessBypassed && ! enabledNow && dynamic_cast<ExternalPlugin*> (plugin.get()) == nullptr)
    {
        shouldProcessPlugin = false;
        skippedWhileBypassed = true;
    }
    else if (enabledNow && skippedWhileBypassed)
    {
        skippedWhileBypassed = false;
        warmupSamplesLeft = latencyNumSamples;
    }
    bool isAllNotesOff = inputBuffers.midi.isAllNotesOff || playHeadState.didPlayheadJump();

    if (trackMuteState != nullptr)
    {
        if (! trackMuteState->shouldTrackContentsBeProcessed())
        {
            shouldProcessPlugin = shouldProcessPlugin && trackMuteState->shouldTrackBeAudible();
            isAllNotesOff = isAllNotesOff || trackMuteState->wasJustMuted();
        }
    }

    // Fast path for disabled plugins: skip sub-block loop and pass MIDI straight through
    if (! shouldProcessPlugin)
    {
        outputBuffers.midi.copyFrom (inputBuffers.midi);
        outputBuffers.midi.isAllNotesOff = isAllNotesOff;
    }
    else
    {
        const auto timingStart = Plugin::processTimingEnabled.load (std::memory_order_relaxed)
                                    ? juce::Time::getHighResolutionTicks() : 0;
        const auto blockTimeRange = getEditTimeRange();
        auto inputMidiIter = inputBuffers.midi.begin();

        // Process in blocks
        for (int subBlockNum = 0;; ++subBlockNum)
        {
            auto numSamplesThisBlock = std::min (subBlockSize, numSamplesLeft);

            auto outputAudioBuffer = toAudioBuffer (outputAudioView.getFrameRange (frameRangeWithStartAndLength (numSamplesDone, numSamplesThisBlock)));

            const auto blockPropStart = (numSamplesDone / (double) blockNumSamples);
            const auto blockPropEnd = ((numSamplesDone + numSamplesThisBlock) / (double) blockNumSamples);
            const auto subBlockTimeRange = TimeRange (toPosition (blockTimeRange.getLength()) * blockPropStart,
                                                      toPosition (blockTimeRange.getLength()) * blockPropEnd);

            midiMessageArray.clear();
            midiMessageArray.isAllNotesOff = isAllNotesOff;

            for (auto end = inputBuffers.midi.end(); inputMidiIter != end; ++inputMidiIter)
            {
                const auto timestamp = inputMidiIter->getTimeStamp();

                // If the time range is empty, we need to pass through all the MIDI as it means the playhead is stopped
                if (! subBlockTimeRange.isEmpty()
                    && timestamp >= subBlockTimeRange.getEnd().inSeconds())
                   break;

                midiMessageArray.addMidiMessage (*inputMidiIter,
                                                 timestamp - subBlockTimeRange.getStart().inSeconds(),
                                                 inputMidiIter->mpeSourceID);
            }

            // Process the plugin
            plugin->applyToBufferWithAutomation (getPluginRenderContext ({ blockTimeRange.getStart() + toDuration (subBlockTimeRange.getStart()),
                                                                           blockTimeRange.getStart() + toDuration (subBlockTimeRange.getEnd()) },
                                                                         outputAudioBuffer));

            // Then copy the buffers to the outputs
            if (subBlockNum == 0)
                outputBuffers.midi.swapWith (midiMessageArray);
            else
                outputBuffers.midi.mergeFrom (midiMessageArray);

            numSamplesDone += numSamplesThisBlock;
            numSamplesLeft -= numSamplesThisBlock;

            if (numSamplesLeft == 0)
                break;

            isAllNotesOff = false;
        }

        if (timingStart != 0)
            plugin->processTime.add (juce::Time::getHighResolutionTicks() - timingStart);
    }

    // If the plugin was bypassed, use the delayed audio
    if (latencyProcessor)
    {
        const bool warmingUp = warmupSamplesLeft > 0 && enabledNow;
        if (warmingUp) warmupSamplesLeft = std::max (0, warmupSamplesLeft - (int) blockNumSamples);

        // A slightly better approach would be to crossfade between the processed and latency block to minimise any discrepancies
        if (enabledNow && ! warmingUp)
        {
            auto numSamples = (int) blockNumSamples;
            latencyProcessor->clearAudio (numSamples);
            latencyProcessor->clearMIDI (numSamples);
        }
        else
        {
            outputBuffers.midi.clear();

            // If no inputs have been added to the fifo, there won't be any samples available so skip
            if (numInputChannelsToCopy > 0)
                latencyProcessor->readAudioOverwriting (outputAudioView);

            latencyProcessor->readMIDI (outputBuffers.midi, (int) blockNumSamples);
        }
    }

    // a host's fair A / B (Plugin::levelMatch): the output follows the input's K-weighted loudness
    if (matching)
    {
        const double blockOutPower = kPower (outputAudioView, kOut);
        const double a = 1.0 - std::exp (-(double) blockNumSamples / (3.0 * sampleRate));
        if (! lmActive) { lmIn = blockInPower; lmOut = blockOutPower; lmGain = 1.0; lmActive = true; }
        lmIn += (blockInPower - lmIn) * a;
        lmOut += (blockOutPower - lmOut) * a;
        const double target = lmOut > 1e-10 ? juce::jlimit (0.25, 4.0, std::sqrt ((lmIn + 1e-12) / (lmOut + 1e-12))) : lmGain;
        const auto n = (int) blockNumSamples;
        for (choc::buffer::ChannelCount c = 0; c < outputAudioView.getNumChannels(); ++c)
        {
            auto* d = outputAudioView.getChannel (c).data.data;
            for (int i = 0; i < n; ++i) d[i] *= (float) (lmGain + (target - lmGain) * (i + 1) / n);
        }
        lmGain = target;
        plugin->levelMatchDb.store ((float) (20.0 * std::log10 (lmGain)), std::memory_order_relaxed);
    }
    else if (lmActive)
    {
        lmActive = false;
        for (auto* k : { &kIn, &kOut }) for (auto& f : *k) f.clear();
        plugin->levelMatchDb.store (0.0f, std::memory_order_relaxed);
    }

   #if TRACKTION_SANITISE_PLUGIN_OUTPUT
    // Some plugins flake and add NaNs so zero these out to avoid killing all the audio downstream
    sanitise (outputAudioView);
   #endif
}

//==============================================================================
void PluginNode::KFilter::prepare (double sr)
{
    // ITU-R BS.1770 K-weighting at any rate: a +4 dB high shelf from ~1.5 kHz, then a 38 Hz high-pass
    const double pi = juce::MathConstants<double>::pi;
    {
        const double A = std::pow (10.0, 4.0 / 40.0), w0 = 2 * pi * 1500.0 / sr, cw = std::cos (w0), al = std::sin (w0) / (2 * 0.7071), sA = 2 * std::sqrt (A) * al;
        const double a0 = (A + 1) - (A - 1) * cw + sA;
        b[0][0] = A * ((A + 1) + (A - 1) * cw + sA) / a0; b[0][1] = -2 * A * ((A - 1) + (A + 1) * cw) / a0; b[0][2] = A * ((A + 1) + (A - 1) * cw - sA) / a0;
        a[0][1] = 2 * ((A - 1) - (A + 1) * cw) / a0; a[0][2] = ((A + 1) - (A - 1) * cw - sA) / a0;
    }
    {
        const double w0 = 2 * pi * 38.0 / sr, cw = std::cos (w0), al = std::sin (w0) / (2 * 0.5), a0 = 1 + al;
        b[1][0] = (1 + cw) / 2 / a0; b[1][1] = -(1 + cw) / a0; b[1][2] = (1 + cw) / 2 / a0;
        a[1][1] = -2 * cw / a0; a[1][2] = (1 - al) / a0;
    }
    clear();
}

double PluginNode::KFilter::run (double x)
{
    for (int s = 0; s < 2; ++s)
    {
        const double y = b[s][0] * x + z[s][0];
        z[s][0] = b[s][1] * x - a[s][1] * y + z[s][1];
        z[s][1] = b[s][2] * x - a[s][2] * y;
        x = y;
    }
    return x;
}

double PluginNode::kPower (choc::buffer::ChannelArrayView<float> v, std::array<KFilter, 2>& k)
{
    double sum = 0;
    const auto n = v.getNumFrames(), nc = std::min<choc::buffer::ChannelCount> (2, v.getNumChannels());
    for (choc::buffer::ChannelCount c = 0; c < nc; ++c)
    {
        auto* d = v.getChannel (c).data.data;
        for (choc::buffer::FrameCount i = 0; i < n; ++i) { const double y = k[(size_t) c].run (d[i]); sum += y * y; }
    }
    return n > 0 && nc > 0 ? sum / (double) (n * nc) : 0.0;
}

//==============================================================================
void PluginNode::initialisePlugin (double sampleRateToUse, int blockSizeToUse)
{
    // The plugin is initialised here, in the constructor, and Plugin::baseClassInitialise
    // only re-runs initialise() when the rate or block size changes, so a placeholder rate
    // passed in here can't be corrected later - it has to be the rate this Node will be
    // prepared and processed at
    jassert (sampleRateToUse > 0.0);
    jassert (blockSizeToUse > 0);

    plugin->baseClassInitialise ({ 0_tp, sampleRateToUse, blockSizeToUse });
    isInitialised = true;

    sampleRate = sampleRateToUse;
    latencyNumSamples = juce::roundToInt (plugin->getLatencySeconds() * sampleRate);
}

PluginRenderContext PluginNode::getPluginRenderContext (TimeRange editTime, juce::AudioBuffer<float>& destBuffer)
{
    return { &destBuffer,
             0, destBuffer.getNumSamples(),
             &midiMessageArray, 0.0,
             editTime + automationAdjustmentTime,
             playHeadState.playHead.isPlaying(), playHeadState.playHead.isUserDragging(),
             isRendering, canProcessBypassed };
}

void PluginNode::replaceLatencyProcessorIfPossible (NodeGraph* nodeGraphToReplace)
{
    if (nodeGraphToReplace == nullptr)
        return;

    const auto props = getNodeProperties();
    const auto nodeIDToLookFor = props.nodeID;

    if (nodeIDToLookFor == 0)
        return;

    if (auto oldNode = findNodeWithID<PluginNode> (*nodeGraphToReplace, nodeIDToLookFor))
    {
        if (! oldNode->latencyProcessor)
            return;

        if (! latencyProcessor)
        {
            if (oldNode->latencyProcessor->hasConfiguration (latencyNumSamples, sampleRate, props.numberOfChannels))
                latencyProcessor = oldNode->latencyProcessor;

            return;
        }

        if (latencyProcessor->hasSameConfigurationAs (*oldNode->latencyProcessor))
            latencyProcessor = oldNode->latencyProcessor;
    }
}

} // namespace tracktion::inline engine
