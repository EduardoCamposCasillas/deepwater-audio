// Tests de Tidechart sin JUCE: la etapa MIDI (contar lo que suena, apagarlo, passthrough), la cola de eventos hacia la
// GUI y los nombres de nota. Se lanzan con ctest. Si algo falla, imprime el archivo, la línea y la condición.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstddef>
#include <thread>
#include <vector>

#include "midi/EventLog.h"
#include "midi/MidiStage.h"
#include "midi/MidiText.h"
#include "midi/NoteTracker.h"

namespace
{
int failures = 0;

#define CHECK(condition)                                                                   \
    do                                                                                     \
    {                                                                                      \
        if (! (condition))                                                                 \
        {                                                                                  \
            std::printf ("  FALLO %s:%d: %s\n", __FILE__, __LINE__, #condition);           \
            ++failures;                                                                    \
        }                                                                                  \
    } while (false)

using namespace tidechart::midi;

// Generador repetible (misma semilla = misma secuencia).
struct Random
{
    std::uint32_t state = 12345;
    std::uint32_t next() noexcept
    {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    int upTo (int n) noexcept { return static_cast<int> (next() % static_cast<std::uint32_t> (n)); }
};

struct Event
{
    std::vector<std::uint8_t> bytes;
    int sample = 0;
    bool operator== (const Event&) const = default;
};

// El instrumento que recibe la salida: cuenta las notas como lo haría un sinte (note-on suma, note-off resta, CC 123
// apaga el canal). Independiente del NoteTracker, para no probarlo consigo mismo.
struct Receiver
{
    std::array<std::array<int, 128>, 16> held {};

    void receive (const std::uint8_t* data, int size)
    {
        if (size < 3)
            return;
        const int kind = data[0] & 0xf0, channel = data[0] & 0x0f;
        if (kind == 0x90 && data[2] > 0)
            ++held[static_cast<size_t> (channel)][data[1]];
        else if ((kind == 0x80 || kind == 0x90) && held[static_cast<size_t> (channel)][data[1]] > 0)
            --held[static_cast<size_t> (channel)][data[1]];
        else if (kind == 0xb0 && (data[1] == 123 || data[1] == 120))
            held[static_cast<size_t> (channel)].fill (0);
    }

    [[nodiscard]] int sounding() const
    {
        int count = 0;
        for (const auto& channel : held)
            for (const int h : channel)
                count += h > 0 ? 1 : 0;
        return count;
    }
};

// Un evento al azar: casi siempre notas (on, off, on con velocidad 0), a veces CC, pitch bend, CC 123 o SysEx.
Event randomEvent (Random& random, int sample)
{
    const auto channel = static_cast<std::uint8_t> (random.upTo (3)); // pocos canales y notas: muchas repeticiones
    const auto note = static_cast<std::uint8_t> (48 + random.upTo (12));
    switch (random.upTo (20))
    {
        case 0: return { { static_cast<std::uint8_t> (0xb0 | channel), 1, static_cast<std::uint8_t> (random.upTo (128)) }, sample };
        case 1: return { { static_cast<std::uint8_t> (0xe0 | channel), 0, 64 }, sample };
        case 2: return { { static_cast<std::uint8_t> (0xb0 | channel), 123, 0 }, sample };
        case 3: return { { 0xf0, 0x7d, 0x01, 0x02, 0xf7 }, sample };
        case 4: case 5: return { { static_cast<std::uint8_t> (0x90 | channel), note, 0 }, sample };
        default:
            if (random.upTo (2) == 0)
                return { { static_cast<std::uint8_t> (0x90 | channel), note, static_cast<std::uint8_t> (1 + random.upTo (127)) }, sample };
            return { { static_cast<std::uint8_t> (0x80 | channel), note, 64 }, sample };
    }
}

void testShortMessage()
{
    std::printf ("Mensajes cortos: note-on con velocidad 0 = note-off, CC 120/123, SysEx y bytes invalidos\n");

    const std::uint8_t on[] { 0x93, 60, 100 }, onZero[] { 0x93, 60, 0 }, off[] { 0x83, 60, 64 }, cc123[] { 0xb2, 123, 0 },
        cc120[] { 0xb2, 120, 0 }, cc1[] { 0xb2, 1, 5 }, sysex[] { 0xf0, 1, 0xf7 }, data[] { 60, 100 };
    CHECK (readShortMessage (on, 3).isNoteOn() && readShortMessage (on, 3).channel() == 3);
    CHECK (readShortMessage (onZero, 3).isNoteOff() && ! readShortMessage (onZero, 3).isNoteOn());
    CHECK (readShortMessage (off, 3).isNoteOff());
    CHECK (readShortMessage (cc123, 3).isAllNotesOff() && readShortMessage (cc120, 3).isAllNotesOff());
    CHECK (! readShortMessage (cc1, 3).isAllNotesOff());
    CHECK (readShortMessage (sysex, 3).status == 0);
    CHECK (readShortMessage (data, 2).status == 0);   // sin byte de estado
    CHECK (readShortMessage (nullptr, 3).status == 0);
    CHECK (readShortMessage (on, 0).status == 0);
    const auto noteOff = ShortMessage::noteOff (15, 127);
    CHECK (noteOff.status == 0x8f && noteOff.data1 == 127 && noteOff.data2 == 0);
}

void testNoteTracker()
{
    std::printf ("NoteTracker: cuenta por canal y nota, notas repetidas, CC 123 y apagar todo\n");

    NoteTracker tracker;
    tracker.noteOn (0, 60);
    tracker.noteOn (0, 60); // la misma tecla dos veces (dos notas que se solapan)
    tracker.noteOn (1, 60); // la misma nota en otro canal es otra tecla
    tracker.noteOn (0, 64);
    CHECK (tracker.soundingCount() == 3);
    CHECK (tracker.count (0, 60) == 2);
    CHECK (tracker.noteOff (0, 60));
    CHECK (tracker.isSounding (0, 60)); // falta el segundo note-off
    CHECK (! tracker.noteOff (5, 70));  // nunca sonó: no cuenta
    CHECK (tracker.soundingCount() == 3);

    std::vector<ShortMessage> sent;
    CHECK (tracker.releaseAll ([&] (ShortMessage m) { sent.push_back (m); }) == 3);
    CHECK (sent.size() == 3);
    for (const auto& m : sent)
        CHECK (m.isNoteOff());
    CHECK (tracker.soundingCount() == 0);
    CHECK (tracker.releaseAll ([&] (ShortMessage) { CHECK (false); }) == 0);

    tracker.observe ({ 0x92, 40, 90 });
    tracker.observe ({ 0x92, 41, 90 });
    tracker.observe ({ 0x93, 41, 90 });
    tracker.observe ({ 0xb2, 123, 0 }); // All Notes Off solo en el canal 3
    CHECK (tracker.soundingCount() == 1 && tracker.isSounding (3, 41));
    tracker.observe ({ 0x93, 41, 0 }); // note-on con velocidad 0
    CHECK (tracker.soundingCount() == 0);

    // 300 note-on de la misma tecla: la cuenta se satura en 255 y no da la vuelta a 0.
    for (int i = 0; i < 300; ++i)
        tracker.noteOn (9, 36);
    CHECK (tracker.count (9, 36) == 255 && tracker.soundingCount() == 1);
}

void testPassThroughAndRelease()
{
    std::printf ("MidiStage: todo pasa igual y en su muestra; apagar deja al receptor sin notas (10 semillas)\n");

    for (std::uint32_t seed = 1; seed <= 10; ++seed)
    {
        Random random { seed * 7919u };
        MidiStage stage;
        Receiver receiver;
        bool identical = true;
        bool matchesTracker = true;

        for (int block = 0; block < 2000; ++block)
        {
            const int numSamples = 1 + random.upTo (512);
            std::vector<Event> in, out;
            const int events = random.upTo (6);
            int sample = 0;
            for (int e = 0; e < events; ++e)
            {
                sample = std::min (numSamples - 1, sample + random.upTo (numSamples));
                in.push_back (randomEvent (random, sample));
            }

            auto sink = [&] (const std::uint8_t* data, int size, int at) {
                out.push_back ({ std::vector<std::uint8_t> (data, data + size), at });
                receiver.receive (data, size);
            };
            const bool release = random.upTo (100) == 0;
            if (release)
                stage.requestRelease();
            stage.beginBlock (sink);
            const size_t released = out.size();
            if (release)
                matchesTracker = matchesTracker && receiver.sounding() == 0;
            for (const auto& event : in)
                stage.process (event.bytes.data(), static_cast<int> (event.bytes.size()), event.sample, sink);

            identical = identical && std::vector<Event> (out.begin() + static_cast<std::ptrdiff_t> (released), out.end()) == in;
            matchesTracker = matchesTracker && receiver.sounding() == stage.getSounding().soundingCount();
        }
        CHECK (identical);
        CHECK (matchesTracker);

        // Fin: se apaga todo y el receptor no tiene ninguna nota.
        stage.requestRelease();
        int offs = 0;
        stage.beginBlock ([&] (const std::uint8_t* data, int size, int at) {
            CHECK (at == 0);
            CHECK ((data[0] & 0xf0) == 0x80);
            receiver.receive (data, size);
            ++offs;
        });
        CHECK (receiver.sounding() == 0);
        CHECK (stage.getSounding().soundingCount() == 0);
        CHECK (offs == 0 || stage.releaseCount() > 0);
    }
}

void testRepeatedNoteNeedsTwoOffs()
{
    std::printf ("MidiStage: una tecla que sono dos veces recibe dos note-off al apagar\n");

    MidiStage stage;
    std::vector<Event> out;
    auto sink = [&] (const std::uint8_t* data, int size, int at) { out.push_back ({ std::vector<std::uint8_t> (data, data + size), at }); };
    const std::uint8_t on[] { 0x90, 60, 100 };
    stage.process (on, 3, 10, sink);
    stage.process (on, 3, 20, sink);
    out.clear();
    stage.requestRelease();
    stage.beginBlock (sink);
    CHECK (out.size() == 2);
    CHECK (stage.releaseCount() == 1);

    // Apagar sin nada sonando (el host reinicia el audio a menudo) no manda nada ni cuenta.
    MidiStage idle;
    idle.requestRelease();
    idle.beginBlock ([&] (const std::uint8_t*, int, int) { CHECK (false); });
    CHECK (idle.releaseCount() == 0);

    // Sin petición, beginBlock no manda nada.
    out.clear();
    stage.beginBlock (sink);
    CHECK (out.empty());
    CHECK (stage.releaseCount() == 1);
}

void testPanic()
{
    std::printf ("Panico: note-off de lo que suena y All Notes Off en los 16 canales, en la muestra 0\n");

    MidiStage stage;
    Receiver receiver;
    std::vector<Event> out;
    auto sink = [&] (const std::uint8_t* data, int size, int at) {
        out.push_back ({ std::vector<std::uint8_t> (data, data + size), at });
        receiver.receive (data, size);
    };
    const std::uint8_t a[] { 0x90, 60, 100 }, b[] { 0x95, 67, 100 };
    stage.process (a, 3, 0, sink);
    stage.process (b, 3, 5, sink);
    out.clear();

    stage.requestPanic();
    stage.requestRelease(); // las dos a la vez: una sola tanda
    stage.beginBlock (sink);
    CHECK (out.size() == 2 + 16);
    int allNotesOff = 0;
    for (const auto& event : out)
    {
        CHECK (event.sample == 0);
        if (event.bytes[0] >= 0xb0 && event.bytes[0] <= 0xbf && event.bytes[1] == 123)
            ++allNotesOff;
    }
    CHECK (allNotesOff == 16);
    CHECK (receiver.sounding() == 0);
    CHECK (stage.getSounding().soundingCount() == 0);
    CHECK (stage.releaseCount() == 1);
}

void testRequestsFromAnotherThread()
{
    std::printf ("Peticiones desde otro hilo (GUI) mientras el audio procesa: ninguna se pierde, nada queda colgado\n");

    MidiStage stage;
    Receiver receiver;
    std::atomic<bool> running { true };
    std::atomic<int> requested { 0 };
    std::thread gui ([&] {
        for (int i = 0; i < 2000 && running.load(); ++i)
        {
            if (i % 2 == 0)
                stage.requestRelease();
            else
                stage.requestPanic();
            requested.fetch_add (1);
            std::this_thread::yield();
        }
    });

    Random random { 99 };
    auto sink = [&] (const std::uint8_t* data, int size, int) { receiver.receive (data, size); };
    for (int block = 0; block < 20000; ++block)
    {
        stage.beginBlock (sink);
        for (int e = 0; e < 3; ++e)
        {
            const auto event = randomEvent (random, 0);
            stage.process (event.bytes.data(), static_cast<int> (event.bytes.size()), 0, sink);
        }
    }
    running.store (false);
    gui.join();

    stage.requestRelease();
    stage.beginBlock (sink);
    CHECK (receiver.sounding() == 0);
    CHECK (stage.releaseCount() >= 1);
    CHECK (stage.releaseCount() <= static_cast<std::uint32_t> (requested.load()) + 1);
}

void testEventLog()
{
    std::printf ("EventLog: orden, lleno = descarta sin esperar, y un productor y un consumidor en hilos distintos\n");

    EventLog<8> log;
    for (int i = 0; i < 10; ++i)
        log.push ({ ShortMessage { 0x90, static_cast<std::uint8_t> (i), 100 }, 3, false, static_cast<double> (i) });
    CHECK (log.droppedCount() == 2);
    std::vector<int> seen;
    CHECK (log.drain ([&] (const LoggedEvent& e) { seen.push_back (e.message.data1); }) == 8);
    CHECK ((seen == std::vector<int> { 0, 1, 2, 3, 4, 5, 6, 7 }));
    CHECK (log.drain ([&] (const LoggedEvent&) {}) == 0);

    // En hilos: el consumidor ve una secuencia creciente sin huecos salvo los descartados.
    EventLog<64> shared;
    constexpr int total = 200000;
    std::thread producer ([&] {
        for (int i = 0; i < total; ++i)
            shared.push ({ ShortMessage {}, 0, true, static_cast<double> (i) });
    });
    double last = -1.0;
    bool increasing = true;
    int received = 0;
    while (received + static_cast<int> (shared.droppedCount()) < total)
        received += shared.drain ([&] (const LoggedEvent& e) {
            increasing = increasing && e.ppq > last;
            last = e.ppq;
        });
    producer.join();
    received += shared.drain ([&] (const LoggedEvent& e) {
        increasing = increasing && e.ppq > last;
        last = e.ppq;
    });
    CHECK (increasing);
    CHECK (received + static_cast<int> (shared.droppedCount()) == total);
}

void testNoteNames()
{
    std::printf ("Nombres como FL (C5 = 60, A5 = 69) y descripciones del monitor\n");

    CHECK (flNoteName (60) == "C5");
    CHECK (flNoteName (69) == "A5");
    CHECK (flNoteName (0) == "C0");
    CHECK (flNoteName (61) == "C#5");
    CHECK (flNoteName (127) == "G10");
    CHECK (flNoteName (36) == "C3"); // el kick del General MIDI
    CHECK (describe ({ 0x90, 60, 100 }, 3) == "Note on   C5 (60)  vel 100");
    CHECK (describe ({ 0x90, 60, 0 }, 3) == "Note off  C5 (60)");
    CHECK (describe ({ 0xb0, 123, 0 }, 3) == "CC 123 (All Notes Off) = 0");
    CHECK (describe ({ 0xe0, 0, 64 }, 3) == "Pitch bend 0");
    CHECK (describe ({}, 12) == "SysEx (12 bytes)");
}
} // namespace

int main()
{
    testShortMessage();
    testNoteTracker();
    testPassThroughAndRelease();
    testRepeatedNoteNeedsTwoOffs();
    testPanic();
    testRequestsFromAnotherThread();
    testEventLog();
    testNoteNames();

    if (failures == 0)
        std::printf ("\nTodos los tests de Tidechart pasaron.\n");
    else
        std::printf ("\n%d comprobaciones fallaron.\n", failures);
    return failures == 0 ? 0 : 1;
}
