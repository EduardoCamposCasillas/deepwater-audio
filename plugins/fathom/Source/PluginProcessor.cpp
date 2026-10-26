#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"
#include "gui/Format.h"

namespace
{
juce::String gainToText (float db, int /*maximumLength*/) { return fathom::gui::format::db (db); }
juce::String hzToText (float hz, int /*maximumLength*/) { return fathom::gui::format::hz (hz); }
juce::String qToText (float q, int /*maximumLength*/) { return juce::String (q, q < 10.0f ? 2 : 1); }
juce::String percentToText (float percent, int /*maximumLength*/) { return juce::String (juce::roundToInt (percent)) + " %"; }

// "0.5 ms", "10 ms", "1.50 s".
juce::String msToText (float ms, int /*maximumLength*/)
{
    if (ms < 9.95f)
        return juce::String (ms, 1) + " ms";
    if (ms < 999.5f)
        return juce::String (juce::roundToInt (ms)) + " ms";
    return juce::String (ms / 1000.0f, 2) + " s";
}

// "1.5 s" = 1500 ms; "10" o "10 ms" = 10 ms.
float textToMs (const juce::String& text)
{
    const float value = text.getFloatValue();
    const auto trimmed = text.trim();
    return trimmed.endsWithIgnoreCase ("s") && ! trimmed.endsWithIgnoreCase ("ms") ? value * 1000.0f : value;
}

// Balance: "C" en el centro, "L 30" / "R 30" a los lados (como el paneo del mixer de FL).
juce::String panToText (float pan, int /*maximumLength*/)
{
    const int amount = juce::roundToInt (pan);
    return amount == 0 ? juce::String ("C") : (amount < 0 ? "L " : "R ") + juce::String (std::abs (amount));
}

float textToPan (const juce::String& text)
{
    const auto trimmed = text.trim();
    const float amount = std::abs (trimmed.retainCharacters ("0123456789.").getFloatValue());
    if (trimmed.startsWithIgnoreCase ("L"))
        return -amount;
    if (trimmed.startsWithIgnoreCase ("R"))
        return amount;
    return trimmed.getFloatValue(); // "C" = 0; un número con signo, tal cual
}

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
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          // Fase 5: otra pista para comparar en el analizador (más adelante, el detector del EQ
                          // dinámico y la igualación). Solo se lee: no cambia el audio.
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "FathomState", createParameterLayout())
{
    namespace id = fathom::params;
    bypassParam = parameters.getRawParameterValue (id::bypass);
    outputGainParam = parameters.getRawParameterValue (id::outputGain);
    gainScaleParam = parameters.getRawParameterValue (id::gainScale);
    outputPanParam = parameters.getRawParameterValue (id::outputPan);
    phaseInvertParam = parameters.getRawParameterValue (id::phaseInvert);
    autoGainParam = parameters.getRawParameterValue (id::autoGain);
    analogResponseParam = parameters.getRawParameterValue (id::analogResponse);
    phaseModeParam = parameters.getRawParameterValue (id::phaseMode);
    linearQualityParam = parameters.getRawParameterValue (id::linearQuality);
    for (int b = 0; b < id::numBands; ++b)
    {
        auto& band = bandParams[static_cast<size_t> (b)];
        band.enabled = parameters.getRawParameterValue (id::band (b, id::enabled));
        band.frequency = parameters.getRawParameterValue (id::band (b, id::frequency));
        band.gain = parameters.getRawParameterValue (id::band (b, id::gain));
        band.q = parameters.getRawParameterValue (id::band (b, id::q));
        band.shape = parameters.getRawParameterValue (id::band (b, id::shape));
        band.slope = parameters.getRawParameterValue (id::band (b, id::slope));
        band.stereo = parameters.getRawParameterValue (id::band (b, id::stereo));
        band.dynamic = parameters.getRawParameterValue (id::band (b, id::dynamic));
        band.dynamicRange = parameters.getRawParameterValue (id::band (b, id::dynamicRange));
        band.threshold = parameters.getRawParameterValue (id::band (b, id::threshold));
        band.attack = parameters.getRawParameterValue (id::band (b, id::attack));
        band.release = parameters.getRawParameterValue (id::band (b, id::release));
        band.autoThreshold = parameters.getRawParameterValue (id::band (b, id::autoThreshold));
        band.dynamicSource = parameters.getRawParameterValue (id::band (b, id::dynamicSource));
    }
    bypassParameter = dynamic_cast<juce::AudioParameterBool*> (parameters.getParameter (id::bypass));
    jassert (bypassParameter != nullptr);

    // Los valores por defecto, leídos de los propios parámetros (convertidos igual que cualquier valor). Los de la
    // dinámica (Fase 9) son los de DynamicSettings: el layout los toma de ahí.
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
                                                  juce::roundToInt (defaultOf (b, id::slope)),
                                                  true,
                                                  static_cast<fathom::dsp::Placement> (juce::roundToInt (defaultOf (b, id::stereo))) };

    std::vector<juce::RangedAudioParameter*> undoable;
    for (auto* parameter : juce::AudioProcessor::getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter); ranged != nullptr && ranged != bypassParameter)
            undoable.push_back (ranged);
    history.attach (std::move (undoable));

    // Fase 10: cambiar de modo de fase cambia la latencia; el host se entera desde el hilo de mensajes (no desde el
    // audio: avisarle es una llamada al sistema).
    startTimerHz (10);
}

FathomAudioProcessor::~FathomAudioProcessor()
{
    stopTimer();
}

fathom::dsp::PhaseMode FathomAudioProcessor::getPhaseMode() const noexcept
{
    return static_cast<fathom::dsp::PhaseMode> (juce::jlimit (0, fathom::dsp::numPhaseModes - 1, juce::roundToInt (phaseModeParam->load())));
}

int FathomAudioProcessor::getLinearQuality() const noexcept
{
    return juce::jlimit (0, fathom::dsp::numLinearQualities - 1, juce::roundToInt (linearQualityParam->load()));
}

double FathomAudioProcessor::getTailLengthSeconds() const
{
    // Después de la latencia todavía queda la segunda mitad del FIR (la cola del filtro).
    const double sampleRate = preparedSampleRate.load (std::memory_order_relaxed);
    switch (getPhaseMode())
    {
        case fathom::dsp::PhaseMode::linear: return fathom::dsp::PhaseSizes::linearLength (getLinearQuality(), sampleRate) / sampleRate;
        case fathom::dsp::PhaseMode::natural: return fathom::dsp::PhaseSizes::naturalLength (sampleRate) / sampleRate;
        case fathom::dsp::PhaseMode::zeroLatency: break;
    }
    return 0.0;
}

void FathomAudioProcessor::timerCallback()
{
    engine.providePhaseMemory(); // si se eligió fase natural o lineal por primera vez, su memoria (fuera del audio)
    if (const int latency = engineLatency.load (std::memory_order_relaxed); latency != getLatencySamples())
        setLatencySamples (latency);
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

    // Fase 6. Escala: 0 % = plano, 100 % = las bandas tal cual, 200 % = el doble de dB. Balance: −100 a +100.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::gainScale, id::versionHintPhase6 }, "Gain Scale",
        juce::NormalisableRange<float> (0.0f, 100.0f * Engine::maxGainScale, 1.0f), 100.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("%")
            .withStringFromValueFunction (percentToText)
            .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); })));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::outputPan, id::versionHintPhase6 }, "Output Pan", juce::NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (panToText).withValueFromStringFunction (textToPan)));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::phaseInvert, id::versionHintPhase6 }, "Phase Invert", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::autoGain, id::versionHintPhase6 }, "Auto Gain", false));
    // Fase 7. Encendida por defecto: un proyecto de las Fases 1-6 (que no la tiene) pasa a sonar con la respuesta
    // analógica; la diferencia está en las bandas agudas (con 44.1/48 kHz, sobre todo encima de ~8 kHz).
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::analogResponse, id::versionHintPhase7 },
                                                            "Analog Response", true));
    // Fase 10. Cero latencia por defecto: un proyecto de las Fases 1-9 suena igual. La calidad solo cuenta en fase
    // lineal (media: 4096 muestras a 44.1/48 kHz, 58 ms de latencia).
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::phaseMode, id::versionHintPhase10 }, "Phase Mode",
                                                              juce::StringArray { "Zero Latency", "Natural Phase", "Linear Phase" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::linearQuality, id::versionHintPhase10 },
                                                              "Linear Quality",
                                                              juce::StringArray { "Low", "Medium", "High", "Very High", "Maximum" },
                                                              fathom::dsp::defaultLinearQuality));

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
    // El orden de fathom::dsp::Placement.
    const juce::StringArray placements { "Stereo", "Left", "Right", "Mid", "Side" };
    jassert (placements.size() == fathom::dsp::numPlacements);

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
    // Fase 8, después de todo lo anterior (el orden de los parámetros es el que ve el host). Estéreo por defecto: un
    // proyecto de las Fases 1-7 suena igual.
    for (int b = 0; b < id::numBands; ++b)
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::band (b, id::stereo), id::versionHintPhase8 },
                                                                  "Band " + juce::String (b + 1) + " Stereo", placements, 0));

    // Fase 9: la dinámica de cada banda, apagada (un proyecto de las Fases 1-8 suena igual). Ataque y liberación en
    // escala log (de 0.5 a 5 ms recorre lo mismo que de 30 a 300).
    using Dynamics = fathom::dsp::DynamicSettings;
    const Dynamics dynamics;
    const auto dbAttributes = juce::AudioParameterFloatAttributes()
                                  .withLabel ("dB")
                                  .withStringFromValueFunction (gainToText)
                                  .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); });
    const auto msAttributes =
        juce::AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (msToText).withValueFromStringFunction (textToMs);
    for (int b = 0; b < id::numBands; ++b)
    {
        const auto name = "Band " + juce::String (b + 1) + " ";
        const auto parameterId = [b] (const char* setting) { return juce::ParameterID { id::band (b, setting), id::versionHintPhase9 }; };
        layout.add (std::make_unique<juce::AudioParameterBool> (parameterId (id::dynamic), name + "Dynamic", dynamics.enabled));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            parameterId (id::dynamicRange), name + "Dyn Range", juce::NormalisableRange<float> (Dynamics::minRangeDb, Dynamics::maxRangeDb, 0.01f),
            dynamics.rangeDb, dbAttributes));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            parameterId (id::threshold), name + "Threshold",
            juce::NormalisableRange<float> (Dynamics::minThresholdDb, Dynamics::maxThresholdDb, 0.1f), dynamics.thresholdDb, dbAttributes));
        layout.add (std::make_unique<juce::AudioParameterFloat> (parameterId (id::attack), name + "Attack",
                                                                 logRange (Dynamics::minAttackMs, Dynamics::maxAttackMs), dynamics.attackMs,
                                                                 msAttributes));
        layout.add (std::make_unique<juce::AudioParameterFloat> (parameterId (id::release), name + "Release",
                                                                 logRange (Dynamics::minReleaseMs, Dynamics::maxReleaseMs),
                                                                 dynamics.releaseMs, msAttributes));
        layout.add (std::make_unique<juce::AudioParameterBool> (parameterId (id::autoThreshold), name + "Auto Threshold", dynamics.autoThreshold));
        layout.add (std::make_unique<juce::AudioParameterChoice> (parameterId (id::dynamicSource), name + "Dyn Source",
                                                                  juce::StringArray { "Band", "Sidechain" }, dynamics.external ? 1 : 0));
    }
    return layout;
}

fathom::dsp::BandSettings FathomAudioProcessor::getBandSettings (int band) const noexcept
{
    const auto& p = bandParams[static_cast<size_t> (band)];
    fathom::dsp::BandSettings settings { p.enabled->load() >= 0.5f,
                                         p.frequency->load(),
                                         p.gain->load(),
                                         p.q->load(),
                                         static_cast<fathom::dsp::Shape> (juce::jlimit (0, fathom::dsp::numShapes - 1, juce::roundToInt (p.shape->load()))),
                                         juce::jlimit (0, static_cast<int> (fathom::dsp::slopes.size()) - 1, juce::roundToInt (p.slope->load())),
                                         // Con fase natural, siempre la analógica (el FIR corrige desde ahí).
                                         analogResponseParam->load() >= 0.5f || getPhaseMode() == fathom::dsp::PhaseMode::natural,
                                         static_cast<fathom::dsp::Placement> (
                                             juce::jlimit (0, fathom::dsp::numPlacements - 1, juce::roundToInt (p.stereo->load()))) };
    auto& d = settings.dynamics;
    d.enabled = p.dynamic->load() >= 0.5f;
    d.rangeDb = p.dynamicRange->load();
    d.thresholdDb = p.threshold->load();
    d.attackMs = p.attack->load();
    d.releaseMs = p.release->load();
    d.autoThreshold = p.autoThreshold->load() >= 0.5f;
    d.external = p.dynamicSource->load() >= 0.5f;
    return settings;
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
    // Sin buffers propios: el motor procesa en el sitio y las tomas del analizador son de tamaño fijo. El motor sí
    // reserva aquí lo de los modos de fase (y arranca su hilo de diseño si no estamos renderizando offline).
    setOutputSettings();
    engine.setRealtime (! isNonRealtime());
    engine.prepare (sampleRate, bypassParam->load() >= 0.5f, outputGainParam->load(), getAllBandSettings());
    analyzerSampleRate.store (sampleRate);
    preparedSampleRate.store (sampleRate);
    engineLatency.store (engine.getLatencySamples());
    setLatencySamples (engine.getLatencySamples());
}

void FathomAudioProcessor::releaseResources() {}

bool FathomAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

bool FathomAudioProcessor::isSidechainConnected() const
{
    const auto* bus = getBus (true, 1);
    return bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() > 0;
}

void FathomAudioProcessor::setOutputSettings() noexcept
{
    engine.setGainScale (gainScaleParam->load() * 0.01f);
    engine.setPan (outputPanParam->load() * 0.01f);
    engine.setPhaseInverted (phaseInvertParam->load() >= 0.5f);
    engine.setAutoGain (autoGainParam->load() >= 0.5f);
    engine.setPhaseMode (getPhaseMode(), getLinearQuality());
}

namespace
{
constexpr const char* analyzerViewTag = "AnalyzerView";
constexpr const char* compareTag = "Compare";  // hijo del estado: qué lado está activo y los valores del otro
constexpr const char* otherSideTag = "Other"; // hijo de Compare: un atributo por parámetro (ID = valor en sus unidades)
} // namespace

int FathomAudioProcessor::getCompareSide() const
{
    return parameters.state.getChildWithName (compareTag).getProperty ("side", 0);
}

std::vector<float> FathomAudioProcessor::otherSideValues() const
{
    // El otro lado todavía no existe (la primera vez): nace como copia del activo. Si existe pero le falta un parámetro
    // (guardado por una versión anterior), ese parámetro va a su valor por defecto, como en los presets.
    const auto other = parameters.state.getChildWithName (compareTag).getChildWithName (otherSideTag);
    if (! other.isValid())
        return history.snapshot();
    std::vector<float> values;
    for (const auto* parameter : history.getTrackedParameters())
    {
        const juce::Identifier id { parameter->paramID };
        values.push_back (other.hasProperty (id) ? parameter->convertTo0to1 (static_cast<float> (other.getProperty (id)))
                                                 : parameter->getDefaultValue());
    }
    return values;
}

void FathomAudioProcessor::storeOtherSide (const std::vector<float>& values)
{
    // En las unidades de cada parámetro (Hz, dB...), no normalizados: si un rango cambia en una versión futura, el
    // valor guardado sigue significando lo mismo.
    auto compare = parameters.state.getOrCreateChildWithName (compareTag, nullptr);
    compare.removeChild (compare.getChildWithName (otherSideTag), nullptr);
    juce::ValueTree other { otherSideTag };
    const auto& tracked = history.getTrackedParameters();
    for (size_t i = 0; i < tracked.size() && i < values.size(); ++i)
        other.setProperty (juce::Identifier { tracked[i]->paramID }, tracked[i]->convertFrom0to1 (values[i]), nullptr);
    compare.appendChild (other, nullptr);
}

bool FathomAudioProcessor::switchCompareSide()
{
    if (! history.isIdle())
        return false;
    const auto current = history.snapshot();
    const auto next = otherSideValues();
    storeOtherSide (current);
    parameters.state.getOrCreateChildWithName (compareTag, nullptr).setProperty ("side", 1 - getCompareSide(), nullptr);
    history.swapStack (otherSideHistory);
    history.load (next); // las bandas suavizan y funden: el cambio no hace clic
    return true;
}

void FathomAudioProcessor::copyToOtherSide()
{
    storeOtherSide (history.snapshot());
}

fathom::analysis::ViewSettings FathomAudioProcessor::getAnalyzerView() const
{
    // Lo que falte (un proyecto de las Fases 1-4) queda con su valor por defecto.
    const fathom::analysis::ViewSettings defaults;
    const auto tree = parameters.state.getChildWithName (analyzerViewTag);
    fathom::analysis::ViewSettings view;
    view.tiltIndex = tree.getProperty ("tilt", defaults.tiltIndex);
    view.rangeIndex = tree.getProperty ("range", defaults.rangeIndex);
    view.resolutionIndex = tree.getProperty ("resolution", defaults.resolutionIndex);
    view.speedIndex = tree.getProperty ("speed", defaults.speedIndex);
    view.holdIndex = tree.getProperty ("hold", defaults.holdIndex);
    view.channelsIndex = tree.getProperty ("channels", defaults.channelsIndex);
    view.showInput = tree.getProperty ("showInput", defaults.showInput);
    view.showOutput = tree.getProperty ("showOutput", defaults.showOutput);
    view.showSidechain = tree.getProperty ("showSidechain", defaults.showSidechain);
    return view;
}

void FathomAudioProcessor::setAnalyzerView (const fathom::analysis::ViewSettings& view)
{
    // Sin UndoManager: cambiar la vista no es una edición del sonido (no entra al historial de deshacer).
    auto tree = parameters.state.getOrCreateChildWithName (analyzerViewTag, nullptr);
    tree.setProperty ("tilt", view.tiltIndex, nullptr);
    tree.setProperty ("range", view.rangeIndex, nullptr);
    tree.setProperty ("resolution", view.resolutionIndex, nullptr);
    tree.setProperty ("speed", view.speedIndex, nullptr);
    tree.setProperty ("hold", view.holdIndex, nullptr);
    tree.setProperty ("channels", view.channelsIndex, nullptr);
    tree.setProperty ("showInput", view.showInput, nullptr);
    tree.setProperty ("showOutput", view.showOutput, nullptr);
    tree.setProperty ("showSidechain", view.showSidechain, nullptr);
}

void FathomAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Los canales del bus principal van primero en el buffer; los del sidechain, después (solo se leen).
    const int numSamples = buffer.getNumSamples();
    const int mainChannels = getMainBusNumInputChannels();
    const int numChannels = juce::jmin (buffer.getNumChannels(), mainChannels, fathom::dsp::Engine::maxChannels);
    // Canales de salida sin entrada (no debería pasar con los layouts aceptados): mejor silencio que basura.
    for (int ch = mainChannels; ch < juce::jmin (buffer.getNumChannels(), getMainBusNumOutputChannels()); ++ch)
        buffer.clear (ch, 0, numSamples);

    // El sidechain (Fase 5 para el analizador; Fase 9, también para las bandas dinámicas con el detector externo). La
    // vista del bus apunta a los canales del propio buffer: no reserva memoria.
    const auto* sidechainBus = getBus (true, 1);
    const auto sidechain = sidechainBus != nullptr && sidechainBus->isEnabled() ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float>();
    const int sidechainChannels = juce::jmin (sidechain.getNumChannels(), 2);

    // Para la GUI: la entrada tal cual llega (el motor procesa en el sitio, así que antes de procesar). Sin editor
    // abierto no hay quien la lea: no se copia.
    const bool feedAnalyzer = analyzerActive.load (std::memory_order_relaxed);
    if (feedAnalyzer)
    {
        pushTaps (inputTap, inputSideTap, buffer.getArrayOfReadPointers(), numChannels, numSamples);
        // Sin bus conectado no se escribe: la GUI ve que no llega audio y ese espectro cae a silencio.
        if (sidechainChannels > 0)
            pushTaps (sidechainTap, sidechainSideTap, sidechain.getArrayOfReadPointers(), sidechainChannels, numSamples);
    }

    engine.setBypassed (bypassParam->load() >= 0.5f);
    engine.setOutputDb (outputGainParam->load());
    engine.setRealtime (! isNonRealtime()); // renderizando offline, los FIR se diseñan aquí mismo (el render es siempre igual)
    setOutputSettings(); // antes de las bandas: la escala de ganancia se aplica al pasarlas
    for (int b = 0; b < fathom::dsp::Engine::numBands; ++b)
        engine.setBand (b, getBandSettings (b));
    engine.setSolo (soloBand.load (std::memory_order_relaxed));
    engine.process (buffer.getArrayOfWritePointers(), numChannels, numSamples,
                    sidechainChannels > 0 ? sidechain.getArrayOfReadPointers() : nullptr, sidechainChannels);
    autoGainDb.store (engine.getAutoGainDb(), std::memory_order_relaxed);
    engineLatency.store (engine.getLatencySamples(), std::memory_order_relaxed);
    for (int b = 0; b < fathom::dsp::Engine::numBands; ++b)
    {
        const auto& dynamics = engine.getDynamics (b);
        auto& live = dynamicStates[static_cast<size_t> (b)];
        live.gainDb.store (dynamics.getGainDb(), std::memory_order_relaxed);
        live.levelDb.store (dynamics.getLevelDb(), std::memory_order_relaxed);
        live.thresholdDb.store (dynamics.getThresholdDb(), std::memory_order_relaxed);
    }

    if (feedAnalyzer)
        pushTaps (outputTap, outputSideTap, buffer.getArrayOfReadPointers(), numChannels, numSamples);
}

void FathomAudioProcessor::pushTaps (AnalyzerTap& mid, AnalyzerTap& side, const float* const* channels, int numChannels, int numSamples) noexcept
{
    mid.push (channels, numChannels, numSamples); // (L + R) / 2: el mid
    if (numChannels >= 2)
        side.pushSide (channels[0], channels[1], numSamples);
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
