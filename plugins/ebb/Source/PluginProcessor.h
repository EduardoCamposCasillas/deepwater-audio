#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "deepwater/dsp/TripleBuffer.h"
#include "dsp/Engine.h"
#include "dsp/HostClock.h"
#include "TransportSnapshot.h"

// Efecto mono/estéreo. Fase 2: modo tempo, una curva de ducking sincronizada con la posición del host (PlayHead).
// Fase 3: la forma de esa curva es una curva por puntos editable, guardada en el estado del proyecto.
// El audio está en ebb::dsp (sin JUCE); aquí solo se conectan los parámetros, el estado y la GUI.
class EbbAudioProcessor final : public juce::AudioProcessor
{
public:
    EbbAudioProcessor();
    ~EbbAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock; // evita ocultar la versión double de la clase base

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // El bypass propio es el parámetro que el host usa como su bypass: el botón de FL y el de la GUI son el mismo,
    // y los dos pasan por el fundido del motor.
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParameter; }

    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }

    // Hilo de la GUI: el último estado del reloj publicado por el audio. true si es nuevo desde la lectura anterior.
    bool readTransport (TransportSnapshot& out) noexcept { return transportBox.read (out); }

    // Hilo de la GUI: la ganancia más baja que sonó desde la lectura anterior (1 = sin ducking), y la reinicia.
    // Así el medidor no se pierde un golpe aunque entre dos cuadros de la GUI pasen varios bloques.
    float takeMinGain() noexcept { return minGainSinceRead.exchange (1.0f); }

    // Hilo de mensajes: la forma de la curva. No es un parámetro (tiene un número variable de puntos y no se puede
    // automatizar): se guarda como hijo "Curve" del estado, y al audio llega por un buzón lock-free. La versión sube
    // en cada cambio, para que la GUI sepa cuándo redibujar (p. ej. al cargar un proyecto).
    const deepwater::dsp::BreakpointCurve& getCurve() const noexcept { return curve; }
    void setCurve (const deepwater::dsp::BreakpointCurve& newCurve);
    int getCurveVersion() const noexcept { return curveVersion.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    ebb::dsp::TransportInfo readHostTransport() noexcept;
    ebb::dsp::TempoSettings readTempoSettings() const noexcept;
    void publishTransport (int numSamples) noexcept;
    void loadCurveFromState();

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* divisionParam = nullptr;
    std::atomic<float>* depthParam = nullptr;
    std::atomic<float>* durationParam = nullptr;
    std::atomic<float>* smoothingParam = nullptr;
    juce::AudioParameterBool* bypassParameter = nullptr;

    ebb::dsp::Engine engine;
    ebb::dsp::HostClock clock;
    double currentSampleRate = 44100.0;
    deepwater::dsp::TripleBuffer<TransportSnapshot> transportBox;
    std::atomic<float> minGainSinceRead { 1.0f };

    deepwater::dsp::BreakpointCurve curve;                       // hilo de mensajes
    deepwater::dsp::TripleBuffer<deepwater::dsp::BreakpointCurve> curveBox; // mensajes → audio
    deepwater::dsp::BreakpointCurve audioCurve;                  // hilo de audio: la última leída del buzón
    std::atomic<int> curveVersion { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EbbAudioProcessor)
};
