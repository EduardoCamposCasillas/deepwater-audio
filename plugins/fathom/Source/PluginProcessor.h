#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "deepwater/dsp/AnalyzerTap.h"
#include "dsp/Engine.h"

// Efecto mono/estéreo: 24 bandas de EQ con todas las formas y la ganancia de salida (Fase 3). El audio deja una copia de lo que entra
// y de lo que sale en dos tomas lock-free (AnalyzerTap) para el analizador de la GUI. El audio está en fathom::dsp
// (sin JUCE); aquí solo se conectan los parámetros, el estado y la GUI.
class FathomAudioProcessor final : public juce::AudioProcessor
{
public:
    // 16384 muestras: alcanza para la FFT más grande del analizador (8192 a 96 kHz) con margen.
    using AnalyzerTap = deepwater::dsp::AnalyzerTap<16384>;

    FathomAudioProcessor();
    ~FathomAudioProcessor() override = default;

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

    // Los ajustes actuales de una banda (0-23) o de todas (leen atómicos: sirven en el hilo de audio y en el de la GUI).
    fathom::dsp::BandSettings getBandSettings (int band) const noexcept;
    fathom::dsp::Engine::BandArray getAllBandSettings() const noexcept;

    // Hilo de la GUI: las tomas del analizador y la frecuencia de muestreo con la que se escribieron.
    const AnalyzerTap& getInputTap() const noexcept { return inputTap; }
    const AnalyzerTap& getOutputTap() const noexcept { return outputTap; }
    double getAnalyzerSampleRate() const noexcept { return analyzerSampleRate.load (std::memory_order_relaxed); }

    // Hilo de la GUI: true mientras haya un editor abierto (sin él, el audio no copia muestras a las tomas).
    void setAnalyzerActive (bool active) noexcept { analyzerActive.store (active, std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* outputGainParam = nullptr;
    struct BandParameters
    {
        std::atomic<float>* enabled = nullptr;
        std::atomic<float>* frequency = nullptr;
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* q = nullptr;
        std::atomic<float>* shape = nullptr;
        std::atomic<float>* slope = nullptr;
    };
    std::array<BandParameters, fathom::dsp::Engine::numBands> bandParams;
    juce::AudioParameterBool* bypassParameter = nullptr;

    fathom::dsp::Engine engine;
    AnalyzerTap inputTap, outputTap;
    std::atomic<double> analyzerSampleRate { 44100.0 };
    std::atomic<bool> analyzerActive { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FathomAudioProcessor)
};
