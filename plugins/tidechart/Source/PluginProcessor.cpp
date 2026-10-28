#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

namespace
{
// Bytes reservados para el MIDI de salida de un bloque (un mensaje de 3 bytes ocupa 9). Si un bloque trajera más, el
// MidiBuffer crecería una vez (reservando memoria) y desde ahí conserva esa capacidad.
constexpr int reservedMidiBytes = 16384;
} // namespace

TidechartAudioProcessor::TidechartAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "TidechartState", createParameterLayout())
{
    bypassParam = parameters.getRawParameterValue (tidechart::params::bypass);
    bypassParameter = dynamic_cast<juce::AudioParameterBool*> (parameters.getParameter (tidechart::params::bypass));
    jassert (bypassParameter != nullptr);
    outputMidi.ensureSize (reservedMidiBytes);
}

juce::AudioProcessorValueTreeState::ParameterLayout TidechartAudioProcessor::createParameterLayout()
{
    namespace id = tidechart::params;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::bypass, id::versionHintPhase1 }, "Bypass", false));
    return layout;
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

// Fase 1: bypass y activo hacen lo mismo (todo pasa tal cual). Desde la Fase 3, en bypass las notas de entrada pasarán
// sin transformar y las generadas que suenen recibirán su note-off.
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

    stage.beginBlock (sink);
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
    transportBox.write (snapshot);
}

void TidechartAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (const auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void TidechartAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
    // Cargar un proyecto cambia lo que el plugin hace con las notas (desde la Fase 3): lo que suena se apaga.
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
