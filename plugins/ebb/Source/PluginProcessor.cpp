#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

#include "deepwater/dsp/Decibels.h"

namespace
{
// Profundidad en % de amplitud y, entre paréntesis, lo mismo en dB: 50 % = la mitad de amplitud = −6 dB.
juce::String depthToText (float percent, int)
{
    const float gain = 1.0f - percent / 100.0f;
    if (gain >= 1.0f)
        return "0.0 % (0 dB)";
    const auto db = gain <= 0.0f ? juce::String::fromUTF8 ("\xe2\x88\x9e") : juce::String (-deepwater::dsp::gainToDb (gain), 1);
    return juce::String (percent, 1) + juce::String::fromUTF8 (" % (\xe2\x88\x92") + db + " dB)";
}
} // namespace

EbbAudioProcessor::EbbAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "EbbState", createParameterLayout())
{
    bypassParam = parameters.getRawParameterValue (ebb::params::bypass);
    divisionParam = parameters.getRawParameterValue (ebb::params::division);
    depthParam = parameters.getRawParameterValue (ebb::params::depth);
    durationParam = parameters.getRawParameterValue (ebb::params::duration);
    smoothingParam = parameters.getRawParameterValue (ebb::params::smoothing);
    bypassParameter = dynamic_cast<juce::AudioParameterBool*> (parameters.getParameter (ebb::params::bypass));
    jassert (bypassParameter != nullptr);
}

juce::AudioProcessorValueTreeState::ParameterLayout EbbAudioProcessor::createParameterLayout()
{
    namespace id = ebb::params;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::bypass, id::versionHintPhase1 }, "Bypass", false));

    // Fase 2: modo tempo.
    juce::StringArray divisionNames;
    for (const auto& division : ebb::dsp::duckDivisions)
        divisionNames.add (division.name);
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::division, id::versionHintPhase2 }, "Division",
                                                              divisionNames, ebb::dsp::defaultDuckDivision));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::depth, id::versionHintPhase2 }, "Depth", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 70.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (depthToText)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::duration, id::versionHintPhase2 }, "Duration", juce::NormalisableRange<float> (5.0f, 100.0f, 0.1f), 70.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("%")
            .withStringFromValueFunction ([] (float percent, int) { return juce::String (percent, 1) + " %"; })));

    // Los primeros milisegundos son los que importan (0.5 ms vs 3 ms se oye; 15 vs 18, no): rango con sesgo, 3 ms al centro.
    juce::NormalisableRange<float> smoothingRange (0.0f, 20.0f, 0.01f);
    smoothingRange.setSkewForCentre (3.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::smoothing, id::versionHintPhase2 }, "Smoothing", smoothingRange, 2.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("ms")
            .withStringFromValueFunction ([] (float ms, int) { return juce::String (ms, ms < 10.0f ? 2 : 1) + " ms"; })));
    return layout;
}

void EbbAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    // Sin buffers propios: el motor procesa en el sitio y el reloj y la curva son de tamaño fijo.
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate, bypassParam->load() >= 0.5f, readTempoSettings());
    clock.prepare (sampleRate);
    setLatencySamples (0);
}

void EbbAudioProcessor::releaseResources() {}

bool EbbAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Mono → mono o estéreo → estéreo. (El bus sidechain llega en la Fase 5.)
    const auto& output = layouts.getMainOutputChannelSet();
    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == output;
}

ebb::dsp::TempoSettings EbbAudioProcessor::readTempoSettings() const noexcept
{
    ebb::dsp::TempoSettings settings;
    settings.division = static_cast<int> (divisionParam->load());
    settings.depth = depthParam->load() / 100.0f;
    settings.duration = durationParam->load() / 100.0f;
    settings.smoothingMs = smoothingParam->load();
    return settings;
}

ebb::dsp::TransportInfo EbbAudioProcessor::readHostTransport() noexcept
{
    // getPosition() está pensado para llamarse desde processBlock: no reserva memoria ni bloquea. Cada dato es
    // opcional; el Standalone no tiene PlayHead y el reloj corre libre a 120 BPM.
    ebb::dsp::TransportInfo info;
    if (auto* host = getPlayHead())
        if (const auto position = host->getPosition())
        {
            info.hasHost = true;
            info.isPlaying = position->getIsPlaying();
            info.isLooping = position->getIsLooping();
            // juce::Optional → std::optional (el reloj no conoce JUCE).
            if (const auto bpm = position->getBpm())
                info.bpm = *bpm;
            if (const auto ppq = position->getPpqPosition())
                info.ppqPosition = *ppq;
            if (const auto barStart = position->getPpqPositionOfLastBarStart())
                info.ppqLastBarStart = *barStart;
            if (const auto signature = position->getTimeSignature())
                info.timeSignature = ebb::dsp::TimeSignature { signature->numerator, signature->denominator };
        }
    return info;
}

void EbbAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), getTotalNumInputChannels(), ebb::dsp::Engine::maxChannels);
    // Canales de salida sin entrada (no debería pasar con los layouts aceptados): mejor silencio que basura.
    for (int ch = getTotalNumInputChannels(); ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    clock.beginBlock (readHostTransport(), numSamples);
    engine.setBypassed (bypassParam->load() >= 0.5f);
    engine.setSettings (readTempoSettings());
    engine.process (buffer.getArrayOfWritePointers(), numChannels, numSamples, clock);
    publishTransport (numSamples);

    // Mínimo acumulado hasta que la GUI lo lea. Si la GUI lo reinicia justo entre la lectura y la escritura se pierde a
    // lo sumo un bloque del medidor: no vale la pena un bucle compare-exchange en el hilo de audio.
    if (const float blockMin = engine.blockMinGain(); blockMin < minGainSinceRead.load (std::memory_order_relaxed))
        minGainSinceRead.store (blockMin, std::memory_order_relaxed);
}

void EbbAudioProcessor::publishTransport (int numSamples) noexcept
{
    TransportSnapshot snapshot;
    snapshot.ppq = clock.blockStartPpq();
    snapshot.bpm = clock.bpm();
    snapshot.timeSignature = clock.timeSignature();
    if (const auto barStart = clock.lastBarStart())
    {
        snapshot.lastBarStart = *barStart;
        snapshot.hasLastBarStart = true;
    }
    snapshot.source = clock.getSource();
    snapshot.looping = clock.isLooping();
    snapshot.jumps = clock.jumpCount();
    snapshot.sampleRate = currentSampleRate;
    snapshot.blockSize = numSamples;
    transportBox.write (snapshot);
}

void EbbAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Se ejecuta en el hilo de mensajes (al guardar el proyecto de FL), no en el de audio.
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void EbbAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            parameters.replaceState (juce::ValueTree::fromXml (*xml));

            // Lección de Undertow (Fase 11, pluginval --randomise): un parámetro on/off puede tener un valor "crudo"
            // intermedio que puso el host (0.34 = Off). Si el estado restaurado dice lo mismo, replaceState no lo toca
            // y el host sigue viendo 0.34. Se lleva cada parámetro discreto a su valor exacto.
            for (auto* parameter : juce::AudioProcessor::getParameters())
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter); ranged != nullptr && ranged->isDiscrete())
                    if (const float exact = ranged->convertTo0to1 (ranged->convertFrom0to1 (ranged->getValue())); exact != ranged->getValue())
                        ranged->setValueNotifyingHost (exact);
        }
}

juce::AudioProcessorEditor* EbbAudioProcessor::createEditor()
{
    return new EbbAudioProcessorEditor (*this); // el host toma la propiedad del editor
}

// Punto de entrada que JUCE llama para crear una instancia del plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EbbAudioProcessor(); // el wrapper del formato (VST3/Standalone) toma la propiedad
}
