// Tests del procesador completo de Tidechart (con JUCE): exactamente lo que hace el plugin dentro de FL. MIDI al azar a
// 4 sample rates y con bloques de 1 a 4096 muestras; bypass, reinicios del audio, pánico y cargar el estado a mitad de
// una nota; un host que reproduce, para, salta y hace loop; acordes de una tecla con sus parámetros y la paleta;
// lo que se arrastra al Piano Roll (Fase 7) contra lo que suena.
// Comprueba que el MIDI sale como debe (en bypass, idéntico) y en su muestra, que la salida de audio es silencio, que
// ninguna nota queda colgada y que processBlock no reserva memoria.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include "deepwater/theory/KeyDetection.h"
#include "deepwater/theory/Progression.h"
#include "deepwater/theory/Text.h"
#include "MidiFileExport.h"
#include "Parameters.h"
#include "PluginProcessor.h"

// --- Reservas de memoria en el hilo de audio -------------------------------------------------------------------------
//
// Regla del proyecto: processBlock no puede reservar ni liberar memoria. Este programa REEMPLAZA operator new / delete
// (C++ lo permite una vez por ejecutable) por versiones que, mientras 'counting' está activo, cuentan cada llamada del
// hilo que mide (la misma técnica que los tests de presets de los otros plugins).
namespace allocation
{
thread_local bool counting = false;
std::atomic<int> count { 0 };

void note (void* p) noexcept
{
    if (p != nullptr && counting)
        count.fetch_add (1, std::memory_order_relaxed);
}

void* allocate (std::size_t size)
{
    note (reinterpret_cast<void*> (1));
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

void* allocateAligned (std::size_t size, std::align_val_t alignment)
{
    note (reinterpret_cast<void*> (1));
    if (void* p = _aligned_malloc (size == 0 ? 1 : size, static_cast<std::size_t> (alignment)))
        return p;
    throw std::bad_alloc();
}
} // namespace allocation

void* operator new (std::size_t size) { return allocation::allocate (size); }
void* operator new[] (std::size_t size) { return allocation::allocate (size); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept
{
    allocation::note (reinterpret_cast<void*> (1));
    return std::malloc (size == 0 ? 1 : size);
}
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept
{
    allocation::note (reinterpret_cast<void*> (1));
    return std::malloc (size == 0 ? 1 : size);
}
void* operator new (std::size_t size, std::align_val_t alignment) { return allocation::allocateAligned (size, alignment); }
void* operator new[] (std::size_t size, std::align_val_t alignment) { return allocation::allocateAligned (size, alignment); }
void operator delete (void* p) noexcept { allocation::note (p); std::free (p); }
void operator delete[] (void* p) noexcept { allocation::note (p); std::free (p); }
void operator delete (void* p, std::size_t) noexcept { allocation::note (p); std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { allocation::note (p); std::free (p); }
void operator delete (void* p, std::align_val_t) noexcept { allocation::note (p); _aligned_free (p); }
void operator delete[] (void* p, std::align_val_t) noexcept { allocation::note (p); _aligned_free (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept { allocation::note (p); _aligned_free (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { allocation::note (p); _aligned_free (p); }

namespace
{
int failures = 0;

void check (bool condition, const juce::String& message)
{
    if (! condition)
    {
        ++failures;
        std::printf ("    FALLO: %s\n", message.toRawUTF8());
    }
}

// Un evento MIDI en una muestra absoluta de la simulación.
struct Event
{
    juce::MidiMessage message;
    juce::int64 sample = 0;
};

bool sameEvent (const Event& a, const Event& b)
{
    return a.sample == b.sample && a.message.getRawDataSize() == b.message.getRawDataSize()
           && std::memcmp (a.message.getRawData(), b.message.getRawData(), static_cast<size_t> (a.message.getRawDataSize())) == 0;
}

// Un host de prueba: reproduce a tempo constante desde 'ppq', o está parado. El test mueve la posición entre bloques.
class TestHead final : public juce::AudioPlayHead
{
public:
    bool playing = true;
    bool looping = false;
    double bpm = 120.0;
    double ppq = 0.0;
    int numerator = 4, denominator = 4;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        info.setIsLooping (looping);
        info.setBpm (bpm);
        info.setTimeSignature (TimeSignature { numerator, denominator });
        info.setPpqPosition (ppq);
        const double bar = 4.0 * numerator / denominator;
        info.setPpqPositionOfLastBarStart (std::floor (ppq / bar) * bar);
        return info;
    }
};

// Corre un bloque como lo hace un host: un buffer MIDI que se reutiliza (reservado como el wrapper VST3 de JUCE) y un
// buffer de audio con basura (la salida tiene que quedar en silencio). Devuelve los eventos de salida en muestras
// absolutas y suma las reservas de memoria de processBlock.
struct Rig
{
    TidechartAudioProcessor& processor;
    juce::MidiBuffer hostMidi;
    juce::AudioBuffer<float> audio;
    juce::int64 position = 0;
    int allocations = 0;
    bool silent = true;

    Rig (TidechartAudioProcessor& p, int channels, int maxBlock) : processor (p), audio (channels, maxBlock)
    {
        hostMidi.ensureSize (2048);
    }

    std::vector<Event> run (const std::vector<Event>& input, int numSamples, bool bypassedCall = false)
    {
        hostMidi.clear();
        for (const auto& event : input)
            if (event.sample >= position && event.sample < position + numSamples)
                hostMidi.addEvent (event.message, static_cast<int> (event.sample - position));
        for (int c = 0; c < audio.getNumChannels(); ++c)
            for (int i = 0; i < numSamples; ++i)
                audio.setSample (c, i, 0.5f);

        juce::AudioBuffer<float> block (audio.getArrayOfWritePointers(), audio.getNumChannels(), numSamples);
        allocation::count.store (0);
        allocation::counting = true;
        if (bypassedCall)
            processor.processBlockBypassed (block, hostMidi);
        else
            processor.processBlock (block, hostMidi);
        allocation::counting = false;
        allocations += allocation::count.load();

        for (int c = 0; c < block.getNumChannels(); ++c)
            for (int i = 0; i < numSamples; ++i)
                silent = silent && block.getSample (c, i) == 0.0f;

        std::vector<Event> out;
        for (const auto metadata : hostMidi)
            out.push_back ({ metadata.getMessage(), position + metadata.samplePosition });
        position += numSamples;
        return out;
    }
};

std::unique_ptr<TidechartAudioProcessor> makeProcessor (double sampleRate, int maxBlock, int channels = 2)
{
    auto processor = std::make_unique<TidechartAudioProcessor>();
    auto layout = processor->getBusesLayout();
    layout.outputBuses.getReference (0) = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    check (processor->setBusesLayout (layout), "layout de salida " + juce::String (channels) + " canales");
    processor->setRateAndBufferSizeDetails (sampleRate, maxBlock);
    processor->prepareToPlay (sampleRate, maxBlock);
    return processor;
}

// Cambia un parámetro como lo haría el host (valor en sus unidades: índice de la lista, 0/1).
void setParameter (TidechartAudioProcessor& processor, const char* id, float value)
{
    auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (processor.getParameters().getParameter (id));
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

// Lo que tiene que salir con los acordes activos y los parámetros por defecto (La menor, tríadas, teclas blancas): la
// misma entrada por una etapa MIDI sin JUCE, evento por evento. El procesador, con bloques de cualquier tamaño, debe
// sacar exactamente esto.
std::vector<Event> expectedChords (const std::vector<Event>& input)
{
    tidechart::midi::MidiStage stage;
    tidechart::midi::ChordSettings settings;
    settings.enabled = true;
    stage.setChordSettings (settings);
    std::vector<Event> out;
    juce::int64 at = 0;
    auto sink = [&] (const std::uint8_t* data, int size, int) { out.push_back ({ juce::MidiMessage (data, size), at }); };
    stage.beginBlock (sink);
    for (const auto& event : input)
    {
        at = event.sample;
        stage.process (event.message.getRawData(), event.message.getRawDataSize(), 0, sink);
    }
    return out;
}

TransportSnapshot snapshotOf (TidechartAudioProcessor& processor)
{
    TransportSnapshot snapshot;
    processor.readTransport (snapshot);
    return snapshot;
}

// Notas al azar (con solapes, la misma tecla dos veces, velocidad 0), CC, pitch bend y SysEx, ordenadas en el tiempo,
// terminando con el note-off de cada note-on (como un Piano Roll bien formado, que al final no deja nada sonando).
std::vector<Event> randomStream (juce::Random& random, juce::int64 length)
{
    std::vector<Event> events;
    std::vector<std::pair<int, int>> held; // (canal, nota)
    for (juce::int64 sample = 0; sample < length - 1000; sample += 1 + random.nextInt (400))
    {
        const int channel = 1 + random.nextInt (2);
        const int note = 48 + random.nextInt (12);
        const int what = random.nextInt (10);
        if (what < 4)
        {
            events.push_back ({ juce::MidiMessage::noteOn (channel, note, static_cast<juce::uint8> (1 + random.nextInt (127))), sample });
            held.emplace_back (channel, note);
        }
        else if (what < 7 && ! held.empty())
        {
            const auto index = static_cast<size_t> (random.nextInt (static_cast<int> (held.size())));
            const auto [c, n] = held[index];
            held.erase (held.begin() + static_cast<std::ptrdiff_t> (index));
            // A veces como note-on con velocidad 0 (lo que manda mucho hardware).
            events.push_back ({ random.nextBool() ? juce::MidiMessage::noteOff (c, n) : juce::MidiMessage::noteOn (c, n, static_cast<juce::uint8> (0)), sample });
        }
        else if (what == 7)
            events.push_back ({ juce::MidiMessage::controllerEvent (channel, 1, random.nextInt (128)), sample });
        else if (what == 8)
            events.push_back ({ juce::MidiMessage::pitchWheel (channel, random.nextInt (16384)), sample });
        else
        {
            const juce::uint8 data[] { 0x7d, 0x01, 0x02 };
            events.push_back ({ juce::MidiMessage::createSysExMessage (data, 3), sample });
        }
    }
    for (const auto& [c, n] : held)
        events.push_back ({ juce::MidiMessage::noteOff (c, n), length - 10 });
    return events;
}

void testPassThrough()
{
    std::printf ("MIDI de salida: en bypass identico, con acordes el de la etapa de referencia, en su muestra; audio en "
                 "silencio; sin reservas (4 sample rates, bloques 1-4096, mono y estereo)\n");

    const double sampleRates[] { 44100.0, 48000.0, 88200.0, 96000.0 };
    const int blockSizes[] { 1, 7, 64, 256, 512, 1024, 4096, 0 }; // 0 = tamaño variable de bloque a bloque
    juce::Random random (4242);
    for (const bool chords : { false, true })
        for (const double sampleRate : sampleRates)
            for (const int blockSize : blockSizes)
                for (const int channels : { 1, 2 })
                {
                    const int maxBlock = blockSize == 0 ? 4096 : blockSize;
                    auto processor = makeProcessor (sampleRate, maxBlock, channels);
                    if (! chords)
                        setParameter (*processor, tidechart::params::bypass, 1.0f);
                    Rig rig (*processor, channels, maxBlock);
                    const juce::int64 length = static_cast<juce::int64> (sampleRate * 3.0);
                    const auto input = randomStream (random, length);
                    const auto expected = chords ? expectedChords (input) : input;

                    std::vector<Event> output;
                    while (rig.position < length)
                    {
                        const int n = static_cast<int> (std::min<juce::int64> (blockSize == 0 ? 1 + random.nextInt (4096) : blockSize,
                                                                              length - rig.position));
                        for (auto& e : rig.run (input, n))
                            output.push_back (std::move (e));
                    }

                    const auto tag = juce::String (chords ? "acordes, " : "bypass, ") + juce::String (sampleRate / 1000.0, 1) + " kHz, bloque "
                                     + (blockSize == 0 ? juce::String ("variable") : juce::String (blockSize)) + ", " + juce::String (channels) + " canales";
                    bool identical = output.size() == expected.size();
                    for (size_t i = 0; identical && i < expected.size(); ++i)
                        identical = sameEvent (expected[i], output[i]);
                    check (identical, tag + ": el MIDI no salio como se esperaba (" + juce::String (static_cast<int> (output.size())) + " de "
                                          + juce::String (static_cast<int> (expected.size())) + ")");
                    check (rig.silent, tag + ": la salida de audio no es silencio");
                    check (rig.allocations == 0, tag + ": processBlock reservo memoria " + juce::String (rig.allocations) + " veces");
                    check (snapshotOf (*processor).soundingCount == 0, tag + ": quedaron notas sonando");
                    check (snapshotOf (*processor).heldDegrees == 0, tag + ": la paleta quedo encendida");
                }
}

// Algo suena; pasa 'interrupt' (algo que corta el flujo normal); el bloque siguiente tiene que empezar con los note-off
// en la muestra 0, y después el note-off del host no deja la cuenta mal. En bypass las teclas suenan tal cual (la
// repetida necesita dos note-off) y el note-off tardío pasa; con acordes, C5 = Am en el canal 3 (tocado dos veces: el
// acorde se repite, no se duplica) y G5 = Em en el canal 1: 6 notas, y el note-off tardío ya no tiene acorde que soltar.
void expectReleaseAfter (const juce::String& name, const std::function<void (TidechartAudioProcessor&)>& interrupt, bool panic = false)
{
    for (const bool chords : { false, true })
    {
        auto processor = makeProcessor (48000.0, 512);
        if (! chords)
            setParameter (*processor, tidechart::params::bypass, 1.0f);
        Rig rig (*processor, 2, 512);
        const juce::String tag = name + (chords ? " (acordes)" : " (bypass)");
        std::vector<Event> input { { juce::MidiMessage::noteOn (3, 60, static_cast<juce::uint8> (100)), 100 },
                                   { juce::MidiMessage::noteOn (3, 60, static_cast<juce::uint8> (90)), 200 }, // la misma tecla otra vez
                                   { juce::MidiMessage::noteOn (1, 67, static_cast<juce::uint8> (80)), 300 } };
        rig.run (input, 512);
        check (snapshotOf (*processor).soundingCount == (chords ? 6 : 2), tag + ": antes, lo que suena");

        interrupt (*processor);
        const auto out = rig.run ({}, 512);
        int offs = 0, allNotesOff = 0;
        for (const auto& e : out)
        {
            check (e.sample == 512, tag + ": el note-off no cayo en la muestra 0 del bloque");
            offs += e.message.isNoteOff() ? 1 : 0;
            allNotesOff += e.message.isAllNotesOff() ? 1 : 0;
        }
        check (offs == (chords ? 6 : 3), tag + ": note-off que salieron: " + juce::String (offs));
        check (allNotesOff == (panic ? 16 : 0), tag + ": All Notes Off: " + juce::String (allNotesOff));
        const auto after = snapshotOf (*processor);
        check (after.soundingCount == 0 && after.heldDegrees == 0, tag + ": quedaron notas sonando");
        check (after.releases >= 1, tag + ": no se conto el apagado");

        // El note-off del host llega después: en bypass pasa tal cual (el instrumento ignora un note-off de más); con
        // acordes no sale nada.
        const auto late = rig.run ({ { juce::MidiMessage::noteOff (3, 60), rig.position + 5 } }, 512);
        check (chords ? late.empty() : (late.size() == 1 && late[0].message.isNoteOff()), tag + ": el note-off tardio");
        check (snapshotOf (*processor).soundingCount == 0, tag + ": la cuenta quedo mal tras el note-off tardio");
        check (rig.allocations == 0, tag + ": reservas de memoria: " + juce::String (rig.allocations));
    }
}

void testNoHungNotes()
{
    std::printf ("Sin notas colgadas: reinicio del audio, reset, cambio de sample rate, cargar el estado y panico\n");

    expectReleaseAfter ("releaseResources + prepareToPlay", [] (TidechartAudioProcessor& p) {
        p.releaseResources();
        p.prepareToPlay (48000.0, 512);
    });
    expectReleaseAfter ("prepareToPlay a 96 kHz", [] (TidechartAudioProcessor& p) { p.prepareToPlay (96000.0, 512); });
    expectReleaseAfter ("reset", [] (TidechartAudioProcessor& p) { p.reset(); });
    expectReleaseAfter ("setStateInformation", [] (TidechartAudioProcessor& p) {
        juce::MemoryBlock state;
        p.getStateInformation (state);
        p.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    });
    expectReleaseAfter ("panico", [] (TidechartAudioProcessor& p) { p.panic(); }, true);
}

void testBypass()
{
    std::printf ("Bypass: el MIDI pasa igual (parametro y processBlockBypassed); entrar y salir a mitad de una nota apaga "
                 "lo del otro modo en la muestra 0\n");

    auto processor = makeProcessor (44100.0, 256);
    Rig rig (*processor, 2, 256);
    auto* bypass = processor->getParameters().getParameter (tidechart::params::bypass);
    check (processor->getBypassParameter() == bypass, "el bypass del host es el parametro propio");

    // Activo: C5 dispara Am.
    auto out = rig.run ({ { juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)), 10 } }, 256);
    check (out.size() == 3, "acorde activo");
    check (snapshotOf (*processor).heldDegrees == 0b1, "la paleta enciende el i");

    // Bypass: Am se apaga en la muestra 0 y la tecla nueva pasa tal cual.
    bypass->setValueNotifyingHost (1.0f);
    out = rig.run ({ { juce::MidiMessage::noteOn (1, 64, static_cast<juce::uint8> (100)), 256 + 20 } }, 256);
    check (out.size() == 4, "3 note-off del acorde + el note-on en bypass");
    check (out.size() == 4 && out[0].sample == 256 && out[0].message.isNoteOff() && out[3].sample == 276 && out[3].message.getNoteNumber() == 64,
           "orden y muestras al entrar en bypass");
    check (snapshotOf (*processor).bypassed, "la GUI ve el bypass");
    out = rig.run ({ { juce::MidiMessage::noteOff (1, 60), 512 + 30 } }, 256, true); // un host que llama a processBlockBypassed
    check (out.size() == 1 && out[0].message.isNoteOff(), "note-off por processBlockBypassed");

    // Salir del bypass con E5 abajo: su nota se apaga en la muestra 0 y su note-off posterior no sale.
    bypass->setValueNotifyingHost (0.0f);
    out = rig.run ({ { juce::MidiMessage::noteOff (1, 64), 768 + 40 } }, 256);
    check (out.size() == 1 && out[0].sample == 768 && out[0].message.isNoteOff() && out[0].message.getNoteNumber() == 64,
           "al quitar el bypass se apaga la nota que paso tal cual");
    const auto snapshot = snapshotOf (*processor);
    check (snapshot.soundingCount == 0 && ! snapshot.bypassed, "sin notas colgadas tras el bypass");
    check (rig.silent && rig.allocations == 0, "bypass: silencio y sin reservas");
}

void testChords()
{
    std::printf ("Acordes con los parametros: tonalidad, modo, septimas, bajo, notas de la escala; cambiarlos con la tecla "
                 "abajo; la paleta\n");

    auto processor = makeProcessor (48000.0, 256);
    Rig rig (*processor, 2, 256);
    namespace id = tidechart::params;
    const auto notesOf = [] (const std::vector<Event>& events, bool ons) {
        std::vector<int> notes;
        for (const auto& e : events)
            if (ons ? e.message.isNoteOn() : e.message.isNoteOff())
                notes.push_back (e.message.getNoteNumber());
        std::sort (notes.begin(), notes.end());
        return notes;
    };

    // Mi frigio, séptimas, con bajo, notas de la escala: F5 = el ♭II, Fmaj7 con F4 abajo, con la velocidad de la tecla.
    setParameter (*processor, id::keyTonic, 4.0f);
    setParameter (*processor, id::keyMode, 2.0f);
    setParameter (*processor, id::chordSize, 1.0f);
    setParameter (*processor, id::chordBass, 1.0f);
    setParameter (*processor, id::keyMapping, 1.0f);
    const auto settings = processor->currentChordSettings (false);
    check (settings.key.tonic == deepwater::theory::notes::E && settings.key.mode == deepwater::theory::Mode::phrygian, "Mi frigio");
    auto out = rig.run ({ { juce::MidiMessage::noteOn (2, 65, static_cast<juce::uint8> (77)), 50 } }, 256);
    check (notesOf (out, true) == std::vector<int> { 53, 65, 69, 72, 76 }, "Fmaj7 con bajo");
    bool sameVelocityAndChannel = ! out.empty();
    for (const auto& e : out)
        sameVelocityAndChannel = sameVelocityAndChannel && e.message.getVelocity() == 77 && e.message.getChannel() == 2 && e.sample == 50;
    check (sameVelocityAndChannel, "velocidad, canal y muestra de la tecla");
    check (snapshotOf (*processor).heldDegrees == 0b10, "la paleta enciende el bII");

    // Cambiar todo con la tecla abajo; al soltarla se apagan las notas que sonaron.
    setParameter (*processor, id::keyTonic, 0.0f);
    setParameter (*processor, id::keyMode, 0.0f);
    setParameter (*processor, id::chordSize, 0.0f);
    setParameter (*processor, id::chordBass, 0.0f);
    setParameter (*processor, id::keyMapping, 0.0f);
    out = rig.run ({}, 256);
    check (out.empty(), "cambiar parametros no corta ni dispara nada");
    out = rig.run ({ { juce::MidiMessage::noteOff (2, 65), rig.position + 3 } }, 256);
    check (notesOf (out, false) == std::vector<int> { 53, 65, 69, 72, 76 }, "al soltar, los note-off de lo que sono");

    // La paleta (Do mayor ahora): el V = G en la muestra 0 del bloque siguiente; soltarlo lo apaga.
    processor->previewChord (4);
    out = rig.run ({}, 256);
    check (notesOf (out, true) == std::vector<int> { 67, 71, 74 } && ! out.empty() && out[0].sample == rig.position - 256, "la paleta toca G");
    check (snapshotOf (*processor).heldDegrees == 0b10000, "la paleta enciende el V");
    processor->previewChord (-1);
    out = rig.run ({}, 256);
    check (notesOf (out, false) == std::vector<int> { 67, 71, 74 }, "la paleta suelta G");
    check (snapshotOf (*processor).soundingCount == 0, "acordes: nada colgado");
    check (rig.allocations == 0, "acordes: reservas de memoria: " + juce::String (rig.allocations));
}

void testTransport()
{
    std::printf ("Reloj del host: posicion, tempo, compas, parado, saltos y loops; sin host, reloj libre\n");

    auto processor = makeProcessor (48000.0, 480);
    Rig rig (*processor, 2, 480);
    TestHead head;
    processor->setPlayHead (&head);

    // Compás 3, tiempo 2 a 90 BPM en 4/4.
    head.bpm = 90.0;
    head.ppq = 9.0;
    rig.run ({}, 480);
    auto s = snapshotOf (*processor);
    const auto pos = deepwater::dsp::musicalPosition (s.ppq, s.timeSignature, s.lastBarStart);
    check (s.source == deepwater::dsp::ClockSource::host, "reproduciendo");
    check (pos.bar == 3 && pos.beat == 2, "posicion 3.2");
    check (std::abs (s.bpm - 90.0) < 1e-9, "tempo 90");

    // Avanza bien un bloque (480 muestras a 90 BPM = 0.015 negras): no es un salto.
    head.ppq += 480.0 * 90.0 / (60.0 * 48000.0);
    rig.run ({}, 480);
    check (snapshotOf (*processor).jumps == 0, "sin saltos falsos");

    // Loop de FL: vuelve al compás 1, con un acorde sostenido que el host apagará con el note-off de su tecla (Tidechart
    // no lo corta: puede ser una tecla tocada en vivo).
    head.looping = true;
    head.ppq = 0.0;
    auto out = rig.run ({ { juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)), rig.position + 1 } }, 480);
    s = snapshotOf (*processor);
    check (s.jumps == 1 && s.looping, "loop detectado como salto");
    check (out.size() == 3 && s.soundingCount == 3, "el acorde sigue sonando tras el salto");

    // Parado en el compás 5 en 7/8.
    head.playing = false;
    head.numerator = 7;
    head.denominator = 8;
    head.ppq = 14.0;
    rig.run ({ { juce::MidiMessage::noteOff (1, 60), rig.position + 2 } }, 480);
    s = snapshotOf (*processor);
    check (s.source == deepwater::dsp::ClockSource::stopped, "parado");
    check (s.timeSignature.numerator == 7 && s.timeSignature.denominator == 8, "compas 7/8");
    check (deepwater::dsp::musicalPosition (s.ppq, s.timeSignature, s.lastBarStart).bar == 5, "compas 5 en 7/8");
    check (s.soundingCount == 0, "el note-off del host apago el acorde");

    // El host deja de informar: reloj libre que avanza al último tempo conocido (90; sin ninguno, 120).
    processor->setPlayHead (nullptr);
    rig.run ({}, 480);
    const double first = snapshotOf (*processor).ppq;
    rig.run ({}, 480);
    s = snapshotOf (*processor);
    check (s.source == deepwater::dsp::ClockSource::freeRunning, "reloj libre sin host");
    check (std::abs (s.ppq - first - 480.0 * 1.5 / 48000.0) < 1e-9, "el reloj libre avanza a 90 BPM");
    check (rig.allocations == 0, "transporte: reservas de memoria: " + juce::String (rig.allocations));
}

// Fase 4: la progresión con un host que reproduce. Con una tecla abajo una vuelta entera, lo que sale tiene que ser
// exactamente renderProgression (la misma progresión como notas en el tiempo), cada note-on en la primera muestra cuya
// posición llega a su semicorchea, a 4 sample rates y con bloques de 1 a 4096 muestras, sin reservar memoria.
void testProgression()
{
    std::printf ("Progresion sincronizada con el host: igual a renderProgression en su muestra, a 4 sample rates y bloques "
                 "1-4096 (dembow, conduccion de voces, bajo); editarla mientras suena; sin reservas\n");

    namespace th = deepwater::theory;
    namespace id = tidechart::params;
    const double sampleRates[] { 44100.0, 48000.0, 88200.0, 96000.0 };
    const int blockSizes[] { 1, 7, 64, 512, 4096, 0 };
    juce::Random random (2026);
    for (const double sampleRate : sampleRates)
        for (const int blockSize : blockSizes)
        {
            const int maxBlock = blockSize == 0 ? 4096 : blockSize;
            auto processor = makeProcessor (sampleRate, maxBlock);
            setParameter (*processor, id::playMode, 1.0f);
            setParameter (*processor, id::rhythm, 4.0f);  // dembow
            setParameter (*processor, id::voicing, 1.0f); // conducción de voces
            setParameter (*processor, id::chordBass, 1.0f);
            TestHead head;
            head.bpm = 120.0;
            processor->setPlayHead (&head);
            Rig rig (*processor, 2, maxBlock);

            const double beatsPerSample = head.bpm / (60.0 * sampleRate);
            const auto progression = processor->getProgression();
            const auto loopSamples = static_cast<juce::int64> (std::ceil (progression.totalBeats() / beatsPerSample));
            const std::vector<Event> input { { juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)), 0 },
                                             { juce::MidiMessage::noteOff (1, 60), loopSamples } };
            std::vector<Event> output;
            while (rig.position < loopSamples + 100)
            {
                head.ppq = static_cast<double> (rig.position) * beatsPerSample;
                const int n = blockSize == 0 ? 1 + random.nextInt (4096) : blockSize;
                for (auto& e : rig.run (input, n))
                    output.push_back (std::move (e));
            }

            // La semicorchea de una muestra, como la calcula el plugin, y la primera muestra que llega a la semicorchea k.
            const auto tickAt = [&] (juce::int64 s) { return static_cast<juce::int64> (std::floor (static_cast<double> (s) * beatsPerSample * 4.0 + 1.0e-6)); };
            const auto sampleOfTick = [&] (juce::int64 k) {
                auto s = static_cast<juce::int64> (static_cast<double> (k) / (4.0 * beatsPerSample));
                while (tickAt (s) < k)
                    ++s;
                while (s > 0 && tickAt (s - 1) >= k)
                    --s;
                return s;
            };
            th::RenderOptions options;
            options.style = th::VoicingStyle::voiceLed;
            options.tonicNote = 57;
            options.bass = true;
            options.rhythm = 4;
            std::vector<std::array<int, 3>> expected, got;
            for (const auto& e : th::renderProgression (progression, processor->currentChordSettings (false).key, options))
                expected.push_back ({ static_cast<int> (sampleOfTick (juce::roundToInt (e.start * 4.0))), e.note, e.velocity });
            for (const auto& e : output)
                if (e.message.isNoteOn() && e.sample < loopSamples)
                    got.push_back ({ static_cast<int> (e.sample), e.message.getNoteNumber(), e.message.getVelocity() });
            std::sort (expected.begin(), expected.end());
            std::sort (got.begin(), got.end());

            const auto tag = juce::String (sampleRate / 1000.0, 1) + " kHz, bloque " + (blockSize == 0 ? juce::String ("variable") : juce::String (blockSize));
            check (! expected.empty() && got == expected, tag + ": la progresion no salio como renderProgression (" + juce::String (static_cast<int> (got.size()))
                                                              + " de " + juce::String (static_cast<int> (expected.size())) + " note-on)");
            check (snapshotOf (*processor).soundingCount == 0, tag + ": quedaron notas sonando");
            check (rig.allocations == 0, tag + ": processBlock reservo memoria " + juce::String (rig.allocations) + " veces");
            processor->setPlayHead (nullptr);
        }

    // Editar la progresión mientras suena (desde el hilo de la GUI): el bloque siguiente toca el paso nuevo. Y lo que
    // publica el audio para la GUI: el paso, que suena y la semicorchea del patrón.
    auto processor = makeProcessor (48000.0, 512);
    setParameter (*processor, id::playMode, 1.0f);
    TestHead head;
    processor->setPlayHead (&head);
    Rig rig (*processor, 2, 512);
    head.ppq = 4.5; // compás 2: ♭VI = F
    rig.run ({ { juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)), 0 } }, 512);
    auto s = snapshotOf (*processor);
    check (s.progressionStep == 1 && s.progressionSounding && s.rhythmStep == -1, "el paso 2 suena (sostenido: sin semicorchea de patron)");
    check (s.soundingChannels[65] != 0 && s.soundingChannels[69] != 0 && s.soundingChannels[72] != 0, "F suena");
    auto edited = processor->getProgression();
    edited.steps[1].numeral = { 4, 0, th::chordType ("m") }; // iv = Dm
    processor->setProgression (edited);
    head.ppq += 512.0 * 2.0 / 48000.0;
    rig.run ({}, 512);
    s = snapshotOf (*processor);
    check (s.soundingChannels[62] != 0 && s.soundingChannels[65] != 0 && s.soundingChannels[69] != 0 && s.soundingCount == 3, "editada: Dm");
    setParameter (*processor, id::rhythm, 4.0f);
    head.ppq += 512.0 * 2.0 / 48000.0;
    rig.run ({ { juce::MidiMessage::noteOff (1, 60), rig.position + 10 } }, 512);
    s = snapshotOf (*processor);
    check (s.rhythmStep >= 0 && s.soundingCount == 0 && ! s.progressionSounding, "soltar la tecla apaga la progresion");
    check (rig.allocations == 0, "editar: reservas de memoria: " + juce::String (rig.allocations));
    processor->setPlayHead (nullptr);
}

// Las notas de renderProgression como eventos de JUCE a 120 BPM (una negra = 'samplesPerBeat'), note-off primero.
std::vector<Event> eventsOf (const std::vector<deepwater::theory::NoteEvent>& notes, double samplesPerBeat, int channel = 1)
{
    std::vector<Event> events;
    for (const auto& n : notes)
    {
        events.push_back ({ juce::MidiMessage::noteOn (channel, n.note, static_cast<juce::uint8> (n.velocity)), std::llround (n.start * samplesPerBeat) });
        events.push_back ({ juce::MidiMessage::noteOff (channel, n.note), std::llround ((n.start + n.length) * samplesPerBeat) });
    }
    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) {
        return a.sample != b.sample ? a.sample < b.sample : a.message.isNoteOff() && ! b.message.isNoteOff();
    });
    return events;
}

void testKeyDetection()
{
    std::printf ("Deteccion de tonalidad: escuchando, el MIDI pasa tal cual y se detecta (Mi frigio con FL reproduciendo, a 4 "
                 "sample rates y bloques de cualquier tamano); al dejar de escuchar vuelven los acordes; empezar de cero\n");
    namespace th = deepwater::theory;

    const th::Key ePhrygian { th::notes::E, th::Mode::phrygian };
    th::RenderOptions options;
    options.style = th::VoicingStyle::voiceLed;
    options.tonicNote = th::tonicNoteNear (ePhrygian, 60);
    options.bass = true;
    options.rhythm = 4;
    th::Progression progression; // i–♭II de un compás cada uno
    for (const char* numeral : { "i", "bII" })
        progression.steps.push_back ({ *th::parseRomanNumeral (numeral), 4 });

    for (const double sampleRate : { 44100.0, 48000.0, 88200.0, 96000.0 })
        for (const int blockSize : { 1, 64, 512, 4096, 0 })
        {
            // Con bloques de 1 muestra, solo 2 compases (es lento); el resto, los 16.
            const double samplesPerBeat = sampleRate / 2.0;
            const double beats = blockSize == 1 ? 8.0 : 64.0;
            options.loops = static_cast<int> (beats / 8.0); // 16 compases = dos frases
            const auto input = eventsOf (th::renderProgression (progression, ePhrygian, options), samplesPerBeat);
            auto processor = makeProcessor (sampleRate, 4096);
            TestHead head;
            processor->setPlayHead (&head);
            Rig rig (*processor, 2, 4096);
            processor->setListening (true);

            juce::Random random (static_cast<juce::int64> (sampleRate) + blockSize);
            std::vector<Event> output;
            const auto length = static_cast<juce::int64> (beats * samplesPerBeat) + 10;
            while (rig.position < length)
            {
                const int n = static_cast<int> (std::min<juce::int64> (blockSize > 0 ? blockSize : 1 + random.nextInt (4096), length - rig.position));
                for (const auto& e : rig.run (input, n))
                    output.push_back (e);
                head.ppq += n / samplesPerBeat;
            }
            const juce::String where = juce::String (sampleRate) + " Hz, bloques de " + juce::String (blockSize);
            bool same = output.size() == input.size();
            for (size_t i = 0; same && i < output.size(); ++i)
                same = sameEvent (output[i], input[i]);
            check (same, "escuchando, el MIDI pasa tal cual (" + where + ")");
            const auto snapshot = snapshotOf (*processor);
            const auto ranking = th::rankKeys (snapshot.keyEvidence);
            check (snapshot.listening && ranking[0].key == ePhrygian && th::keyConfidence (snapshot.keyEvidence, ranking) != th::KeyConfidence::none,
                   "detecta Mi frigio (" + where + "): " + juce::String (th::keyName (ranking[0].key)));
            check (snapshot.heardSeconds > 0.7 * beats / 2.0 && snapshot.soundingCount == 0, "lo escuchado y nada colgado (" + where + ")");
            check (rig.allocations == 0 && rig.silent, "escuchando: sin reservas de memoria y en silencio (" + where + ")");
            processor->setPlayHead (nullptr);
        }

    // Al dejar de escuchar vuelven los acordes; lo escuchado se conserva. Una tecla abajo al cambiar no queda colgada.
    auto processor = makeProcessor (48000.0, 512);
    Rig rig (*processor, 2, 512);
    processor->setListening (true);
    auto out = rig.run ({ { juce::MidiMessage::noteOn (1, 64, static_cast<juce::uint8> (90)), 10 },
                          { juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (90)), 20 } }, 512);
    check (out.size() == 2, "escuchando: las dos notas pasan solas");
    for (int i = 0; i < 100; ++i)
        rig.run ({}, 512);
    const auto heard = snapshotOf (*processor).keyEvidence;
    check (heard.pitchWeight[0] > 0.0 && heard.pitchWeight[4] > 0.0 && heard.tonicCue[0] == 1.0, "lo escuchado (Do es el bajo y la primera)");
    processor->setListening (false);
    out = rig.run ({}, 512);
    check (out.size() == 2 && out[0].message.isNoteOff() && out[1].message.isNoteOff() && out[0].sample == rig.position - 512,
           "al dejar de escuchar, lo que paso se apaga en la muestra 0");
    out = rig.run ({ { juce::MidiMessage::noteOn (1, 62, static_cast<juce::uint8> (90)), rig.position + 5 } }, 512);
    check (out.size() == 3, "sin escuchar, una tecla vuelve a disparar un acorde");
    auto snapshot = snapshotOf (*processor);
    check (! snapshot.listening && snapshot.keyEvidence == heard, "lo escuchado se conserva al dejar de escuchar");
    out = rig.run ({ { juce::MidiMessage::noteOff (1, 62), rig.position + 5 }, { juce::MidiMessage::noteOff (1, 60), rig.position + 6 },
                     { juce::MidiMessage::noteOff (1, 64), rig.position + 7 } }, 512);
    check (snapshotOf (*processor).soundingCount == 0, "nada colgado al cambiar entre escuchar y acordes");
    processor->setListening (true);
    rig.run ({}, 512);
    snapshot = snapshotOf (*processor);
    check (snapshot.listening && snapshot.keyEvidence == th::KeyEvidence {} && snapshot.heardSeconds == 0.0, "volver a escuchar empieza de cero");
    check (rig.allocations == 0, "escuchar y dejar de escuchar: reservas de memoria: " + juce::String (rig.allocations));
}

void testLayoutsAndState()
{
    std::printf ("Buses (sin entradas, salida mono o estereo), estado del proyecto y monitor de la GUI\n");

    TidechartAudioProcessor processor;
    auto layout = processor.getBusesLayout();
    check (layout.inputBuses.isEmpty(), "sin entradas de audio");
    check (processor.acceptsMidi() && processor.producesMidi(), "MIDI de entrada y salida");
    auto surround = layout;
    surround.outputBuses.getReference (0) = juce::AudioChannelSet::create5point1();
    check (! processor.checkBusesLayoutSupported (surround), "5.1 rechazado");

    processor.getParameters().getParameter (tidechart::params::bypass)->setValueNotifyingHost (1.0f);
    setParameter (processor, tidechart::params::keyTonic, 3.0f);
    setParameter (processor, tidechart::params::keyMode, 9.0f);
    setParameter (processor, tidechart::params::chordSize, 1.0f);
    setParameter (processor, tidechart::params::keyMapping, 1.0f);
    setParameter (processor, tidechart::params::chordBass, 1.0f);
    juce::MemoryBlock state;
    processor.getStateInformation (state);
    TidechartAudioProcessor restored;
    check (restored.currentChordSettings (false) == tidechart::midi::ChordSettings { true }, "por defecto: La menor, triadas, teclas blancas");
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    check (restored.getParameters().getRawParameterValue (tidechart::params::bypass)->load() > 0.5f, "el bypass viaja con el proyecto");
    check (restored.currentChordSettings (false) == processor.currentChordSettings (false), "los ajustes de acordes viajan con el proyecto");
    restored.setStateInformation ("basura", 6); // un estado corrupto no rompe nada
    check (restored.getParameters().getRawParameterValue (tidechart::params::bypass)->load() > 0.5f, "estado corrupto ignorado");

    // Fase 4: los parámetros nuevos y la progresión viajan con el proyecto; la progresión se guarda legible.
    namespace th = deepwater::theory;
    setParameter (processor, tidechart::params::playMode, 1.0f);
    setParameter (processor, tidechart::params::voicing, 2.0f);
    setParameter (processor, tidechart::params::rhythm, 7.0f);
    th::Progression custom;
    custom.steps.push_back ({ { 1, 0, th::chordType ("m7") }, 2 });
    custom.steps.push_back ({ { 2, -1, th::chordType ("maj7") }, 6 });
    custom.steps.push_back ({ { 5, 0, th::chordType ("7b9") }, 16 });
    processor.setProgression (custom);
    juce::MemoryBlock withProgression;
    processor.getStateInformation (withProgression);
    TidechartAudioProcessor reloaded;
    check (reloaded.getProgression() == TidechartAudioProcessor::defaultProgression(), "por defecto: i bVI bIII bVII");
    const auto before = reloaded.progressionChangeCount();
    reloaded.setStateInformation (withProgression.getData(), static_cast<int> (withProgression.getSize()));
    check (reloaded.getProgression() == custom, "la progresion viaja con el proyecto");
    check (reloaded.progressionChangeCount() != before, "la GUI se entera de la progresion cargada");
    check (reloaded.currentChordSettings (false) == processor.currentChordSettings (false), "modo, voicing y ritmo viajan con el proyecto");
    const auto xml = juce::AudioProcessor::getXmlFromBinary (withProgression.getData(), static_cast<int> (withProgression.getSize()));
    check (xml != nullptr && xml->toString().contains ("chord=\"bIImaj7\"") && xml->toString().contains ("beats=\"16\""), "progresion legible en el XML");
    // Guardar otra vez no la duplica (no queda dentro del árbol de parámetros).
    juce::MemoryBlock again;
    reloaded.getStateInformation (again);
    const auto xmlAgain = juce::AudioProcessor::getXmlFromBinary (again.getData(), static_cast<int> (again.getSize()));
    check (xmlAgain != nullptr && xmlAgain->getNumChildElements() == xml->getNumChildElements(), "la progresion no se duplica al guardar dos veces");
    // Un proyecto de la Fase 3 (sin progresión) carga la de fábrica.
    reloaded.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    check (reloaded.getProgression() == TidechartAudioProcessor::defaultProgression(), "proyecto sin progresion: la de fabrica");

    // El monitor recibe cada evento de entrada y de salida, y los contadores de notas.
    auto p = makeProcessor (44100.0, 128);
    Rig rig (*p, 2, 128);
    rig.run ({ { juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)), 3 },
               { juce::MidiMessage::controllerEvent (1, 64, 127), 4 } },
             128);
    int in = 0, out = 0;
    p->drainMidiLog ([&] (const tidechart::midi::LoggedEvent& e) { (e.output ? out : in)++; });
    check (in == 2 && out == 4, "monitor: 2 de entrada; de salida, el acorde de 3 notas y el CC");
    check (p->notesIn() == 1 && p->notesOut() == 3, "contadores de notas");

    // El editor se crea y se destruye sin problemas.
    std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
    check (editor != nullptr && editor->getWidth() > 0, "editor");
}

void testLines()
{
    std::printf ("Lineas (Fase 6): bajo y melodia con un host = renderLine en su muestra, a 4 sample rates y bloques 1-4096, "
                 "sin reservas; cambiarlas mientras suenan; capturar deja pasar el MIDI\n");

    namespace th = deepwater::theory;
    namespace id = tidechart::params;
    const double sampleRates[] { 44100.0, 48000.0, 88200.0, 96000.0 };
    const int blockSizes[] { 1, 7, 64, 512, 4096, 0 };
    juce::Random random (606);
    for (const double sampleRate : sampleRates)
        for (const int blockSize : blockSizes)
        {
            const int maxBlock = blockSize == 0 ? 4096 : blockSize;
            auto processor = makeProcessor (sampleRate, maxBlock);
            setParameter (*processor, id::playMode, 1.0f);
            setParameter (*processor, id::playChords, 0.0f);
            setParameter (*processor, id::playBass, 1.0f);
            setParameter (*processor, id::playMelody, 1.0f);
            TestHead head;
            processor->setPlayHead (&head);
            Rig rig (*processor, 2, maxBlock);

            const auto lines = processor->getLines();
            const double beatsPerSample = head.bpm / (60.0 * sampleRate);
            const auto loopSamples = static_cast<juce::int64> (std::ceil (2.0 * lines.melody.length / 4.0 / beatsPerSample));
            const std::vector<Event> input { { juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)), 0 },
                                             { juce::MidiMessage::noteOff (1, 60), loopSamples } };
            std::vector<Event> output;
            while (rig.position < loopSamples + 100)
            {
                head.ppq = static_cast<double> (rig.position) * beatsPerSample;
                const int n = blockSize == 0 ? 1 + random.nextInt (4096) : blockSize;
                for (auto& e : rig.run (input, n))
                    output.push_back (std::move (e));
            }

            const auto tickAt = [&] (juce::int64 s) { return static_cast<juce::int64> (std::floor (static_cast<double> (s) * beatsPerSample * 4.0 + 1.0e-6)); };
            const auto sampleOfTick = [&] (juce::int64 k) {
                auto s = static_cast<juce::int64> (static_cast<double> (k) / (4.0 * beatsPerSample));
                while (tickAt (s) < k)
                    ++s;
                while (s > 0 && tickAt (s - 1) >= k)
                    --s;
                return s;
            };
            std::vector<std::array<int, 3>> expected, got;
            for (const auto* line : { &lines.bass, &lines.melody })
                for (const auto& e : th::renderLine (*line, 2))
                    expected.push_back ({ static_cast<int> (sampleOfTick (juce::roundToInt (e.start * 4.0))), e.note, e.velocity });
            for (const auto& e : output)
                if (e.message.isNoteOn() && e.sample < loopSamples)
                    got.push_back ({ static_cast<int> (e.sample), e.message.getNoteNumber(), e.message.getVelocity() });
            std::sort (expected.begin(), expected.end());
            std::sort (got.begin(), got.end());

            const auto tag = juce::String (sampleRate / 1000.0, 1) + " kHz, bloque " + (blockSize == 0 ? juce::String ("variable") : juce::String (blockSize));
            check (! expected.empty() && got == expected, tag + ": las lineas no salieron como renderLine (" + juce::String (static_cast<int> (got.size()))
                                                              + " de " + juce::String (static_cast<int> (expected.size())) + " note-on)");
            check (snapshotOf (*processor).soundingCount == 0, tag + ": quedaron notas sonando");
            check (rig.allocations == 0, tag + ": processBlock reservo memoria " + juce::String (rig.allocations) + " veces");
            processor->setPlayHead (nullptr);
        }

    // Cambiar la melodía mientras suena (desde el hilo de la GUI): el bloque siguiente toca la nueva, sin reservar memoria.
    auto processor = makeProcessor (48000.0, 512);
    setParameter (*processor, id::playMode, 1.0f);
    setParameter (*processor, id::playMelody, 1.0f);
    TestHead head;
    processor->setPlayHead (&head);
    Rig rig (*processor, 2, 512);
    rig.run ({ { juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)), 0 } }, 512);
    check ((snapshotOf (*processor).linesSounding & 0b10) != 0, "la melodia suena (snapshot)");
    auto melody = processor->getLines().melodySettings;
    melody.generator.style = th::MelodyStyle::sung;
    melody.generator.seed = 9;
    processor->setMelodySettings (melody);
    check (processor->getLines().melody == th::generateMelody (processor->getProgression(), processor->currentChordSettings (false).key, melody.generator),
           "setMelodySettings genera con la progresion y la tonalidad de ahora");
    for (int block = 0; block < 40; ++block)
    {
        head.ppq += 512.0 * 2.0 / 48000.0;
        rig.run ({}, 512);
    }
    head.ppq += 512.0 * 2.0 / 48000.0;
    rig.run ({ { juce::MidiMessage::noteOff (1, 60), rig.position + 3 } }, 512);
    check (snapshotOf (*processor).soundingCount == 0, "soltar la tecla apaga las lineas");
    check (rig.allocations == 0, "cambiar la melodia: reservas de memoria: " + juce::String (rig.allocations));

    // Cambiar la progresión o la tonalidad regenera las líneas (las fijas sin variación no se tocan).
    const auto before = processor->getLines();
    auto progression = processor->getProgression();
    progression.steps[1].numeral = { 2, -1, th::chordType ("") }; // ♭II
    processor->setProgression (progression);
    processor->refreshLines();
    check (! (processor->getLines().bass == before.bass) && processor->getLines().bass == th::generateBass (progression, th::Key { th::notes::A, th::Mode::aeolian }, before.bassSettings.generator),
           "la progresion cambio: el bajo se regenera");
    th::Line written;
    written.length = 64;
    written.notes.push_back ({ 0, 8, 71, 100 });
    processor->captureMelody (written);
    setParameter (*processor, id::keyTonic, 4.0f);
    processor->refreshLines();
    check (processor->getLines().melody == written && processor->getLines().melodySettings.hasSource, "una melodia capturada no se regenera");

    // Capturando, el MIDI pasa tal cual (como escuchando) y el monitor sabe si FL reproducía.
    processor->setCapturing (true);
    rig.allocations = 0;
    while (processor->drainMidiLog ([] (const tidechart::midi::LoggedEvent&) {}) > 0) {}
    const auto passed = rig.run ({ { juce::MidiMessage::noteOn (1, 64, static_cast<juce::uint8> (90)), rig.position + 5 } }, 512);
    check (passed.size() == 1 && passed[0].message.isNoteOn() && passed[0].message.getNoteNumber() == 64, "capturando, la nota pasa tal cual");
    check (snapshotOf (*processor).capturing, "el snapshot dice que captura");
    bool sawPlaying = false;
    processor->drainMidiLog ([&] (const tidechart::midi::LoggedEvent& e) { sawPlaying = sawPlaying || (! e.output && e.playing); });
    check (sawPlaying, "el monitor marca que FL reproducia");
    processor->setCapturing (false);
    rig.run ({}, 512);
    check (snapshotOf (*processor).soundingCount == 0 && ! snapshotOf (*processor).capturing, "al dejar de capturar se apaga lo que paso");
    processor->setPlayHead (nullptr);
}

void testLinesState()
{
    std::printf ("Lineas en el proyecto: ajustes y notas viajan, legibles; las notas guardadas mandan al cargar; un proyecto "
                 "sin lineas genera las de fabrica\n");

    namespace th = deepwater::theory;
    namespace id = tidechart::params;
    TidechartAudioProcessor processor;
    tidechart::BassSettings bass;
    bass.generator.style = th::BassStyle::walking;
    bass.generator.density = 0.8;
    bass.generator.low = 30;
    bass.generator.high = 48;
    bass.generator.seed = 7;
    processor.setBassSettings (bass);
    tidechart::MelodySettings melody;
    melody.generator.style = th::MelodyStyle::rnb;
    melody.generator.seed = 5;
    melody.variation = 0.4;
    melody.variationSeed = 3;
    processor.setMelodySettings (melody);
    setParameter (processor, id::playBass, 1.0f);
    juce::MemoryBlock state;
    processor.getStateInformation (state);

    TidechartAudioProcessor reloaded;
    const auto changes = reloaded.linesChangeCount();
    reloaded.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    const auto a = processor.getLines(), b = reloaded.getLines();
    check (b.bassSettings == a.bassSettings && b.melodySettings == a.melodySettings, "los ajustes de las lineas viajan con el proyecto");
    check (b.bass == a.bass && b.melody == a.melody && ! b.melody.notes.empty(), "las notas de las lineas viajan con el proyecto");
    check (reloaded.linesChangeCount() != changes, "la GUI se entera de las lineas cargadas");
    check (reloaded.currentChordSettings (false).playBass && ! reloaded.currentChordSettings (false).playMelody, "las capas viajan con el proyecto");
    const auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    check (xml != nullptr && xml->toString().contains ("<Bass style=\"walking\"") && xml->toString().contains ("style=\"rnb\"")
               && xml->toString().contains ("note=\"A2\""),
           "lineas legibles en el XML");

    // Las notas guardadas mandan: se edita una a mano en el XML y al cargar suena esa (aunque el generador diera otra).
    auto edited = std::make_unique<juce::XmlElement> (*xml);
    auto* melodyElement = edited->getChildByName ("Lines")->getChildByName ("Melody");
    melodyElement->getChildByName ("Note")->setAttribute ("note", "G#7");
    juce::MemoryBlock editedState;
    juce::AudioProcessor::copyXmlToBinary (*edited, editedState);
    reloaded.setStateInformation (editedState.getData(), static_cast<int> (editedState.getSize()));
    check (reloaded.getLines().melody.notes[0].note == 92, "la nota escrita a mano (G#7 = 92, con C5 = 60) es la que queda");
    reloaded.refreshLines();
    check (reloaded.getLines().melody.notes[0].note == 92, "y no se regenera mientras no cambien la progresion ni la tonalidad");
    setParameter (reloaded, id::keyMode, 1.0f); // dórico
    reloaded.refreshLines();
    check (reloaded.getLines().melody.notes[0].note != 92, "cambiar la tonalidad la regenera");

    // Una melodía capturada viaja con su base.
    th::Line written;
    written.length = 64;
    written.notes.push_back ({ 0, 4, 69, 100 });
    written.notes.push_back ({ 6, 2, 72, 90 });
    processor.captureMelody (written);
    juce::MemoryBlock captured;
    processor.getStateInformation (captured);
    reloaded.setStateInformation (captured.getData(), static_cast<int> (captured.getSize()));
    check (reloaded.getLines().melodySettings.hasSource && reloaded.getLines().melodySettings.source == written
               && reloaded.getLines().melody == written,
           "la melodia capturada viaja como base");

    // Un proyecto sin líneas (Fase 5): las de fábrica con su progresión y tonalidad, y las capas apagadas.
    juce::XmlElement old (*xml);
    old.removeChildElement (old.getChildByName ("Lines"), true);
    for (auto* param : old.getChildIterator())
        if (param->getStringAttribute ("id").startsWith ("play") && param->getStringAttribute ("id") != "playMode")
            param->setAttribute ("value", param->getStringAttribute ("id") == "playChords" ? 1.0 : 0.0);
    juce::MemoryBlock oldState;
    juce::AudioProcessor::copyXmlToBinary (old, oldState);
    TidechartAudioProcessor fresh;
    fresh.setStateInformation (oldState.getData(), static_cast<int> (oldState.getSize()));
    const auto key = fresh.currentChordSettings (false).key;
    check (fresh.getLines().melody == th::generateMelody (fresh.getProgression(), key, th::MelodyOptions {})
               && fresh.getLines().bass == th::generateBass (fresh.getProgression(), key, th::BassOptions {}),
           "proyecto sin lineas: las de fabrica");
    check (! fresh.currentChordSettings (false).playBass && ! fresh.currentChordSettings (false).playMelody, "proyecto sin lineas: capas apagadas");
}
} // namespace

// Pinta el editor fuera de pantalla en un PNG, con FL "reproduciendo" la progresión con dembow en el compás 2 y la
// detección después de escuchar i–♭II en Mi frigio (para revisar la GUI sin capturar la pantalla):
//   TidechartProcessorTests --snapshot gui.png          (la vista simple, la de por defecto)
//   TidechartProcessorTests --snapshot gui.png full     (la vista completa)
int saveSnapshot (const juce::File& file, bool full)
{
    namespace th = deepwater::theory;
    auto processor = makeProcessor (48000.0, 512);
    TestHead head;
    processor->setPlayHead (&head);
    Rig rig (*processor, 2, 512);

    // Antes, la detección escuchó 16 compases de i–♭II en Mi frigio (con bajo y dembow).
    const th::Key ePhrygian { th::notes::E, th::Mode::phrygian };
    th::Progression listened;
    for (const char* numeral : { "i", "bII" })
        listened.steps.push_back ({ *th::parseRomanNumeral (numeral), 4 });
    th::RenderOptions options;
    options.style = th::VoicingStyle::voiceLed;
    options.tonicNote = th::tonicNoteNear (ePhrygian, 60);
    options.bass = true;
    options.rhythm = 4;
    options.loops = 8;
    const auto input = eventsOf (th::renderProgression (listened, ePhrygian, options), 24000.0);
    processor->setListening (true);
    while (rig.position < 64 * 24000 + 10)
    {
        rig.run (input, 512);
        head.ppq += 512.0 / 24000.0;
    }
    processor->setListening (false);

    setParameter (*processor, tidechart::params::playMode, 1.0f);
    setParameter (*processor, tidechart::params::rhythm, 4.0f);
    setParameter (*processor, tidechart::params::voicing, 1.0f);
    setParameter (*processor, tidechart::params::playBass, 1.0f); // Fase 6: el bajo y la melodía también
    setParameter (*processor, tidechart::params::playMelody, 1.0f);
    head.ppq = 4.0;
    rig.position = 0;
    if (full)
        processor->getParameters().state.setProperty ("view", "full", nullptr);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditor());
    for (int block = 0; block < 6; ++block)
    {
        rig.run (block == 0 ? std::vector<Event> { { juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)), 0 } } : std::vector<Event> {}, 512);
        head.ppq += 512.0 * 2.0 / 48000.0;
    }
    juce::Thread::sleep (100);
    juce::Timer::callPendingTimersSynchronously();
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    file.deleteFile();
    juce::FileOutputStream stream (file);
    const bool ok = stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream);
    std::printf ("%s %s\n", ok ? "GUI guardada en" : "No se pudo guardar", file.getFullPathName().toRawUTF8());
    processor->setPlayHead (nullptr);
    return ok ? 0 : 1;
}

// Fase 7: lo que se arrastra al Piano Roll es lo que suena. Para cada capa sola (acordes, bajo, melodía), el procesador
// con un host a 120 BPM y una tecla en C5 durante una vuelta: las notas que salen (inicio, duración, nota, velocidad),
// pasadas a negras, tienen que ser exactamente las del clip. Y el clip como archivo .mid: leído de vuelta, las mismas
// notas en ticks exactos, cada note-on con su note-off, el note-off primero si coinciden, el fin de pista al final de la
// vuelta, el nombre, el tempo y el compás.
void testExport()
{
    std::printf ("Arrastrar al Piano Roll (Fase 7): cada capa exportada = lo que suena en vivo (4 ajustes x 2 progresiones); "
                 "el .mid leido de vuelta tiene las mismas notas, exactas al tick\n");

    namespace th = deepwater::theory;
    namespace id = tidechart::params;
    using tidechart::ExportLayer;

    struct Setup
    {
        const char* name;
        int tonic, mode, size, voicing, rhythm, bass;
    };
    const Setup setups[] { { "La menor, por defecto", 9, static_cast<int> (th::Mode::aeolian), 0, 0, 0, 0 },
                           { "La menor, conduccion + dembow + bajo", 9, static_cast<int> (th::Mode::aeolian), 0, 1, 4, 1 },
                           { "Mi frigio, septimas drop 2 + dembow gaps", 4, static_cast<int> (th::Mode::phrygian), 1, 2, 8, 0 },
                           { "Fa# dorico, corcheas + bajo", 6, static_cast<int> (th::Mode::dorian), 0, 1, 2, 1 } };
    const char* progressions[] { "i bVI bIII bVII", "i bII" }; // la segunda dura 2 compases: los acordes se repiten 2 veces

    for (const auto* text : progressions)
        for (const auto& setup : setups)
        {
            const double sampleRate = 48000.0; // a 120 BPM, una negra = 24000 muestras exactas
            const double samplesPerBeat = 24000.0;
            auto processor = makeProcessor (sampleRate, 512);
            th::Progression progression;
            const auto numerals = th::parseProgression (text); // con nombre: el temporal no vive lo que dura el for
            for (const auto& numeral : *numerals)
                progression.steps.push_back ({ numeral, 4 });
            processor->setProgression (progression);
            setParameter (*processor, id::keyTonic, static_cast<float> (setup.tonic));
            setParameter (*processor, id::keyMode, static_cast<float> (setup.mode));
            setParameter (*processor, id::chordSize, static_cast<float> (setup.size));
            setParameter (*processor, id::voicing, static_cast<float> (setup.voicing));
            setParameter (*processor, id::rhythm, static_cast<float> (setup.rhythm));
            setParameter (*processor, id::chordBass, static_cast<float> (setup.bass));
            setParameter (*processor, id::playMode, 1.0f);

            const auto chords = processor->exportClip (ExportLayer::chords);
            const auto bass = processor->exportClip (ExportLayer::bass);
            const auto melody = processor->exportClip (ExportLayer::melody);
            const auto tag = juce::String (text) + ", " + setup.name;
            const double expectedLength = th::lineLength (progression) / 4.0;
            check (expectedLength > 0.0 && processor->getProgression() == progression, tag + ": la progresion de la prueba no llego al procesador");
            check (chords.lengthBeats == expectedLength && bass.lengthBeats == expectedLength && melody.lengthBeats == expectedLength,
                   tag + ": las tres capas no duran una vuelta de las lineas (" + juce::String (chords.lengthBeats) + ", "
                       + juce::String (bass.lengthBeats) + ", " + juce::String (melody.lengthBeats) + ")");

            for (const auto& [layer, clip] : { std::pair { ExportLayer::chords, &chords }, std::pair { ExportLayer::bass, &bass },
                                               std::pair { ExportLayer::melody, &melody } })
            {
                setParameter (*processor, id::playChords, layer == ExportLayer::chords ? 1.0f : 0.0f);
                setParameter (*processor, id::playBass, layer == ExportLayer::bass ? 1.0f : 0.0f);
                setParameter (*processor, id::playMelody, layer == ExportLayer::melody ? 1.0f : 0.0f);
                processor->reset();
                TestHead head;
                processor->setPlayHead (&head);
                Rig rig (*processor, 2, 512);
                const auto loopSamples = static_cast<juce::int64> (clip->lengthBeats * samplesPerBeat);
                const std::vector<Event> input { { juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)), 0 },
                                                 { juce::MidiMessage::noteOff (1, 60), loopSamples } };
                std::vector<Event> output;
                while (rig.position < loopSamples + 512)
                {
                    head.ppq = static_cast<double> (rig.position) / samplesPerBeat;
                    for (auto& e : rig.run (input, 512))
                        output.push_back (std::move (e));
                }
                processor->setPlayHead (nullptr);

                // Cada note-on con su note-off (la misma nota: el primero que sigue abierto).
                std::vector<th::NoteEvent> live;
                std::array<std::vector<std::size_t>, 128> open;
                for (const auto& e : output)
                {
                    const auto& m = e.message;
                    if (m.isNoteOn())
                    {
                        open[static_cast<std::size_t> (m.getNoteNumber())].push_back (live.size());
                        live.push_back ({ m.getNoteNumber(), static_cast<double> (e.sample) / samplesPerBeat, 0.0, m.getVelocity() });
                    }
                    else if (m.isNoteOff() && ! open[static_cast<std::size_t> (m.getNoteNumber())].empty())
                    {
                        auto& stack = open[static_cast<std::size_t> (m.getNoteNumber())];
                        auto& n = live[stack.front()];
                        stack.erase (stack.begin());
                        n.length = static_cast<double> (e.sample) / samplesPerBeat - n.start;
                    }
                }
                const auto byStart = [] (const th::NoteEvent& a, const th::NoteEvent& b) {
                    return a.start != b.start ? a.start < b.start : a.note < b.note;
                };
                // La vuelta siguiente empieza en la misma muestra en que se suelta la tecla: su primer ataque no cuenta.
                std::erase_if (live, [&] (const th::NoteEvent& n) { return n.start >= clip->lengthBeats; });
                std::sort (live.begin(), live.end(), byStart);
                const auto layerName = tidechart::exportLayerName (layer);
                check (! clip->empty() && live == clip->notes, tag + ", " + layerName + ": lo exportado no es lo que suena ("
                                                                    + juce::String (static_cast<int> (clip->notes.size())) + " notas contra "
                                                                    + juce::String (static_cast<int> (live.size())) + ")");

                // El archivo MIDI, leído de vuelta.
                juce::MemoryOutputStream written;
                check (tidechart::toMidiFile (*clip, "Tidechart " + layerName, 93.0).writeTo (written, 1), tag + ": no se pudo escribir el .mid");
                juce::MemoryInputStream readBack (written.getData(), written.getDataSize(), false);
                juce::MidiFile file;
                int format = 0;
                check (file.readFrom (readBack, true, &format), tag + ": el .mid no se puede leer");
                check (format == 1 && file.getNumTracks() == 1 && file.getTimeFormat() == tidechart::midiFileTicksPerQuarter,
                       tag + ": formato, pistas o resolucion");
                if (file.getNumTracks() != 1)
                    continue;
                auto track = *file.getTrack (0);
                track.updateMatchedPairs();
                std::vector<th::NoteEvent> fromFile;
                bool name = false, tempo = false, meter = false, matched = true, channelOne = true;
                double end = -1.0;
                for (int i = 0; i < track.getNumEvents(); ++i)
                {
                    const auto* event = track.getEventPointer (i);
                    const auto& m = event->message;
                    name = name || (m.isTrackNameEvent() && m.getTextFromTextMetaEvent() == "Tidechart " + layerName);
                    tempo = tempo || (m.isTempoMetaEvent() && std::abs (m.getTempoSecondsPerQuarterNote() - 60.0 / 93.0) < 1.0e-6);
                    if (m.isTimeSignatureMetaEvent())
                    {
                        int numerator = 0, denominator = 0;
                        m.getTimeSignatureInfo (numerator, denominator);
                        meter = numerator == 4 && denominator == 4;
                    }
                    if (m.isEndOfTrackMetaEvent())
                        end = m.getTimeStamp();
                    if (m.isNoteOn())
                    {
                        channelOne = channelOne && m.getChannel() == 1;
                        if (event->noteOffObject == nullptr)
                        {
                            matched = false;
                            continue;
                        }
                        fromFile.push_back ({ m.getNoteNumber(), m.getTimeStamp() / tidechart::midiFileTicksPerQuarter,
                                              (event->noteOffObject->message.getTimeStamp() - m.getTimeStamp()) / tidechart::midiFileTicksPerQuarter,
                                              m.getVelocity() });
                    }
                }
                std::sort (fromFile.begin(), fromFile.end(), byStart);
                check (name && tempo && meter, tag + ", " + layerName + ": faltan el nombre, el tempo o el compas");
                check (matched && channelOne, tag + ", " + layerName + ": un note-on sin note-off o fuera del canal 1");
                check (fromFile == clip->notes, tag + ", " + layerName + ": las notas del .mid no son las del clip");
                check (end == clip->lengthBeats * tidechart::midiFileTicksPerQuarter, tag + ", " + layerName + ": el fin de pista no esta al final de la vuelta");
            }
        }

    // Una nota repetida pegada a sí misma (termina en el tick en que empieza la siguiente): en el archivo, el note-off
    // va antes que el note-on, o un programa que lea en orden la cortaría al empezar.
    tidechart::ExportClip repeated;
    repeated.notes = { { 60, 0.0, 0.25, 100 }, { 60, 0.25, 0.25, 90 } };
    repeated.lengthBeats = 4.0;
    const auto midi = tidechart::toMidiFile (repeated, "repetida", 120.0);
    std::vector<juce::String> order;
    for (const auto* event : *midi.getTrack (0))
        if (event->message.isNoteOnOrOff())
            order.push_back ((event->message.isNoteOn() ? "on " : "off ") + juce::String (event->message.getTimeStamp()));
    check (order == std::vector<juce::String> { "on 0", "off 240", "on 240", "off 480" }, "nota repetida: el note-off no va antes del note-on");

    // Progresión vacía: nada que arrastrar.
    auto processor = makeProcessor (48000.0, 512);
    processor->setProgression ({});
    check (processor->exportClip (ExportLayer::chords).empty(), "progresion vacia: el clip de acordes deberia estar vacio");

    // El archivo en disco: se reemplaza entero (uno más corto no deja restos del anterior).
    const auto file = juce::File::createTempFile (".mid");
    tidechart::ExportClip longClip;
    for (int i = 0; i < 64; ++i)
        longClip.notes.push_back ({ 48 + i % 24, i * 0.25, 0.25, 100 });
    longClip.lengthBeats = 16.0;
    check (tidechart::writeMidiFile (longClip, "larga", 120.0, file), "no se pudo escribir el .mid largo");
    check (tidechart::writeMidiFile (repeated, "corta", 120.0, file), "no se pudo escribir el .mid corto");
    juce::FileInputStream in (file);
    juce::MidiFile reread;
    check (in.openedOk() && reread.readFrom (in) && reread.getNumTracks() == 1 && reread.getTrack (0)->getNumEvents() == 8,
           "el .mid sobrescrito no tiene solo lo nuevo (3 metas + 4 notas + fin)");
    file.deleteFile();
}

int main (int argc, char* argv[])
{
    const juce::ScopedJuceInitialiser_GUI init;
    if ((argc == 3 || argc == 4) && juce::String (argv[1]) == "--snapshot")
        return saveSnapshot (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[2])),
                             argc == 4 && juce::String (argv[3]) == "full");

    testPassThrough();
    testNoHungNotes();
    testBypass();
    testChords();
    testTransport();
    testProgression();
    testKeyDetection();
    testLayoutsAndState();
    testLines();
    testLinesState();
    testExport();

    if (failures == 0)
        std::printf ("\nTodos los tests del procesador de Tidechart pasaron.\n");
    else
        std::printf ("\n%d comprobaciones fallaron.\n", failures);
    return failures == 0 ? 0 : 1;
}
