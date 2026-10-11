#pragma once

#include <array>
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/Engine.h"

// Fase 7: efecto mono/estéreo: input (drive) → tono pre (cortes, emphasis) → 1-4 bandas Linkwitz-Riley, cada una con su
// modo, drive, auto gain y solo/mute (saturación con oversampling) → tono post (de-emphasis, cortes, tilt) → filtro DC →
// mix con la original alineada → delta → output, con medidores de entrada y salida y bypass con fundido. Una envolvente
// y un LFO (libre o al tempo del host) mueven el drive, el mix, el emphasis, el tilt o el drive de una banda. Todo el
// audio está en riptide::dsp::Engine (sin JUCE); aquí solo se conectan los parámetros, el estado, el tempo del host, la
// latencia y los medidores con la GUI.
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
    float getAutoGainDb (int band = 0) const noexcept
    {
        return autoGainDb[static_cast<size_t> (band)].load (std::memory_order_relaxed);
    }
    // La frecuencia a la que suena cada cruce (las perillas pueden pedir otra: el motor los separa media octava).
    float getCrossoverHz (int index) const noexcept
    {
        return crossoverHz[static_cast<size_t> (index)].load (std::memory_order_relaxed);
    }
    // Modulación (al final del último bloque): el valor de la envolvente (0..1) y del LFO (−1..+1), y cuánto mueve cada
    // destino en sus unidades (dB, mezcla 0..1 u octavas).
    float getEnvelopeValue() const noexcept { return envelopeValue.load (std::memory_order_relaxed); }
    float getLfoValue() const noexcept { return lfoValue.load (std::memory_order_relaxed); }
    float getModulation (riptide::dsp::ModTarget target) const noexcept
    {
        return modulation[static_cast<size_t> (target)].load (std::memory_order_relaxed);
    }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    riptide::dsp::EngineSettings readSettings() const noexcept;
    riptide::dsp::Transport readTransport() noexcept;
    void publishMeters (int numChannels) noexcept;
    void publishBands() noexcept;
    void timerCallback() override;

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* inputGainParam = nullptr;
    std::atomic<float>* outputGainParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* oversamplingParam = nullptr;
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* lowCutParam = nullptr;
    std::atomic<float>* lowCutPositionParam = nullptr;
    std::atomic<float>* highCutParam = nullptr;
    std::atomic<float>* highCutPositionParam = nullptr;
    std::atomic<float>* emphasisParam = nullptr;
    std::atomic<float>* emphasisFreqParam = nullptr;
    std::atomic<float>* tiltParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* autoGainParam = nullptr;
    std::atomic<float>* deltaParam = nullptr;
    std::atomic<float>* bandsParam = nullptr;
    std::array<std::atomic<float>*, riptide::dsp::maxCrossovers> crossoverParams {};
    std::array<std::atomic<float>*, riptide::dsp::maxBands> bandModeParams {}, bandDriveParams {}, bandSoloParams {},
        bandMuteParams {};
    std::atomic<float>* envAttackParam = nullptr;
    std::atomic<float>* envReleaseParam = nullptr;
    std::atomic<float>* envSensitivityParam = nullptr;
    std::atomic<float>* envTargetParam = nullptr;
    std::atomic<float>* envAmountParam = nullptr;
    std::atomic<float>* lfoShapeParam = nullptr;
    std::atomic<float>* lfoSyncParam = nullptr;
    std::atomic<float>* lfoRateParam = nullptr;
    std::atomic<float>* lfoDivisionParam = nullptr;
    std::atomic<float>* lfoTargetParam = nullptr;
    std::atomic<float>* lfoAmountParam = nullptr;
    juce::AudioParameterBool* bypassParameter = nullptr;

    riptide::dsp::Engine engine;

    StereoMeter inputMeter;
    StereoMeter outputMeter;
    std::atomic<int> meteredChannels { 2 };
    std::atomic<int> engineLatency { 0 }; // la escribe el audio; el temporizador se la informa al host
    std::array<std::atomic<float>, riptide::dsp::maxBands> autoGainDb {};      // la compensación de cada banda, para la GUI
    std::array<std::atomic<float>, riptide::dsp::maxCrossovers> crossoverHz {}; // los cruces que suenan, para la GUI
    std::atomic<float> envelopeValue { 0.0f }, lfoValue { 0.0f };
    std::array<std::atomic<float>, riptide::dsp::numModTargets> modulation {};  // cuánto se mueve cada destino

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RiptideAudioProcessor)
};
