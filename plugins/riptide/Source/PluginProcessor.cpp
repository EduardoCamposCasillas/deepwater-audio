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
                                                                   float maxHz, float defaultHz, float offHz,
                                                                   int version = riptide::params::versionHintPhase4)
{
    juce::NormalisableRange<float> range (
        minHz, maxHz,
        [] (float start, float end, float proportion) { return start * std::pow (end / start, proportion); },
        [] (float start, float end, float hz) { return std::log (hz / start) / std::log (end / start); });
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, version }, name, range, defaultHz,
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

// Rango logarítmico (cada octava, o cada ×2 de tiempo, ocupa lo mismo en el recorrido de la perilla).
juce::NormalisableRange<float> logRange (float minimum, float maximum)
{
    return { minimum, maximum,
             [] (float start, float end, float proportion) { return start * std::pow (end / start, proportion); },
             [] (float start, float end, float value) { return std::log (value / start) / std::log (end / start); } };
}

// Tiempos como "0.5 ms", "35 ms" o "1.20 s".
juce::String timeToText (float ms)
{
    if (ms < 9.95f)
        return juce::String (ms, 1) + " ms";
    return ms < 999.5f ? juce::String (juce::roundToInt (ms)) + " ms" : juce::String (ms / 1000.0f, 2) + " s";
}

float textToTime (const juce::String& text)
{
    // "1.2 s" son segundos; "35", "35 ms", milisegundos.
    const auto trimmed = text.trim();
    const bool seconds = trimmed.endsWithIgnoreCase ("s") && ! trimmed.endsWithIgnoreCase ("ms");
    return trimmed.getFloatValue() * (seconds ? 1000.0f : 1.0f);
}

std::unique_ptr<juce::AudioParameterFloat> makeTimeParameter (const char* id, const juce::String& name, float minMs,
                                                              float maxMs, float defaultMs)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, riptide::params::versionHintPhase7 }, name, logRange (minMs, maxMs), defaultMs,
        juce::AudioParameterFloatAttributes()
            .withLabel ("ms")
            .withStringFromValueFunction ([] (float ms, int) { return timeToText (ms); })
            .withValueFromStringFunction (textToTime));
}

// Cantidad de una ruta de modulación: −100..+100 % ("+35 %"). 0 = no mueve nada.
std::unique_ptr<juce::AudioParameterFloat> makeAmountParameter (const char* id, const juce::String& name)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, riptide::params::versionHintPhase7 }, name, juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("%")
            .withStringFromValueFunction ([] (float percent, int)
                                          {
                                              const int rounded = juce::roundToInt (percent);
                                              return (rounded > 0 ? "+" : "") + juce::String (rounded) + " %";
                                          })
            .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); }));
}

// Destino de una ruta: los nombres salen de riptide::dsp::modTargetNames (índice = valor del enum; solo añadir al final).
std::unique_ptr<juce::AudioParameterChoice> makeTargetParameter (const char* id, const juce::String& name)
{
    juce::StringArray targets;
    for (const auto* target : riptide::dsp::modTargetNames)
        targets.add (target);
    return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, riptide::params::versionHintPhase7 }, name,
                                                         targets, static_cast<int> (riptide::dsp::ModTarget::drive));
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
    mixParam = parameters.getRawParameterValue (id::mix);
    autoGainParam = parameters.getRawParameterValue (id::autoGain);
    deltaParam = parameters.getRawParameterValue (id::delta);
    bandsParam = parameters.getRawParameterValue (id::bands);
    for (size_t i = 0; i < crossoverParams.size(); ++i)
        crossoverParams[i] = parameters.getRawParameterValue (id::crossover[i]);
    for (size_t b = 0; b < bandModeParams.size(); ++b)
    {
        bandModeParams[b] = parameters.getRawParameterValue (id::bandMode[b]);
        bandDriveParams[b] = parameters.getRawParameterValue (id::bandDrive[b]);
        bandSoloParams[b] = parameters.getRawParameterValue (id::bandSolo[b]);
        bandMuteParams[b] = parameters.getRawParameterValue (id::bandMute[b]);
    }
    envAttackParam = parameters.getRawParameterValue (id::envAttack);
    envReleaseParam = parameters.getRawParameterValue (id::envRelease);
    envSensitivityParam = parameters.getRawParameterValue (id::envSensitivity);
    envTargetParam = parameters.getRawParameterValue (id::envTarget);
    envAmountParam = parameters.getRawParameterValue (id::envAmount);
    lfoShapeParam = parameters.getRawParameterValue (id::lfoShape);
    lfoSyncParam = parameters.getRawParameterValue (id::lfoSync);
    lfoRateParam = parameters.getRawParameterValue (id::lfoRate);
    lfoDivisionParam = parameters.getRawParameterValue (id::lfoDivision);
    lfoTargetParam = parameters.getRawParameterValue (id::lfoTarget);
    lfoAmountParam = parameters.getRawParameterValue (id::lfoAmount);
    // Antes del primer prepareToPlay la GUI ya muestra los cruces: que lean los valores por defecto, no 0 Hz.
    for (size_t i = 0; i < crossoverHz.size(); ++i)
        crossoverHz[i].store (riptide::dsp::defaultCrossoverHz[i]);
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

    // Mezcla. Por defecto 100 %, auto gain y delta apagados: un proyecto de la Fase 4 suena igual.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::mix, id::versionHintPhase5 }, "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("%")
            .withStringFromValueFunction ([] (float percent, int) { return juce::String (juce::roundToInt (percent)) + " %"; })
            .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); })));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::autoGain, id::versionHintPhase5 }, "Auto Gain", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::delta, id::versionHintPhase5 }, "Delta", false));

    // Multibanda. Por defecto 1 banda con el modo global y sin drive propio: un proyecto de la Fase 5 suena igual.
    // Índice de "bands" = número de bandas − 1 (no cambiar el orden).
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::bands, id::versionHintPhase6 }, "Bands",
                                                              juce::StringArray { "1", "2", "3", "4" }, 0));
    for (size_t i = 0; i < id::crossover.size(); ++i)
        layout.add (makeFrequencyParameter (id::crossover[i], "Crossover " + juce::String (i + 1), dsp::crossoverMinHz,
                                            dsp::crossoverMaxHz, dsp::defaultCrossoverHz[i], -1.0f, id::versionHintPhase6));
    // Modo de cada banda: 0 = el global; desde 1, los modos en el orden de modeNames (un modo nuevo va al final, aquí
    // también).
    juce::StringArray bandModes { "Global" };
    bandModes.addArray (modes);
    for (size_t b = 0; b < id::bandMode.size(); ++b)
    {
        const juce::String band = "Band " + juce::String (b + 1);
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::bandMode[b], id::versionHintPhase6 },
                                                                  band + " Mode", bandModes, 0));
        layout.add (makeDbParameter (id::bandDrive[b], band + " Drive", dsp::maxBandDriveDb, id::versionHintPhase6));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::bandSolo[b], id::versionHintPhase6 },
                                                                band + " Solo", false));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::bandMute[b], id::versionHintPhase6 },
                                                                band + " Mute", false));
    }

    // Modulación. Amount 0 en las dos fuentes: un proyecto de la Fase 6 suena igual. Destino por defecto: Drive, el uso más
    // común (la envolvente hace la saturación dinámica; el LFO la hace "respirar" al tempo).
    layout.add (makeTimeParameter (id::envAttack, "Env Attack", dsp::envelopeMinAttackMs, dsp::envelopeMaxAttackMs, 5.0f));
    layout.add (makeTimeParameter (id::envRelease, "Env Release", dsp::envelopeMinReleaseMs, dsp::envelopeMaxReleaseMs, 150.0f));
    layout.add (makeDbParameter (id::envSensitivity, "Env Sensitivity", dsp::maxSensitivityDb, id::versionHintPhase7));
    layout.add (makeTargetParameter (id::envTarget, "Env Target"));
    layout.add (makeAmountParameter (id::envAmount, "Env Amount"));

    juce::StringArray shapes;
    for (const auto* shape : deepwater::dsp::lfoShapeNames)
        shapes.add (shape);
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::lfoShape, id::versionHintPhase7 }, "LFO Shape",
                                                              shapes, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::lfoSync, id::versionHintPhase7 }, "LFO Sync", true));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::lfoRate, id::versionHintPhase7 }, "LFO Rate", logRange (dsp::lfoMinHz, dsp::lfoMaxHz), 1.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("Hz")
            .withStringFromValueFunction ([] (float hz, int) { return juce::String (hz, hz < 1.0f ? 2 : 1) + " Hz"; })
            .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); })));
    juce::StringArray divisions;
    for (const auto& division : deepwater::dsp::lfoDivisions)
        divisions.add (division.name);
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::lfoDivision, id::versionHintPhase7 },
                                                              "LFO Division", divisions, deepwater::dsp::defaultLfoDivision));
    layout.add (makeTargetParameter (id::lfoTarget, "LFO Target"));
    layout.add (makeAmountParameter (id::lfoAmount, "LFO Amount"));
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
    riptide::dsp::EngineSettings settings { inputGainParam->load(),
                                            outputGainParam->load(),
                                            bypassParam->load() >= 0.5f,
                                            static_cast<int> (std::lround (oversamplingParam->load())),
                                            static_cast<riptide::dsp::Mode> (mode),
                                            tone,
                                            mixParam->load() / 100.0f,
                                            autoGainParam->load() >= 0.5f,
                                            deltaParam->load() >= 0.5f };
    settings.numBands = static_cast<int> (std::lround (bandsParam->load())) + 1;
    for (size_t i = 0; i < crossoverParams.size(); ++i)
        settings.crossoverHz[i] = crossoverParams[i]->load();
    for (size_t b = 0; b < bandModeParams.size(); ++b)
    {
        auto& band = settings.bands[b];
        const int bandMode = juce::jlimit (0, riptide::dsp::numModes, static_cast<int> (std::lround (bandModeParams[b]->load())));
        band.ownMode = bandMode > 0;
        band.mode = static_cast<riptide::dsp::Mode> (juce::jmax (0, bandMode - 1));
        band.driveDb = bandDriveParams[b]->load();
        band.solo = bandSoloParams[b]->load() >= 0.5f;
        band.mute = bandMuteParams[b]->load() >= 0.5f;
    }

    const auto target = [] (const std::atomic<float>* parameter)
    {
        return static_cast<riptide::dsp::ModTarget> (
            juce::jlimit (0, riptide::dsp::numModTargets - 1, static_cast<int> (std::lround (parameter->load()))));
    };
    auto& envelope = settings.modulation.envelope;
    envelope.attackMs = envAttackParam->load();
    envelope.releaseMs = envReleaseParam->load();
    envelope.sensitivityDb = envSensitivityParam->load();
    envelope.target = target (envTargetParam);
    envelope.amount = envAmountParam->load() / 100.0f;
    auto& lfo = settings.modulation.lfo;
    const int lastShape = static_cast<int> (deepwater::dsp::lfoShapeNames.size()) - 1;
    lfo.shape = static_cast<deepwater::dsp::LfoShape> (juce::jlimit (0, lastShape, static_cast<int> (std::lround (lfoShapeParam->load()))));
    lfo.sync = lfoSyncParam->load() >= 0.5f;
    lfo.rateHz = lfoRateParam->load();
    lfo.division = static_cast<int> (std::lround (lfoDivisionParam->load()));
    lfo.target = target (lfoTargetParam);
    lfo.amount = lfoAmountParam->load() / 100.0f;
    return settings;
}

riptide::dsp::Transport RiptideAudioProcessor::readTransport() noexcept
{
    // El host informa el tempo y la posición de la canción. getPosition() está pensado para llamarse desde processBlock:
    // no reserva memoria ni bloquea. Sin host (Standalone) quedan los 120 BPM por defecto.
    riptide::dsp::Transport transport;
    if (auto* host = getPlayHead())
        if (const auto position = host->getPosition())
        {
            if (const auto bpm = position->getBpm())
                transport.bpm = *bpm;
            if (const auto ppq = position->getPpqPosition())
            {
                transport.ppqPosition = *ppq;
                transport.hasPosition = true;
            }
            transport.isPlaying = position->getIsPlaying();
        }
    return transport;
}

void RiptideAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    // El motor no reserva memoria: todo (las etapas de los cinco factores, el retardo del camino original) es de tamaño
    // fijo. Aquí solo se diseñan los filtros para esta frecuencia de muestreo.
    engine.prepare (sampleRate, readSettings());
    engine.setTransport (readTransport());
    for (auto* meter : { &inputMeter, &outputMeter })
        for (auto& channel : *meter)
        {
            channel.peak.store (0.0f);
            channel.rms.store (0.0f);
        }
    engineLatency.store (engine.getLatency());
    setLatencySamples (engine.getLatency());
    analyzerSampleRate.store (sampleRate);
    publishBands();
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

    // Para la GUI: la entrada tal cual llega (el motor procesa en el sitio, así que antes de procesar).
    const int numSamples = buffer.getNumSamples();
    inputTap.push (buffer.getArrayOfReadPointers(), numChannels, numSamples);
    float peak = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, numSamples));
    publishPeak (inputPeak, peak);

    engine.setSettings (readSettings());
    engine.setTransport (readTransport());
    engine.process (buffer.getArrayOfWritePointers(), numChannels, numSamples);
    outputTap.push (buffer.getArrayOfReadPointers(), numChannels, numSamples);
    engineLatency.store (engine.getLatency(), std::memory_order_relaxed);
    publishBands();
    publishMeters (numChannels);
}

void RiptideAudioProcessor::publishBands() noexcept
{
    for (size_t b = 0; b < autoGainDb.size(); ++b)
        autoGainDb[b].store (engine.getAutoGainDb (static_cast<int> (b)), std::memory_order_relaxed);
    for (size_t i = 0; i < crossoverHz.size(); ++i)
        crossoverHz[i].store (engine.getCrossoverHz (static_cast<int> (i)), std::memory_order_relaxed);
    envelopeValue.store (engine.getEnvelopeValue(), std::memory_order_relaxed);
    lfoValue.store (engine.getLfoValue(), std::memory_order_relaxed);
    for (size_t t = 0; t < modulation.size(); ++t)
        modulation[t].store (engine.getModulation (static_cast<riptide::dsp::ModTarget> (t)), std::memory_order_relaxed);
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
