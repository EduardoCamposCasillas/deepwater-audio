#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "analysis/Masking.h"
#include "analysis/SpectrumView.h"
#include "deepwater/dsp/SpectrumAnalyzer.h"
#include "gui/BandEdits.h"
#include "gui/Theme.h"

namespace fathom::gui
{

// Fase 14: lo que el visor sabe de las DEMÁS instancias de Fathom del proyecto (link/InstanceRegistry.h). Corre en el
// hilo de mensajes, una vez por cuadro:
//  - El espectro de salida de cada otra instancia que se ve (lo que esa pista manda a la mezcla), analizado aquí con las
//    muestras que esa instancia deja en su toma: mientras alguien mira, todas copian su salida aunque su ventana esté
//    cerrada.
//  - Los choques (analysis/Masking.h) entre la salida de ESTA instancia y la de cada otra: espectros promediados unos
//    segundos, en bandas críticas.
//  - La curva y los nodos de la instancia que se edita desde aquí, y los gestos de edición sobre sus parámetros
//    (BandEdits sobre su procesador: el paso de deshacer queda en SU historial).
// Nunca guarda punteros a otra instancia de un cuadro al siguiente: la busca por su número cada vez. Si una instancia se
// borra (se quita el plugin de la pista) mientras se la edita, la lista avisa antes de que desaparezca y la edición se
// cierra ahí.
class InstanceLink
{
public:
    static constexpr int maxShown = 8;   // cuántas otras se ven a la vez (más, y el visor se vuelve ilegible)
    static constexpr double staleSeconds = 0.25;
    static constexpr float tiltForMasking = 4.5f; // la inclinación de los choques (fija: no depende de la vista)

    // Un análisis: la última ventana de una toma, nivel por punto (sin inclinación) y lo que entra al promedio lento.
    struct Trace
    {
        deepwater::dsp::SpectrumAnalyzer analyzer;
        std::uint64_t lastWritten = 0;
        double secondsWithoutAudio = 1.0e9;
        std::vector<float> raw, slowInput;
    };

    struct Other
    {
        int id = 0;
        juce::String name;
        juce::Colour colour;
        bool sounding = false;
        double sampleRate = 44100.0;
        Trace trace;
        std::vector<float> shown;          // su salida como se dibuja (con la inclinación y la velocidad de la vista)
        analysis::SlowSpectrum slow;       // promediada para los choques (con la inclinación fija)
        std::vector<float> collision;      // choque con esta instancia, por punto (0-1)
        std::vector<analysis::Collision> zones;

        // Solo la editable: sus bandas, su curva total (dB por punto, con su salida y su escala) y sus nodos.
        std::array<dsp::BandSettings, dsp::Engine::numBands> bands {};
        std::array<dsp::BandSettings, dsp::Engine::numBands> defaults {};
        std::array<float, dsp::Engine::numBands> nodeDb {};
        std::vector<float> curveDb;
        float outputDb = 0.0f, scale = 1.0f;
    };

    explicit InstanceLink (FathomAudioProcessor& p) : self (p), registry (FathomAudioProcessor::Registry::shared())
    {
        listenerToken = registry.addListener ([this] (int id) { instanceRemoved (id); });
    }

    ~InstanceLink()
    {
        edits.reset(); // cierra el gesto sobre la otra (que sigue viva: si no, la lista ya habría avisado)
        registry.removeListener (listenerToken);
        setWatching (false);
    }

    // El panel abierto también mira (para mostrar quién suena aunque todavía no se vea ninguna).
    void setPanelOpen (bool open) { panelOpen = open; }

    // Una vez por cuadro. 'points' = los del visor (uno cada 2 px, de minHz a maxHz). Devuelve true si algo visible cambió.
    bool update (const analysis::LogAxis& axis, const analysis::ViewSettings& view, double elapsed)
    {
        auto& settings = self.getLinkSettings();
        bool changed = pruneShown (settings);
        setWatching (panelOpen || ! settings.shown.empty());
        if (axis.points != pointsAxis.points)
        {
            pointsAxis = axis;
            for (auto& other : others)
            {
                other.shown.assign (axis.points, deepwater::dsp::SpectrumAnalyzer::floorDb);
                other.slow.reset();
                other.collision.clear();
                other.zones.clear();
            }
            selfSlow.reset();
            changed = true;
        }

        // Esta instancia: su salida, solo si hay con quién chocar.
        const bool needSelf = settings.collisions && ! settings.shown.empty();
        if (needSelf)
            changed = analyse (selfTrace, self.getOutputTap(), self.getAnalyzerSampleRate(), elapsed) || changed;
        else if (! selfSlow.db.empty())
            selfSlow.reset();
        if (needSelf)
            selfSlow.feed (selfTrace.slowInput, elapsed);

        // Las otras que se ven, en el orden en que se eligieron.
        std::vector<Other> next;
        for (const int id : settings.shown)
        {
            auto* processor = registry.find (id);
            if (processor == nullptr || processor == &self)
                continue;
            Other other = take (id);
            other.id = id;
            other.name = processor->getInstanceName();
            other.colour = colourFor (*processor);
            other.sampleRate = processor->getAnalyzerSampleRate();
            if (other.shown.size() != axis.points)
                other.shown.assign (axis.points, deepwater::dsp::SpectrumAnalyzer::floorDb);
            changed = analyseOther (other, *processor, view, elapsed) || changed;
            if (needSelf && ! selfSlow.db.empty() && ! other.slow.db.empty())
            {
                const auto a = analysis::erbSmooth (selfSlow.db, axis);
                const auto b = analysis::erbSmooth (other.slow.db, axis);
                const auto& sensitivity = analysis::maskingSensitivities[static_cast<size_t> (
                    std::clamp (settings.sensitivityIndex, 0, static_cast<int> (analysis::maskingSensitivities.size()) - 1))];
                other.collision = analysis::collisionAmount (a, b, axis, sensitivity.spanDb);
                other.zones = analysis::collisionZones (other.collision, a, b, axis);
            }
            else
            {
                changed = changed || ! other.collision.empty();
                other.collision.clear();
                other.zones.clear();
            }
            if (id == settings.editable)
                changed = refreshEditable (other, *processor) || changed;
            next.push_back (std::move (other));
        }
        others = std::move (next);
        if (settings.editable < 0 || registry.find (settings.editable) == nullptr)
            edits.reset();
        return changed;
    }

    const std::vector<Other>& getShown() const noexcept { return others; }

    // La que se edita desde aquí (o nullptr).
    const Other* getEditable() const noexcept
    {
        for (const auto& other : others)
            if (other.id == self.getLinkSettings().editable)
                return &other;
        return nullptr;
    }

    // Para el panel: todas las demás que existen ahora (número, nombre, color y si mandan audio).
    struct Available
    {
        int id;
        juce::String name;
        juce::Colour colour;
        bool sounding;
    };
    std::vector<Available> available()
    {
        std::vector<Available> list;
        for (const int id : registry.ids())
            if (auto* processor = registry.find (id); processor != nullptr && processor != &self)
            {
                auto& probe = probes[id];
                const auto written = processor->getOutputTap().getWritten();
                const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
                if (written != probe.lastWritten)
                {
                    probe.lastWritten = written;
                    probe.lastAudioSeconds = now;
                }
                list.push_back ({ id, processor->getInstanceName(), colourFor (*processor), now - probe.lastAudioSeconds < 0.5 });
            }
        return list;
    }

    // Los gestos sobre la instancia editable (nullptr si no hay). Se crean al primer uso y se cierran si cambia.
    BandEdits* editsForEditable()
    {
        const int id = self.getLinkSettings().editable;
        auto* processor = id >= 0 ? registry.find (id) : nullptr;
        if (processor == nullptr)
        {
            edits.reset();
            return nullptr;
        }
        if (edits == nullptr || editsId != id)
        {
            edits.reset();
            edits = std::make_unique<BandEdits> (*processor);
            editsId = id;
        }
        return edits.get();
    }

    // Los ajustes de ahora de una banda de la editable (leídos de sus atómicos).
    bool readEditableBand (int band, dsp::BandSettings& out) const
    {
        const int id = self.getLinkSettings().editable;
        if (auto* processor = id >= 0 ? registry.find (id) : nullptr; processor != nullptr)
        {
            out = processor->getBandSettings (band);
            return true;
        }
        return false;
    }

    // Una banda "en uso" de la editable (encendida o tocada), como las propias del visor.
    static bool inUse (const Other& other, int b) noexcept
    {
        const auto& s = other.bands[static_cast<size_t> (b)];
        const auto& d = other.defaults[static_cast<size_t> (b)];
        const auto near = [] (float x, float y) { return std::abs (x - y) <= 1.0e-4f * std::max (1.0f, std::abs (y)); };
        return s.enabled || ! (near (s.frequency, d.frequency) && near (s.gainDb, d.gainDb) && near (s.q, d.q) && s.shape == d.shape
                               && s.slopeIndex == d.slopeIndex && s.placement == d.placement);
    }

    // El color de una instancia: el de su pista si FL lo manda (y no es casi negro), si no uno por número.
    static juce::Colour colourFor (const FathomAudioProcessor& processor)
    {
        if (const auto track = processor.getTrackColour(); track.has_value() && track->getBrightness() > 0.25f)
            return track->withAlpha (1.0f);
        return theme::instanceColour (processor.getInstanceId());
    }

private:
    struct Probe
    {
        std::uint64_t lastWritten = 0;
        double lastAudioSeconds = -1.0e9;
    };

    // Analiza la toma si llegó audio nuevo y deja el nivel de cada punto. Devuelve true si llegó algo.
    bool analyse (Trace& trace, const FathomAudioProcessor::AnalyzerTap& tap, double sampleRate, double elapsed)
    {
        const int order = analysis::fftOrder (1, sampleRate); // ~93 ms: los choques miran el balance, no los golpes
        if (order != trace.analyzer.getOrder())
            trace.analyzer.prepare (order);
        const auto size = static_cast<size_t> (trace.analyzer.getSize());
        if (scratch.size() < size)
            scratch.resize (size);
        const auto written = tap.getWritten();
        bool fresh = false;
        if (written != trace.lastWritten && tap.copyLatest (scratch.data(), size))
        {
            trace.lastWritten = written;
            trace.secondsWithoutAudio = 0.0;
            trace.analyzer.analyze (scratch.data());
            fresh = true;
        }
        else
        {
            trace.secondsWithoutAudio += elapsed;
        }
        const bool silent = trace.secondsWithoutAudio > staleSeconds;
        const auto points = pointsAxis.points;
        trace.raw.resize (points);
        trace.slowInput.resize (points);
        const double halfStep = points < 2 ? 1.0 : std::sqrt (pointsAxis.hz (1) / pointsAxis.hz (0));
        for (size_t i = 0; i < points; ++i)
        {
            const double hz = pointsAxis.hz (i);
            trace.raw[i] = silent ? deepwater::dsp::SpectrumAnalyzer::floorDb : trace.analyzer.levelDb (hz / halfStep, hz * halfStep, sampleRate);
            trace.slowInput[i] = trace.raw[i] + analysis::tiltDb (hz, tiltForMasking);
        }
        return fresh || ! silent;
    }

    bool analyseOther (Other& other, const FathomAudioProcessor& processor, const analysis::ViewSettings& view, double elapsed)
    {
        auto& trace = other.trace;
        analyse (trace, processor.getOutputTap(), other.sampleRate, elapsed);
        other.sounding = trace.secondsWithoutAudio <= staleSeconds;
        other.slow.feed (trace.slowInput, elapsed);

        bool changed = false;
        for (size_t i = 0; i < other.shown.size() && i < trace.raw.size(); ++i)
        {
            const float previous = other.shown[i];
            other.shown[i] = analysis::follow (previous, trace.raw[i] + analysis::tiltDb (pointsAxis.hz (i), view.tilt()), elapsed, view.speed());
            changed = changed || std::abs (other.shown[i] - previous) >= 0.05f;
        }
        return changed;
    }

    // La curva de la editable: sus bandas encendidas (en reposo, con su escala) más su salida. Sin su fase lineal ni lo
    // que mueve su dinámica: es para ubicar y mover sus nodos, no para medirla.
    bool refreshEditable (Other& other, FathomAudioProcessor& processor)
    {
        auto& parameters = processor.getParameters();
        const float outputDb = parameters.getRawParameterValue (params::outputGain)->load();
        const float scale = parameters.getRawParameterValue (params::gainScale)->load() * 0.01f;
        bool changed = other.curveDb.size() != pointsAxis.points || outputDb != other.outputDb || scale != other.scale;
        for (int b = 0; b < dsp::Engine::numBands; ++b)
        {
            const auto settings = processor.getBandSettings (b);
            auto& painted = other.bands[static_cast<size_t> (b)];
            changed = changed || ! sameBand (settings, painted);
            painted = settings;
            other.defaults[static_cast<size_t> (b)] = processor.getDefaultBandSettings (b);
        }
        if (! changed || other.sampleRate <= 0.0)
            return false;
        other.outputDb = outputDb;
        other.scale = scale;
        other.curveDb.assign (pointsAxis.points, outputDb);
        for (int b = 0; b < dsp::Engine::numBands; ++b)
        {
            auto settings = other.bands[static_cast<size_t> (b)];
            settings.gainDb *= scale;
            auto active = settings;
            active.enabled = true;
            const auto cascade = dsp::Band::coefficientsFor (active, other.sampleRate);
            other.nodeDb[static_cast<size_t> (b)] =
                dsp::usesGain (settings.shape) ? static_cast<float> (cascade.magnitudeDb (settings.frequency, other.sampleRate)) : 0.0f;
            if (settings.enabled)
                for (size_t i = 0; i < pointsAxis.points; ++i)
                    other.curveDb[i] += static_cast<float> (cascade.magnitudeDb (pointsAxis.hz (i), other.sampleRate));
        }
        return true;
    }

    static bool sameBand (const dsp::BandSettings& a, const dsp::BandSettings& b) noexcept
    {
        return a.enabled == b.enabled && a.frequency == b.frequency && a.gainDb == b.gainDb && a.q == b.q && a.shape == b.shape
               && a.slopeIndex == b.slopeIndex && a.analog == b.analog && a.placement == b.placement;
    }

    // Lo que ya se sabía de esa instancia (su analizador, sus promedios), o uno nuevo.
    Other take (int id)
    {
        for (auto& other : others)
            if (other.id == id)
                return std::move (other);
        return {};
    }

    // Las que ya no existen salen de la lista; más de maxShown no se ven.
    bool pruneShown (link::LinkSettings& settings)
    {
        bool changed = false;
        for (const int id : std::vector<int> (settings.shown))
            if (registry.find (id) == nullptr || id == self.getInstanceId())
            {
                settings.forget (id);
                changed = true;
            }
        while (settings.shown.size() > static_cast<size_t> (maxShown))
            settings.setShown (settings.shown.front(), false);
        return changed;
    }

    // La lista avisa (antes de que desaparezca) que una instancia se va: si se estaba editando, la edición se cierra.
    void instanceRemoved (int id)
    {
        if (edits != nullptr && editsId == id)
            edits.reset();
        self.getLinkSettings().forget (id);
        probes.erase (id);
    }

    void setWatching (bool watch)
    {
        if (watch == watching)
            return;
        watching = watch;
        if (watch)
            registry.startWatching();
        else
            registry.stopWatching();
    }

    FathomAudioProcessor& self;
    FathomAudioProcessor::Registry& registry;
    int listenerToken = 0;
    bool watching = false, panelOpen = false;
    analysis::LogAxis pointsAxis;
    Trace selfTrace;
    analysis::SlowSpectrum selfSlow;
    std::vector<Other> others;
    std::map<int, Probe> probes;
    std::vector<float> scratch;
    std::unique_ptr<BandEdits> edits;
    int editsId = -1;
};

} // namespace fathom::gui
