#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

namespace
{
constexpr float maxGainDb = 24.0f;

// Ganancias como "+3.5 dB", "-12.0 dB" o "0.0 dB".
juce::String gainToText (float db, int /*maxLength*/)
{
    const float rounded = std::round (db * 10.0f) / 10.0f;
    return (rounded > 0.0f ? "+" : "") + juce::String (rounded == 0.0f ? 0.0f : rounded, 1) + " dB";
}

std::unique_ptr<juce::AudioParameterFloat> makeDbParameter (const char* id, const juce::String& name, float maxDb, int version)
{
    // Lineal en dB: cada milímetro de la perilla cambia lo mismo en volumen percibido.
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, version }, name, juce::NormalisableRange<float> (-maxDb, maxDb, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction (gainToText)
            .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); }));
}

// Frecuencias como "80 Hz" o "2.5 kHz"; el extremo que apaga el filtro (offHz) se muestra como "Off".
juce::String frequencyToText (float hz, float offHz)
{
    if (offHz > 0.0f && riptide::dsp::isCutOff (hz, offHz))
        return "Off";
    return hz < 999.5f ? juce::String (juce::roundToInt (hz)) + " Hz" : juce::String (hz / 1000.0f, 1) + " kHz";
}

float textToFrequency (const juce::String& text, float offHz)
{
    if (offHz > 0.0f && text.trim().equalsIgnoreCase ("off"))
        return offHz;
    const float value = text.getFloatValue();
    return text.containsIgnoreCase ("k") ? value * 1000.0f : value;
}

// Perilla de frecuencia logarítmica: cada octava ocupa lo mismo en el recorrido (el oído escucha en octavas).
// offHz < 0 = el parámetro no tiene posición de apagado.
std::unique_ptr<juce::AudioParameterFloat> makeFrequencyParameter (const char* id, const juce::String& name, float minHz,
                                                                   float maxHz, float defaultHz, float offHz)
{
    juce::NormalisableRange<float> range (
        minHz, maxHz,
        [] (float start, float end, float proportion) { return start * std::pow (end / start, proportion); },
        [] (float start, float end, float hz) { return std::log (hz / start) / std::log (end / start); });
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, riptide::params::versionHintPhase4 }, name, range, defaultHz,
        juce::AudioParameterFloatAttributes()
            .withLabel ("Hz")
            .withStringFromValueFunction ([offHz] (float hz, int) { return frequencyToText (hz, offHz); })
            .withValueFromStringFunction ([offHz] (const juce::String& text) { return textToFrequency (text, offHz); }));
}

std::unique_ptr<juce::AudioParameterChoice> makePositionParameter (const char* id, const juce::String& name, bool post)
{
    // Índice 0 = antes de la curva, 1 = después (no cambiar el orden: los proyectos guardan el índice).
    return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, riptide::params::versionHintPhase4 }, name,
                                                         juce::StringArray { "Pre", "Post" }, post ? 1 : 0);
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
    oversamplingParam = parameters.getRawParameterValue (id::oversampling);
    modeParam = parameters.getRawParameterValue (id::mode);
    lowCutParam = parameters.getRawParameterValue (id::lowCut);
    lowCutPositionParam = parameters.getRawParameterValue (id::lowCutPosition);
    highCutParam = parameters.getRawParameterValue (id::highCut);
    highCutPositionParam = parameters.getRawParameterValue (id::highCutPosition);
    emphasisParam = parameters.getRawParameterValue (id::emphasis);
    emphasisFreqParam = parameters.getRawParameterValue (id::emphasisFreq);
    tiltParam = parameters.getRawParameterValue (id::tilt);
    bypassParameter = dynamic_cast<juce::AudioParameterBool*> (parameters.getParameter (id::bypass));
    jassert (bypassParameter != nullptr);
    startTimerHz (20);
}

RiptideAudioProcessor::~RiptideAudioProcessor()
{
    stopTimer();
}

juce::AudioProcessorValueTreeState::ParameterLayout RiptideAudioProcessor::createParameterLayout()
{
    namespace id = riptide::params;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (makeDbParameter (id::inputGain, "Input", maxGainDb, id::versionHint));
    layout.add (makeDbParameter (id::outputGain, "Output", maxGainDb, id::versionHint));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::bypass, id::versionHint }, "Bypass", false));
    // El índice de cada opción es el número de etapas ×2 del motor (no cambiar el orden: los proyectos guardan el
    // índice). 4x por defecto: el aliasing de tanh queda muy por debajo de lo audible con 1.7 ms de latencia a 44.1 kHz.
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::oversampling, id::versionHintPhase2 },
                                                              "Oversampling",
                                                              juce::StringArray { "Off", "2x", "4x", "8x", "16x" }, 2));
    // Los nombres salen de riptide::dsp::modeNames: índice del parámetro = valor del enum (solo añadir al final).
    // Tape por defecto: el modo más suave y el que menos "se nota" como efecto.
    juce::StringArray modes;
    for (const auto* name : riptide::dsp::modeNames)
        modes.add (name);
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::mode, id::versionHintPhase3 }, "Mode", modes, 0));

    // Tono. Los valores por defecto dejan la señal intacta (cortes apagados, emphasis y tilt en 0): un proyecto de la
    // Fase 3 suena igual. Low Cut antes de la curva y High Cut después, como suele usarse (ver Tone.h).
    namespace dsp = riptide::dsp;
    layout.add (makeFrequencyParameter (id::lowCut, "Low Cut", dsp::lowCutOffHz, dsp::lowCutMaxHz, dsp::lowCutOffHz, dsp::lowCutOffHz));
    layout.add (makePositionParameter (id::lowCutPosition, "Low Cut Position", false));
    layout.add (makeFrequencyParameter (id::highCut, "High Cut", dsp::highCutMinHz, dsp::highCutOffHz, dsp::highCutOffHz,
                                        dsp::highCutOffHz));
    layout.add (makePositionParameter (id::highCutPosition, "High Cut Position", true));
    layout.add (makeDbParameter (id::emphasis, "Emphasis", dsp::maxEmphasisDb, id::versionHintPhase4));
    layout.add (makeFrequencyParameter (id::emphasisFreq, "Emphasis Freq", dsp::emphasisMinHz, dsp::emphasisMaxHz, 1000.0f, -1.0f));
    layout.add (makeDbParameter (id::tilt, "Tilt", dsp::maxTiltDb, id::versionHintPhase4));
    return layout;
}

riptide::dsp::EngineSettings RiptideAudioProcessor::readSettings() const noexcept
{
    const int mode = juce::jlimit (0, riptide::dsp::numModes - 1, static_cast<int> (std::lround (modeParam->load())));
    riptide::dsp::ToneSettings tone;
    tone.lowCutHz = lowCutParam->load();
    tone.lowCutPost = lowCutPositionParam->load() >= 0.5f;
    tone.highCutHz = highCutParam->load();
    tone.highCutPost = highCutPositionParam->load() >= 0.5f;
    tone.emphasisDb = emphasisParam->load();
    tone.emphasisHz = emphasisFreqParam->load();
    tone.tiltDb = tiltParam->load();
    return { inputGainParam->load(), outputGainParam->load(), bypassParam->load() >= 0.5f,
             static_cast<int> (std::lround (oversamplingParam->load())), static_cast<riptide::dsp::Mode> (mode), tone };
}

void RiptideAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    // El motor no reserva memoria: todo (las etapas de los cinco factores, el retardo del camino original) es de tamaño
    // fijo. Aquí solo se diseñan los filtros para esta frecuencia de muestreo.
    engine.prepare (sampleRate, readSettings());
    for (auto* meter : { &inputMeter, &outputMeter })
        for (auto& channel : *meter)
        {
            channel.peak.store (0.0f);
            channel.rms.store (0.0f);
        }
    engineLatency.store (engine.getLatency());
    setLatencySamples (engine.getLatency());
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
    engineLatency.store (engine.getLatency(), std::memory_order_relaxed);
    publishMeters (numChannels);
}

void RiptideAudioProcessor::timerCallback()
{
    // El motor cambia de factor en silencio; aquí solo se le cuenta al host, que vuelve a alinear esta pista con las
    // demás. Entre el cambio y el aviso pasan como mucho 50 ms.
    if (const int latency = engineLatency.load (std::memory_order_relaxed); latency != getLatencySamples())
        setLatencySamples (latency);
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
