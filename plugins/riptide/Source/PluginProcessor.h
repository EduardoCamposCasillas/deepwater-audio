#pragma once

#include <array>
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/Engine.h"

// Fase 2: efecto mono/estéreo: input (drive) → tanh con oversampling → filtro DC → output, con medidores de entrada
// y salida y bypass con fundido. Todo el audio está en riptide::dsp::Engine (sin JUCE); aquí solo se conectan los
// parámetros, el estado, la latencia y los medidores con la GUI.
//
// El temporizador corre en el hilo de mensajes: informa al host la latencia nueva cuando el audio cambia de factor de
// oversampling (setLatencySamples avisa al host, y eso no se hace desde el hilo de audio).
class RiptideAudioProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    // Lo que el audio publica para la GUI: el pico desde la última lectura (la GUI lo recoge y lo pone a 0) y el
    // RMS actual. Atómicos: el audio escribe y la GUI lee sin locks.
    struct MeterChannel
    {
        std::atomic<float> peak { 0.0f };
        std::atomic<float> rms { 0.0f };
    };
    using StereoMeter = std::array<MeterChannel, riptide::dsp::Engine::maxChannels>;

    RiptideAudioProcessor();
    ~RiptideAudioProcessor() override;

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

    StereoMeter& getInputMeter() noexcept { return inputMeter; }
    StereoMeter& getOutputMeter() noexcept { return outputMeter; }
    int getMeteredChannels() const noexcept { return meteredChannels.load (std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    riptide::dsp::EngineSettings readSettings() const noexcept;
    void publishMeters (int numChannels) noexcept;
    void timerCallback() override;

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* inputGainParam = nullptr;
    std::atomic<float>* outputGainParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* oversamplingParam = nullptr;
    juce::AudioParameterBool* bypassParameter = nullptr;

    riptide::dsp::Engine engine;

    StereoMeter inputMeter;
    StereoMeter outputMeter;
    std::atomic<int> meteredChannels { 2 };
    std::atomic<int> engineLatency { 0 }; // la escribe el audio; el temporizador se la informa al host

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RiptideAudioProcessor)
};
