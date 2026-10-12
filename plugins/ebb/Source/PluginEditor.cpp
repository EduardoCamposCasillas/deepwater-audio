#include "PluginEditor.h"
#include "Parameters.h"
#include "PluginProcessor.h"

namespace
{
constexpr int editorWidth = 960;
constexpr int editorHeight = 750;
constexpr int headerHeight = 44;
constexpr int topRowHeight = 170;   // transporte + vista previa de la curva
constexpr int transportWidth = 500; // cabe "123 . 4 . 4" y los detalles
constexpr int curveEditorHeight = 210;
constexpr int controlsHeight = 160;
constexpr int sidechainHeight = 124;
constexpr int bandsWidth = 330; // panel BANDAS, a la derecha del editor de la curva
constexpr int footerHeight = 26;
constexpr int gap = 8;
constexpr int padding = 12;
constexpr int framesPerSecond = 30;
constexpr int customShapeId = 1000; // id del menú para "Personalizada" (las formas de fábrica van de 1 en adelante)

juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }
} // namespace

EbbAudioProcessorEditor::EbbAudioProcessorEditor (EbbAudioProcessor& p) : AudioProcessorEditor (p), processor (p)
{
    namespace id = ebb::params;
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (transportView);
    addAndMakeVisible (curveView);
    addAndMakeVisible (curveEditor);
    curveEditor.onChange = [this] (const deepwater::dsp::BreakpointCurve& curve) {
        processor.setCurve (curve);
        showCurve();
    };

    // Fase 4: modo y opciones MIDI.
    setUpComboLabel (modeLabel, "MODO");
    setUpCombo (modeBox, id::mode,
                utf8 ("Qu\xc3\xa9 dispara la curva. Tempo: una cuadr\xc3\xad" "cula fija (cada 1/4, 1/8...). MIDI: cada nota "
                      "que le llega a Ebb (copia las notas del kick a un canal MIDI Out): sigue el patr\xc3\xb3n real del "
                      "kick, aunque sea irregular como el del dembow."),
                modeAttachment);
    setUpComboLabel (noteLabel, "NOTA MIDI");
    setUpCombo (noteBox, id::midiNote,
                utf8 ("Qu\xc3\xa9 nota dispara la curva. Todas: cualquier nota. Una nota: solo esa (para mandar todo el "
                      "patr\xc3\xb3n de bater\xc3\xad" "a por el mismo canal y que solo el kick dispare). Nombres como en "
                      "FL: C5 = do central (MIDI 60)."),
                noteAttachment);

    setUpComboLabel (midiLabel, "ACTIVIDAD MIDI");
    midiActivity.setTooltip (utf8 ("Se enciende con cada nota que llega. Verde agua: dispar\xc3\xb3 la curva. \xc3\x81mbar: "
                                   "lleg\xc3\xb3 pero no dispar\xc3\xb3 (modo Tempo u otra nota). Apagada: no llega MIDI; "
                                   "revisa el puerto MIDI de Ebb y el del canal MIDI Out."));
    addAndMakeVisible (midiActivity);
    useLastNoteButton.setButtonText (utf8 ("USAR \xc3\x9aLTIMA NOTA"));
    useLastNoteButton.setTooltip (utf8 ("Pone en NOTA MIDI la \xc3\xbaltima nota que lleg\xc3\xb3: toca la nota del kick y "
                                        "pulsa aqu\xc3\xad."));
    useLastNoteButton.onClick = [this] { useLastMidiNote(); };
    addAndMakeVisible (useLastNoteButton);
    velocityButton.setButtonText ("VELOCIDAD");
    velocityButton.setClickingTogglesState (true);
    velocityButton.setTooltip (utf8 ("Encendido: la velocidad de cada nota escala la profundidad (velocidad 64 = la mitad "
                                     "de la ca\xc3\xad" "da). Los kicks fantasma o m\xc3\xa1s suaves bajan menos el bajo. "
                                     "Apagado: todos los golpes bajan igual."));
    addAndMakeVisible (velocityButton);
    velocityAttachment = std::make_unique<ButtonAttachment> (processor.getParameters(), id::velocity, velocityButton);

    setUpComboLabel (divisionLabel, utf8 ("DIVISI\xc3\x93N"));
    setUpCombo (divisionBox, id::division, {}, divisionAttachment); // el tooltip depende del modo (updateModeControls)

    // Formas de fábrica: elegir una la copia al editor (y se puede seguir editando: entonces pasa a "Personalizada").
    setUpComboLabel (shapeLabel, "FORMA");
    for (size_t i = 0; i < ebb::dsp::curveShapes.size(); ++i)
        shapeBox.addItem (utf8 (ebb::dsp::curveShapes[i].name), static_cast<int> (i) + 1);
    shapeBox.addSeparator();
    shapeBox.addItem ("Personalizada", customShapeId);
    shapeBox.setItemEnabled (customShapeId, false); // se llega editando, no eligiéndola
    shapeBox.setTooltip (utf8 ("Formas de ca\xc3\xad" "da y vuelta del volumen. Cl\xc3\xa1sica: la de la Fase 2. R\xc3\xa1pida: se "
                               "nota poco (mezcla). Lenta: bombeo marcado. Retenci\xc3\xb3n: deja pasar un kick largo. Entrada "
                               "suave: sin clic. Eco y Puerta: efectos r\xc3\xadtmicos. Al editar los puntos pasa a "
                               "\"Personalizada\"."));
    shapeBox.onChange = [this] {
        const int index = shapeBox.getSelectedId() - 1;
        if (index >= 0 && index < static_cast<int> (ebb::dsp::curveShapes.size()))
        {
            processor.setCurve (ebb::dsp::curveShapes[static_cast<size_t> (index)].curve);
            showCurve();
        }
    };
    addAndMakeVisible (shapeBox);

    setUpKnob (depthKnob, id::depth, "PROFUNDIDAD",
               utf8 ("Cu\xc3\xa1nto baja el volumen en el golpe. 100 % = silencio. Entre par\xc3\xa9ntesis, lo mismo en dB: "
                     "para limpiar graves en la mezcla bastan 15\xe2\x80\x93" "30 % (1.5\xe2\x80\x93" "3 dB); el bombeo "
                     "como efecto empieza hacia el 60 %."));
    setUpKnob (durationKnob, id::duration, utf8 ("DURACI\xc3\x93N"),
               utf8 ("Qu\xc3\xa9 parte del ciclo dura la curva del editor. 100 % = la curva ocupa el ciclo entero (bombeo "
                     "continuo); valores bajos = una ca\xc3\xad" "da corta y el resto del ciclo en el valor del \xc3\xbaltimo "
                     "punto (normalmente, volumen completo)."));
    setUpKnob (smoothingKnob, id::smoothing, "SUAVIZADO",
               utf8 ("Redondea los cambios bruscos de volumen (sobre todo la ca\xc3\xad" "da del golpe). 0 ms = ca\xc3\xad" "da "
                     "instant\xc3\xa1nea: en un bajo se oye un clic. 1\xe2\x80\x93" "5 ms = sin clic y con golpe. "
                     "M\xc3\xa1s = una entrada blanda, que se come el ataque del ducking."));

    // Fase 5: sidechain.
    setUpComboLabel (sidechainLabel, utf8 ("SIDECHAIN  \xc2\xb7  nivel del kick contra el umbral (arrastra la l\xc3\xadnea)"));
    sidechainLabel.setJustificationType (juce::Justification::centredLeft);
    sidechainMeter.setTooltip (utf8 ("El nivel que llega por la entrada sidechain. Cada kick tiene que pasar la l\xc3\xadnea "
                                     "\xc3\xa1mbar (UMBRAL) y, entre kick y kick, bajar de la punteada (6 dB menos). Luz verde "
                                     "agua: el golpe dispar\xc3\xb3 la curva. \xc3\x81mbar: se detect\xc3\xb3 pero no est\xc3\xa1s "
                                     "en modo Audio. Barra quieta: no llega sidechain (revisa el ruteo en FL)."));
    auto* thresholdParameter = processor.getParameters().getParameter (id::threshold);
    sidechainMeter.onDragStart = [thresholdParameter] { thresholdParameter->beginChangeGesture(); };
    sidechainMeter.onDragEnd = [thresholdParameter] { thresholdParameter->endChangeGesture(); };
    sidechainMeter.onThresholdDrag = [thresholdParameter] (float db) {
        thresholdParameter->setValueNotifyingHost (thresholdParameter->convertTo0to1 (db));
    };
    addAndMakeVisible (sidechainMeter);
    setUpKnob (thresholdKnob, id::threshold, "UMBRAL",
               utf8 ("Nivel del sidechain a partir del cual hay golpe. B\xc3\xa1jalo hasta que todos los kicks disparen; "
                     "s\xc3\xba" "belo si la cola de un kick largo impide que dispare el siguiente (tiene que bajar 6 dB "
                     "por debajo del umbral para rearmarse)."));
    setUpKnob (holdKnob, id::hold, utf8 ("RETENCI\xc3\x93N"),
               utf8 ("Tiempo m\xc3\xadnimo entre dos golpes: lo que llegue antes se ignora. Evita que un kick con dos "
                     "ataques (click + cuerpo) o un flam dispare dos veces. Que sea menor que la distancia entre los kicks "
                     "m\xc3\xa1s juntos del patr\xc3\xb3n (una semicorchea a 95 BPM = 158 ms)."));
    setUpKnob (lookaheadKnob, id::lookahead, "LOOKAHEAD",
               utf8 ("Adelanta la curva: retrasa la se\xc3\xb1" "al que se aten\xc3\xba" "a estos ms y FL atrasa las dem\xc3\xa1s "
                     "pistas lo mismo (compensaci\xc3\xb3n de latencia). As\xc3\xad el volumen ya est\xc3\xa1 abajo cuando "
                     "llega el ataque del kick. Sirve en los tres modos. 0 = sin latencia. No se automatiza."));

    // Fase 6: bandas y mezcla.
    setUpComboLabel (bandLabel, "BANDA");
    setUpCombo (bandBox, id::band,
                utf8 ("Todo: baja la se\xc3\xb1" "al entera (bombeo cl\xc3\xa1sico). Graves: solo lo que est\xc3\xa1 por debajo "
                      "del CRUCE; el kick gana su hueco en el sub y los arm\xc3\xb3nicos del bajo siguen sonando (no se oye "
                      "bombear). Agudos: solo lo de arriba (platillos, la cola de una reverb) sin tocar el sub."),
                bandAttachment);
    bandView.setTooltip (utf8 ("Respuesta en frecuencia. Gris: sin ducking. Acento: en lo m\xc3\xa1s hondo del golpe "
                               "(PROFUNDIDAD \xc3\x97 MIX); lo relleno es lo que quita el ducking. Blanca: en vivo. "
                               "\xc3\x81mbar punteada: el cruce."));
    addAndMakeVisible (bandView);
    setUpKnob (crossoverKnob, id::crossover, "CRUCE",
               utf8 ("D\xc3\xb3nde se parten graves y agudos (Linkwitz-Riley, 24 dB/oct: sin ducking la suma es plana). "
                     "Kick contra bajo: 80\xe2\x80\x93" "150 Hz (el sub y el cuerpo del kick). M\xc3\xa1s alto = el ducking "
                     "se come tambi\xc3\xa9n el gru\xc3\xb1ido del bajo; m\xc3\xa1s bajo = solo el sub."));
    setUpKnob (mixKnob, id::mix, "MIX",
               utf8 ("Cu\xc3\xa1nto del efecto se oye: mezcla la se\xc3\xb1" "al con ducking con la original (en fase). En un "
                     "efecto de volumen equivale a escalar la PROFUNDIDAD: 50 % = la mitad de la ca\xc3\xad" "da. \xc3\x9atil "
                     "para automatizar la entrada del efecto sin tocar la curva."));

    bypassButton.setClickingTogglesState (true);
    bypassButton.setTooltip (utf8 ("Escuchar la se\xc3\xb1" "al original. Es el mismo bypass que el de FL y entra con un "
                                   "fundido de 20 ms (sin clics)."));
    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<ButtonAttachment> (processor.getParameters(), id::bypass, bypassButton);

    updateModeControls (shownMode);
    updateBandControls (shownBand);
    setSize (editorWidth, editorHeight);
    timerCallback(); // sin esperar al primer cuadro
    startTimerHz (framesPerSecond);
}

EbbAudioProcessorEditor::~EbbAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void EbbAudioProcessorEditor::setUpComboLabel (juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, ebb::gui::theme::dimText);
    addAndMakeVisible (label);
}

void EbbAudioProcessorEditor::setUpCombo (juce::ComboBox& box, const char* parameterId, const juce::String& tooltip,
                                          std::unique_ptr<ComboBoxAttachment>& attachment)
{
    box.addItemList (processor.getParameters().getParameter (parameterId)->getAllValueStrings(), 1);
    box.setTooltip (tooltip);
    addAndMakeVisible (box);
    attachment = std::make_unique<ComboBoxAttachment> (processor.getParameters(), parameterId, box);
}

// El filtro de nota pasa a ser la última nota recibida (como un "MIDI learn" de un solo clic).
void EbbAudioProcessorEditor::useLastMidiNote()
{
    if (lastMidiNote < 0)
        return;
    auto* parameter = processor.getParameters().getParameter (ebb::params::midiNote);
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (static_cast<float> (lastMidiNote + 1)));
    parameter->endChangeGesture();
}

// En modo MIDI la división ya no dice cada cuánto hay golpe (eso lo deciden las notas), sino cuánto dura la curva.
void EbbAudioProcessorEditor::updateModeControls (ebb::dsp::TriggerMode mode)
{
    shownMode = mode;
    const bool midi = mode == ebb::dsp::TriggerMode::midi;
    const bool hits = ebb::dsp::isHitMode (mode);
    divisionLabel.setText (hits ? juce::String ("LARGO") : utf8 ("DIVISI\xc3\x93N"), juce::dontSendNotification);
    divisionBox.setTooltip (
        hits ? utf8 ("Modo MIDI o Audio: cu\xc3\xa1nto dura la curva despu\xc3\xa9s de cada golpe, en figuras al tempo de FL "
                     "(multiplicado por la DURACI\xc3\x93N). 1/4 con duraci\xc3\xb3n 70 % = vuelve en 0.7 negras. Si llega otro "
                     "golpe antes, la curva vuelve a empezar desde donde est\xc3\xa9 el volumen.")
             : utf8 ("Cada cu\xc3\xa1nto baja el volumen, en figuras musicales al tempo de FL. 1/4 = un golpe por "
                     "tiempo (el kick del dembow cl\xc3\xa1sico). T = tresillo: tres golpes en el espacio de dos. "
                     "1 bar = un golpe por comp\xc3\xa1s."));
    for (auto* component : std::initializer_list<juce::Component*> { &noteBox, &noteLabel, &velocityButton })
        component->setEnabled (midi);
    const bool audio = mode == ebb::dsp::TriggerMode::audio;
    for (auto* component : std::initializer_list<juce::Component*> { &sidechainMeter, &thresholdKnob.slider, &thresholdKnob.label,
                                                                      &holdKnob.slider, &holdKnob.label })
        component->setEnabled (audio);
    repaint();
}

// En "Todo" no hay cruce: la perilla se atenúa (sigue guardando su valor).
void EbbAudioProcessorEditor::updateBandControls (ebb::dsp::DuckBand band)
{
    shownBand = band;
    const bool split = band != ebb::dsp::DuckBand::full;
    crossoverKnob.slider.setEnabled (split);
    crossoverKnob.label.setEnabled (split);
}

void EbbAudioProcessorEditor::setUpKnob (Knob& knob, const char* parameterId, const juce::String& name, const juce::String& tooltip)
{
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 120, 18);
    knob.slider.setTooltip (tooltip);
    addAndMakeVisible (knob.slider);
    knob.attachment = std::make_unique<SliderAttachment> (processor.getParameters(), parameterId, knob.slider);
    // Doble clic = valor por defecto.
    auto* parameter = processor.getParameters().getParameter (parameterId);
    knob.slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    knob.label.setColour (juce::Label::textColourId, ebb::gui::theme::dimText);
    addAndMakeVisible (knob.label);
}

// Lleva la curva del procesador al editor y al menú (que muestra la forma de fábrica si coincide exacta).
void EbbAudioProcessorEditor::showCurve()
{
    shownCurveVersion = processor.getCurveVersion();
    const auto& curve = processor.getCurve();
    curveEditor.setCurve (curve);
    const int shape = ebb::dsp::findCurveShape (curve);
    shapeBox.setSelectedId (shape >= 0 ? shape + 1 : customShapeId, juce::dontSendNotification);
}

void EbbAudioProcessorEditor::timerCallback()
{
    if (processor.readTransport (lastSnapshot))
        transportView.setSnapshot (lastSnapshot);

    // La curva cambia también desde fuera del editor (un proyecto o un preset cargado).
    if (processor.getCurveVersion() != shownCurveVersion)
        showCurve();

    const auto activity = processor.getMidiActivity();
    midiActivity.update (activity.notes, activity.triggers, activity.lastNote);
    lastMidiNote = activity.lastNote;
    useLastNoteButton.setEnabled (lastMidiNote >= 0);

    auto& parameters = processor.getParameters();
    const auto mode = ebb::dsp::triggerModeFromIndex (static_cast<int> (parameters.getRawParameterValue (ebb::params::mode)->load()));
    if (mode != shownMode)
        updateModeControls (mode);

    ebb::dsp::TempoSettings settings;
    settings.mode = mode;
    settings.division = static_cast<int> (parameters.getRawParameterValue (ebb::params::division)->load());
    settings.depth = parameters.getRawParameterValue (ebb::params::depth)->load() / 100.0f;
    settings.duration = parameters.getRawParameterValue (ebb::params::duration)->load() / 100.0f;
    settings.smoothingMs = parameters.getRawParameterValue (ebb::params::smoothing)->load();
    const float mix = parameters.getRawParameterValue (ebb::params::mix)->load() / 100.0f;
    const auto band = ebb::dsp::duckBandFromIndex (static_cast<int> (parameters.getRawParameterValue (ebb::params::band)->load()));
    if (band != shownBand)
        updateBandControls (band);
    const auto sidechain = processor.takeSidechainActivity();
    sidechainMeter.update (sidechain.peakEnvelope, sidechain.detected, sidechain.triggers,
                           parameters.getRawParameterValue (ebb::params::threshold)->load());
    const float minGain = processor.takeMinGain();
    bandView.update (band, parameters.getRawParameterValue (ebb::params::crossover)->load(), 1.0f - settings.depth * mix, minGain);
    // La vista previa muestra lo que suena: con mix, la caída es profundidad × mix.
    settings.depth *= mix;
    curveView.setCurve (settings, processor.getCurve(), lastSnapshot.bpm, lastSnapshot.timeSignature);
    curveView.setMinGain (minGain);
}

// La fila del editor de la curva se comparte con el panel BANDAS (a la derecha): así la ventana no crece.
juce::Rectangle<int> EbbAudioProcessorEditor::editorPanelArea() const
{
    return getLocalBounds()
        .withTrimmedTop (headerHeight + topRowHeight + gap)
        .withHeight (curveEditorHeight)
        .reduced (padding, 0)
        .withTrimmedRight (bandsWidth + gap);
}

juce::Rectangle<int> EbbAudioProcessorEditor::controlsArea() const
{
    return getLocalBounds()
        .withTrimmedTop (headerHeight + topRowHeight + curveEditorHeight + 2 * gap)
        .withHeight (controlsHeight)
        .reduced (padding, 0);
}

juce::Rectangle<int> EbbAudioProcessorEditor::sidechainArea() const
{
    return getLocalBounds()
        .withTrimmedTop (headerHeight + topRowHeight + curveEditorHeight + controlsHeight + 3 * gap)
        .withHeight (sidechainHeight)
        .reduced (padding, 0);
}

juce::Rectangle<int> EbbAudioProcessorEditor::bandsArea() const
{
    const auto row = getLocalBounds().withTrimmedTop (headerHeight + topRowHeight + gap).withHeight (curveEditorHeight).reduced (padding, 0);
    return row.withTrimmedLeft (row.getWidth() - bandsWidth);
}

void EbbAudioProcessorEditor::paint (juce::Graphics& g)
{
    using namespace ebb::gui;
    g.fillAll (theme::background);

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (padding, 0);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("EBB", header.removeFromLeft (60), juce::Justification::centredLeft);
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("ducker  -  Fase 6: ducking por bandas", header, juce::Justification::centredLeft);

    for (const auto& panelArea : { editorPanelArea(), controlsArea(), sidechainArea(), bandsArea() })
    {
        const auto panel = panelArea.toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (panel, 8.0f);
        g.setColour (theme::panelOutline);
        g.drawRoundedRectangle (panel.reduced (0.5f), 8.0f, 1.0f);
    }

    auto caption = editorPanelArea().reduced (12, 0).removeFromTop (28);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("FORMA DE LA CURVA", caption, juce::Justification::centredLeft);
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (utf8 ("arrastra (May\xc3\xbas = cuadr\xc3\xad" "cula)  \xc2\xb7  doble clic: a\xc3\xb1" "adir / quitar  "
                      "\xc2\xb7  hueco: tensi\xc3\xb3n"),
                caption, juce::Justification::centredRight);

    auto bandsCaption = bandsArea().reduced (12, 0).removeFromTop (28);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("BANDAS", bandsCaption, juce::Justification::centredLeft);
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (utf8 ("qu\xc3\xa9 parte baja con el golpe"), bandsCaption, juce::Justification::centredRight);

    g.setColour (theme::dimText.withAlpha (0.6f));
    g.setFont (juce::FontOptions (11.0f));
    const auto footer = [this] {
        switch (shownMode)
        {
            case ebb::dsp::TriggerMode::midi:
                return utf8 ("Modo MIDI: manda las notas del kick a Ebb con un canal MIDI Out (mismo puerto en los dos).");
            case ebb::dsp::TriggerMode::audio:
                return utf8 ("Modo Audio: en el mixer, \"Sidechain to this track\" desde el kick; en Ebb, el puerto sidechain "
                             "en \xe2\x9a\x99 > Processing.");
            case ebb::dsp::TriggerMode::tempo:
                break;
        }
        return utf8 ("Modo Tempo: con FL parado no hay ducking. Para seguir un kick irregular, usa MIDI o Audio.");
    }();
    g.drawText (footer, getLocalBounds().removeFromBottom (footerHeight), juce::Justification::centred);
}

void EbbAudioProcessorEditor::resized()
{
    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (padding, 8);
    bypassButton.setBounds (header.removeFromRight (90));

    auto top = getLocalBounds().withTrimmedTop (headerHeight).withHeight (topRowHeight).reduced (padding, 0);
    transportView.setBounds (top.removeFromLeft (transportWidth));
    top.removeFromLeft (gap);
    curveView.setBounds (top);

    curveEditor.setBounds (editorPanelArea().withTrimmedTop (28).reduced (6, 0).withTrimmedBottom (6));

    // Controles: dos columnas de menús (modo y nota; división y forma), la columna MIDI y las tres perillas en el resto.
    auto controls = controlsArea().reduced (14, 8);
    const auto placeCombo = [] (juce::Rectangle<int>& column, juce::Label& label, juce::ComboBox& box) {
        label.setBounds (column.removeFromTop (20));
        box.setBounds (column.removeFromTop (28));
        column.removeFromTop (14);
    };
    for (const auto& [first, second] : { std::pair { std::pair { &modeLabel, &modeBox }, std::pair { &noteLabel, &noteBox } },
                                         std::pair { std::pair { &divisionLabel, &divisionBox }, std::pair { &shapeLabel, &shapeBox } } })
    {
        auto column = controls.removeFromLeft (170);
        controls.removeFromLeft (16);
        placeCombo (column, *first.first, *first.second);
        placeCombo (column, *second.first, *second.second);
    }

    auto midiColumn = controls.removeFromLeft (170);
    controls.removeFromLeft (16);
    midiLabel.setBounds (midiColumn.removeFromTop (20));
    midiActivity.setBounds (midiColumn.removeFromTop (28));
    midiColumn.removeFromTop (6);
    useLastNoteButton.setBounds (midiColumn.removeFromTop (28));
    midiColumn.removeFromTop (8);
    velocityButton.setBounds (midiColumn.removeFromTop (28));

    const int knobWidth = controls.getWidth() / 3;
    for (auto* knob : { &depthKnob, &durationKnob, &smoothingKnob })
    {
        auto column = controls.removeFromLeft (knobWidth);
        knob->label.setBounds (column.removeFromTop (20));
        knob->slider.setBounds (column);
    }

    // Sidechain: el medidor a la izquierda (ancho de las tres primeras columnas) y las tres perillas alineadas con las de
    // arriba.
    auto sidechain = sidechainArea().reduced (14, 6);
    auto meterColumn = sidechain.removeFromLeft (3 * 170 + 2 * 16);
    sidechain.removeFromLeft (16);
    sidechainLabel.setBounds (meterColumn.removeFromTop (22));
    sidechainMeter.setBounds (meterColumn.removeFromTop (52));
    for (auto* knob : { &thresholdKnob, &holdKnob, &lookaheadKnob })
    {
        auto column = sidechain.removeFromLeft (knobWidth);
        knob->label.setBounds (column.removeFromTop (18));
        knob->slider.setBounds (column);
    }

    // Bandas: la respuesta arriba y, debajo, el menú BANDA y las perillas CRUCE y MIX.
    auto bands = bandsArea().withTrimmedTop (28).reduced (6, 0).withTrimmedBottom (6);
    bandView.setBounds (bands.removeFromTop (72));
    bands.removeFromTop (4);
    const int bandColumnWidth = bands.getWidth() / 3;
    auto bandColumn = bands.removeFromLeft (bandColumnWidth).reduced (4, 0);
    bandLabel.setBounds (bandColumn.removeFromTop (20));
    bandBox.setBounds (bandColumn.removeFromTop (28));
    for (auto* knob : { &crossoverKnob, &mixKnob })
    {
        auto column = bands.removeFromLeft (bandColumnWidth);
        knob->label.setBounds (column.removeFromTop (18));
        knob->slider.setBounds (column);
    }
}
