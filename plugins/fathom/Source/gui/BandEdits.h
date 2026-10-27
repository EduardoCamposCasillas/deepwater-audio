#pragma once

#include <algorithm>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "PluginProcessor.h"

namespace fathom::gui
{

// Cambiar los parámetros de las bandas desde el visor como lo haría una perilla: con gestos (el host graba bien la
// automatización y el historial de deshacer sabe dónde empieza y termina cada edición).
//
//   edits.begin();  ... edits.set (...) cuantas veces haga falta (arrastrar) ...  edits.end();   → un paso de deshacer
//
// Cada parámetro abre su gesto la primera vez que se toca y todos se cierran en end().
class BandEdits
{
public:
    explicit BandEdits (FathomAudioProcessor& p) : processor (p), parameters (p.getParameters()) {}
    ~BandEdits() { end(); }

    void begin()
    {
        if (active)
            return;
        active = true;
        processor.getEditHistory().beginTransaction();
    }

    void end()
    {
        if (! active)
            return;
        for (auto* parameter : touched)
            parameter->endChangeGesture();
        touched.clear();
        active = false;
        processor.getEditHistory().endTransaction();
    }

    [[nodiscard]] bool isActive() const noexcept { return active; }

    // Un ajuste de una banda en sus unidades (Hz, dB, Q; índice en las listas; 0/1 en el encendido).
    void set (int band, const char* setting, float value)
    {
        jassert (active);
        auto* parameter = parameters.getParameter (params::band (band, setting));
        if (parameter == nullptr)
            return;
        const float normalised = parameter->convertTo0to1 (value);
        if (normalised == parameter->getValue())
            return;
        if (std::find (touched.begin(), touched.end(), parameter) == touched.end())
        {
            parameter->beginChangeGesture();
            touched.push_back (parameter);
        }
        parameter->setValueNotifyingHost (normalised);
    }

    // Todos los ajustes de una banda (solo se tocan los que cambian). Fase 12: también el canal y si es dinámica o
    // espectral (una banda libre reutilizada, o borrada, no se queda con la dinámica que tuvo); los demás ajustes de la
    // dinámica (umbral, rango...) se conservan: solo cuentan si se enciende.
    void setSettings (int band, const dsp::BandSettings& s)
    {
        set (band, params::enabled, s.enabled ? 1.0f : 0.0f);
        set (band, params::frequency, s.frequency);
        set (band, params::gain, s.gainDb);
        set (band, params::q, s.q);
        set (band, params::shape, static_cast<float> (static_cast<int> (s.shape)));
        set (band, params::slope, static_cast<float> (s.slopeIndex));
        set (band, params::stereo, static_cast<float> (static_cast<int> (s.placement)));
        set (band, params::dynamic, s.dynamics.enabled ? 1.0f : 0.0f);
        set (band, params::spectral, s.dynamics.spectral ? 1.0f : 0.0f);
    }

    // Borrar una banda = devolverla a como nace (apagada, en su frecuencia inicial): queda libre otra vez.
    void reset (int band) { setSettings (band, processor.getDefaultBandSettings (band)); }

private:
    FathomAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& parameters;
    std::vector<juce::RangedAudioParameter*> touched;
    bool active = false;
};

} // namespace fathom::gui
