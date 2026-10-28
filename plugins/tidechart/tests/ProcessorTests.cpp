// Tests del procesador completo de Tidechart (con JUCE): exactamente lo que hace el plugin dentro de FL. MIDI al azar a
// 4 sample rates y con bloques de 1 a 4096 muestras; bypass, reinicios del audio, pánico y cargar el estado a mitad de
// una nota; un host que reproduce, para, salta y hace loop. Comprueba que el MIDI pasa idéntico y en su muestra, que
// la salida de audio es silencio, que ninguna nota queda colgada y que processBlock no reserva memoria.

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

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
    std::printf ("Passthrough: el MIDI sale identico y en su muestra; audio en silencio; sin reservas (4 sample rates, "
                 "bloques 1-4096, mono y estereo)\n");

    const double sampleRates[] { 44100.0, 48000.0, 88200.0, 96000.0 };
    const int blockSizes[] { 1, 7, 64, 256, 512, 1024, 4096, 0 }; // 0 = tamaño variable de bloque a bloque
    juce::Random random (4242);
    for (const double sampleRate : sampleRates)
        for (const int blockSize : blockSizes)
            for (const int channels : { 1, 2 })
            {
                const int maxBlock = blockSize == 0 ? 4096 : blockSize;
                auto processor = makeProcessor (sampleRate, maxBlock, channels);
                Rig rig (*processor, channels, maxBlock);
                const juce::int64 length = static_cast<juce::int64> (sampleRate * 3.0);
                const auto input = randomStream (random, length);

                std::vector<Event> output;
                while (rig.position < length)
                {
                    const int n = static_cast<int> (std::min<juce::int64> (blockSize == 0 ? 1 + random.nextInt (4096) : blockSize,
                                                                          length - rig.position));
                    for (auto& e : rig.run (input, n))
                        output.push_back (std::move (e));
                }

                const auto tag = juce::String (sampleRate / 1000.0, 1) + " kHz, bloque " + (blockSize == 0 ? juce::String ("variable") : juce::String (blockSize))
                                 + ", " + juce::String (channels) + " canales";
                bool identical = output.size() == input.size();
                for (size_t i = 0; identical && i < input.size(); ++i)
                    identical = sameEvent (input[i], output[i]);
                check (identical, tag + ": el MIDI no salio identico (" + juce::String (static_cast<int> (output.size())) + " de "
                                      + juce::String (static_cast<int> (input.size())) + ")");
                check (rig.silent, tag + ": la salida de audio no es silencio");
                check (rig.allocations == 0, tag + ": processBlock reservo memoria " + juce::String (rig.allocations) + " veces");
                check (snapshotOf (*processor).soundingCount == 0, tag + ": quedaron notas sonando");
            }
}

// Una nota suena; pasa 'interrupt' (algo que corta el flujo normal); el bloque siguiente tiene que empezar con su
// note-off en la muestra 0, y después el note-off del host pasa igual sin dejar la cuenta mal.
void expectReleaseAfter (const juce::String& name, const std::function<void (TidechartAudioProcessor&)>& interrupt, bool panic = false)
{
    auto processor = makeProcessor (48000.0, 512);
    Rig rig (*processor, 2, 512);
    std::vector<Event> input { { juce::MidiMessage::noteOn (3, 60, static_cast<juce::uint8> (100)), 100 },
                               { juce::MidiMessage::noteOn (3, 60, static_cast<juce::uint8> (90)), 200 }, // la misma tecla otra vez
                               { juce::MidiMessage::noteOn (1, 67, static_cast<juce::uint8> (80)), 300 } };
    rig.run (input, 512);
    check (snapshotOf (*processor).soundingCount == 2, name + ": antes, 2 teclas sonando");

    interrupt (*processor);
    const auto out = rig.run ({}, 512);
    int offs = 0, allNotesOff = 0;
    for (const auto& e : out)
    {
        check (e.sample == 512, name + ": el note-off no cayo en la muestra 0 del bloque");
        offs += e.message.isNoteOff() ? 1 : 0;
        allNotesOff += e.message.isAllNotesOff() ? 1 : 0;
    }
    check (offs == 3, name + ": se esperaban 3 note-off (la tecla repetida necesita dos), salieron " + juce::String (offs));
    check (allNotesOff == (panic ? 16 : 0), name + ": All Notes Off: " + juce::String (allNotesOff));
    const auto after = snapshotOf (*processor);
    check (after.soundingCount == 0, name + ": quedaron notas sonando");
    check (after.releases >= 1, name + ": no se conto el apagado");

    // El note-off del host llega después y pasa tal cual (el instrumento ignora un note-off de más).
    const auto late = rig.run ({ { juce::MidiMessage::noteOff (3, 60), rig.position + 5 } }, 512);
    check (late.size() == 1 && late[0].message.isNoteOff(), name + ": el note-off tardio no paso");
    check (snapshotOf (*processor).soundingCount == 0, name + ": la cuenta quedo mal tras el note-off tardio");
    check (rig.allocations == 0, name + ": reservas de memoria: " + juce::String (rig.allocations));
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
    std::printf ("Bypass: el MIDI pasa igual (parametro y processBlockBypassed), cambiarlo a mitad de una nota no la cuelga\n");

    auto processor = makeProcessor (44100.0, 256);
    Rig rig (*processor, 2, 256);
    auto* bypass = processor->getParameters().getParameter (tidechart::params::bypass);
    check (processor->getBypassParameter() == bypass, "el bypass del host es el parametro propio");

    auto out = rig.run ({ { juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)), 10 } }, 256);
    check (out.size() == 1, "note-on activo");
    bypass->setValueNotifyingHost (1.0f);
    out = rig.run ({ { juce::MidiMessage::noteOn (1, 64, static_cast<juce::uint8> (100)), 256 + 20 } }, 256);
    check (out.size() == 1 && out[0].sample == 276, "note-on en bypass");
    check (snapshotOf (*processor).bypassed, "la GUI ve el bypass");
    out = rig.run ({ { juce::MidiMessage::noteOff (1, 60), 512 + 30 } }, 256, true); // un host que llama a processBlockBypassed
    check (out.size() == 1 && out[0].message.isNoteOff(), "note-off por processBlockBypassed");
    bypass->setValueNotifyingHost (0.0f);
    out = rig.run ({ { juce::MidiMessage::noteOff (1, 64), 768 + 40 } }, 256);
    check (out.size() == 1 && out[0].sample == 808, "note-off al quitar el bypass");
    const auto snapshot = snapshotOf (*processor);
    check (snapshot.soundingCount == 0 && ! snapshot.bypassed, "sin notas colgadas tras el bypass");
    check (rig.silent && rig.allocations == 0, "bypass: silencio y sin reservas");
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

    // Loop de FL: vuelve al compás 1, con una nota sostenida que el host apagará (Tidechart no la corta: puede ser una
    // nota tocada en vivo).
    head.looping = true;
    head.ppq = 0.0;
    auto out = rig.run ({ { juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)), rig.position + 1 } }, 480);
    s = snapshotOf (*processor);
    check (s.jumps == 1 && s.looping, "loop detectado como salto");
    check (out.size() == 1 && s.soundingCount == 1, "la nota sigue sonando tras el salto");

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
    check (s.soundingCount == 0, "el host apago la nota");

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
    juce::MemoryBlock state;
    processor.getStateInformation (state);
    TidechartAudioProcessor restored;
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    check (restored.getParameters().getRawParameterValue (tidechart::params::bypass)->load() > 0.5f, "el bypass viaja con el proyecto");
    restored.setStateInformation ("basura", 6); // un estado corrupto no rompe nada
    check (restored.getParameters().getRawParameterValue (tidechart::params::bypass)->load() > 0.5f, "estado corrupto ignorado");

    // El monitor recibe cada evento de entrada y de salida, y los contadores de notas.
    auto p = makeProcessor (44100.0, 128);
    Rig rig (*p, 2, 128);
    rig.run ({ { juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)), 3 },
               { juce::MidiMessage::controllerEvent (1, 64, 127), 4 } },
             128);
    int in = 0, out = 0;
    p->drainMidiLog ([&] (const tidechart::midi::LoggedEvent& e) { (e.output ? out : in)++; });
    check (in == 2 && out == 2, "monitor: 2 de entrada y 2 de salida");
    check (p->notesIn() == 1 && p->notesOut() == 1, "contadores de notas");

    // El editor se crea y se destruye sin problemas.
    std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
    check (editor != nullptr && editor->getWidth() > 0, "editor");
}
} // namespace

int main()
{
    const juce::ScopedJuceInitialiser_GUI init;

    testPassThrough();
    testNoHungNotes();
    testBypass();
    testTransport();
    testLayoutsAndState();

    if (failures == 0)
        std::printf ("\nTodos los tests del procesador de Tidechart pasaron.\n");
    else
        std::printf ("\n%d comprobaciones fallaron.\n", failures);
    return failures == 0 ? 0 : 1;
}
