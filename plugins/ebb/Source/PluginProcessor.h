#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "deepwater/dsp/TransientDetector.h"
#include "deepwater/dsp/TripleBuffer.h"
#include "dsp/Engine.h"
#include "dsp/HostClock.h"
#include "dsp/LiveScope.h"
#include "presets/EbbPresets.h"
#include "TransportSnapshot.h"

// Efecto mono/estéreo. Fase 2: modo tempo, una curva de ducking sincronizada con la posición del host (PlayHead).
// Fase 3: la forma de esa curva es una curva por puntos editable, guardada en el estado del proyecto.
// Fase 4: modo MIDI, cada nota recibida dispara la curva en su muestra exacta.
// Fase 5: modo audio, una entrada sidechain (2.º bus de entrada) cuyos golpes disparan la curva, y lookahead.
// Fase 6: la curva se aplica a toda la señal o a una banda (graves / agudos), y mix dry/wet.
// Fase 7: publica para la GUI el visor en vivo (LiveScope), los golpes de la curva y por dónde va.
// Fase 8: presets (libs/presets), con la curva como estado extra. El nombre del preset y si se modificó viajan con el
// proyecto de FL.
// El audio está en ebb::dsp (sin JUCE); aquí solo se conectan los parámetros, el estado y la GUI.
class EbbAudioProcessor final : public juce::AudioProcessor, private juce::Timer
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
    bool acceptsMidi() const override { return true; }
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

    // Hilo de la GUI: actividad MIDI, para comprobar el ruteo en FL. Contadores de notas recibidas (todas) y de las que
    // dispararon (las que pasan el filtro y en modo MIDI), y el número de la última nota recibida (−1 = ninguna).
    struct MidiActivity
    {
        std::uint32_t notes = 0;
        std::uint32_t triggers = 0;
        int lastNote = -1;
    };
    MidiActivity getMidiActivity() const noexcept
    {
        return { midiNotesReceived.load(), midiTriggersFired.load(), lastMidiNote.load() };
    }

    // Hilo de la GUI: el sidechain. Envolvente más alta desde la lectura anterior (amplitud; la reinicia) y contadores
    // de golpes detectados (siempre) y de los que dispararon la curva (solo en modo audio).
    struct SidechainActivity
    {
        float peakEnvelope = 0.0f;
        std::uint32_t detected = 0;
        std::uint32_t triggers = 0;
    };
    SidechainActivity takeSidechainActivity() noexcept
    {
        return { sidechainPeakSinceRead.exchange (0.0f), sidechainDetected.load(), sidechainTriggersFired.load() };
    }

    // Fase 7, hilo de la GUI. El visor en vivo solo se alimenta mientras la GUI está abierta (setLiveViewOpen).
    const ebb::dsp::LiveScope& getLiveScope() const noexcept { return liveScope; }
    void setLiveViewBars (int bars) noexcept { liveScope.setBars (bars); }
    void setLiveViewOpen (bool open) noexcept { liveViewOpen.store (open); }

    // Fase 7, hilo de la GUI: cuántas curvas empezaron (para la luz de GOLPE) y por dónde va la que suena, en el eje x
    // del editor (−1 = ninguna). Ver Engine::hitCount y Engine::curvePosition.
    std::uint32_t getCurveHits() const noexcept { return curveHits.load (std::memory_order_relaxed); }
    float getCurvePosition() const noexcept { return curvePosition.load (std::memory_order_relaxed); }

    // Fase 8: biblioteca de presets, preset actual y guardar/cargar (solo desde el hilo de mensajes).
    deepwater::presets::PresetManager& getPresetManager() noexcept { return presetManager; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void timerCallback() override;
    [[nodiscard]] int lookaheadSamplesFromParameter (double sampleRate) const noexcept;
    std::span<const ebb::dsp::Trigger> detectSidechain (juce::AudioBuffer<float>& buffer, int numSamples) noexcept;
    ebb::dsp::TransportInfo readHostTransport() noexcept;
    ebb::dsp::TempoSettings readTempoSettings() const noexcept;
    void publishTransport (int numSamples) noexcept;
    void feedLiveScopeSidechain (juce::AudioBuffer<float>& buffer, int numSamples) noexcept;
    void loadCurveFromState();
    std::span<const ebb::dsp::Trigger> collectTriggers (const juce::MidiBuffer& midi, int numSamples) noexcept;

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* divisionParam = nullptr;
    std::atomic<float>* depthParam = nullptr;
    std::atomic<float>* durationParam = nullptr;
    std::atomic<float>* smoothingParam = nullptr;
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* midiNoteParam = nullptr;
    std::atomic<float>* velocityParam = nullptr;
    std::atomic<float>* thresholdParam = nullptr;
    std::atomic<float>* holdParam = nullptr;
    std::atomic<float>* lookaheadParam = nullptr;
    std::atomic<float>* bandParam = nullptr;
    std::atomic<float>* crossoverParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    juce::AudioParameterBool* bypassParameter = nullptr;

    ebb::dsp::Engine engine;
    ebb::dsp::HostClock clock;
    std::atomic<double> currentSampleRate { 44100.0 };
    deepwater::dsp::TripleBuffer<TransportSnapshot> transportBox;
    std::atomic<float> minGainSinceRead { 1.0f };

    // Hilo de mensajes. Empieza en la forma por defecto ANTES de crear el PresetManager (que la toma como la de "Init").
    deepwater::dsp::BreakpointCurve curve = ebb::dsp::defaultCurve();
    deepwater::dsp::TripleBuffer<deepwater::dsp::BreakpointCurve> curveBox; // mensajes → audio
    deepwater::dsp::BreakpointCurve audioCurve;                  // hilo de audio: la última leída del buzón
    std::atomic<int> curveVersion { 0 };

    // Disparos MIDI del bloque actual, reservados de antemano. Si llegan más (no pasa con un kick), gana el último.
    std::array<ebb::dsp::Trigger, 256> triggers {};
    std::atomic<std::uint32_t> midiNotesReceived { 0 }, midiTriggersFired { 0 };
    std::atomic<int> lastMidiNote { -1 };

    // Sidechain (Fase 5): el detector corre siempre (para el medidor), pero solo dispara la curva en modo audio.
    deepwater::dsp::TransientDetector detector;
    std::array<ebb::dsp::Trigger, 256> audioTriggers {};
    std::atomic<float> sidechainPeakSinceRead { 0.0f };
    std::atomic<std::uint32_t> sidechainDetected { 0 }, sidechainTriggersFired { 0 };

    // Lookahead en muestras YA informado al host. Lo cambia el temporizador (hilo de mensajes): avisar al host de una
    // latencia nueva toma un lock dentro de JUCE, y eso no se hace en el hilo de audio. El audio usa este valor, así el
    // retraso real y la latencia informada coinciden.
    std::atomic<int> reportedLookahead { 0 };

    // Fase 7: para la GUI.
    ebb::dsp::LiveScope liveScope;
    std::atomic<bool> liveViewOpen { false };
    std::atomic<std::uint32_t> curveHits { 0 };
    std::atomic<float> curvePosition { -1.0f };

    // Al final: usa 'parameters' y 'curve' desde su constructor.
    deepwater::presets::PresetManager presetManager {
        parameters, ebb::presets::format ([this] { return curve; }, [this] (const deepwater::dsp::BreakpointCurve& c) { setCurve (c); })
    };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EbbAudioProcessor)
};
