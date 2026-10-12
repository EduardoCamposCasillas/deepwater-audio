#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

namespace
{
// "+3.5 dB", "-12.0 dB" o "0.0 dB".
juce::String gainToText (float db, int /*maximumLength*/)
{
    const float rounded = std::round (db * 10.0f) / 10.0f;
    return (rounded > 0.0f ? "+" : "") + juce::String (rounded == 0.0f ? 0.0f : rounded, 1) + " dB";
}
} // namespace

FathomAudioProcessor::FathomAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "FathomState", createParameterLayout())
{
    namespace id = fathom::params;
    bypassParam = parameters.getRawParameterValue (id::bypass);
    outputGainParam = parameters.getRawParameterValue (id::outputGain);
    bypassParameter = dynamic_cast<juce::AudioParameterBool*> (parameters.getParameter (id::bypass));
    jassert (bypassParameter != nullptr);
}

juce::AudioProcessorValueTreeState::ParameterLayout FathomAudioProcessor::createParameterLayout()
{
    namespace id = fathom::params;
    using Engine = fathom::dsp::Engine;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::bypass, id::versionHintPhase1 }, "Bypass", false));
    // Lineal en dB: cada milímetro de la perilla cambia lo mismo en volumen percibido.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::outputGain, id::versionHintPhase1 }, "Output",
        juce::NormalisableRange<float> (Engine::minOutputDb, Engine::maxOutputDb, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction (gainToText)
            .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); })));
    return layout;
}

void FathomAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    // Fase 1 no tiene buffers propios: el motor procesa en el sitio y las tomas del analizador son de tamaño fijo.
    engine.prepare (sampleRate, bypassParam->load() >= 0.5f, outputGainParam->load());
    analyzerSampleRate.store (sampleRate);
    setLatencySamples (0);
}

void FathomAudioProcessor::releaseResources() {}

bool FathomAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Mono → mono o estéreo → estéreo.
    const auto& output = layouts.getMainOutputChannelSet();
    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == output;
}

void FathomAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), getTotalNumInputChannels(), fathom::dsp::Engine::maxChannels);
    // Canales de salida sin entrada (no debería pasar con los layouts aceptados): mejor silencio que basura.
    for (int ch = getTotalNumInputChannels(); ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    // Para la GUI: la entrada tal cual llega (el motor procesa en el sitio, así que antes de procesar). Sin editor
    // abierto no hay quien la lea: no se copia.
    const bool feedAnalyzer = analyzerActive.load (std::memory_order_relaxed);
    if (feedAnalyzer)
        inputTap.push (buffer.getArrayOfReadPointers(), numChannels, numSamples);

    engine.setBypassed (bypassParam->load() >= 0.5f);
    engine.setOutputDb (outputGainParam->load());
    engine.process (buffer.getArrayOfWritePointers(), numChannels, numSamples);

    if (feedAnalyzer)
        outputTap.push (buffer.getArrayOfReadPointers(), numChannels, numSamples);
}

void FathomAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Se ejecuta en el hilo de mensajes (al guardar el proyecto de FL), no en el de audio.
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void FathomAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
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

juce::AudioProcessorEditor* FathomAudioProcessor::createEditor()
{
    return new FathomAudioProcessorEditor (*this); // el host toma la propiedad del editor
}

// Punto de entrada que JUCE llama para crear una instancia del plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FathomAudioProcessor(); // el wrapper del formato (VST3/Standalone) toma la propiedad
}
