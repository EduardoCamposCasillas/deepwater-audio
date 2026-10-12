#include "PluginEditor.h"
#include "Parameters.h"
#include "PluginProcessor.h"

#include <array>
#include <cmath>

namespace
{
// Diseño a zoom 100 %. La pantalla del usuario es de 1280 × 720 lógicos (área útil de 672 px de alto): a 100 % cabe
// con la barra de título de la ventana de FL.
constexpr int headerHeight = 44;
constexpr int liveRowHeight = 168;   // visor en vivo + vista previa
constexpr int editorRowHeight = 200; // editor de la curva + bandas
constexpr int controlsHeight = 140;
constexpr int footerHeight = 22;
constexpr int sideWidth = 330;       // columna derecha: vista previa y bandas
constexpr int gap = 8;
constexpr int padding = 12;
constexpr int captionHeight = 28;    // título de los paneles del editor de la curva y de las bandas
constexpr int presetBarWidth = 400;
constexpr juce::uint32 messageSeconds = 6;
constexpr int baseWidth = 1160;
constexpr int baseHeight = headerHeight + liveRowHeight + gap + editorRowHeight + gap + controlsHeight + footerHeight;

// Fila de controles: columnas de izquierda a derecha.
constexpr int modeColumnWidth = 150;
constexpr int knobWidth = 104;
constexpr int groupGap = 16;
constexpr int comboLabelHeight = 18;
constexpr int comboHeight = 26;

constexpr int framesPerSecond = 30;
constexpr int customShapeId = 1000; // id del menú para "Personalizada" (las formas de fábrica van de 1 en adelante)

constexpr float minZoom = 0.7f;
constexpr float maxZoom = 2.0f;
constexpr std::array<int, 9> zoomSteps { 70, 80, 90, 100, 115, 130, 150, 175, 200 };
constexpr const char* zoomPropertyId = "uiZoom";      // propiedad del estado (se guarda con el proyecto de FL)
constexpr const char* liveBarsPropertyId = "liveBars"; // compases del visor en vivo (también en el proyecto)

juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }

std::unique_ptr<juce::PropertiesFile> openSettings()
{
    // %APPDATA%\Deepwater\Ebb.settings (en Windows). Solo guarda preferencias de la interfaz.
    juce::PropertiesFile::Options options;
    options.applicationName = "Ebb";
    options.filenameSuffix = ".settings";
    options.folderName = "Deepwater";
    options.osxLibrarySubFolder = "Application Support";
    return std::make_unique<juce::PropertiesFile> (options);
}

// Zonas del lienzo (a zoom 100 %).
juce::Rectangle<int> liveRow() { return { padding, headerHeight, baseWidth - 2 * padding, liveRowHeight }; }
juce::Rectangle<int> editorRow() { return liveRow().translated (0, liveRowHeight + gap).withHeight (editorRowHeight); }
juce::Rectangle<int> controlsArea() { return editorRow().translated (0, editorRowHeight + gap).withHeight (controlsHeight); }
juce::Rectangle<int> editorPanelArea() { return editorRow().withTrimmedRight (sideWidth + gap); }
juce::Rectangle<int> bandsArea() { return editorRow().withTrimmedLeft (editorRow().getWidth() - sideWidth); }
} // namespace


EbbAudioProcessorEditor::EbbAudioProcessorEditor (EbbAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p), settings (openSettings()),
      presets (p.getPresetManager(), ebb::gui::theme::presetPalette(), settings.get())
{
    namespace id = ebb::params;
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (content);
    content.setSize (baseWidth, baseHeight);
    content.onPaint = [this] (juce::Graphics& g) { paintContent (g); };

    zoomButton.setTooltip (utf8 ("Tama\xc3\xb1o de la ventana. Tambi\xc3\xa9n puedes arrastrar la esquina inferior derecha. "
                                 "Se guarda con el proyecto."));
    zoomButton.onClick = [this] { showZoomMenu(); };
    content.addAndMakeVisible (zoomButton);
    content.addAndMakeVisible (transportView);

    // Fase 7: el visor en vivo. Sus compases se guardan con el proyecto, como el zoom.
    liveView.setTooltip (utf8 ("Lo que pasa por Ebb, comp\xc3\xa1s a comp\xc3\xa1s (el cursor blanco barre al ritmo de FL). "
                               "Gris: la se\xc3\xb1" "al que entra; verde agua: la que sale; l\xc3\xadnea blanca: el volumen "
                               "(arriba = entero); \xc3\xa1mbar: el kick del sidechain; tri\xc3\xa1ngulos: cada golpe que "
                               "dispar\xc3\xb3 la curva. Si el kick y el hueco del bajo no coinciden, sube el LOOKAHEAD o "
                               "revisa la nota MIDI."));
    const int bars = ebb::dsp::LiveScope::validBars (static_cast<int> (processor.getParameters().state.getProperty (liveBarsPropertyId, 1)));
    liveView.setBars (bars);
    processor.setLiveViewBars (bars);
    liveView.onBarsChange = [this] (int newBars) {
        processor.setLiveViewBars (newBars);
        processor.getParameters().state.setProperty (liveBarsPropertyId, newBars, nullptr);
    };
    content.addAndMakeVisible (liveView);
    content.addAndMakeVisible (curveView);

    curveEditor.setTooltip (utf8 ("Arrastra los puntos (con May\xc3\xbas, a la cuadr\xc3\xad" "cula). Doble clic en un hueco: "
                                  "a\xc3\xb1" "ade un punto; doble clic o clic derecho en un punto: lo quita. El c\xc3\xadrculo "
                                  "hueco de cada tramo es su tensi\xc3\xb3n (arr\xc3\xa1stralo; doble clic = recta). El punto "
                                  "blanco: por d\xc3\xb3nde va la curva que suena."));
    content.addAndMakeVisible (curveEditor);
    curveEditor.onChange = [this] (const deepwater::dsp::BreakpointCurve& curve) {
        processor.setCurve (curve);
        showCurve();
    };

    // Formas de fábrica: elegir una la copia al editor (y se puede seguir editando: entonces pasa a "Personalizada").
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
    content.addAndMakeVisible (shapeBox);

    // Modo y división.
    setUpComboLabel (modeLabel, "MODO");
    setUpCombo (modeBox, id::mode,
                utf8 ("Qu\xc3\xa9 dispara la curva. Tempo: una cuadr\xc3\xad" "cula fija (cada 1/4, 1/8...). MIDI: cada nota "
                      "que le llega a Ebb (copia las notas del kick a un canal MIDI Out): sigue el patr\xc3\xb3n real del "
                      "kick, aunque sea irregular como el del dembow. Audio: cada golpe del kick que entra por el "
                      "sidechain."),
                modeAttachment);
    setUpComboLabel (divisionLabel, utf8 ("DIVISI\xc3\x93N"));
    setUpCombo (divisionBox, id::division, {}, divisionAttachment); // el tooltip depende del modo (updateModeControls)

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

    // MIDI (Fase 4).
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
    content.addAndMakeVisible (midiActivity);
    useLastNoteButton.setButtonText (utf8 ("USAR \xc3\x9aLTIMA NOTA"));
    useLastNoteButton.setTooltip (utf8 ("Pone en NOTA MIDI la \xc3\xbaltima nota que lleg\xc3\xb3: toca la nota del kick y "
                                        "pulsa aqu\xc3\xad."));
    useLastNoteButton.onClick = [this] { useLastMidiNote(); };
    content.addAndMakeVisible (useLastNoteButton);
    velocityButton.setButtonText ("VELOCIDAD");
    velocityButton.setClickingTogglesState (true);
    velocityButton.setTooltip (utf8 ("Encendido: la velocidad de cada nota escala la profundidad (velocidad 64 = la mitad "
                                     "de la ca\xc3\xad" "da). Los kicks fantasma o m\xc3\xa1s suaves bajan menos el bajo. "
                                     "Apagado: todos los golpes bajan igual."));
    content.addAndMakeVisible (velocityButton);
    velocityAttachment = std::make_unique<ButtonAttachment> (processor.getParameters(), id::velocity, velocityButton);

    // Sidechain (Fase 5).
    setUpComboLabel (sidechainLabel, utf8 ("SIDECHAIN  \xc2\xb7  arrastra la l\xc3\xadnea del umbral"));
    sidechainLabel.setJustificationType (juce::Justification::centredLeft);
    sidechainMeter.setTooltip (utf8 ("El nivel que llega por la entrada sidechain. Cada kick tiene que pasar la l\xc3\xadnea "
                                     "\xc3\xa1mbar (UMBRAL) y, entre kick y kick, bajar de la punteada (6 dB menos). Luz verde "
                                     "agua: el golpe dispar\xc3\xb3 la curva. Barra quieta: no llega sidechain (revisa el ruteo "
                                     "en FL)."));
    auto* thresholdParameter = processor.getParameters().getParameter (id::threshold);
    sidechainMeter.onDragStart = [thresholdParameter] { thresholdParameter->beginChangeGesture(); };
    sidechainMeter.onDragEnd = [thresholdParameter] { thresholdParameter->endChangeGesture(); };
    sidechainMeter.onThresholdDrag = [thresholdParameter] (float db) {
        thresholdParameter->setValueNotifyingHost (thresholdParameter->convertTo0to1 (db));
    };
    content.addAndMakeVisible (sidechainMeter);
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

    // Bandas y mezcla (Fase 6).
    setUpComboLabel (bandLabel, "BANDA");
    setUpCombo (bandBox, id::band,
                utf8 ("Todo: baja la se\xc3\xb1" "al entera (bombeo cl\xc3\xa1sico). Graves: solo lo que est\xc3\xa1 por debajo "
                      "del CRUCE; el kick gana su hueco en el sub y los arm\xc3\xb3nicos del bajo siguen sonando (no se oye "
                      "bombear). Agudos: solo lo de arriba (platillos, la cola de una reverb) sin tocar el sub."),
                bandAttachment);
    bandView.setTooltip (utf8 ("Respuesta en frecuencia. Gris: sin ducking. Acento: en lo m\xc3\xa1s hondo del golpe "
                               "(PROFUNDIDAD \xc3\x97 MIX); lo relleno es lo que quita el ducking. Blanca: en vivo. "
                               "\xc3\x81mbar punteada: el cruce."));
    content.addAndMakeVisible (bandView);
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
    content.addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<ButtonAttachment> (processor.getParameters(), id::bypass, bypassButton);

    // Presets (Fase 8). Al final: el navegador y el panel de guardar quedan por encima de todo lo demás.
    presets.onMessage = [this] (const juce::String& text) { showMessage (text); };
    presets.addTo (content);

    updateModeControls (shownMode);
    updateBandControls (shownBand);
    layOutContent();

    // El zoom se decide ANTES de poner los límites: setResizeLimits ya redimensiona la ventana (aún de 0 × 0) y eso no
    // debe guardarse como el zoom elegido.
    const float startZoom = initialZoom();
    // Ventana redimensionable (esquina inferior derecha) con la proporción del diseño fija.
    setResizable (true, true);
    setResizeLimits (juce::roundToInt (baseWidth * minZoom), juce::roundToInt (baseHeight * minZoom),
                     juce::roundToInt (baseWidth * maxZoom), juce::roundToInt (baseHeight * maxZoom));
    if (auto* windowConstrainer = getConstrainer())
        windowConstrainer->setFixedAspectRatio (static_cast<double> (baseWidth) / baseHeight);
    rememberZoom = true;
    setZoom (startZoom);
    resized(); // por si el tamaño ya era ese (setSize no llamaría a resized y el zoom no se guardaría)

    processor.setLiveViewOpen (true);
    timerCallback(); // sin esperar al primer cuadro
    startTimerHz (framesPerSecond);
}

EbbAudioProcessorEditor::~EbbAudioProcessorEditor()
{
    stopTimer();
    processor.setLiveViewOpen (false);
    if (settings != nullptr)
        settings->saveIfNeeded();
    setLookAndFeel (nullptr);
}

float EbbAudioProcessorEditor::initialZoom() const
{
    // 1.º el zoom guardado en el proyecto; 2.º el último que usó el usuario; 3.º el mayor (≤ 100 %) que cabe en pantalla.
    const auto saved = processor.getParameters().state.getProperty (zoomPropertyId);
    if (! saved.isVoid())
        return static_cast<float> (saved);
    if (settings != nullptr && settings->containsKey (zoomPropertyId))
        return static_cast<float> (settings->getDoubleValue (zoomPropertyId, 1.0));

    float zoomThatFits = 1.0f;
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto area = display->userBounds.toFloat();
        zoomThatFits = juce::jmin (1.0f, 0.92f * area.getWidth() / baseWidth, 0.88f * area.getHeight() / baseHeight);
    }
    return std::floor (zoomThatFits * 20.0f) / 20.0f; // en pasos de 5 %
}

void EbbAudioProcessorEditor::setZoom (float newZoom)
{
    newZoom = juce::jlimit (minZoom, maxZoom, newZoom);
    setSize (juce::roundToInt (baseWidth * newZoom), juce::roundToInt (baseHeight * newZoom));
}

void EbbAudioProcessorEditor::showZoomMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Zoom");
    const int current = juce::roundToInt (zoom * 100.0f);
    for (const int step : zoomSteps)
        menu.addItem (step, juce::String (step) + " %", true, step == current);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&zoomButton),
                        [safeThis = juce::Component::SafePointer<EbbAudioProcessorEditor> (this)] (int result) {
                            if (safeThis != nullptr && result > 0)
                                safeThis->setZoom (static_cast<float> (result) / 100.0f);
                        });
}

void EbbAudioProcessorEditor::showMessage (const juce::String& text)
{
    message = text;
    messageExpiry = juce::Time::getApproximateMillisecondCounter() + messageSeconds * 1000;
    content.repaint (0, baseHeight - footerHeight, baseWidth, footerHeight);
}

void EbbAudioProcessorEditor::setUpComboLabel (juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, ebb::gui::theme::dimText);
    content.addAndMakeVisible (label);
}

void EbbAudioProcessorEditor::setUpCombo (juce::ComboBox& box, const char* parameterId, const juce::String& tooltip,
                                          std::unique_ptr<ComboBoxAttachment>& attachment)
{
    box.addItemList (processor.getParameters().getParameter (parameterId)->getAllValueStrings(), 1);
    box.setTooltip (tooltip);
    content.addAndMakeVisible (box);
    attachment = std::make_unique<ComboBoxAttachment> (processor.getParameters(), parameterId, box);
}

void EbbAudioProcessorEditor::setUpKnob (Knob& knob, const char* parameterId, const juce::String& name, const juce::String& tooltip)
{
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 120, 18);
    knob.slider.setTooltip (tooltip);
    content.addAndMakeVisible (knob.slider);
    knob.attachment = std::make_unique<SliderAttachment> (processor.getParameters(), parameterId, knob.slider);
    // Doble clic = valor por defecto.
    auto* parameter = processor.getParameters().getParameter (parameterId);
    knob.slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    knob.label.setColour (juce::Label::textColourId, ebb::gui::theme::dimText);
    content.addAndMakeVisible (knob.label);
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

// En modo MIDI o audio la división ya no dice cada cuánto hay golpe (eso lo deciden los golpes), sino cuánto dura la
// curva. El hueco de la derecha de la fila de controles muestra lo del modo: MIDI en Tempo y MIDI, sidechain en Audio.
void EbbAudioProcessorEditor::updateModeControls (ebb::dsp::TriggerMode mode)
{
    shownMode = mode;
    const bool midi = mode == ebb::dsp::TriggerMode::midi;
    const bool audio = mode == ebb::dsp::TriggerMode::audio;
    const bool hits = ebb::dsp::isHitMode (mode);
    divisionLabel.setText (hits ? juce::String ("LARGO") : utf8 ("DIVISI\xc3\x93N"), juce::dontSendNotification);
    divisionBox.setTooltip (
        hits ? utf8 ("Modo MIDI o Audio: cu\xc3\xa1nto dura la curva despu\xc3\xa9s de cada golpe, en figuras al tempo de FL "
                     "(multiplicado por la DURACI\xc3\x93N). 1/4 con duraci\xc3\xb3n 70 % = vuelve en 0.7 negras. Si llega otro "
                     "golpe antes, la curva vuelve a empezar desde donde est\xc3\xa9 el volumen.")
             : utf8 ("Cada cu\xc3\xa1nto baja el volumen, en figuras musicales al tempo de FL. 1/4 = un golpe por "
                     "tiempo (el kick del dembow cl\xc3\xa1sico). T = tresillo: tres golpes en el espacio de dos. "
                     "1 bar = un golpe por comp\xc3\xa1s."));

    for (auto* component : std::initializer_list<juce::Component*> { &noteLabel, &noteBox, &midiLabel, &midiActivity,
                                                                      &useLastNoteButton, &velocityButton })
        component->setVisible (! audio);
    for (auto* component : std::initializer_list<juce::Component*> { &noteBox, &noteLabel, &velocityButton })
        component->setEnabled (midi);
    for (auto* component : std::initializer_list<juce::Component*> { &sidechainLabel, &sidechainMeter, &thresholdKnob.slider,
                                                                      &thresholdKnob.label, &holdKnob.slider, &holdKnob.label })
        component->setVisible (audio);
    content.repaint();
}

// En "Todo" no hay cruce: la perilla se atenúa (sigue guardando su valor).
void EbbAudioProcessorEditor::updateBandControls (ebb::dsp::DuckBand band)
{
    shownBand = band;
    const bool split = band != ebb::dsp::DuckBand::full;
    crossoverKnob.slider.setEnabled (split);
    crossoverKnob.label.setEnabled (split);
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

    presets.update(); // nombre del preset y "*" si se tocó algo (también la curva)
    if (messageExpiry != 0 && juce::Time::getApproximateMillisecondCounter() > messageExpiry)
    {
        messageExpiry = 0;
        message.clear();
        content.repaint (0, baseHeight - footerHeight, baseWidth, footerHeight);
    }

    // La curva cambia también desde fuera del editor (un proyecto o un preset cargado).
    if (processor.getCurveVersion() != shownCurveVersion)
        showCurve();
    curveEditor.setPlayhead (processor.getCurvePosition());
    liveView.update (processor.getLiveScope(), lastSnapshot, processor.getCurveHits());

    const auto activity = processor.getMidiActivity();
    midiActivity.update (activity.notes, activity.triggers, activity.lastNote);
    lastMidiNote = activity.lastNote;
    useLastNoteButton.setEnabled (lastMidiNote >= 0);

    auto& parameters = processor.getParameters();
    const auto mode = ebb::dsp::triggerModeFromIndex (static_cast<int> (parameters.getRawParameterValue (ebb::params::mode)->load()));
    if (mode != shownMode)
        updateModeControls (mode);

    ebb::dsp::TempoSettings preview;
    preview.mode = mode;
    preview.division = static_cast<int> (parameters.getRawParameterValue (ebb::params::division)->load());
    preview.depth = parameters.getRawParameterValue (ebb::params::depth)->load() / 100.0f;
    preview.duration = parameters.getRawParameterValue (ebb::params::duration)->load() / 100.0f;
    preview.smoothingMs = parameters.getRawParameterValue (ebb::params::smoothing)->load();
    const float mix = parameters.getRawParameterValue (ebb::params::mix)->load() / 100.0f;
    const auto band = ebb::dsp::duckBandFromIndex (static_cast<int> (parameters.getRawParameterValue (ebb::params::band)->load()));
    if (band != shownBand)
        updateBandControls (band);
    const auto sidechain = processor.takeSidechainActivity();
    sidechainMeter.update (sidechain.peakEnvelope, sidechain.detected, sidechain.triggers,
                           parameters.getRawParameterValue (ebb::params::threshold)->load());
    const float minGain = processor.takeMinGain();
    bandView.update (band, parameters.getRawParameterValue (ebb::params::crossover)->load(), 1.0f - preview.depth * mix, minGain);
    // La vista previa muestra lo que suena: con mix, la caída es profundidad × mix.
    preview.depth *= mix;
    curveView.setCurve (preview, processor.getCurve(), lastSnapshot.bpm, lastSnapshot.timeSignature);
    curveView.setMinGain (minGain);
}

void EbbAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Todo se dibuja en 'content' (paintContent); esto solo cubre el borde que puede quedar al redondear la escala.
    g.fillAll (ebb::gui::theme::background);
}

void EbbAudioProcessorEditor::paintContent (juce::Graphics& g)
{
    using namespace ebb::gui;
    g.fillAll (theme::background);

    auto header = juce::Rectangle<int> (0, 0, baseWidth, headerHeight).reduced (padding, 0);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("EBB", header.removeFromLeft (60), juce::Justification::centredLeft);

    for (const auto& panelArea : { editorPanelArea(), controlsArea(), bandsArea() })
    {
        const auto panel = panelArea.toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (panel, 8.0f);
        g.setColour (theme::panelOutline);
        g.drawRoundedRectangle (panel.reduced (0.5f), 8.0f, 1.0f);
    }

    const auto paintCaption = [&g] (juce::Rectangle<int> area, const juce::String& title, const juce::String& hint, int hintRightInset) {
        auto caption = area.reduced (12, 0).removeFromTop (captionHeight);
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (title, caption, juce::Justification::centredLeft);
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (hint, caption.withTrimmedRight (hintRightInset), juce::Justification::centredRight);
    };
    paintCaption (editorPanelArea(), "FORMA DE LA CURVA",
                  utf8 ("arrastra (May\xc3\xbas = cuadr\xc3\xad" "cula)  \xc2\xb7  doble clic: a\xc3\xb1" "adir / quitar  "
                        "\xc2\xb7  hueco: tensi\xc3\xb3n"),
                  196);
    paintCaption (bandsArea(), "BANDAS", utf8 ("qu\xc3\xa9 parte baja con el golpe"), 0);

    // Separadores entre los grupos de la fila de controles.
    g.setColour (theme::panelOutline);
    for (const int x : separators)
        g.drawVerticalLine (x, static_cast<float> (controlsArea().getY() + 14), static_cast<float> (controlsArea().getBottom() - 14));
    if (shownMode == ebb::dsp::TriggerMode::tempo)
    {
        g.setColour (theme::dimText.withAlpha (0.7f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText (utf8 ("En Tempo la nota y la velocidad no se usan; la luz igual avisa si llega MIDI."),
                          tempoHintArea, juce::Justification::centredLeft, 2);
    }

    // Pie: la ayuda del modo o, durante unos segundos, el último mensaje de los presets.
    g.setFont (juce::FontOptions (11.0f));
    const auto footerArea = juce::Rectangle<int> (0, baseHeight - footerHeight, baseWidth, footerHeight);
    if (message.isNotEmpty())
    {
        g.setColour (theme::freeRunning);
        g.drawText (message, footerArea, juce::Justification::centred);
        return;
    }
    g.setColour (theme::dimText.withAlpha (0.6f));
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
    g.drawText (footer, footerArea, juce::Justification::centred);
}

void EbbAudioProcessorEditor::resized()
{
    // El contenido siempre mide lo mismo: solo cambia la escala con la que se dibuja.
    zoom = static_cast<float> (getWidth()) / static_cast<float> (baseWidth);
    content.setTransform (juce::AffineTransform::scale (zoom));
    zoomButton.setButtonText (juce::String (juce::roundToInt (zoom * 100.0f)) + " %");

    // Se recuerda en el proyecto y como preferencia para la próxima instancia.
    if (! rememberZoom)
        return;
    processor.getParameters().state.setProperty (zoomPropertyId, zoom, nullptr);
    if (settings != nullptr)
        settings->setValue (zoomPropertyId, zoom);
}

void EbbAudioProcessorEditor::layOutContent()
{
    // Cabecera: EBB | presets | reloj | zoom | bypass. El reloj se queda con lo que sobra (sus datos de la derecha
    // desaparecen si no caben). El navegador y el panel de guardar tapan las tres filas.
    auto header = juce::Rectangle<int> (0, 0, baseWidth, headerHeight).reduced (padding, 8);
    bypassButton.setBounds (header.removeFromRight (90));
    header.removeFromRight (gap);
    zoomButton.setBounds (header.removeFromRight (64));
    header.removeFromRight (2 * gap);
    header.removeFromLeft (60);
    const auto presetBar = header.removeFromLeft (presetBarWidth);
    header.removeFromLeft (2 * gap);
    transportView.setBounds (header);
    presets.setBounds (presetBar, liveRow().getUnion (controlsArea()));

    auto live = liveRow();
    curveView.setBounds (live.removeFromRight (sideWidth));
    live.removeFromRight (gap);
    liveView.setBounds (live);

    const auto editorPanel = editorPanelArea();
    shapeBox.setBounds (editorPanel.reduced (12, 0).removeFromTop (captionHeight).removeFromRight (180).withSizeKeepingCentre (180, 22));
    curveEditor.setBounds (editorPanel.withTrimmedTop (captionHeight).reduced (6, 0).withTrimmedBottom (6));

    // Bandas: la respuesta arriba y, debajo, el menú BANDA y las perillas CRUCE y MIX.
    auto bands = bandsArea().withTrimmedTop (captionHeight).reduced (6, 0).withTrimmedBottom (6);
    bandView.setBounds (bands.removeFromTop (62));
    bands.removeFromTop (4);
    const int bandColumnWidth = bands.getWidth() / 3;
    auto bandColumn = bands.removeFromLeft (bandColumnWidth).reduced (4, 0);
    bandLabel.setBounds (bandColumn.removeFromTop (comboLabelHeight));
    bandBox.setBounds (bandColumn.removeFromTop (comboHeight));
    for (auto* knob : { &crossoverKnob, &mixKnob })
    {
        auto column = bands.removeFromLeft (bandColumnWidth);
        knob->label.setBounds (column.removeFromTop (comboLabelHeight));
        knob->slider.setBounds (column);
    }

    // Controles: MODO y DIVISIÓN | 3 perillas | MIDI o SIDECHAIN | LOOKAHEAD.
    auto controls = controlsArea().reduced (14, 8);
    const auto placeKnob = [] (Knob& knob, juce::Rectangle<int> column) {
        knob.label.setBounds (column.removeFromTop (comboLabelHeight));
        knob.slider.setBounds (column);
    };
    const auto placeCombo = [] (juce::Rectangle<int>& column, juce::Label& label, juce::ComboBox& box) {
        label.setBounds (column.removeFromTop (comboLabelHeight));
        box.setBounds (column.removeFromTop (comboHeight));
        column.removeFromTop (12);
    };

    auto modeColumn = controls.removeFromLeft (modeColumnWidth);
    placeCombo (modeColumn, modeLabel, modeBox);
    placeCombo (modeColumn, divisionLabel, divisionBox);
    controls.removeFromLeft (groupGap);
    for (auto* knob : { &depthKnob, &durationKnob, &smoothingKnob })
        placeKnob (*knob, controls.removeFromLeft (knobWidth));

    controls.removeFromLeft (groupGap / 2);
    separators[0] = controls.getX();
    controls.removeFromLeft (groupGap / 2);
    placeKnob (lookaheadKnob, controls.removeFromRight (knobWidth));
    controls.removeFromRight (groupGap / 2);
    separators[1] = controls.getRight();
    controls.removeFromRight (groupGap / 2);

    // MIDI: dos columnas (nota y velocidad | actividad y "usar última nota"); en Tempo, una nota abajo a la derecha.
    {
        auto context = controls;
        const int columnWidth = (context.getWidth() - 3 * groupGap) / 3;
        auto noteColumn = context.removeFromLeft (columnWidth);
        context.removeFromLeft (groupGap);
        auto activityColumn = context.removeFromLeft (columnWidth);
        context.removeFromLeft (groupGap);
        placeCombo (noteColumn, noteLabel, noteBox);
        velocityButton.setBounds (noteColumn.removeFromTop (comboHeight));
        midiLabel.setBounds (activityColumn.removeFromTop (comboLabelHeight));
        midiActivity.setBounds (activityColumn.removeFromTop (comboHeight));
        activityColumn.removeFromTop (12);
        useLastNoteButton.setBounds (activityColumn.removeFromTop (comboHeight));
        tempoHintArea = context.withTrimmedTop (comboLabelHeight).withHeight (2 * comboHeight + 12);
    }

    // Sidechain: el medidor (con su título) y las perillas UMBRAL y RETENCIÓN.
    {
        auto context = controls;
        placeKnob (holdKnob, context.removeFromRight (knobWidth));
        placeKnob (thresholdKnob, context.removeFromRight (knobWidth));
        context.removeFromRight (12);
        sidechainLabel.setBounds (context.removeFromTop (22));
        sidechainMeter.setBounds (context.removeFromTop (52));
    }
}
