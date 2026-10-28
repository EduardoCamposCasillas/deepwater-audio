#pragma once

#include <atomic>
#include <cstdint>

#include <juce_audio_processors/juce_audio_processors.h>

#include "deepwater/dsp/HostClock.h"
#include "deepwater/dsp/TripleBuffer.h"
#include "midi/EventLog.h"
#include "midi/MidiStage.h"
#include "TransportSnapshot.h"

// Herramienta MIDI: recibe notas (del Piano Roll o de un teclado) y emite MIDI hacia otro instrumento. No genera audio:
// su salida estéreo siempre es silencio (FL espera al menos una salida en un generador).
// Fase 1: el MIDI pasa tal cual (passthrough) por la etapa que garantiza que ninguna nota queda colgada (MidiStage), y
// se lee la posición del host (HostClock) para mostrarla en la GUI.
// El MIDI está en tidechart::midi (sin JUCE); aquí solo se conectan los buffers de JUCE, los parámetros y la GUI.
class TidechartAudioProcessor final : public juce::AudioProcessor
{
public:
    TidechartAudioProcessor();
    ~TidechartAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock; // evita ocultar la versión double de la clase base
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // El bypass propio es el parámetro que el host usa como su bypass: el botón de FL y el de la GUI son el mismo.
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParameter; }

    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }

    // Hilo de la GUI: el último estado del reloj y de las notas publicado por el audio. true si es nuevo.
    bool readTransport (TransportSnapshot& out) noexcept { return transportBox.read (out); }

    // Hilo de la GUI: los eventos MIDI que entraron y salieron desde la lectura anterior, en orden.
    template <typename Fn>
    int drainMidiLog (Fn&& fn) { return midiLog.drain (std::forward<Fn> (fn)); }

    // Cualquier hilo: el botón de pánico. Al principio del bloque siguiente se apaga todo lo que suena.
    void panic() noexcept { stage.requestPanic(); }

    // Contadores de notas (note-on) que entraron y salieron, para comprobar el ruteo en FL.
    std::uint32_t notesIn() const noexcept { return notesInCount.load (std::memory_order_relaxed); }
    std::uint32_t notesOut() const noexcept { return notesOutCount.load (std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void processMidi (juce::AudioBuffer<float>&, juce::MidiBuffer&, bool bypassed) noexcept;
    deepwater::dsp::TransportInfo readHostTransport() noexcept;
    void publishTransport (int numSamples, bool bypassed) noexcept;

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* bypassParam = nullptr;
    juce::AudioParameterBool* bypassParameter = nullptr;

    tidechart::midi::MidiStage stage;
    deepwater::dsp::HostClock clock;
    // Aquí se arma la salida de cada bloque, que después se copia al buffer del host.
    juce::MidiBuffer outputMidi;
    double currentSampleRate = 44100.0;

    deepwater::dsp::TripleBuffer<TransportSnapshot> transportBox;
    TransportSnapshot snapshot; // la copia que se rellena en el audio
    tidechart::midi::EventLog<1024> midiLog;
    std::atomic<std::uint32_t> notesInCount { 0 };
    std::atomic<std::uint32_t> notesOutCount { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TidechartAudioProcessor)
};
