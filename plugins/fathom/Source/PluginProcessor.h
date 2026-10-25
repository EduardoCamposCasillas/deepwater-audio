#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "analysis/SpectrumView.h"
#include "deepwater/dsp/AnalyzerTap.h"
#include "dsp/Engine.h"
#include "edit/EditHistory.h"

// Efecto mono/estéreo: 24 bandas de EQ con todas las formas y la ganancia de salida (Fase 3), editables sobre el visor
// con deshacer/rehacer (Fase 4). El audio deja una copia de lo que entra, de lo que sale y del sidechain (Fase 5) en tres
// tomas lock-free (AnalyzerTap) para el analizador de la GUI. Para escuchar y comparar (Fase 6): solo de banda, auto
// gain, escala de ganancia, balance, polaridad y comparación A/B. El audio está en fathom::dsp (sin JUCE); aquí solo se
// conectan los parámetros, el estado y la GUI.
class FathomAudioProcessor final : public juce::AudioProcessor
{
public:
    // 32768 muestras: la FFT más grande del analizador (resolución máxima a 88.2/96 kHz).
    using AnalyzerTap = deepwater::dsp::AnalyzerTap<32768>;

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

    // Los ajustes con que nace cada banda (los valores por defecto de sus parámetros). Una banda apagada y así está
    // "libre": el doble clic del visor la usa para crear una nueva.
    const fathom::dsp::BandSettings& getDefaultBandSettings (int band) const noexcept { return defaultBands[static_cast<size_t> (band)]; }

    // Hilo de la GUI: deshacer/rehacer. Vive en el procesador y no en el editor: sobrevive a cerrar y abrir la ventana.
    fathom::edit::EditHistory& getEditHistory() noexcept { return history; }

    // Hilo de la GUI: las tomas del analizador y la frecuencia de muestreo con la que se escribieron.
    const AnalyzerTap& getInputTap() const noexcept { return inputTap; }
    const AnalyzerTap& getOutputTap() const noexcept { return outputTap; }
    const AnalyzerTap& getSidechainTap() const noexcept { return sidechainTap; }

    // Hilo de la GUI: si el host conectó el bus de sidechain (en FL: "Sidechain to this track" y el puerto en Processing).
    bool isSidechainConnected() const;

    // Hilo de la GUI: cómo se ve el analizador (inclinación, rango, resolución...). No son parámetros (no tiene sentido
    // automatizarlos) pero se guardan con el proyecto: viven como un hijo del estado del APVTS.
    fathom::analysis::ViewSettings getAnalyzerView() const;
    void setAnalyzerView (const fathom::analysis::ViewSettings&);
    double getAnalyzerSampleRate() const noexcept { return analyzerSampleRate.load (std::memory_order_relaxed); }

    // Hilo de la GUI: true mientras haya un editor abierto (sin él, el audio no copia muestras a las tomas).
    void setAnalyzerActive (bool active) noexcept { analyzerActive.store (active, std::memory_order_relaxed); }

    // Solo de banda: la banda (0-23) cuya zona se escucha, o −1. No es un parámetro (no se guarda ni se automatiza: es
    // para escuchar mientras se edita); el editor lo apaga al cerrarse.
    void setSoloBand (int band) noexcept { soloBand.store (band, std::memory_order_relaxed); }
    int getSoloBand() const noexcept { return soloBand.load (std::memory_order_relaxed); }

    // Lo que está compensando el auto gain ahora (0 dB si está apagado), para mostrarlo.
    float getAutoGainDb() const noexcept { return autoGainDb.load (std::memory_order_relaxed); }

    // Comparación A/B (hilo de la GUI). Dos estados completos (todos los parámetros menos el bypass): los parámetros son
    // el lado activo y el otro se guarda aparte (en el proyecto, como hijo "Compare" del estado). Cada lado tiene su
    // propio historial de deshacer. switchCompareSide devuelve false si hay una edición a medias.
    int getCompareSide() const; // 0 = A, 1 = B
    bool switchCompareSide();
    void copyToOtherSide();     // el lado activo pasa a ser también el otro (A → B o B → A)

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* outputGainParam = nullptr;
    std::atomic<float>* gainScaleParam = nullptr;
    std::atomic<float>* outputPanParam = nullptr;
    std::atomic<float>* phaseInvertParam = nullptr;
    std::atomic<float>* autoGainParam = nullptr;
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
    fathom::dsp::Engine::BandArray defaultBands;

    fathom::dsp::Engine engine;
    AnalyzerTap inputTap, outputTap, sidechainTap;
    std::atomic<double> analyzerSampleRate { 44100.0 };
    std::atomic<bool> analyzerActive { false };
    std::atomic<int> soloBand { -1 };
    std::atomic<float> autoGainDb { 0.0f };
    fathom::edit::EditHistory history; // después de 'parameters': se destruye antes (deja de escucharlos)
    fathom::edit::UndoStack otherSideHistory; // el historial del lado de la comparación que no está activo

    void setOutputSettings() noexcept; // pasa al motor la escala, el balance, la polaridad y el auto gain
    std::vector<float> otherSideValues() const;
    void storeOtherSide (const std::vector<float>& values);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FathomAudioProcessor)
};
