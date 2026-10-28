#pragma once

#include <array>
#include <cstdint>
#include <tuple>

#include "deepwater/dsp/HostClock.h"
#include "deepwater/theory/KeyDetection.h"
#include "midi/NoteTracker.h"

namespace tidechart::midi
{

// Fase 5: escucha el MIDI que entra y junta la evidencia para detectar la tonalidad (deepwater::theory::KeyEvidence),
// lo mismo que deepwater::theory::keyEvidence calcula de una lista de notas:
//   - cuánto suena cada clase de altura, exacto a la muestra: entre un evento y el siguiente, cada nota abajo suma ese
//     tiempo (en segundos) y la más grave lo suma otra vez (el bajo cuenta doble);
//   - los votos de "casa": con el host reproduciendo, la nota más grave de las que empiezan al inicio de cada frase de
//     8 compases (contados desde el compás 1, como la progresión). Cada pasada cuenta: si FL hace loop o salta y vuelve
//     a pasar por el compás 1, es otra frase. Sin el host reproduciendo (tocando en vivo con FL parado, o en el
//     Standalone), un solo voto: la nota más grave de las primeras que se tocan (las que empiezan en 50 ms).
// Solo mira lo que entra; no emite nada. Tamaño fijo, sin reservar memoria: corre en el hilo de audio.
class KeyListener
{
public:
    // Margen para que las notas de un acorde tocado a mano cuenten como "la primera" juntas.
    static constexpr double firstChordSeconds = 0.05;

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        // El audio se reinició: los note-off de lo que estaba abajo pudieron perderse.
        forgetHeld();
    }

    // Empieza a escuchar de cero.
    void reset() noexcept
    {
        found = {};
        heard = 0.0;
        forgetHeld();
        elapsedSamples = 0;
        hasWindow = false;
        firstOnsetSample = -1;
        starts = 0;
    }

    void forgetHeld() noexcept
    {
        for (auto& channel : perChannel)
            channel.fill (0);
        held.fill (0);
    }

    // Al principio de cada bloque, con el reloj ya avanzado (nullptr = sin tiempo musical).
    void beginBlock (const deepwater::dsp::HostClock* blockClock) noexcept
    {
        clock = blockClock;
        position = 0;
        if (clock != nullptr && clock->startedThisBlock())
            ++starts; // volver a darle play y pasar por el compás 1 es otra frase
    }

    // Un evento que entra, en su muestra dentro del bloque (en orden).
    void observe (ShortMessage message, int sample) noexcept
    {
        advanceTo (sample);
        const int channel = message.channel();
        const int note = message.data1;
        if (message.isNoteOn())
        {
            auto& count = perChannel[static_cast<std::size_t> (channel)][static_cast<std::size_t> (note)];
            if (count < 255)
            {
                ++count;
                ++held[static_cast<std::size_t> (note)];
            }
            noteStarted (note, sample);
        }
        else if (message.isNoteOff())
        {
            auto& count = perChannel[static_cast<std::size_t> (channel)][static_cast<std::size_t> (note)];
            if (count > 0)
            {
                --count;
                --held[static_cast<std::size_t> (note)];
            }
        }
        else if (message.isAllNotesOff())
        {
            for (int n = 0; n < numNotes; ++n)
            {
                auto& count = perChannel[static_cast<std::size_t> (channel)][static_cast<std::size_t> (n)];
                held[static_cast<std::size_t> (n)] = static_cast<std::uint16_t> (held[static_cast<std::size_t> (n)] - count);
                count = 0;
            }
        }
    }

    // Al final del bloque: el tiempo desde el último evento.
    void endBlock (int numSamples) noexcept
    {
        advanceTo (numSamples);
        elapsedSamples += numSamples;
    }

    [[nodiscard]] const deepwater::theory::KeyEvidence& evidence() const noexcept { return found; }
    // Cuántos segundos sonó algo (con al menos una nota abajo).
    [[nodiscard]] double heardSeconds() const noexcept { return heard; }

private:
    void advanceTo (int sample) noexcept
    {
        if (sample <= position)
            return;
        const double seconds = (sample - position) / sampleRate;
        position = sample;
        int lowest = -1;
        for (int n = 0; n < numNotes; ++n)
            if (const int count = held[static_cast<std::size_t> (n)]; count > 0)
            {
                if (lowest < 0)
                    lowest = n;
                found.pitchWeight[static_cast<std::size_t> (n % 12)] += seconds * count;
            }
        if (lowest >= 0)
        {
            found.pitchWeight[static_cast<std::size_t> (lowest % 12)] += deepwater::theory::KeyEvidence::bassWeight * seconds;
            heard += seconds;
        }
    }

    // ¿Esta nota es la más grave de las que abren una frase (o de las primeras)? Cada ventana tiene un solo voto, que
    // pasa a una nota más grave si llega otra dentro de la misma ventana.
    void noteStarted (int note, int sample) noexcept
    {
        namespace th = deepwater::theory;
        if (clock != nullptr && clock->getSource() == deepwater::dsp::ClockSource::host)
        {
            const double beatsPerPhrase = th::phraseBars * clock->timeSignature().quartersPerBar();
            const long long phrase = th::phraseCueIndex (clock->ppqAt (sample), beatsPerPhrase);
            if (phrase >= 0)
                vote (std::make_tuple (phrase, clock->jumpCount(), starts), note);
            return;
        }
        // Sin el host reproduciendo: las primeras notas después de empezar a escuchar.
        const long long now = elapsedSamples + sample;
        if (firstOnsetSample < 0)
            firstOnsetSample = now;
        if (now - firstOnsetSample <= static_cast<long long> (firstChordSeconds * sampleRate))
            vote (std::make_tuple (-1LL, std::uint32_t {}, std::uint32_t {}), note);
    }

    using Window = std::tuple<long long, std::uint32_t, std::uint32_t>; // frase, saltos del host, veces que arrancó

    void vote (Window window, int note) noexcept
    {
        if (! hasWindow || window != currentWindow)
        {
            hasWindow = true;
            currentWindow = window;
            windowNote = note;
            found.tonicCue[static_cast<std::size_t> (note % 12)] += 1.0;
        }
        else if (note < windowNote)
        {
            found.tonicCue[static_cast<std::size_t> (windowNote % 12)] -= 1.0;
            found.tonicCue[static_cast<std::size_t> (note % 12)] += 1.0;
            windowNote = note;
        }
    }

    double sampleRate = 44100.0;
    const deepwater::dsp::HostClock* clock = nullptr;
    int position = 0; // la muestra del bloque hasta la que ya se contó

    std::array<std::array<std::uint8_t, numNotes>, numChannels> perChannel {};
    std::array<std::uint16_t, numNotes> held {}; // notas abajo por número, sumando los canales

    deepwater::theory::KeyEvidence found;
    double heard = 0.0;
    long long elapsedSamples = 0;
    long long firstOnsetSample = -1;
    std::uint32_t starts = 0;
    bool hasWindow = false;
    Window currentWindow {};
    int windowNote = 0;
};

} // namespace tidechart::midi
