#pragma once

#include <array>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstdint>

#include "deepwater/dsp/HostClock.h"
#include "deepwater/theory/Progression.h"
#include "deepwater/theory/Rhythm.h"
#include "midi/ChordTrigger.h"
#include "midi/NoteTracker.h"

namespace tidechart::midi
{

// La etapa MIDI de Tidechart, sin JUCE: lo que entra, lo que sale y la garantía de que nada queda colgado.
//
// Fase 1: todo lo que entra sale tal cual, en su misma muestra (passthrough). Es lo que hace en bypass.
// Fase 3: con los acordes activos (ChordSettings::enabled), cada tecla dispara un acorde de la tonalidad (ChordTrigger)
// y la tecla en sí no sale; lo que no es una nota (CC, pitch bend, sustain) sigue pasando igual. La paleta de la GUI
// también hace sonar acordes (requestPreview).
// Fase 4: voicing con conducción de voces, ritmo de acordes sincronizado con el host y la PROGRESIÓN (PlayMode).
// TODO lo que sale pasa por emit(), que lo cuenta en el NoteTracker. Así, cuando algo corta el flujo normal, se sabe
// exactamente qué note-off faltan:
//   - requestRelease(): el host reinició el audio (prepareToPlay / releaseResources / reset) o se cargó un proyecto.
//     Al principio del bloque siguiente se apaga lo que suena.
//   - requestPanic(): el botón de pánico. Además de lo anterior, CC 123 (All Notes Off) en los 16 canales, por si el
//     receptor tiene notas colgadas que no salieron de aquí (de antes de cargar el plugin, o de otro programa).
// Las peticiones pueden venir de otro hilo (la GUI, el hilo de mensajes): son atómicos y el audio las recoge al
// empezar el bloque.
//
// Lo que NO se apaga aquí: las notas que solo pasan. Su note-off lo manda el host (FL los manda al parar, al saltar y
// al hacer loop) o el teclado al soltar la tecla; cortarlas al parar el transporte silenciaría una nota que se está
// tocando en vivo. Los acordes de una tecla siguen la misma regla: suenan mientras la tecla esté abajo, y el note-off
// de la tecla (el del host al parar, saltar o hacer loop) los apaga.
// Cambiar entre bypass y acordes apaga lo del otro modo en la muestra 0: al entrar en bypass, los acordes; al salir,
// las notas que pasaron tal cual (su note-off llegaría ya con los acordes activos, que no lo dejan pasar). Cambiar
// entre teclas y progresión apaga lo que suena y olvida las teclas abajo (su note-off ya no hace nada).
//
// EL TIEMPO (Fase 4). Lo que suena es una función de la posición del host: en cada semicorchea, el acorde es el del
// paso de la progresión que cae ahí (o el de la tecla), y suena si esa semicorchea está dentro de un golpe del patrón
// rítmico. La etapa recorre las muestras del bloque buscando dónde cambia la semicorchea (exacto a la muestra con
// HostClock::ppqAt) y en cada cambio, y en cada tecla, compara lo que debería sonar con lo que suena:
//   - debería sonar y no suena, o es otro acorde (otro paso, otras notas, otro canal), o empieza un golpe → se ataca;
//   - no debería sonar (un silencio del patrón) y suena → se suelta.
// Así un salto, un loop o un cambio de tempo dan solos el acorde de esa posición, sin historia que se desincronice.
// Con el host parado no hay ritmo: el acorde suena sostenido (para escuchar lo que hay en esa posición).
//
// Sink: sink (const std::uint8_t* data, int size, int sample), el que escribe en el MidiBuffer de salida.
class MidiStage
{
public:
    void requestRelease() noexcept { releasePending.store (true, std::memory_order_release); }
    void requestPanic() noexcept { panicPending.store (true, std::memory_order_release); }

    // Cualquier hilo: la paleta de la GUI pide que suene el acorde del grado 0..6 (−1 = soltar), con el tamaño de acorde
    // de los ajustes. Se atiende al principio del bloque siguiente, en la octava del Do central (C5).
    void requestPreview (int degree) noexcept
    {
        previewRequest.store (degree < 0 || degree > 6 ? 0 : degree + 1, std::memory_order_release);
    }
    // Cualquier hilo: lo mismo con cualquier grado escrito (un prestado, un paso de la progresión).
    void requestPreviewChord (const deepwater::theory::RomanNumeral& numeral) noexcept
    {
        previewRequest.store (previewCode (numeral), std::memory_order_release);
    }

    // Hilo de audio, antes de beginBlock: los ajustes de este bloque (los arma el procesador con los parámetros).
    void setChordSettings (const ChordSettings& newSettings) noexcept { pendingSettings = newSettings; }
    [[nodiscard]] const ChordSettings& getChordSettings() const noexcept { return settings; }

    // Hilo de audio, antes de beginBlock: la progresión que publicó la GUI. Si cambió, se vuelve a calcular su voicing.
    void setProgression (const deepwater::theory::Progression& newProgression) noexcept
    {
        if (newProgression == progression)
            return;
        progression = newProgression;
        voicedValid = false;
    }
    [[nodiscard]] const deepwater::theory::Progression& getProgression() const noexcept { return progression; }

    // Al principio de cada bloque, antes de los eventos: atiende las peticiones pendientes en la muestra 0. 'blockClock'
    // es el reloj del host ya avanzado a este bloque (nullptr = sin tiempo: todo sostenido, como en la Fase 3).
    template <typename Sink>
    void beginBlock (Sink&& sink, const deepwater::dsp::HostClock* blockClock = nullptr)
    {
        clock = blockClock;
        auto out = [&] (ShortMessage m) { emit (m, 0, sink); };

        const bool panic = panicPending.exchange (false, std::memory_order_acq_rel);
        const bool release = releasePending.exchange (false, std::memory_order_acq_rel);
        if (panic || release)
        {
            // Copia: emit() actualiza 'sounding' mientras se recorre.
            NoteTracker toRelease = sounding;
            const int sent = toRelease.releaseAll (out);
            if (panic)
                for (int channel = 0; channel < numChannels; ++channel)
                    emit (ShortMessage::controller (channel, 123, 0), 0, sink);
            if (panic || sent > 0) // el host reinicia el audio a menudo sin nada sonando: eso no se cuenta
                ++releases;
            // Ya se apagó todo: los acordes que sonaban se olvidan (el note-off tardío de su tecla no hará nada).
            chords.clear();
            forgetHeld();
            previewPlaying = 0;
        }

        // Bypass ↔ acordes: lo del modo que se deja se apaga. Teclas ↔ progresión: se apaga todo lo generado.
        if (pendingSettings.enabled != settings.enabled)
        {
            if (settings.enabled)
                chords.releaseAll (out);
            else
            {
                NoteTracker passed = sounding;
                passed.releaseAll (out);
            }
            forgetHeld();
            previewPlaying = 0;
        }
        else if (settings.enabled && pendingSettings.playMode != settings.playMode)
        {
            chords.releaseAll (out);
            forgetHeld();
            previewPlaying = 0;
        }
        settings = pendingSettings;

        // La paleta: suelta el acorde anterior y hace sonar el pedido (solo con los acordes activos).
        const int wanted = settings.enabled ? previewRequest.load (std::memory_order_acquire) : 0;
        if (wanted != previewPlaying)
        {
            chords.release (ChordTrigger::previewSlot, out);
            std::optional<TriggeredChord> chord;
            if (wanted >= 1 && wanted <= 7)
                chord = chordForDegree (wanted - 1, 60, settings);
            else if (wanted >= numeralCodeBase)
                chord = chordForNumeral (numeralFromCode (wanted), 60, settings);
            if (chord)
                chords.press (ChordTrigger::previewSlot, 0, previewVelocity, voiced (*chord), out);
            previewPlaying = wanted;
        }

        // El tiempo: la semicorchea de la muestra 0 y lo que debe sonar ahí.
        cursor = 0;
        const long long tick = tickAt (0);
        const bool newTick = tick != lastTick;
        lastTick = tick;
        evaluate (0, newTick, sink);
    }

    // Un evento de entrada (bytes crudos, en su muestra del bloque). En bypass sale igual; con los acordes activos, una
    // tecla dispara su acorde (o la progresión) y se apaga con el note-off de la misma tecla (una tecla que no dispara
    // nada no sale). Antes, lo que el tiempo haya cambiado hasta esa muestra.
    template <typename Sink>
    void process (const std::uint8_t* data, int size, int sample, Sink&& sink)
    {
        advanceTo (sample, sink);
        const auto message = readShortMessage (data, size);
        if (! settings.enabled || ! (message.isNoteOn() || message.isNoteOff()))
        {
            emitRaw (data, size, sample, sink);
            return;
        }

        auto out = [&] (ShortMessage m) { emit (m, sample, sink); };
        const int slot = ChordTrigger::keySlot (message.channel(), message.data1);
        auto& isTrigger = triggers[static_cast<std::size_t> (slot)];

        if (settings.playMode == PlayMode::progression)
        {
            if (message.isNoteOff())
            {
                if (isTrigger && --triggerCount == 0)
                    chords.release (ChordTrigger::progressionSlot, out);
                isTrigger = false;
                return;
            }
            if (! isTrigger)
                ++triggerCount;
            isTrigger = true;
            trigger = { slot, message.channel(), message.data1, message.data2 };
            evaluate (sample, false, sink);
            return;
        }

        if (message.isNoteOff())
        {
            forgetKey (slot);
            chords.release (slot, out);
            return;
        }
        if (! chordForKey (message.data1, settings))
            return;
        rememberKey (slot, message.channel(), message.data1, message.data2);
        if (const auto r = rhythmAt (lastTick); r.gate)
            pressKey (heldKeys[static_cast<std::size_t> (findKey (slot))], r.accent, sample, sink);
    }

    // Al final del bloque: lo que el tiempo cambie hasta su última muestra.
    template <typename Sink>
    void endBlock (int numSamples, Sink&& sink)
    {
        advanceTo (numSamples - 1, sink);
    }

    // Lo que suena en el receptor según lo que salió de aquí.
    [[nodiscard]] const NoteTracker& getSounding() const noexcept { return sounding; }
    // Cuántas veces se apagó algo que sonaba, más los pánicos (para la GUI y los tests).
    [[nodiscard]] std::uint32_t releaseCount() const noexcept { return releases; }
    // Los acordes que suenan (qué grados, para encender la paleta).
    [[nodiscard]] const ChordTrigger& getChords() const noexcept { return chords; }
    // El paso de la progresión en la posición actual (−1 si está vacía) y si está sonando.
    [[nodiscard]] int progressionStep() const noexcept { return progression.stepAt (lastTick == LLONG_MIN ? 0 : lastTick); }
    [[nodiscard]] bool progressionSounding() const noexcept { return chords.isHeld (ChordTrigger::progressionSlot); }
    // La semicorchea del patrón rítmico en la posición actual (−1 sin ritmo o con el host parado).
    [[nodiscard]] int rhythmStep() const noexcept { return rhythmAt (lastTick).step; }

    static constexpr int previewVelocity = 100;
    static constexpr int maxHeldKeys = 64;

private:
    struct Rhythm
    {
        bool gate = true;      // suena en esta semicorchea
        bool hitStart = false; // empieza un golpe
        int accent = 100;
        int step = -1;         // semicorchea del patrón (−1 = sin ritmo)
    };

    struct HeldKey
    {
        int slot = -1;
        int channel = 0, note = 0, velocity = 0;
    };

    // La semicorchea absoluta de una muestra del bloque (desde el principio de la canción). El margen absorbe errores de
    // redondeo del host (una posición de 0.9999999 negras es el tiempo 2).
    [[nodiscard]] long long tickAt (int sample) const noexcept
    {
        if (clock == nullptr)
            return 0;
        return static_cast<long long> (std::floor (clock->ppqAt (sample) * deepwater::theory::sixteenthsPerQuarter + 1.0e-6));
    }

    [[nodiscard]] Rhythm rhythmAt (long long tick) const noexcept
    {
        const auto& pattern = deepwater::theory::rhythmPattern (settings.rhythm);
        if (pattern.steps <= 0 || clock == nullptr || ! clock->isRunning() || tick == LLONG_MIN)
            return {};
        Rhythm r;
        r.step = deepwater::theory::patternStep (pattern, tick);
        const auto* hit = deepwater::theory::hitAt (pattern, r.step);
        r.gate = hit != nullptr;
        r.hitStart = r.gate && hit->start == r.step;
        r.accent = r.gate ? hit->accent : 100;
        return r;
    }

    // ¿Hace falta mirar cada muestra? Solo si algo depende del tiempo y el reloj avanza.
    [[nodiscard]] bool timingActive() const noexcept
    {
        if (! settings.enabled || clock == nullptr || ! clock->isRunning())
            return false;
        if (settings.playMode == PlayMode::progression)
            return triggerCount > 0;
        return heldKeyCount > 0 && deepwater::theory::rhythmPattern (settings.rhythm).steps > 0;
    }

    // Recorre las muestras hasta 'target' (incluida): en cada cambio de semicorchea, evaluate.
    template <typename Sink>
    void advanceTo (int target, Sink& sink)
    {
        if (target <= cursor)
            return;
        if (! timingActive())
        {
            cursor = target;
            lastTick = tickAt (target);
            return;
        }
        for (int s = cursor + 1; s <= target; ++s)
            if (const long long t = tickAt (s); t != lastTick)
            {
                lastTick = t;
                evaluate (s, true, sink);
            }
        cursor = target;
    }

    // Compara lo que debería sonar en esta muestra con lo que suena, y ataca o suelta. 'isTick' = empieza una
    // semicorchea (solo ahí cuenta el principio de un golpe; una tecla a mitad de semicorchea no re-ataca nada).
    template <typename Sink>
    void evaluate (int sample, bool isTick, Sink& sink)
    {
        if (! settings.enabled)
            return;
        auto out = [&] (ShortMessage m) { emit (m, sample, sink); };
        const auto r = rhythmAt (lastTick);

        if (settings.playMode == PlayMode::progression)
        {
            const int step = progression.stepAt (lastTick);
            const bool soundingNow = chords.isHeld (ChordTrigger::progressionSlot);
            if (triggerCount == 0 || step < 0 || ! r.gate)
            {
                if (soundingNow)
                    chords.release (ChordTrigger::progressionSlot, out);
                return;
            }
            ensureVoiced (trigger.note - trigger.note % 12);
            const auto& wanted = voicedSteps[static_cast<std::size_t> (step)];
            if (! soundingNow || step != playingStep || ! (wanted.notes == playingNotes) || trigger.channel != playingChannel
                || (isTick && r.hitStart))
            {
                chords.press (ChordTrigger::progressionSlot, trigger.channel, deepwater::theory::accentedVelocity (trigger.velocity, r.accent),
                              wanted, out);
                playingStep = step;
                playingNotes = wanted.notes;
                playingChannel = trigger.channel;
            }
            return;
        }

        for (int i = 0; i < heldKeyCount; ++i)
        {
            const auto& key = heldKeys[static_cast<std::size_t> (i)];
            const bool soundingNow = chords.isHeld (key.slot);
            if (r.gate && (! soundingNow || (isTick && r.hitStart)))
                pressKey (key, r.accent, sample, sink);
            else if (! r.gate && soundingNow)
                chords.release (key.slot, out);
        }
    }

    // Hace sonar el acorde de una tecla abajo con los ajustes de ahora y el voicing que menos mueve las voces.
    template <typename Sink>
    void pressKey (const HeldKey& key, int accent, int sample, Sink& sink)
    {
        auto out = [&] (ShortMessage m) { emit (m, sample, sink); };
        const auto chord = chordForKey (key.note, settings);
        if (! chord)
        {
            chords.release (key.slot, out);
            return;
        }
        chords.press (key.slot, key.channel, deepwater::theory::accentedVelocity (key.velocity, accent), voiced (*chord), out);
    }

    // El acorde con el voicing de los ajustes, desde las voces del último acorde de una tecla o de la paleta.
    [[nodiscard]] TriggeredChord voiced (const TriggeredChord& chord) noexcept
    {
        auto result = withVoicing (chord, settings, lastVoicing);
        lastVoicing = upperVoices (result);
        return result;
    }

    // El voicing de la progresión entera (con la tónica cerca del Do 'octaveC'), si cambió algo desde la última vez.
    void ensureVoiced (int octaveC) noexcept
    {
        if (voicedValid && voicedOctave == octaveC && voicedSettings == settings)
            return;
        namespace th = deepwater::theory;
        const int tonicNote = th::tonicNoteNear (settings.key, octaveC);
        const auto steps = th::voiceProgression (progression, settings.key, settings.voicing, tonicNote, settings.bass);
        for (std::size_t i = 0; i < progression.steps.size(); ++i)
        {
            const auto& step = steps[i];
            voicedSteps[i] = { paletteDegree (step.chord, settings.key), step.chord, step.notes(), step.rootNote, tonicNote,
                               step.bassNote >= 0 && step.bassNote <= 127 };
        }
        voicedValid = true;
        voicedOctave = octaveC;
        voicedSettings = settings;
    }

    [[nodiscard]] int findKey (int slot) const noexcept
    {
        for (int i = 0; i < heldKeyCount; ++i)
            if (heldKeys[static_cast<std::size_t> (i)].slot == slot)
                return i;
        return -1;
    }

    void rememberKey (int slot, int channel, int note, int velocity) noexcept
    {
        int index = findKey (slot);
        if (index < 0)
        {
            if (heldKeyCount >= maxHeldKeys)
                return;
            index = heldKeyCount++;
        }
        heldKeys[static_cast<std::size_t> (index)] = { slot, channel, note, velocity };
    }

    void forgetKey (int slot) noexcept
    {
        const int index = findKey (slot);
        if (index < 0)
            return;
        heldKeys[static_cast<std::size_t> (index)] = heldKeys[static_cast<std::size_t> (heldKeyCount - 1)];
        --heldKeyCount;
    }

    // Olvida las teclas abajo (ya se apagó lo que disparaban): su note-off tardío no hará nada.
    void forgetHeld() noexcept
    {
        heldKeyCount = 0;
        triggers.fill (false);
        triggerCount = 0;
        playingStep = -1;
        lastVoicing.clear();
    }

    template <typename Sink>
    void emit (ShortMessage message, int sample, Sink& sink)
    {
        const std::uint8_t bytes[3] { message.status, message.data1, message.data2 };
        emitRaw (bytes, 3, sample, sink);
    }

    template <typename Sink>
    void emitRaw (const std::uint8_t* data, int size, int sample, Sink& sink)
    {
        sounding.observe (readShortMessage (data, size));
        sink (data, size, sample);
    }

    NoteTracker sounding;
    std::atomic<bool> releasePending { false };
    std::atomic<bool> panicPending { false };
    std::uint32_t releases = 0;

    ChordTrigger chords;
    ChordSettings settings;        // los del bloque en curso
    ChordSettings pendingSettings; // los del bloque siguiente
    std::atomic<int> previewRequest { 0 };
    int previewPlaying = 0;
    deepwater::theory::Voicing lastVoicing; // las voces del último acorde de una tecla o de la paleta

    // Teclas abajo en el modo teclas (para volver a atacar su acorde en cada golpe del ritmo).
    std::array<HeldKey, maxHeldKeys> heldKeys {};
    int heldKeyCount = 0;

    // La progresión: qué teclas la mantienen sonando, la última (su canal, octava y velocidad) y lo que suena.
    deepwater::theory::Progression progression;
    std::array<bool, numChannels * numNotes> triggers {};
    int triggerCount = 0;
    HeldKey trigger;
    std::array<TriggeredChord, deepwater::theory::maxProgressionSteps> voicedSteps {};
    bool voicedValid = false;
    int voicedOctave = 0;
    ChordSettings voicedSettings;
    int playingStep = -1;
    int playingChannel = 0;
    deepwater::theory::StaticVector<int, 9> playingNotes;

    // El tiempo del bloque.
    const deepwater::dsp::HostClock* clock = nullptr;
    long long lastTick = LLONG_MIN; // la semicorchea de la última muestra mirada
    int cursor = 0;                 // la última muestra mirada del bloque
};

} // namespace tidechart::midi
