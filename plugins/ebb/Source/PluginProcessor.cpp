#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

EbbAudioProcessor::EbbAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "EbbState", createParameterLayout())
{
    bypassParam = parameters.getRawParameterValue (ebb::params::bypass);
    bypassParameter = dynamic_cast<juce::AudioParameterBool*> (parameters.getParameter (ebb::params::bypass));
    jassert (bypassParameter != nullptr);
}

juce::AudioProcessorValueTreeState::ParameterLayout EbbAudioProcessor::createParameterLayout()
{
    namespace id = ebb::params;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::bypass, id::versionHint }, "Bypass", false));
    return layout;
}

void EbbAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    // Fase 1 no tiene buffers propios: el motor procesa en el sitio y el reloj es de tamaño fijo.
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate, bypassParam->load() >= 0.5f);
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
    engine.process (buffer.getArrayOfWritePointers(), numChannels, numSamples);
    publishTransport (numSamples);
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
