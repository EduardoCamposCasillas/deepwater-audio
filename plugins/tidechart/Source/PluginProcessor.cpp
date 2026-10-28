#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

#include "deepwater/theory/Text.h"

namespace
{
constexpr const char* progressionTag = "Progression";

// Bytes reservados para el MIDI de salida de un bloque (un mensaje de 3 bytes ocupa 9). Si un bloque trajera más, el
// MidiBuffer crecería una vez (reservando memoria) y desde ahí conserva esa capacidad.
constexpr int reservedMidiBytes = 16384;
} // namespace

TidechartAudioProcessor::TidechartAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "TidechartState", createParameterLayout())
{
    namespace id = tidechart::params;
    bypassParam = parameters.getRawParameterValue (id::bypass);
    keyTonicParam = parameters.getRawParameterValue (id::keyTonic);
    keyModeParam = parameters.getRawParameterValue (id::keyMode);
    chordSizeParam = parameters.getRawParameterValue (id::chordSize);
    keyMappingParam = parameters.getRawParameterValue (id::keyMapping);
    chordBassParam = parameters.getRawParameterValue (id::chordBass);
    playModeParam = parameters.getRawParameterValue (id::playMode);
    voicingParam = parameters.getRawParameterValue (id::voicing);
    rhythmParam = parameters.getRawParameterValue (id::rhythm);
    bypassParameter = dynamic_cast<juce::AudioParameterBool*> (parameters.getParameter (tidechart::params::bypass));
    jassert (bypassParameter != nullptr);
    outputMidi.ensureSize (reservedMidiBytes);
    setProgression (defaultProgression());
}

deepwater::theory::Progression TidechartAudioProcessor::defaultProgression()
{
    namespace th = deepwater::theory;
    th::Progression result;
    for (const auto& numeral : { th::RomanNumeral { 1, 0, th::chordType ("m") }, th::RomanNumeral { 6, -1, th::chordType ("") },
                                 th::RomanNumeral { 3, -1, th::chordType ("") }, th::RomanNumeral { 7, -1, th::chordType ("") } })
        result.steps.push_back ({ numeral, 4 });
    return result;
}

deepwater::theory::Progression TidechartAudioProcessor::getProgression() const
{
    const juce::ScopedLock lock (progressionLock);
    return progression;
}

void TidechartAudioProcessor::setProgression (const deepwater::theory::Progression& newProgression)
{
    {
        const juce::ScopedLock lock (progressionLock);
        progression = newProgression;
        for (auto& step : progression.steps)
            step.beats = deepwater::theory::Progression::clampBeats (step.beats);
        progressionBox.write (progression);
    }
    progressionChanges.fetch_add (1, std::memory_order_acq_rel);
}

juce::AudioProcessorValueTreeState::ParameterLayout TidechartAudioProcessor::createParameterLayout()
{
    namespace id = tidechart::params;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::bypass, id::versionHintPhase1 }, "Bypass", false));

    // Fase 3. Los nombres van en ASCII (los muestra el host); la GUI usa los suyos con ♯ y ♭. El orden de las listas
    // es parte del formato del proyecto: no reordenar.
    const juce::StringArray tonics { "C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B" };
    const juce::StringArray modes { "Major (ionian)", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Minor (aeolian)", "Locrian",
                                    "Harmonic minor", "Melodic minor", "Phrygian dominant" };
    jassert (modes.size() == deepwater::theory::modeCount);
    // La menor por defecto: el punto de partida del reggaetón.
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::keyTonic, id::versionHintPhase3 }, "Key", tonics, 9));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::keyMode, id::versionHintPhase3 }, "Mode", modes,
                                                              static_cast<int> (deepwater::theory::Mode::aeolian)));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::chordSize, id::versionHintPhase3 }, "Chords",
                                                              juce::StringArray { "Triads", "Sevenths" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::keyMapping, id::versionHintPhase3 }, "Keys",
                                                              juce::StringArray { "White keys = degrees", "Scale notes" }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::chordBass, id::versionHintPhase3 }, "Bass", false));

    // Fase 4. Por defecto, lo de la Fase 3: cada tecla su acorde, en estado fundamental y sostenido.
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::playMode, id::versionHintPhase4 }, "Play",
                                                              juce::StringArray { "Keys: one chord each", "Progression (synced)" }, 0));
    const juce::StringArray voicings { "Root position", "Voice leading", "Open (drop 2)" };
    jassert (voicings.size() == deepwater::theory::voicingStyleCount);
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::voicing, id::versionHintPhase4 }, "Voicing", voicings, 0));
    const juce::StringArray rhythms { "Sustain", "Quarters", "Eighths", "Offbeat", "Dembow", "Dembow long", "Dembow offbeat",
                                      "Dembow shifted", "Dembow gaps" };
    jassert (rhythms.size() == deepwater::theory::rhythmPatternCount);
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::rhythm, id::versionHintPhase4 }, "Rhythm", rhythms, 0));
    return layout;
}

tidechart::midi::ChordSettings TidechartAudioProcessor::currentChordSettings (bool bypassed) const noexcept
{
    namespace th = deepwater::theory;
    const auto choice = [] (const std::atomic<float>* p, int count) {
        return juce::jlimit (0, count - 1, juce::roundToInt (p->load (std::memory_order_relaxed)));
    };
    tidechart::midi::ChordSettings settings;
    settings.enabled = ! bypassed;
    // standardKey escribe la tónica con menos alteraciones PARA ESE MODO: la tecla 10 es Si♭ en mayor (2 ♭; La♯ mayor
    // tendría dobles sostenidos) y en menor (5 ♭ contra 7 ♯). Si empatan (Mi♭ / Re♯ menor, 6 y 6), sostenidos, como FL.
    settings.key = th::standardKey (choice (keyTonicParam, 12), static_cast<th::Mode> (choice (keyModeParam, th::modeCount)));
    settings.size = choice (chordSizeParam, 2) == 0 ? th::ChordSize::triad : th::ChordSize::seventh;
    settings.mapping = choice (keyMappingParam, 2) == 0 ? tidechart::midi::KeyMapping::whiteKeys : tidechart::midi::KeyMapping::scaleNotes;
    settings.bass = chordBassParam->load (std::memory_order_relaxed) >= 0.5f;
    settings.playMode = choice (playModeParam, 2) == 0 ? tidechart::midi::PlayMode::keys : tidechart::midi::PlayMode::progression;
    settings.voicing = static_cast<th::VoicingStyle> (choice (voicingParam, th::voicingStyleCount));
    settings.rhythm = choice (rhythmParam, th::rhythmPatternCount);
    return settings;
}

void TidechartAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    clock.prepare (currentSampleRate);
    outputMidi.ensureSize (reservedMidiBytes);
    snapshot.sampleRate = currentSampleRate;
    snapshot.blockSize = samplesPerBlock;
    // El audio se reinició: si algo sonaba, su note-off pudo perderse en el corte. Se apaga en el primer bloque.
    stage.requestRelease();
}

void TidechartAudioProcessor::releaseResources()
{
    stage.requestRelease();
}

void TidechartAudioProcessor::reset()
{
    stage.requestRelease();
}

bool TidechartAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Ninguna entrada de audio y una salida mono o estéreo (siempre en silencio).
    for (const auto& input : layouts.inputBuses)
        if (! input.isDisabled())
            return false;
    const auto& output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo();
}

void TidechartAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    processMidi (buffer, midi, bypassParam->load (std::memory_order_relaxed) >= 0.5f);
}

// Algunos hosts no usan el parámetro de bypass y llaman a esto directamente: el MIDI pasa igual (por la misma etapa,
// así lo que suena sigue contado).
void TidechartAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    processMidi (buffer, midi, true);
}

// En bypass el MIDI pasa tal cual; activo, cada tecla dispara un acorde. Al cambiar de uno a otro, la etapa apaga lo que
// sonaba del modo anterior en la muestra 0.
void TidechartAudioProcessor::processMidi (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, bool bypassed) noexcept
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear(); // no genera audio

    clock.beginBlock (readHostTransport(), numSamples);

    outputMidi.clear(); // conserva la capacidad (no libera memoria)
    auto sink = [&] (const std::uint8_t* data, int size, int sample) {
        outputMidi.addEvent (data, size, sample);
        const auto message = tidechart::midi::readShortMessage (data, size);
        if (message.isNoteOn())
            notesOutCount.fetch_add (1, std::memory_order_relaxed);
        midiLog.push ({ message, static_cast<std::uint16_t> (size), true, clock.ppqAt (sample) });
    };

    // La progresión publicada por la GUI (read copia la última aunque no haya nada nuevo: son pocos bytes).
    progressionBox.read (audioProgression);
    stage.setProgression (audioProgression);
    stage.setChordSettings (currentChordSettings (bypassed));
    stage.beginBlock (sink, &clock);
    const int lastSample = juce::jmax (0, numSamples - 1);
    for (const auto metadata : midi)
    {
        const int sample = juce::jlimit (0, lastSample, metadata.samplePosition);
        const auto message = tidechart::midi::readShortMessage (metadata.data, metadata.numBytes);
        if (message.isNoteOn())
            notesInCount.fetch_add (1, std::memory_order_relaxed);
        midiLog.push ({ message, static_cast<std::uint16_t> (metadata.numBytes), false, clock.ppqAt (sample) });
        stage.process (metadata.data, metadata.numBytes, sample, sink);
    }
    // Los cambios de acorde y los golpes del ritmo que caen después del último evento.
    stage.endBlock (numSamples, sink);

    // La salida se copia al buffer del host. clear() conserva su capacidad, y el host lo reserva para lo que trae (el
    // wrapper VST3 de JUCE, 2048 bytes): mientras salga lo mismo que entra no reserva memoria. (Intercambiarlos con
    // swapWith no copia, pero el buffer reservado aquí pasaría a ser del host y el nuestro sería el suyo, más chico.)
    midi.clear();
    midi.addEvents (outputMidi, 0, -1, 0);
    publishTransport (numSamples, bypassed);
}

deepwater::dsp::TransportInfo TidechartAudioProcessor::readHostTransport() noexcept
{
    // getPosition() está pensado para llamarse desde processBlock: no reserva memoria ni bloquea. Cada dato es
    // opcional; el Standalone no tiene PlayHead y el reloj corre libre a 120 BPM.
    deepwater::dsp::TransportInfo info;
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
                info.timeSignature = deepwater::dsp::TimeSignature { signature->numerator, signature->denominator };
        }
    return info;
}

void TidechartAudioProcessor::publishTransport (int numSamples, bool bypassed) noexcept
{
    snapshot.ppq = clock.ppqAt (0);
    snapshot.bpm = clock.bpm();
    snapshot.timeSignature = clock.timeSignature();
    const auto barStart = clock.lastBarStart();
    snapshot.hasLastBarStart = barStart.has_value();
    snapshot.lastBarStart = barStart.value_or (0.0);
    snapshot.source = clock.getSource();
    snapshot.looping = clock.isLooping();
    snapshot.jumps = clock.jumpCount();
    snapshot.sampleRate = currentSampleRate;
    snapshot.blockSize = numSamples;

    const auto& sounding = stage.getSounding();
    std::fill (std::begin (snapshot.soundingChannels), std::end (snapshot.soundingChannels), std::uint16_t {});
    sounding.forEachSounding ([this] (int channel, int note, int) {
        snapshot.soundingChannels[note] = static_cast<std::uint16_t> (snapshot.soundingChannels[note] | (1u << channel));
    });
    snapshot.soundingCount = sounding.soundingCount();
    snapshot.releases = stage.releaseCount();
    snapshot.bypassed = bypassed;
    snapshot.heldDegrees = stage.getChords().heldDegrees();
    snapshot.progressionStep = static_cast<std::int8_t> (stage.progressionStep());
    snapshot.progressionSounding = stage.progressionSounding();
    snapshot.rhythmStep = static_cast<std::int8_t> (stage.rhythmStep());
    transportBox.write (snapshot);
}

// La progresión va en el estado como un elemento propio, legible: <Progression><Step chord="bVI" beats="4"/>...</Progression>.
// El grado en texto romano (ida y vuelta probada en libs/theory) y no como índices: así un preset o el servidor MCP la
// pueden escribir a mano.
void TidechartAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (const auto xml = parameters.copyState().createXml())
    {
        auto* element = xml->createNewChildElement (progressionTag);
        for (const auto& step : getProgression().steps)
        {
            auto* stepElement = element->createNewChildElement ("Step");
            stepElement->setAttribute ("chord", juce::String (deepwater::theory::romanNumeralText (step.numeral)));
            stepElement->setAttribute ("beats", step.beats);
        }
        copyXmlToBinary (*xml, destData);
    }
}

void TidechartAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            // Un proyecto de antes de la Fase 4 no trae progresión: la de fábrica.
            auto loaded = defaultProgression();
            if (auto* element = xml->getChildByName (progressionTag))
            {
                loaded.steps.clear();
                for (auto* stepElement : element->getChildWithTagNameIterator ("Step"))
                    if (const auto numeral = deepwater::theory::parseRomanNumeral (stepElement->getStringAttribute ("chord").toStdString()))
                        loaded.steps.push_back ({ *numeral, stepElement->getIntAttribute ("beats", 4) });
                xml->removeChildElement (element, true); // no es un parámetro: fuera del árbol del APVTS
            }
            setProgression (loaded);
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
        }
    // Cargar un proyecto cambia lo que el plugin hace con las notas (otra tonalidad, otros acordes): lo que suena se apaga.
    stage.requestRelease();
}

juce::AudioProcessorEditor* TidechartAudioProcessor::createEditor()
{
    return new TidechartAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TidechartAudioProcessor(); // el wrapper del formato (VST3/Standalone) toma la propiedad
}
