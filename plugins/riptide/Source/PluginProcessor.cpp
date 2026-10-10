#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

namespace
{
constexpr float minGainDb = -24.0f;
constexpr float maxGainDb = 24.0f;

// Ganancias como "+3.5 dB", "-12.0 dB" o "0.0 dB".
juce::String gainToText (float db, int /*maxLength*/)
{
    const float rounded = std::round (db * 10.0f) / 10.0f;
    return (rounded > 0.0f ? "+" : "") + juce::String (rounded == 0.0f ? 0.0f : rounded, 1) + " dB";
}

std::unique_ptr<juce::AudioParameterFloat> makeGainParameter (const char* id, const juce::String& name)
{
    // Lineal en dB: cada milímetro de la perilla cambia lo mismo en volumen percibido.
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, riptide::params::versionHint }, name,
        juce::NormalisableRange<float> (minGainDb, maxGainDb, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction (gainToText)
            .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); }));
}

// Publica el pico de este bloque sin perder el de bloques anteriores que la GUI aún no leyó: se queda con el mayor.
// compare_exchange en bucle = "máximo atómico" sin locks (en x64 casi siempre acierta a la primera).
void publishPeak (std::atomic<float>& target, float peak) noexcept
{
    float current = target.load (std::memory_order_relaxed);
    while (peak > current && ! target.compare_exchange_weak (current, peak, std::memory_order_relaxed))
    {
    }
}
} // namespace

RiptideAudioProcessor::RiptideAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "RiptideState", createParameterLayout())
{
    namespace id = riptide::params;
    inputGainParam = parameters.getRawParameterValue (id::inputGain);
    outputGainParam = parameters.getRawParameterValue (id::outputGain);
    bypassParam = parameters.getRawParameterValue (id::bypass);
    bypassParameter = dynamic_cast<juce::AudioParameterBool*> (parameters.getParameter (id::bypass));
    jassert (bypassParameter != nullptr);
}

juce::AudioProcessorValueTreeState::ParameterLayout RiptideAudioProcessor::createParameterLayout()
{
    namespace id = riptide::params;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (makeGainParameter (id::inputGain, "Input"));
    layout.add (makeGainParameter (id::outputGain, "Output"));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::bypass, id::versionHint }, "Bypass", false));
    return layout;
}

riptide::dsp::EngineSettings RiptideAudioProcessor::readSettings() const noexcept
{
    return { inputGainParam->load(), outputGainParam->load(), bypassParam->load() >= 0.5f };
}

void RiptideAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    // Fase 1 no tiene buffers propios: el motor procesa en el sitio y sus medidores y rampas son de tamaño fijo.
    engine.prepare (sampleRate, readSettings());
    for (auto* meter : { &inputMeter, &outputMeter })
        for (auto& channel : *meter)
        {
            channel.peak.store (0.0f);
            channel.rms.store (0.0f);
        }
    setLatencySamples (0);
}

void RiptideAudioProcessor::releaseResources() {}

bool RiptideAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Mono → mono o estéreo → estéreo. (Mono → estéreo no tiene sentido hasta que haya algo que abra la imagen.)
    const auto& output = layouts.getMainOutputChannelSet();
    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == output;
}

void RiptideAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numChannels = juce::jmin (buffer.getNumChannels(), getTotalNumInputChannels(), riptide::dsp::Engine::maxChannels);
    // Canales de salida sin entrada (no debería pasar con los layouts aceptados): mejor silencio que basura.
    for (int ch = getTotalNumInputChannels(); ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    engine.setSettings (readSettings());
    engine.process (buffer.getArrayOfWritePointers(), numChannels, buffer.getNumSamples());
    publishMeters (numChannels);
}

void RiptideAudioProcessor::publishMeters (int numChannels) noexcept
{
    meteredChannels.store (numChannels, std::memory_order_relaxed);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto index = static_cast<size_t> (ch);
        publishPeak (inputMeter[index].peak, engine.inputMeter (ch).takePeak());
        publishPeak (outputMeter[index].peak, engine.outputMeter (ch).takePeak());
        inputMeter[index].rms.store (engine.inputMeter (ch).rms(), std::memory_order_relaxed);
        outputMeter[index].rms.store (engine.outputMeter (ch).rms(), std::memory_order_relaxed);
    }
}

void RiptideAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Se ejecuta en el hilo de mensajes (al guardar el proyecto de FL), no en el de audio.
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void RiptideAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
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

juce::AudioProcessorEditor* RiptideAudioProcessor::createEditor()
{
    return new RiptideAudioProcessorEditor (*this); // el host toma la propiedad del editor
}

// Punto de entrada que JUCE llama para crear una instancia del plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RiptideAudioProcessor(); // el wrapper del formato (VST3/Standalone) toma la propiedad
}
