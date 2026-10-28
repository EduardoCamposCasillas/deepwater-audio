// Tests de Tidechart sin JUCE: la etapa MIDI (contar lo que suena, apagarlo, passthrough, un acorde por tecla), la cola
// de eventos hacia la GUI y los nombres de nota. Se lanzan con ctest. Si algo falla, imprime el archivo, la línea y la condición.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstddef>
#include <optional>
#include <thread>
#include <vector>

#include "deepwater/dsp/HostClock.h"
#include "deepwater/theory/Progression.h"
#include "deepwater/theory/Text.h"
#include "midi/ChordTrigger.h"
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

// --- Fase 3: un acorde por tecla ------------------------------------------------------------------------------------

namespace th = deepwater::theory;

std::vector<int> notesOf (const std::optional<TriggeredChord>& chord)
{
    if (! chord)
        return {};
    std::vector<int> notes (chord->notes.begin(), chord->notes.end());
    std::sort (notes.begin(), notes.end());
    return notes;
}

using Notes = std::vector<int>;

void testChordForKey()
{
    std::printf ("Acordes por tecla: teclas blancas = grados, notas de la escala, septimas, bajo, cualquier tonalidad\n");

    ChordSettings am; // La menor, tríadas, teclas blancas
    am.enabled = true;
    CHECK (notesOf (chordForKey (60, am)) == (Notes { 57, 60, 64 })); // C5 → i = Am (A4 C5 E5)
    CHECK (notesOf (chordForKey (62, am)) == (Notes { 59, 62, 65 })); // D5 → ii° = Bdim
    CHECK (notesOf (chordForKey (67, am)) == (Notes { 64, 67, 71 })); // G5 → v = Em
    CHECK (notesOf (chordForKey (71, am)) == (Notes { 67, 71, 74 })); // B5 → VII = G
    CHECK (notesOf (chordForKey (48, am)) == (Notes { 45, 48, 52 })); // una octava abajo, lo mismo
    CHECK (th::chordSymbol (chordForKey (65, am)->chord) == "Dm");    // F5 → iv
    CHECK (th::chordSymbol (chordForKey (69, am)->chord) == "F");     // A5 → VI
    CHECK (chordForKey (69, am)->degree == 5);
    CHECK (! chordForKey (61, am)); // negra: nada
    CHECK (! chordForKey (70, am));

    auto am7 = am;
    am7.size = th::ChordSize::seventh;
    CHECK (notesOf (chordForKey (60, am7)) == (Notes { 57, 60, 64, 67 }));     // Am7
    CHECK (notesOf (chordForKey (71, am7)) == (Notes { 67, 71, 74, 77 }));     // G7 (el ♭VII7)
    CHECK (th::chordSymbol (chordForKey (62, am7)->chord) == "Bm7b5");
    auto bass = am7;
    bass.bass = true;
    CHECK (notesOf (chordForKey (60, bass)) == (Notes { 45, 57, 60, 64, 67 })); // A3 abajo

    ChordSettings c;
    c.enabled = true;
    c.key = { th::notes::C, th::Mode::ionian };
    CHECK (notesOf (chordForKey (60, c)) == (Notes { 60, 64, 67 })); // C
    CHECK (notesOf (chordForKey (71, c)) == (Notes { 71, 74, 77 })); // vii° = Bdim

    // F♯ mayor: la tónica queda debajo del Do (F♯4), no una octava arriba.
    ChordSettings fs;
    fs.enabled = true;
    fs.key = th::standardKey (6, th::Mode::ionian);
    CHECK (notesOf (chordForKey (60, fs)) == (Notes { 54, 58, 61 }));
    CHECK (th::chordSymbol (chordForKey (60, fs)->chord) == "F#");

    // Notas de la escala, en Mi frigio: F = el ♭II mayor con la fundamental en la tecla; lo de fuera, nada.
    ChordSettings phrygian;
    phrygian.enabled = true;
    phrygian.key = { th::notes::E, th::Mode::phrygian };
    phrygian.mapping = KeyMapping::scaleNotes;
    CHECK (notesOf (chordForKey (64, phrygian)) == (Notes { 64, 67, 71 })); // Em
    CHECK (notesOf (chordForKey (65, phrygian)) == (Notes { 65, 69, 72 })); // F
    CHECK (chordForKey (65, phrygian)->degree == 1);
    CHECK (! chordForKey (66, phrygian)); // F♯ no está en Mi frigio

    // Todas las tonalidades, modos, tamaños, modos de tecla y bajo, en las 128 teclas: notas dentro de 0..127, las
    // clases de altura del acorde, y el grado que dice cada modo de tecla.
    bool ok = true;
    for (int tonic = 0; tonic < 12; ++tonic)
        for (int m = 0; m < th::modeCount; ++m)
            for (int variant = 0; variant < 8; ++variant)
            {
                ChordSettings s;
                s.enabled = true;
                s.key = th::standardKey (tonic, static_cast<th::Mode> (m));
                s.size = (variant & 1) != 0 ? th::ChordSize::seventh : th::ChordSize::triad;
                s.mapping = (variant & 2) != 0 ? KeyMapping::scaleNotes : KeyMapping::whiteKeys;
                s.bass = (variant & 4) != 0;
                for (int note = 0; note < 128; ++note)
                {
                    const auto chord = chordForKey (note, s);
                    const int expectedDegree = s.mapping == KeyMapping::whiteKeys ? whiteKeyDegree (note) : s.key.degreeOf (note % 12);
                    if (! chord)
                    {
                        // Solo puede faltar si la tecla no es un grado o si el acorde entero cae fuera de 0..127.
                        ok = ok && (expectedDegree < 0 || note < 12 || note > 115);
                        continue;
                    }
                    ok = ok && chord->degree == expectedDegree;
                    const auto mask = th::chordMask (chord->chord.root.pitchClass(), chord->chord.type);
                    for (const int n : chord->notes)
                        ok = ok && n >= 0 && n < 128 && (mask & (1u << (n % 12))) != 0;
                    ok = ok && chord->chord == *th::diatonicChord (s.key, chord->degree, s.size);
                    if (s.mapping == KeyMapping::scaleNotes && ! s.bass)
                        ok = ok && chord->notes[0] == note; // la fundamental, en la tecla pulsada
                }
            }
    CHECK (ok);
}

// Un receptor que además recuerda cuántos note-on seguidos recibió una nota (el máximo): con los acordes activos
// nunca debe pasar de 1, o un note-off apagaría una nota que otro acorde sigue usando.
struct ChordRig
{
    MidiStage stage;
    Receiver receiver;
    std::vector<Event> out;
    int maxPerNote = 0;

    void sink (const std::uint8_t* data, int size, int at)
    {
        out.push_back ({ std::vector<std::uint8_t> (data, data + size), at });
        receiver.receive (data, size);
        if (size == 3 && (data[0] & 0xf0) == 0x90 && data[2] > 0 && stage.getChordSettings().enabled)
            maxPerNote = std::max (maxPerNote, receiver.held[static_cast<size_t> (data[0] & 0x0f)][data[1]]);
    }
    void begin (const ChordSettings& settings)
    {
        out.clear();
        stage.setChordSettings (settings);
        stage.beginBlock ([this] (const std::uint8_t* d, int s, int at) { sink (d, s, at); });
    }
    void send (std::vector<std::uint8_t> bytes, int at = 0)
    {
        stage.process (bytes.data(), static_cast<int> (bytes.size()), at, [this] (const std::uint8_t* d, int s, int t) { sink (d, s, t); });
    }
};

Event on (int note, int at, int velocity = 100) { return { { 0x90, static_cast<std::uint8_t> (note), static_cast<std::uint8_t> (velocity) }, at }; }
Event off (int note, int at) { return { { 0x80, static_cast<std::uint8_t> (note), 0 }, at }; }

void testChordStage()
{
    std::printf ("MidiStage con acordes: una tecla = un acorde, notas compartidas, cambiar ajustes con la tecla abajo, "
                 "bypass, paleta y apagar\n");

    ChordSettings am;
    am.enabled = true;
    ChordRig rig;
    rig.begin (am);
    CHECK (rig.out.empty());

    // C5 → Am: tres note-on en la muestra de la tecla, con su velocidad; la tecla misma no sale aparte.
    rig.send ({ 0x90, 60, 90 }, 5);
    CHECK (rig.out == (std::vector<Event> { { { 0x90, 57, 90 }, 5 }, { { 0x90, 60, 90 }, 5 }, { { 0x90, 64, 90 }, 5 } }));
    CHECK (rig.stage.getChords().heldDegrees() == 0b1);

    // E5 → C (C E G) con Am todavía abajo: C y E se vuelven a atacar (off + on), G entra.
    rig.out.clear();
    rig.send ({ 0x90, 64, 100 }, 6);
    CHECK (rig.out == (std::vector<Event> { off (60, 6), on (60, 6), off (64, 6), on (64, 6), on (67, 6) }));
    CHECK (rig.stage.getChords().heldDegrees() == 0b101);

    // Soltar C5: solo se apaga A (C y E las sigue usando el acorde de C).
    rig.out.clear();
    rig.send ({ 0x80, 60, 0 }, 7);
    CHECK (rig.out == (std::vector<Event> { off (57, 7) }));
    rig.out.clear();
    rig.send ({ 0x90, 64, 0 }, 8); // note-on con velocidad 0 = note-off
    CHECK (rig.out == (std::vector<Event> { off (60, 8), off (64, 8), off (67, 8) }));
    CHECK (rig.receiver.sounding() == 0 && rig.stage.getChords().activeCount() == 0);

    // Cambiar la tonalidad con la tecla abajo: al soltar se apagan las notas que sonaron, no las del acorde nuevo.
    rig.send ({ 0x90, 60, 100 });
    auto cMajor = am;
    cMajor.key = { th::notes::C, th::Mode::ionian };
    cMajor.size = th::ChordSize::seventh;
    rig.begin (cMajor);
    CHECK (rig.out.empty());
    rig.send ({ 0x80, 60, 0 }, 3);
    CHECK (rig.out == (std::vector<Event> { off (57, 3), off (60, 3), off (64, 3) }));

    // Negras: nada. Lo que no es una nota pasa igual.
    rig.out.clear();
    rig.send ({ 0x90, 61, 100 });
    rig.send ({ 0x80, 61, 0 });
    CHECK (rig.out.empty());
    rig.send ({ 0xb0, 64, 127 }, 9);
    CHECK (rig.out == (std::vector<Event> { { { 0xb0, 64, 127 }, 9 } }));

    // La misma tecla dos veces sin soltar: el acorde se repite (no se duplica) y un note-off lo apaga.
    rig.begin (am);
    rig.send ({ 0x90, 60, 100 });
    rig.send ({ 0x90, 60, 80 });
    rig.send ({ 0x80, 60, 0 });
    CHECK (rig.receiver.sounding() == 0);
    rig.out.clear();
    rig.send ({ 0x80, 60, 0 }); // el segundo note-off no hace nada
    CHECK (rig.out.empty());

    // Bypass: entrar apaga los acordes en la muestra 0; en bypass las teclas pasan tal cual; salir apaga esas.
    rig.send ({ 0x90, 60, 100 });
    auto bypass = am;
    bypass.enabled = false;
    rig.begin (bypass);
    CHECK (rig.out == (std::vector<Event> { off (57, 0), off (60, 0), off (64, 0) }));
    rig.out.clear();
    rig.send ({ 0x90, 62, 70 }, 4);
    CHECK (rig.out == (std::vector<Event> { { { 0x90, 62, 70 }, 4 } }));
    rig.begin (am);
    CHECK (rig.out == (std::vector<Event> { off (62, 0) }));
    rig.out.clear();
    rig.send ({ 0x80, 62, 0 });
    rig.send ({ 0x80, 60, 0 });
    CHECK (rig.out.empty());
    CHECK (rig.receiver.sounding() == 0);

    // La paleta: suena en la muestra 0 del bloque siguiente, en el canal 1, en la octava del C5; cambiar de grado
    // suelta el anterior; −1 lo apaga. En bypass no suena.
    rig.stage.requestPreview (0);
    rig.begin (am);
    CHECK (rig.out == (std::vector<Event> { on (57, 0), on (60, 0), on (64, 0) }));
    rig.stage.requestPreview (4);
    rig.begin (am);
    CHECK (rig.out == (std::vector<Event> { off (57, 0), off (60, 0), off (64, 0), on (64, 0), on (67, 0), on (71, 0) }));
    CHECK (rig.stage.getChords().heldDegrees() == 0b10000);
    rig.begin (am);
    CHECK (rig.out.empty()); // sin cambios, nada
    rig.stage.requestPreview (-1);
    rig.begin (am);
    CHECK (rig.out == (std::vector<Event> { off (64, 0), off (67, 0), off (71, 0) }));
    rig.stage.requestPreview (2);
    rig.begin (bypass);
    CHECK (rig.out.empty());
    rig.stage.requestPreview (-1);

    // Apagar (reinicio del audio): todo en la muestra 0 y los acordes se olvidan; el note-off tardío no manda nada.
    rig.begin (am);
    rig.send ({ 0x90, 60, 100 });
    rig.send ({ 0x91, 67, 100 });
    rig.stage.requestRelease();
    rig.begin (am);
    CHECK (rig.out.size() == 6);
    CHECK (rig.receiver.sounding() == 0 && rig.stage.getChords().activeCount() == 0);
    rig.out.clear();
    rig.send ({ 0x80, 60, 0 });
    CHECK (rig.out.empty());
    CHECK (rig.maxPerNote == 1);
}

ChordSettings randomSettings (Random& random, bool enabled)
{
    ChordSettings s;
    s.enabled = enabled;
    s.key = th::standardKey (random.upTo (12), static_cast<th::Mode> (random.upTo (th::modeCount)));
    s.size = random.upTo (2) == 0 ? th::ChordSize::triad : th::ChordSize::seventh;
    s.mapping = random.upTo (2) == 0 ? KeyMapping::whiteKeys : KeyMapping::scaleNotes;
    s.bass = random.upTo (2) == 0;
    return s;
}

void testChordStageRandom()
{
    std::printf ("MidiStage con acordes al azar (10 semillas): ajustes, bypass, paleta y apagados cambiando a mitad de "
                 "las notas; nunca dos note-on seguidos de la misma nota y, al soltar todo, silencio\n");

    for (std::uint32_t seed = 1; seed <= 10; ++seed)
    {
        Random random { seed * 104729u };
        ChordRig rig;
        auto settings = randomSettings (random, true);
        bool matchesTracker = true;

        for (int block = 0; block < 3000; ++block)
        {
            if (random.upTo (40) == 0)
                settings = randomSettings (random, settings.enabled);
            if (random.upTo (150) == 0)
                settings.enabled = ! settings.enabled;
            if (random.upTo (30) == 0)
                rig.stage.requestPreview (random.upTo (8) - 1);
            if (random.upTo (300) == 0)
                rig.stage.requestRelease();
            if (random.upTo (600) == 0)
                rig.stage.requestPanic();
            rig.begin (settings);
            const int events = random.upTo (5);
            for (int e = 0; e < events; ++e)
            {
                const auto event = randomEvent (random, e);
                rig.send (event.bytes, event.sample);
            }
            matchesTracker = matchesTracker && rig.receiver.sounding() == rig.stage.getSounding().soundingCount();
        }
        CHECK (matchesTracker);
        CHECK (rig.maxPerNote <= 1);

        // Fin: acordes activos, todas las teclas sueltas, la paleta suelta. No hace falta pedir que se apague nada.
        settings.enabled = true;
        rig.stage.requestPreview (-1);
        rig.begin (settings);
        for (int channel = 0; channel < 3; ++channel)
            for (int note = 48; note < 60; ++note)
                rig.send ({ static_cast<std::uint8_t> (0x80 | channel), static_cast<std::uint8_t> (note), 0 });
        CHECK (rig.stage.getChords().activeCount() == 0);
        CHECK (rig.receiver.sounding() == 0);
        CHECK (rig.stage.getSounding().soundingCount() == 0);
    }
}

// --- Fase 4: voicing, ritmo y progresión en el tiempo ---------------------------------------------------------------

th::Progression progressionOf (const char* text, std::vector<int> beats = {})
{
    th::Progression p;
    if (*text == 0)
        return p;
    const auto numerals = th::parseProgression (text);
    CHECK (numerals.has_value());
    if (numerals)
        for (std::size_t i = 0; i < numerals->size(); ++i)
            p.steps.push_back ({ (*numerals)[i], i < beats.size() ? beats[i] : 4 });
    return p;
}

// La etapa con un host: un reloj que reproduce a tempo constante (o parado) y bloques del tamaño que se pida. Todo en
// muestras absolutas. A 120 BPM y 48 kHz una semicorchea son 6000 muestras exactas.
struct TimedRig
{
    MidiStage stage;
    deepwater::dsp::HostClock clock;
    Receiver receiver;
    std::vector<Event> out; // con muestras absolutas
    long long position = 0; // muestra absoluta del principio del bloque siguiente
    double bpm = 120.0;
    double ppq = 0.0;       // posición del host al principio del bloque siguiente
    bool playing = true;
    int maxPerNote = 0;

    TimedRig() { clock.prepare (48000.0); }

    void sink (const std::uint8_t* data, int size, int at)
    {
        out.push_back ({ std::vector<std::uint8_t> (data, data + size), static_cast<int> (position + at) });
        receiver.receive (data, size);
        if (size == 3 && (data[0] & 0xf0) == 0x90 && data[2] > 0 && stage.getChordSettings().enabled)
            maxPerNote = std::max (maxPerNote, receiver.held[static_cast<size_t> (data[0] & 0x0f)][data[1]]);
    }

    // Un bloque de n muestras con estos eventos (muestras relativas al bloque).
    void run (int n, const ChordSettings& settings, const std::vector<Event>& events = {})
    {
        deepwater::dsp::TransportInfo info;
        info.hasHost = true;
        info.isPlaying = playing;
        info.bpm = bpm;
        info.ppqPosition = ppq;
        clock.beginBlock (info, n);
        auto s = [this] (const std::uint8_t* d, int size, int at) { sink (d, size, at); };
        stage.setChordSettings (settings);
        stage.beginBlock (s, &clock);
        for (const auto& e : events)
            stage.process (e.bytes.data(), static_cast<int> (e.bytes.size()), e.sample, s);
        stage.endBlock (n, s);
        position += n;
        if (playing)
            ppq += n * bpm / (60.0 * 48000.0);
    }

    // Varios bloques de tamaños al azar hasta 'until' (muestra absoluta); 'events' en muestras absolutas.
    void runUntil (long long until, const ChordSettings& settings, Random& random, const std::vector<Event>& events = {})
    {
        while (position < until)
        {
            const int n = static_cast<int> (std::min<long long> (1 + random.upTo (4096), until - position));
            std::vector<Event> inBlock;
            for (const auto& e : events)
                if (e.sample >= position && e.sample < position + n)
                    inBlock.push_back ({ e.bytes, static_cast<int> (e.sample - position) });
            run (n, settings, inBlock);
        }
    }

    [[nodiscard]] std::vector<int> soundingNotes() const
    {
        std::vector<int> notes;
        for (const auto& channel : receiver.held)
            for (int n = 0; n < 128; ++n)
                if (channel[static_cast<size_t> (n)] > 0)
                    notes.push_back (n);
        return notes;
    }
};

ChordSettings progressionSettings (th::VoicingStyle voicing, int rhythm, bool bass = false)
{
    ChordSettings s;
    s.enabled = true;
    s.playMode = PlayMode::progression;
    s.voicing = voicing;
    s.rhythm = rhythm;
    s.bass = bass;
    return s;
}

void testKeysWithVoicing()
{
    std::printf ("Teclas con conduccion de voces: Am -> F con A y C quietas, el bajo en su sitio, la paleta tambien; "
                 "estado fundamental = Fase 3\n");

    ChordSettings led;
    led.enabled = true;
    led.voicing = th::VoicingStyle::voiceLed;
    ChordRig rig;
    rig.begin (led);
    rig.send ({ 0x90, 60, 100 }); // C5 → Am (A4 C5 E5)
    rig.send ({ 0x80, 60, 0 });
    rig.out.clear();
    rig.send ({ 0x90, 69, 100 }, 4); // A5 → F: en estado fundamental F5 A5 C6; con conducción, A4 C5 F5
    CHECK (rig.out == (std::vector<Event> { on (57, 4), on (60, 4), on (65, 4) }));
    rig.send ({ 0x80, 69, 0 });

    // Con bajo: la fundamental una octava debajo de donde iría (F4), las voces por encima.
    auto withBass = led;
    withBass.bass = true;
    rig.begin (withBass);
    rig.out.clear();
    rig.send ({ 0x90, 67, 100 }); // G5 → Em; desde F (A4 C5 F5) las voces van a G4 B4 E5, el bajo E4
    std::vector<int> notes;
    for (const auto& e : rig.out)
        notes.push_back (e.bytes[1]);
    std::sort (notes.begin(), notes.end());
    CHECK ((notes == std::vector<int> { 52, 55, 59, 64 }));
    rig.send ({ 0x80, 67, 0 });

    // La paleta también conduce las voces desde lo último que sonó.
    rig.begin (led);
    rig.stage.requestPreview (5); // F: desde Em (G4 B4 E5) → A4 C5 F5
    rig.out.clear();
    rig.begin (led);
    CHECK (rig.out == (std::vector<Event> { on (57, 0), on (60, 0), on (65, 0) }));
    rig.stage.requestPreview (-1);
    rig.begin (led);
    CHECK (rig.receiver.sounding() == 0);

    // Un acorde prestado desde la paleta: ♭II (B♭, del frigio) en La menor, fuera de la escala: no enciende ningún pad.
    rig.stage.requestPreviewChord ({ 2, -1, th::chordType ("") });
    rig.begin (ChordSettings { true });
    std::vector<int> borrowed;
    for (const auto& e : rig.out)
        if ((e.bytes[0] & 0xf0) == 0x90 && e.bytes[2] > 0)
            borrowed.push_back (e.bytes[1]);
    CHECK ((borrowed == std::vector<int> { 58, 62, 65 })); // B♭4 D5 F5
    CHECK (rig.stage.getChords().heldDegrees() == 0);
    rig.stage.requestPreview (-1);
    rig.begin (ChordSettings { true });
    CHECK (rig.receiver.sounding() == 0 && rig.maxPerNote == 1);

    // Los códigos de la paleta van y vuelven para todos los grados, alteraciones y tipos.
    bool codes = true;
    for (int degree = 1; degree <= 7; ++degree)
        for (int accidental = -2; accidental <= 2; ++accidental)
            for (int type = 0; type < th::chordTypeCount; ++type)
            {
                const th::RomanNumeral n { degree, accidental, static_cast<th::ChordTypeId> (type) };
                codes = codes && numeralFromCode (previewCode (n)) == n && previewCode (n) >= numeralCodeBase;
            }
    CHECK (codes);
}

// Las notas que salieron como (muestra, nota, velocidad) de los note-on, y (muestra, nota) de los note-off.
struct NoteLists
{
    std::vector<std::array<int, 3>> ons;
    std::vector<std::array<int, 2>> offs;
};

NoteLists split (const std::vector<Event>& events, int before)
{
    NoteLists lists;
    for (const auto& e : events)
    {
        if (e.sample >= before || e.bytes.size() != 3)
            continue;
        const int kind = e.bytes[0] & 0xf0;
        if (kind == 0x90 && e.bytes[2] > 0)
            lists.ons.push_back ({ e.sample, e.bytes[1], e.bytes[2] });
        else if (kind == 0x80 || kind == 0x90)
            lists.offs.push_back ({ e.sample, e.bytes[1] });
    }
    std::sort (lists.ons.begin(), lists.ons.end());
    std::sort (lists.offs.begin(), lists.offs.end());
    return lists;
}

void testProgressionMatchesRender()
{
    std::printf ("Progresion en vivo = renderProgression: cada ataque y cada note-off en su muestra exacta, con bloques "
                 "al azar, en los 9 ritmos y los 3 voicings, con y sin bajo\n");

    constexpr int sixteenth = 6000; // muestras a 120 BPM y 48 kHz
    const auto progression = progressionOf ("i bVI bIII bVII7", { 4, 2, 2, 8 });
    const th::Key aMinor { th::notes::A, th::Mode::aeolian };
    Random random { 777 };
    for (int rhythm = 0; rhythm < th::rhythmPatternCount; ++rhythm)
        for (int style = 0; style < th::voicingStyleCount; ++style)
            for (const bool bass : { false, true })
            {
                const auto settings = progressionSettings (static_cast<th::VoicingStyle> (style), rhythm, bass);
                TimedRig rig;
                rig.stage.setProgression (progression);
                const long long length = 2LL * progression.totalBeats() * 4 * sixteenth; // dos vueltas
                rig.runUntil (length + 100, settings, random, { on (60, 0, 110), off (60, static_cast<int> (length)) });

                th::RenderOptions options;
                options.style = settings.voicing;
                options.tonicNote = 57;
                options.bass = bass;
                options.rhythm = rhythm;
                options.velocity = 110;
                options.loops = 2;
                std::vector<Event> expected;
                for (const auto& e : th::renderProgression (progression, aMinor, options))
                {
                    const int start = static_cast<int> (e.start * 4 * sixteenth + 0.5);
                    const int end = static_cast<int> ((e.start + e.length) * 4 * sixteenth + 0.5);
                    expected.push_back (on (e.note, start, e.velocity));
                    if (end < length)
                        expected.push_back (off (e.note, end));
                }
                // Lo que suena al soltar la tecla (en 'length') se apaga ahí: se compara antes.
                const auto got = split (rig.out, static_cast<int> (length));
                const auto want = split (expected, static_cast<int> (length));
                const bool same = got.ons == want.ons && got.offs == want.offs && got.ons.size() >= 2 * 4 * 3 && ! got.offs.empty();
                if (! same)
                    std::printf ("    ritmo %d, voicing %d, bajo %d: %d/%d note-on, %d/%d note-off\n", rhythm, style, bass ? 1 : 0,
                                 static_cast<int> (got.ons.size()), static_cast<int> (want.ons.size()), static_cast<int> (got.offs.size()),
                                 static_cast<int> (want.offs.size()));
                CHECK (same);
                CHECK (rig.receiver.sounding() == 0 && rig.maxPerNote <= 1);
            }
}

void testProgressionFollowsHost()
{
    std::printf ("La progresion sigue al host: salto, loop, tempo, parado; soltar, cambiar de modo y apagar\n");

    const auto settings = progressionSettings (th::VoicingStyle::rootPosition, 0);
    TimedRig rig;
    rig.stage.setProgression (progressionOf ("i bVI bIII bVII")); // Am F C G, un compás cada uno
    rig.run (512, settings, { on (60, 10) });
    CHECK ((rig.soundingNotes() == std::vector<int> { 57, 60, 64 }));
    CHECK (rig.stage.progressionStep() == 0 && rig.stage.progressionSounding());

    // Salto al compás 3 (♭III = C): en la muestra 0 del bloque, C suena y Am se apaga.
    rig.ppq = 8.5;
    rig.out.clear();
    rig.run (512, settings);
    CHECK ((rig.soundingNotes() == std::vector<int> { 60, 64, 67 }));
    CHECK (! rig.out.empty() && rig.out.front().sample == rig.position - 512);
    // Loop de vuelta al compás 1: Am.
    rig.ppq = 0.0;
    rig.run (512, settings);
    CHECK ((rig.soundingNotes() == std::vector<int> { 57, 60, 64 }));
    // Tempo al doble a mitad de compás: el cambio a F llega cuando la posición llega al compás 2, no antes.
    rig.ppq = 3.9;
    rig.bpm = 240.0;
    rig.run (512, settings); // 512 muestras a 240 BPM = 0.043 negras: sigue en el compás 1
    CHECK ((rig.soundingNotes() == std::vector<int> { 57, 60, 64 }));
    rig.run (4096, settings); // ahora sí cruza el 4.0
    CHECK ((rig.soundingNotes() == std::vector<int> { 65, 69, 72 }));

    // Parado en el compás 4: suena G sostenido (sin ritmo: para escuchar esa posición).
    rig.playing = false;
    rig.ppq = 13.0;
    auto dembow = progressionSettings (th::VoicingStyle::rootPosition, 4);
    rig.run (512, dembow);
    CHECK ((rig.soundingNotes() == std::vector<int> { 67, 71, 74 }));
    CHECK (rig.stage.rhythmStep() == -1);

    // Soltar la tecla: silencio. El note-off de otra tecla que no la disparaba no hace nada.
    rig.run (512, dembow, { off (60, 3) });
    CHECK (rig.receiver.sounding() == 0 && ! rig.stage.progressionSounding());
    rig.out.clear();
    rig.run (512, dembow, { off (62, 3) });
    CHECK (rig.out.empty());

    // Dos teclas: suena mientras quede una abajo; la octava y el canal, los de la última.
    rig.playing = true;
    rig.ppq = 0.0;
    rig.run (512, settings, { on (60, 0), { { 0x91, 72, 90 }, 5 } });
    CHECK ((rig.soundingNotes() == std::vector<int> { 69, 72, 76 }));
    CHECK (rig.receiver.held[1][69] == 1);
    rig.run (512, settings, { off (60, 0) });
    CHECK (rig.receiver.sounding() == 3);
    // Cambiar a teclas con la tecla abajo apaga todo; su note-off después no manda nada.
    auto keys = settings;
    keys.playMode = PlayMode::keys;
    rig.run (512, keys);
    CHECK (rig.receiver.sounding() == 0);
    rig.out.clear();
    rig.run (512, keys, { { { 0x81, 72, 0 }, 1 } });
    CHECK (rig.out.empty());

    // Editar la progresión mientras suena: el acorde de esa posición cambia en el bloque siguiente.
    rig.run (512, settings, { on (60, 0) });
    rig.stage.setProgression (progressionOf ("iv bVI bIII bVII"));
    rig.run (512, settings);
    CHECK ((rig.soundingNotes() == std::vector<int> { 62, 65, 69 })); // Dm
    // Vaciarla: silencio.
    rig.stage.setProgression ({});
    rig.run (512, settings);
    CHECK (rig.receiver.sounding() == 0 && rig.stage.progressionStep() == -1);
    rig.stage.setProgression (progressionOf ("i bVI"));
    rig.run (512, settings);
    CHECK (rig.receiver.sounding() == 3);
    // Apagar (reinicio del audio) olvida la tecla: su note-off no manda nada.
    rig.stage.requestRelease();
    rig.run (512, settings);
    CHECK (rig.receiver.sounding() == 0);
    rig.out.clear();
    rig.run (512, settings, { off (60, 0) });
    CHECK (rig.out.empty() && rig.maxPerNote <= 1);
}

void testRhythmOnKeys()
{
    std::printf ("Ritmo en el modo teclas: el dembow ataca en 0 3 6 8 11 14 con sus acentos y calla entre golpes\n");

    constexpr int sixteenth = 6000;
    ChordSettings dembow;
    dembow.enabled = true;
    dembow.rhythm = 4;
    TimedRig rig;
    Random random { 99 };
    rig.runUntil (16 * sixteenth + 50, dembow, random, { on (60, 0, 100), off (60, 16 * sixteenth) });
    std::vector<std::pair<int, int>> attacks; // (semicorchea, velocidad) de la A4
    for (const auto& e : rig.out)
        if (e.bytes[0] == 0x90 && e.bytes[1] == 57 && e.bytes[2] > 0 && e.sample < 16 * sixteenth)
            attacks.emplace_back (e.sample / sixteenth, e.bytes[2]);
    CHECK ((attacks == std::vector<std::pair<int, int>> { { 0, 100 }, { 3, 85 }, { 6, 90 }, { 8, 100 }, { 11, 85 }, { 14, 90 } }));
    CHECK (rig.receiver.sounding() == 0 && rig.maxPerNote <= 1);

    // Entre golpes, silencio: a mitad de la semicorchea 2 no suena nada; en la 3, Am.
    TimedRig gaps;
    gaps.run (2 * sixteenth + 3000, dembow, { on (60, 0) });
    CHECK (gaps.receiver.sounding() == 0);
    gaps.run (sixteenth, dembow);
    CHECK (gaps.receiver.sounding() == 3);
    // Una tecla en un silencio espera al golpe siguiente.
    TimedRig late;
    late.ppq = 0.5; // semicorchea 2
    late.run (100, dembow, { on (60, 10) });
    CHECK (late.receiver.sounding() == 0);
    late.run (sixteenth, dembow);
    CHECK (late.receiver.sounding() == 3);
    // Sostenido con el host parado aunque el ritmo sea dembow.
    TimedRig stopped;
    stopped.playing = false;
    stopped.ppq = 0.5;
    stopped.run (100, dembow, { on (60, 10) });
    CHECK (stopped.receiver.sounding() == 3);
}

void testTimedRandom()
{
    std::printf ("Progresion y ritmo al azar (10 semillas): saltos, paradas, tempo, modos, ritmos, voicings y "
                 "progresiones cambiando; nunca dos note-on seguidos de una nota y, al soltar todo, silencio\n");

    for (std::uint32_t seed = 1; seed <= 10; ++seed)
    {
        Random random { seed * 7919u };
        TimedRig rig;
        auto settings = randomSettings (random, true);
        const auto randomize = [&] {
            settings = randomSettings (random, settings.enabled);
            settings.playMode = random.upTo (2) == 0 ? PlayMode::keys : PlayMode::progression;
            settings.voicing = static_cast<th::VoicingStyle> (random.upTo (th::voicingStyleCount));
            settings.rhythm = random.upTo (th::rhythmPatternCount);
        };
        randomize();
        const char* progressions[] { "i bVI bIII bVII", "i bII", "i bVII bVI V", "Imaj7 bVImaj7 iv9", "i", "" };
        bool matchesTracker = true;
        for (int block = 0; block < 2500; ++block)
        {
            if (random.upTo (60) == 0)
                randomize();
            if (random.upTo (200) == 0)
                settings.enabled = ! settings.enabled;
            if (random.upTo (40) == 0)
                rig.stage.setProgression (progressionOf (progressions[random.upTo (6)], { 1 + random.upTo (8), 1 + random.upTo (4) }));
            if (random.upTo (30) == 0)
                rig.stage.requestPreview (random.upTo (8) - 1);
            if (random.upTo (50) == 0)
                rig.ppq = random.upTo (64) * 0.25 + (random.upTo (2) == 0 ? 0.1 : 0.0); // salto o loop
            if (random.upTo (80) == 0)
                rig.playing = ! rig.playing;
            if (random.upTo (80) == 0)
                rig.bpm = 60.0 + random.upTo (140);
            if (random.upTo (400) == 0)
                rig.stage.requestRelease();
            const int n = 1 + random.upTo (2048);
            std::vector<Event> events;
            for (int e = random.upTo (4); e > 0; --e)
            {
                auto event = randomEvent (random, random.upTo (n));
                events.push_back (event);
            }
            std::sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.sample < b.sample; });
            rig.run (n, settings, events);
            matchesTracker = matchesTracker && rig.receiver.sounding() == rig.stage.getSounding().soundingCount();
        }
        CHECK (matchesTracker);
        CHECK (rig.maxPerNote <= 1);

        // Fin: acordes activos, todas las teclas sueltas, la paleta suelta: silencio, sin pedir que se apague nada.
        settings.enabled = true;
        rig.stage.requestPreview (-1);
        std::vector<Event> releases;
        for (int channel = 0; channel < 3; ++channel)
            for (int note = 48; note < 60; ++note)
                releases.push_back ({ { static_cast<std::uint8_t> (0x80 | channel), static_cast<std::uint8_t> (note), 0 }, 0 });
        rig.run (64, settings, releases);
        rig.run (64, settings);
        CHECK (rig.receiver.sounding() == 0);
        CHECK (rig.stage.getSounding().soundingCount() == 0);
    }
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
    testChordForKey();
    testChordStage();
    testChordStageRandom();
    testKeysWithVoicing();
    testProgressionMatchesRender();
    testProgressionFollowsHost();
    testRhythmOnKeys();
    testTimedRandom();
    testEventLog();
    testNoteNames();

    if (failures == 0)
        std::printf ("\nTodos los tests de Tidechart pasaron.\n");
    else
        std::printf ("\n%d comprobaciones fallaron.\n", failures);
    return failures == 0 ? 0 : 1;
}
