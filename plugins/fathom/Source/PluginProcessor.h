#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "analysis/SpectrumView.h"
#include "deepwater/dsp/AnalyzerTap.h"
#include "dsp/Engine.h"
#include "edit/CurveTools.h"
#include "edit/EditHistory.h"
#include "link/InstanceRegistry.h"

// Efecto mono/estéreo: 24 bandas de EQ con todas las formas y la ganancia de salida (Fase 3), editables sobre el visor
// con deshacer/rehacer (Fase 4). El audio deja una copia de lo que entra, de lo que sale y del sidechain (Fase 5) en tres
// tomas lock-free (AnalyzerTap) para el analizador de la GUI. Para escuchar y comparar (Fase 6): solo de banda, auto
// gain, escala de ganancia, balance, polaridad y comparación A/B. Cada banda en estéreo, L, R, mid o side y el
// analizador también del side (Fase 8). EQ dinámico por banda, con el detector en la banda o en el sidechain (Fase 9).
// Modos de fase cero latencia / natural / lineal, con la latencia informada al host (Fase 10). Dinámica espectral por banda
// (Fase 11). Igualar a una referencia y dibujar la curva, convertidas en bandas (Fase 12: todo en la GUI). Color analógico
// (Fase 13). Cada instancia se anota en una lista del proceso para que las demás vean su espectro, sus choques y sus
// bandas, y puedan editarlas (Fase 14).
// El audio está en fathom::dsp (sin JUCE); aquí solo se conectan los parámetros, el estado y la GUI.
class FathomAudioProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    // 32768 muestras: la FFT más grande del analizador (resolución máxima a 88.2/96 kHz).
    using AnalyzerTap = deepwater::dsp::AnalyzerTap<32768>;

    FathomAudioProcessor();
    ~FathomAudioProcessor() override;

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
    double getTailLengthSeconds() const override;

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

    // Fase 12, hilo de la GUI: lo aprendido para IGUALAR, lo dibujado y sus ajustes. Aquí y no en el editor por lo mismo
    // que el historial; no se guarda en el proyecto (lo que queda son las bandas creadas).
    fathom::edit::CurveTools& getCurveTools() noexcept { return curveTools; }

    // Hilo de la GUI: las tomas del analizador y la frecuencia de muestreo con la que se escribieron.
    const AnalyzerTap& getInputTap() const noexcept { return inputTap; }
    const AnalyzerTap& getOutputTap() const noexcept { return outputTap; }
    const AnalyzerTap& getSidechainTap() const noexcept { return sidechainTap; }
    // Fase 8: lo mismo pero del side, (L − R) / 2 (las de arriba son el mid, (L + R) / 2). En mono no se escriben.
    const AnalyzerTap& getInputSideTap() const noexcept { return inputSideTap; }
    const AnalyzerTap& getOutputSideTap() const noexcept { return outputSideTap; }
    const AnalyzerTap& getSidechainSideTap() const noexcept { return sidechainSideTap; }

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

    // Fase 10: el modo de fase y la calidad pedidos (leen los parámetros: sirven en los dos hilos).
    fathom::dsp::PhaseMode getPhaseMode() const noexcept;
    int getLinearQuality() const noexcept;

    // Fase 13: el color analógico pedido y la latencia que agrega cualquier color a la frecuencia de ahora.
    fathom::dsp::ColorMode getColorMode() const noexcept;
    int getColorLatency() const noexcept { return colorLatency.load (std::memory_order_relaxed); }

    // Fase 9, hilo de la GUI: lo que la dinámica le suma ahora a una banda (dB, con la escala: la curva en vivo), el
    // nivel de su zona y el umbral que usa (con el automático, el que calculó). Sin audio se quedan en lo último.
    // Fase 11, con una banda espectral: el bin que más se mueve, el nivel del más fuerte del centro de la zona y el umbral.
    struct DynamicState
    {
        float gainDb = 0.0f, levelDb = -120.0f, thresholdDb = 0.0f;
    };
    DynamicState getDynamicState (int band) const noexcept
    {
        const auto& live = dynamicStates[static_cast<size_t> (band)];
        return { live.gainDb.load (std::memory_order_relaxed), live.levelDb.load (std::memory_order_relaxed),
                 live.thresholdDb.load (std::memory_order_relaxed) };
    }

    // Comparación A/B (hilo de la GUI). Dos estados completos (todos los parámetros menos el bypass): los parámetros son
    // el lado activo y el otro se guarda aparte (en el proyecto, como hijo "Compare" del estado). Cada lado tiene su
    // propio historial de deshacer. switchCompareSide devuelve false si hay una edición a medias.
    // Fase 11, hilo de la GUI (un solo lector: el visor): lo que mueve ahora cada banda espectral, en la grilla de
    // SpectralStage. Devuelve true si llegó algo nuevo.
    bool readSpectralSnapshot (fathom::dsp::SpectralStage::Snapshot& out) noexcept { return engine.readSpectralSnapshot (out); }

    int getCompareSide() const; // 0 = A, 1 = B
    bool switchCompareSide();
    void copyToOtherSide();     // el lado activo pasa a ser también el otro (A → B o B → A)

    // Fase 14, hilo de mensajes: las demás instancias de Fathom del proceso (link/InstanceRegistry.h).
    using Registry = fathom::link::InstanceRegistry<FathomAudioProcessor>;
    int getInstanceId() const noexcept { return instanceId; }
    // Cómo se llama esta instancia para las demás: el nombre puesto a mano, si no el de la pista del mixer de FL (si FL
    // lo manda), si no "Fathom N". El puesto a mano se guarda con el proyecto (no es un parámetro).
    juce::String getInstanceName() const;
    juce::String getCustomName() const;
    void setCustomName (const juce::String& name);
    juce::String getTrackName() const { return trackName; }
    std::optional<juce::Colour> getTrackColour() const { return trackColour; }
    void updateTrackProperties (const TrackProperties& properties) override;
    // Qué ve esta instancia de las demás (solo la GUI; no se guarda: los números cambian al abrir el proyecto).
    fathom::link::LinkSettings& getLinkSettings() noexcept { return linkSettings; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* outputGainParam = nullptr;
    std::atomic<float>* gainScaleParam = nullptr;
    std::atomic<float>* outputPanParam = nullptr;
    std::atomic<float>* phaseInvertParam = nullptr;
    std::atomic<float>* autoGainParam = nullptr;
    std::atomic<float>* analogResponseParam = nullptr;
    std::atomic<float>* phaseModeParam = nullptr;
    std::atomic<float>* linearQualityParam = nullptr;
    std::atomic<float>* analogColorParam = nullptr;
    struct BandParameters
    {
        std::atomic<float>* enabled = nullptr;
        std::atomic<float>* frequency = nullptr;
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* q = nullptr;
        std::atomic<float>* shape = nullptr;
        std::atomic<float>* slope = nullptr;
        std::atomic<float>* stereo = nullptr;
        std::atomic<float>* dynamic = nullptr; // Fase 9
        std::atomic<float>* dynamicRange = nullptr;
        std::atomic<float>* threshold = nullptr;
        std::atomic<float>* attack = nullptr;
        std::atomic<float>* release = nullptr;
        std::atomic<float>* autoThreshold = nullptr;
        std::atomic<float>* dynamicSource = nullptr;
        std::atomic<float>* spectral = nullptr; // Fase 11
    };
    std::array<BandParameters, fathom::dsp::Engine::numBands> bandParams;
    juce::AudioParameterBool* bypassParameter = nullptr;
    fathom::dsp::Engine::BandArray defaultBands;

    fathom::dsp::Engine engine;
    AnalyzerTap inputTap, outputTap, sidechainTap;
    AnalyzerTap inputSideTap, outputSideTap, sidechainSideTap;
    std::atomic<double> analyzerSampleRate { 44100.0 };
    std::atomic<bool> analyzerActive { false };
    std::atomic<int> soloBand { -1 };
    std::atomic<float> autoGainDb { 0.0f };
    std::atomic<int> colorLatency { 0 };
    std::atomic<int> engineLatency { 0 }; // la latencia que pide el motor (el temporizador se la informa al host)
    std::atomic<double> preparedSampleRate { 44100.0 };
    struct LiveDynamics
    {
        std::atomic<float> gainDb { 0.0f }, levelDb { -120.0f }, thresholdDb { 0.0f };
    };
    std::array<LiveDynamics, fathom::dsp::Engine::numBands> dynamicStates;
    fathom::edit::EditHistory history; // después de 'parameters': se destruye antes (deja de escucharlos)
    fathom::edit::UndoStack otherSideHistory; // el historial del lado de la comparación que no está activo
    fathom::edit::CurveTools curveTools;

    // Fase 14. La lista se guarda como referencia: el audio solo lee su atómico (sin pasar por la inicialización
    // de la variable estática en cada bloque).
    Registry& registry = Registry::shared();
    int instanceId = 0;
    juce::String trackName;
    std::optional<juce::Colour> trackColour;
    fathom::link::LinkSettings linkSettings;

    // Hilo de audio: el mid y el side de unos canales a sus tomas.
    static void pushTaps (AnalyzerTap& mid, AnalyzerTap& side, const float* const* channels, int numChannels, int numSamples) noexcept;
    void setOutputSettings() noexcept; // pasa al motor la escala, el balance, la polaridad, el auto gain y el modo de fase
    void timerCallback() override;     // hilo de mensajes: informa al host la latencia nueva (cambiar de modo de fase)
    std::vector<float> otherSideValues() const;
    void storeOtherSide (const std::vector<float>& values);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FathomAudioProcessor)
};
