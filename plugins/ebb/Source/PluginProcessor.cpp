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

juce::String frequencyToText (float hz, int)
{
    return hz < 999.5f ? juce::String (juce::roundToInt (hz)) + " Hz" : juce::String (hz / 1000.0f, 2) + " kHz";
}

float textToFrequency (const juce::String& text)
{
    const float value = text.getFloatValue();
    return text.containsIgnoreCase ("k") ? value * 1000.0f : value;
}
} // namespace

EbbAudioProcessor::EbbAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          // Fase 5: el kick llega por aquí (en FL: "Sidechain to this track" + el puerto en Processing).
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "EbbState", createParameterLayout())
{
    bypassParam = parameters.getRawParameterValue (ebb::params::bypass);
    divisionParam = parameters.getRawParameterValue (ebb::params::division);
    depthParam = parameters.getRawParameterValue (ebb::params::depth);
    durationParam = parameters.getRawParameterValue (ebb::params::duration);
    smoothingParam = parameters.getRawParameterValue (ebb::params::smoothing);
    modeParam = parameters.getRawParameterValue (ebb::params::mode);
    midiNoteParam = parameters.getRawParameterValue (ebb::params::midiNote);
    velocityParam = parameters.getRawParameterValue (ebb::params::velocity);
    thresholdParam = parameters.getRawParameterValue (ebb::params::threshold);
    holdParam = parameters.getRawParameterValue (ebb::params::hold);
    lookaheadParam = parameters.getRawParameterValue (ebb::params::lookahead);
    bandParam = parameters.getRawParameterValue (ebb::params::band);
    crossoverParam = parameters.getRawParameterValue (ebb::params::crossover);
    mixParam = parameters.getRawParameterValue (ebb::params::mix);
    bypassParameter = dynamic_cast<juce::AudioParameterBool*> (parameters.getParameter (ebb::params::bypass));
    jassert (bypassParameter != nullptr);
    setCurve (ebb::dsp::defaultCurve());
    startTimerHz (10); // vigila el lookahead para informar la latencia nueva al host
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

    // Fase 4: modo MIDI trigger. Por defecto, tempo: los proyectos anteriores suenan igual.
    juce::StringArray modeNames;
    for (const auto* name : ebb::dsp::triggerModeNames)
        modeNames.add (name);
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::mode, id::versionHintPhase4 }, "Mode", modeNames, 0));

    juce::StringArray noteNames { "Todas" };
    for (int note = 0; note < 128; ++note)
        noteNames.add (ebb::dsp::flNoteName (note));
    jassert (noteNames.size() == ebb::dsp::midiNoteFilterCount);
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::midiNote, id::versionHintPhase4 }, "MIDI Note",
                                                              noteNames, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::velocity, id::versionHintPhase4 }, "Velocity", false));

    // Fase 5: modo audio sidechain.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::threshold, id::versionHintPhase5 }, "Threshold", juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -24.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float db, int) { return juce::String (db, 1) + " dB"; })));

    // Lo que importa está en los primeros ms (un flam dura 10–40 ms): rango con sesgo, 40 ms al centro.
    juce::NormalisableRange<float> holdRange (0.0f, 250.0f, 1.0f);
    holdRange.setSkewForCentre (40.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::hold, id::versionHintPhase5 }, "Hold", holdRange, 30.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("ms")
            .withStringFromValueFunction ([] (float ms, int) { return juce::String (juce::roundToInt (ms)) + " ms"; })));

    // Cambia la latencia del plugin: no se automatiza (el host tendría que recolocar todas las pistas en plena
    // reproducción). 0 por defecto: los proyectos anteriores no ganan latencia.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::lookahead, id::versionHintPhase5 }, "Lookahead", juce::NormalisableRange<float> (0.0f, 10.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("ms")
            .withAutomatable (false)
            .withStringFromValueFunction ([] (float ms, int) { return juce::String (ms, 1) + " ms"; })));

    // Fase 6: bandas y mezcla. Por defecto "Todo" y mix 100 %: los proyectos anteriores suenan igual (bit a bit).
    juce::StringArray bandNames;
    for (const auto* name : ebb::dsp::duckBandNames)
        bandNames.add (name);
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::band, id::versionHintPhase6 }, "Band", bandNames, 0));

    // Logarítmica: cada octava ocupa lo mismo en el recorrido de la perilla (el oído escucha en octavas).
    juce::NormalisableRange<float> crossoverRange (
        ebb::dsp::crossoverMinHz, ebb::dsp::crossoverMaxHz,
        [] (float start, float end, float proportion) { return start * std::pow (end / start, proportion); },
        [] (float start, float end, float hz) { return std::log (hz / start) / std::log (end / start); });
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::crossover, id::versionHintPhase6 }, "Crossover", crossoverRange, ebb::dsp::defaultCrossoverHz,
        juce::AudioParameterFloatAttributes()
            .withLabel ("Hz")
            .withStringFromValueFunction (frequencyToText)
            .withValueFromStringFunction (textToFrequency)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::mix, id::versionHintPhase6 }, "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("%")
            .withStringFromValueFunction ([] (float percent, int) { return juce::String (percent, 1) + " %"; })));
    return layout;
}

void EbbAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    // El motor procesa en el sitio; lo único que reserva es la línea de retardo del lookahead (10 ms como máximo).
    currentSampleRate = sampleRate;
    const int lookahead = lookaheadSamplesFromParameter (sampleRate);
    setLatencySamples (lookahead);
    reportedLookahead = lookahead;
    engine.prepare (sampleRate, bypassParam->load() >= 0.5f, readTempoSettings(), lookahead);
    clock.prepare (sampleRate);
    detector.prepare (sampleRate);
    liveScope.reset();
}

int EbbAudioProcessor::lookaheadSamplesFromParameter (double sampleRate) const noexcept
{
    const auto samples = static_cast<int> (std::lround (lookaheadParam->load() * 0.001 * sampleRate));
    return juce::jlimit (0, ebb::dsp::Engine::maxLookaheadSamples (sampleRate), samples);
}

// Hilo de mensajes. Primero se informa al host y después el audio empieza a usar el retraso nuevo (con su fundido).
void EbbAudioProcessor::timerCallback()
{
    const int wanted = lookaheadSamplesFromParameter (currentSampleRate.load());
    if (wanted != getLatencySamples())
        setLatencySamples (wanted);
    reportedLookahead = wanted;
}

void EbbAudioProcessor::releaseResources() {}

bool EbbAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Mono → mono o estéreo → estéreo. El sidechain, mono, estéreo o desactivado (el host puede no conectarlo).
    const auto& output = layouts.getMainOutputChannelSet();
    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainInputChannelSet() != output)
        return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto& sidechain = layouts.getChannelSet (true, 1);
        return sidechain.isDisabled() || sidechain == juce::AudioChannelSet::mono() || sidechain == juce::AudioChannelSet::stereo();
    }
    return true;
}

ebb::dsp::TempoSettings EbbAudioProcessor::readTempoSettings() const noexcept
{
    ebb::dsp::TempoSettings settings;
    settings.division = static_cast<int> (divisionParam->load());
    settings.depth = depthParam->load() / 100.0f;
    settings.duration = durationParam->load() / 100.0f;
    settings.smoothingMs = smoothingParam->load();
    settings.mode = ebb::dsp::triggerModeFromIndex (static_cast<int> (modeParam->load()));
    settings.velocitySensitive = velocityParam->load() >= 0.5f;
    settings.band = ebb::dsp::duckBandFromIndex (static_cast<int> (bandParam->load()));
    settings.crossoverHz = crossoverParam->load();
    settings.mix = mixParam->load() / 100.0f;
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

// Las notas del bloque → disparos para el motor, en la muestra exacta en que caen. El MidiBuffer ya viene ordenado por
// muestra. Las notas se cuentan siempre (aunque estemos en modo tempo) para que la GUI muestre si el MIDI llega.
std::span<const ebb::dsp::Trigger> EbbAudioProcessor::collectTriggers (const juce::MidiBuffer& midi, int numSamples) noexcept
{
    const int filter = static_cast<int> (midiNoteParam->load());
    const bool midiMode = ebb::dsp::triggerModeFromIndex (static_cast<int> (modeParam->load())) == ebb::dsp::TriggerMode::midi;
    size_t count = 0;
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (! message.isNoteOn()) // una nota con velocidad 0 es un note-off: no dispara
            continue;
        const int note = message.getNoteNumber();
        midiNotesReceived.fetch_add (1, std::memory_order_relaxed);
        lastMidiNote.store (note, std::memory_order_relaxed);
        if (! midiMode || ! ebb::dsp::noteFilterAccepts (filter, note))
            continue;

        midiTriggersFired.fetch_add (1, std::memory_order_relaxed);
        const ebb::dsp::Trigger trigger { juce::jlimit (0, juce::jmax (0, numSamples - 1), metadata.samplePosition),
                                          message.getFloatVelocity() };
        if (count < triggers.size())
            triggers[count++] = trigger;
        else
            triggers.back() = trigger;
    }
    return { triggers.data(), count };
}

// El sidechain del bloque → disparos para el motor (solo en modo audio). El detector corre siempre, para que el medidor
// muestre si llega el kick antes de elegir el modo.
std::span<const ebb::dsp::Trigger> EbbAudioProcessor::detectSidechain (juce::AudioBuffer<float>& buffer, int numSamples) noexcept
{
    detector.setThresholdDb (thresholdParam->load());
    detector.setHoldSeconds (holdParam->load() * 0.001);

    // Sin bus (o desactivado) el detector recibe silencio. 'sidechain' apunta a los canales de 'buffer' (no copia nada).
    const auto* bus = getBus (true, 1);
    const bool connected = bus != nullptr && bus->isEnabled();
    const auto sidechain = connected ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float>();
    const bool audioMode = ebb::dsp::triggerModeFromIndex (static_cast<int> (modeParam->load())) == ebb::dsp::TriggerMode::audio;

    const auto result = ebb::dsp::detectAudioTriggers (detector, sidechain.getArrayOfReadPointers(), sidechain.getNumChannels(),
                                                       numSamples, audioMode ? std::span (audioTriggers) : std::span<ebb::dsp::Trigger>());
    const auto detected = static_cast<std::uint32_t> (result.detected);
    sidechainDetected.fetch_add (detected, std::memory_order_relaxed);
    if (audioMode)
        sidechainTriggersFired.fetch_add (detected, std::memory_order_relaxed);
    // Como el mínimo de la ganancia: perder un bloque del medidor si la GUI lo lee justo ahora no importa.
    if (result.peakEnvelope > sidechainPeakSinceRead.load (std::memory_order_relaxed))
        sidechainPeakSinceRead.store (result.peakEnvelope, std::memory_order_relaxed);
    return { audioTriggers.data(), static_cast<size_t> (result.stored) };
}

void EbbAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Los canales del bus principal van primero en el buffer; los del sidechain, después (solo se leen).
    const int numSamples = buffer.getNumSamples();
    const int mainChannels = getMainBusNumInputChannels();
    const int numChannels = juce::jmin (buffer.getNumChannels(), mainChannels, ebb::dsp::Engine::maxChannels);
    // Canales de salida sin entrada (no debería pasar con los layouts aceptados): mejor silencio que basura.
    for (int ch = mainChannels; ch < getMainBusNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    // ~400 bytes por bloque; solo si llegó una curva nueva se la pasa al motor (que hace el glide).
    if (curveBox.read (audioCurve))
        engine.setCurve (audioCurve);

    clock.beginBlock (readHostTransport(), numSamples);
    engine.setBypassed (bypassParam->load() >= 0.5f);
    engine.setSettings (readTempoSettings());
    engine.setLookahead (reportedLookahead.load (std::memory_order_relaxed));
    const auto midiTriggers = collectTriggers (midi, numSamples);
    const auto sidechainTriggers = detectSidechain (buffer, numSamples);
    const auto mode = ebb::dsp::triggerModeFromIndex (static_cast<int> (modeParam->load()));
    // Sin GUI abierta, el visor no se alimenta (no hay quién lo mire).
    const bool liveView = liveViewOpen.load (std::memory_order_relaxed);
    if (liveView)
        liveScope.beginBlock (clock, numSamples);
    engine.setScope (liveView ? &liveScope : nullptr);
    engine.process (buffer.getArrayOfWritePointers(), numChannels, numSamples, clock,
                    mode == ebb::dsp::TriggerMode::audio ? sidechainTriggers : midiTriggers);
    if (liveView)
        feedLiveScopeSidechain (buffer, numSamples);
    publishTransport (numSamples);
    curveHits.store (engine.hitCount(), std::memory_order_relaxed);
    curvePosition.store (engine.curvePosition(), std::memory_order_relaxed);

    // Mínimo acumulado hasta que la GUI lo lea. Si la GUI lo reinicia justo entre la lectura y la escritura se pierde a
    // lo sumo un bloque del medidor: no vale la pena un bucle compare-exchange en el hilo de audio.
    if (const float blockMin = engine.blockMinGain(); blockMin < minGainSinceRead.load (std::memory_order_relaxed))
        minGainSinceRead.store (blockMin, std::memory_order_relaxed);
}

// El sidechain al visor, después del motor (que ya abrió las columnas del bloque). Sin bus conectado no hay nada que poner.
void EbbAudioProcessor::feedLiveScopeSidechain (juce::AudioBuffer<float>& buffer, int numSamples) noexcept
{
    const auto* bus = getBus (true, 1);
    if (bus == nullptr || ! bus->isEnabled())
        return;
    const auto sidechain = getBusBuffer (buffer, true, 1);
    const int channels = sidechain.getNumChannels();
    if (channels == 0)
        return;
    for (int i = 0; i < numSamples; ++i)
    {
        float level = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
            level = juce::jmax (level, std::abs (sidechain.getSample (ch, i)));
        liveScope.addSidechain (i, level);
    }
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

void EbbAudioProcessor::setCurve (const deepwater::dsp::BreakpointCurve& newCurve)
{
    curve = newCurve;
    curve.sanitise();
    curveBox.write (curve);
    ++curveVersion;

    // En el estado, en unidades de la curva y legible (el mismo formato que en los presets, ver EbbPresets.h).
    auto& state = parameters.state;
    if (const auto old = state.getChildWithName (ebb::presets::curveTag); old.isValid())
        state.removeChild (old, nullptr);
    state.appendChild (ebb::presets::curveToTree (curve), nullptr);
}

void EbbAudioProcessor::loadCurveFromState()
{
    // Un proyecto de la Fase 2 no tiene curva: queda la forma por defecto (la misma que sonaba entonces). Un estado
    // dañado (menos de dos puntos) también vuelve a ella; el resto lo sanea la curva.
    setCurve (ebb::presets::curveFromTree (parameters.state.getChildWithName (ebb::presets::curveTag), nullptr));
}

void EbbAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Se ejecuta en el hilo de mensajes (al guardar el proyecto de FL), no en el de audio.
    auto state = parameters.copyState();
    presetManager.writeStateProperties (state); // nombre del preset y si estaba modificado
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void EbbAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
            loadCurveFromState();

            // Lección de Undertow (Fase 11, pluginval --randomise): un parámetro on/off puede tener un valor "crudo"
            // intermedio que puso el host (0.34 = Off). Si el estado restaurado dice lo mismo, replaceState no lo toca
            // y el host sigue viendo 0.34. Se lleva cada parámetro discreto a su valor exacto.
            for (auto* parameter : juce::AudioProcessor::getParameters())
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter); ranged != nullptr && ranged->isDiscrete())
                    if (const float exact = ranged->convertTo0to1 (ranged->convertFrom0to1 (ranged->getValue())); exact != ranged->getValue())
                        ranged->setValueNotifyingHost (exact);

            presetManager.stateRestored(); // después de la curva: también es parte del preset
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
