#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"
#include "gui/Format.h"

namespace
{
juce::String gainToText (float db, int /*maximumLength*/) { return fathom::gui::format::db (db); }
juce::String hzToText (float hz, int /*maximumLength*/) { return fathom::gui::format::hz (hz); }
juce::String qToText (float q, int /*maximumLength*/) { return juce::String (q, q < 10.0f ? 2 : 1); }

// "2.5 kHz", "2.5k" o "2500" → 2500 Hz.
float textToHz (const juce::String& text)
{
    const float value = text.getFloatValue();
    return text.containsIgnoreCase ("k") ? value * 1000.0f : value;
}

// Rango logarítmico exacto: cada vuelta de perilla (o de automatización) recorre lo mismo en octavas o en "veces Q".
// skewFactor de JUCE es solo una aproximación; con las tres funciones el mapeo es log de verdad.
juce::NormalisableRange<float> logRange (float minimum, float maximum)
{
    return { minimum, maximum,
             [] (float start, float end, float proportion) { return start * std::pow (end / start, proportion); },
             [] (float start, float end, float value) { return std::log (value / start) / std::log (end / start); },
             [] (float start, float end, float value) { return juce::jlimit (start, end, value); } };
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
    for (int b = 0; b < id::numBands; ++b)
    {
        auto& band = bandParams[static_cast<size_t> (b)];
        band.enabled = parameters.getRawParameterValue (id::band (b, id::enabled));
        band.frequency = parameters.getRawParameterValue (id::band (b, id::frequency));
        band.gain = parameters.getRawParameterValue (id::band (b, id::gain));
        band.q = parameters.getRawParameterValue (id::band (b, id::q));
        band.shape = parameters.getRawParameterValue (id::band (b, id::shape));
        band.slope = parameters.getRawParameterValue (id::band (b, id::slope));
    }
    bypassParameter = dynamic_cast<juce::AudioParameterBool*> (parameters.getParameter (id::bypass));
    jassert (bypassParameter != nullptr);

    // Los valores por defecto, leídos de los propios parámetros (convertidos igual que cualquier valor).
    const auto defaultOf = [this] (int b, const char* setting) {
        auto* parameter = parameters.getParameter (id::band (b, setting));
        return parameter->convertFrom0to1 (parameter->getDefaultValue());
    };
    for (int b = 0; b < id::numBands; ++b)
        defaultBands[static_cast<size_t> (b)] = { defaultOf (b, id::enabled) >= 0.5f,
                                                  defaultOf (b, id::frequency),
                                                  defaultOf (b, id::gain),
                                                  defaultOf (b, id::q),
                                                  static_cast<fathom::dsp::Shape> (juce::roundToInt (defaultOf (b, id::shape))),
                                                  juce::roundToInt (defaultOf (b, id::slope)) };

    std::vector<juce::RangedAudioParameter*> undoable;
    for (auto* parameter : juce::AudioProcessor::getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter); ranged != nullptr && ranged != bypassParameter)
            undoable.push_back (ranged);
    history.attach (std::move (undoable));
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

    // Las 24 bandas, apagadas. Rangos como los de un EQ profesional: 10 Hz-30 kHz (encima de Nyquist se limita),
    // ±30 dB y Q de 0.025 (casi 9 octavas de ancho) a 40 (un 1/36 de octava: para cazar una resonancia). Cada banda
    // empieza en su propia frecuencia (repartidas cada ~0.4 octavas de 30 Hz a 16 kHz): al encender varias desde el
    // panel no quedan una encima de otra.
    using Band = fathom::dsp::BandSettings;
    static constexpr std::array<float, id::numBands> defaultFrequencies { 30.0f,   40.0f,   50.0f,   65.0f,   85.0f,   115.0f,
                                                                          150.0f,  200.0f,  260.0f,  350.0f,  460.0f,  600.0f,
                                                                          800.0f,  1000.0f, 1400.0f, 1800.0f, 2400.0f, 3200.0f,
                                                                          4200.0f, 5500.0f, 7300.0f, 9600.0f, 12500.0f, 16000.0f };
    // El orden de las listas es el de fathom::dsp::Shape y fathom::dsp::slopes (se guarda el índice: no reordenar).
    const juce::StringArray shapes { "Bell",     "Low Shelf", "High Shelf", "Tilt Shelf", "Flat Tilt",
                                     "Low Cut",  "High Cut",  "Notch",      "Band Pass",  "All Pass" };
    jassert (shapes.size() == fathom::dsp::numShapes);
    juce::StringArray slopes;
    for (const int slope : fathom::dsp::slopes)
        slopes.add (juce::String (slope) + " dB/oct");

    for (int b = 0; b < id::numBands; ++b)
    {
        // La banda 1 conserva la versión de la Fase 2 en los ajustes que ya tenía.
        const int hint = b == 0 ? id::versionHintPhase2 : id::versionHintPhase3;
        const auto name = "Band " + juce::String (b + 1) + " ";
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::band (b, id::enabled), hint }, name + "On", false));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id::band (b, id::frequency), hint }, name + "Freq", logRange (Band::minFrequency, Band::maxFrequency),
            defaultFrequencies[static_cast<size_t> (b)],
            juce::AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction (hzToText).withValueFromStringFunction (textToHz)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id::band (b, id::gain), hint }, name + "Gain",
            juce::NormalisableRange<float> (Band::minGainDb, Band::maxGainDb, 0.01f), 0.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel ("dB")
                .withStringFromValueFunction (gainToText)
                .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); })));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id::band (b, id::q), hint }, name + "Q", logRange (Band::minQ, Band::maxQ), 1.0f,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction (qToText)
                .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); })));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::band (b, id::shape), id::versionHintPhase3 },
                                                                  name + "Shape", shapes, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::band (b, id::slope), id::versionHintPhase3 },
                                                                  name + "Slope", slopes, 1));
    }
    return layout;
}

fathom::dsp::BandSettings FathomAudioProcessor::getBandSettings (int band) const noexcept
{
    const auto& p = bandParams[static_cast<size_t> (band)];
    return { p.enabled->load() >= 0.5f,
             p.frequency->load(),
             p.gain->load(),
             p.q->load(),
             static_cast<fathom::dsp::Shape> (juce::jlimit (0, fathom::dsp::numShapes - 1, juce::roundToInt (p.shape->load()))),
             juce::jlimit (0, static_cast<int> (fathom::dsp::slopes.size()) - 1, juce::roundToInt (p.slope->load())) };
}

fathom::dsp::Engine::BandArray FathomAudioProcessor::getAllBandSettings() const noexcept
{
    fathom::dsp::Engine::BandArray bands;
    for (int b = 0; b < fathom::dsp::Engine::numBands; ++b)
        bands[static_cast<size_t> (b)] = getBandSettings (b);
    return bands;
}

void FathomAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    // Sin buffers propios: el motor procesa en el sitio y las tomas del analizador son de tamaño fijo.
    engine.prepare (sampleRate, bypassParam->load() >= 0.5f, outputGainParam->load(), getAllBandSettings());
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
    for (int b = 0; b < fathom::dsp::Engine::numBands; ++b)
        engine.setBand (b, getBandSettings (b));
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
