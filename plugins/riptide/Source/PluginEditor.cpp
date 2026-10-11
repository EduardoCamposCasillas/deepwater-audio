#include "PluginEditor.h"
#include "Parameters.h"
#include "PluginProcessor.h"

namespace
{
constexpr int modulationWidth = 250;
constexpr int centreWidth = 900;   // visores, modos, tono, nivel y bandas
constexpr int headerHeight = 44;
constexpr int padding = 12;
constexpr int meterWidth = 88;
constexpr int displayHeight = 290; // fila de los visores
constexpr int curveWidth = 290;    // la curva es cuadrada
constexpr int toneKnobSize = 70;
constexpr int sectionLabelHeight = 18;
constexpr int positionButtonHeight = 22;
constexpr int framesPerSecond = 30;
constexpr int modeButtonHeight = 26;
constexpr int modeDescriptionHeight = 20;
constexpr int signalFlowHeight = 24;
constexpr int toggleWidth = 96;
constexpr int toggleHeight = 26;
constexpr int crossoverColumnWidth = 76;
constexpr int bandKnobSize = 54;
constexpr int bandStripHeight = 186;
constexpr int toneLevelHeight = 18 + toneKnobSize + 20 + 4 + positionButtonHeight + 8;
constexpr int modKnobSize = 50;
constexpr int modRowHeight = 16 + modKnobSize + 18;
constexpr int zoomButtonWidth = 64;

// Tamaño del diseño (zoom 100 %). La ventana real es este tamaño × zoom.
constexpr int mainPanelHeight = padding + modeButtonHeight + 4 + modeDescriptionHeight + 6 + sectionLabelHeight
                                + toneLevelHeight + 6 + 22 + 4 + bandStripHeight + signalFlowHeight + padding;
constexpr int baseWidth = padding + meterWidth + padding + centreWidth + padding + modulationWidth + padding + meterWidth + padding;
constexpr int baseHeight = headerHeight + padding + displayHeight + padding + mainPanelHeight + padding;

constexpr float minZoom = 0.7f;
constexpr float maxZoom = 2.0f;
constexpr std::array<int, 9> zoomSteps { 70, 80, 90, 100, 115, 130, 150, 175, 200 };
constexpr const char* zoomPropertyId = "uiZoom"; // propiedad del estado (se guarda con el proyecto de FL)

std::unique_ptr<juce::PropertiesFile> openSettings()
{
    // %APPDATA%\Deepwater\Riptide.settings (en Windows). Solo guarda preferencias de la interfaz.
    juce::PropertiesFile::Options options;
    options.applicationName = "Riptide";
    options.filenameSuffix = ".settings";
    options.folderName = "Deepwater";
    options.osxLibrarySubFolder = "Application Support";
    return std::make_unique<juce::PropertiesFile> (options);
}

using riptide::gui::format::db;
using riptide::gui::format::hz;

// Qué hace cada modo, en lenguaje musical (se muestra debajo de los botones y como tooltip). En UTF-8 escapado: MSVC
// no garantiza la codificación de los literales del código fuente.
const std::array<const char*, riptide::dsp::numModes> modeDescriptions {
    "Cinta: rodilla muy suave, comprime m\xc3\xa1s los agudos que los graves y suma cuerpo en 70 Hz. C\xc3\xa1lido, "
    "redondo, pega.",
    "Bulbo: curva asim\xc3\xa9trica, arm\xc3\xb3nicos pares (octavas). Llena y engorda sin sonar sucio; con drive "
    "alto, ronca.",
    "Transistor: limpio hasta cerca del l\xc3\xadmite y despu\xc3\xa9s \xc3\xa1spero. Arm\xc3\xb3nicos impares: "
    "mordida, presencia.",
    "Clip duro: recto hasta 0 dBFS y plano despu\xc3\xa9s. Recorta picos sin tocar lo dem\xc3\xa1s; pasado, suena "
    "a cuadrada.",
    "Rectificador: da vuelta al semiciclo negativo. La fundamental desaparece y sube una octava. Efecto extremo.",
    "Wavefold: en vez de aplanar los picos los dobla hacia abajo. M\xc3\xa1s drive = m\xc3\xa1s pliegues, timbre "
    "met\xc3\xa1lico.",
};
} // namespace

RiptideAudioProcessorEditor::RiptideAudioProcessorEditor (RiptideAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p), settings (openSettings()), spectrumView (p), inputMeter ("IN", p.getInputMeter()),
      outputMeter ("OUT", p.getOutputMeter())
{
    namespace id = riptide::params;
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (content);
    content.setSize (baseWidth, baseHeight);
    content.onPaint = [this] (juce::Graphics& g) { paintContent (g); };

    content.addAndMakeVisible (curveView);
    content.addAndMakeVisible (spectrumView);
    zoomButton.setTooltip (juce::String::fromUTF8 ("TamaÃ±o de la ventana. TambiÃ©n puedes arrastrar la esquina "
                                                   "inferior derecha. Se guarda con el proyecto."));
    zoomButton.onClick = [this] { showZoomMenu(); };
    content.addAndMakeVisible (zoomButton);

    content.addAndMakeVisible (inputMeter);
    content.addAndMakeVisible (outputMeter);
    setUpKnob (inputKnob, id::inputGain, "Input",
               juce::String::fromUTF8 ("Drive: nivel con el que la se\xc3\xb1" "al entra a la curva (lo que mide IN, despu\xc3\xa9s de los cortes PRE y el emphasis). Subirlo = saturar m\xc3\xa1s: m\xc3\xa1s arm\xc3\xb3nicos, m\xc3\xa1s densidad, menos pegada."));
    setUpKnob (outputKnob, id::outputGain, "Output",
               juce::String::fromUTF8 ("Volumen final. Sirve para igualar el volumen con el bypass y comparar a "
                                       "o\xc3\xad" "do justo."));

    setUpKnob (mixKnob, id::mix, "Mix",
               juce::String::fromUTF8 ("Mezcla en paralelo: cu\xc3\xa1nto de la se\xc3\xb1" "al saturada se suma a la original. 100 % = solo saturada; 30-50 % = la original conserva su pegada y la saturada le agrega densidad y arm\xc3\xb3nicos. Las dos llegan alineadas (misma latencia y misma fase)."));
    setUpToggle (autoGainButton, autoGainAttachment, id::autoGain,
                 juce::String::fromUTF8 ("Iguala el volumen de la se\xc3\xb1" "al saturada con el de la original: mover Input o el modo cambia el car\xc3\xa1" "cter, no el volumen. Debajo, la compensaci\xc3\xb3n que aplica. Sigue al nivel de la m\xc3\xba" "sica despacio (1 s) y queda un poco por debajo, nunca por encima."));
    setUpToggle (deltaButton, deltaAttachment, id::delta,
                 juce::String::fromUTF8 ("Escuchar solo lo que Riptide le agrega (o le quita) a la se\xc3\xb1" "al: la salida menos la original. Sin drive, silencio. Sirve para o\xc3\xad" "r los arm\xc3\xb3nicos, el fizz o el barro que agrega cada ajuste. No lo dejes puesto al exportar."));
    autoGainReadout.setJustificationType (juce::Justification::centred);
    autoGainReadout.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    content.addAndMakeVisible (autoGainReadout);
    updateAutoGainReadout();

    setUpSectionLabel (levelSection, "NIVEL Y MEZCLA");
    setUpSectionLabel (toneSection, "TONO");
    setUpKnob (lowCutKnob, id::lowCut, "Low Cut", juce::String::fromUTF8 ("Low Cut (high-pass, 12 dB/oct): quita graves. En PRE los graves ya no llegan a la curva: no la empujan ni la ensucian (m\xc3\xa1s claridad, menos \"barro\"). En POST el bajo satura con fuerza y despu\xc3\xa9s se le quita la fundamental, pero sus arm\xc3\xb3nicos se quedan. Al m\xc3\xadnimo: apagado."));
    setUpKnob (highCutKnob, id::highCut, "High Cut", juce::String::fromUTF8 ("High Cut (low-pass, 12 dB/oct): quita agudos. En POST suaviza el \"fizz\" (los arm\xc3\xb3nicos agudos que crea la saturaci\xc3\xb3n). En PRE los agudos no se saturan: la saturaci\xc3\xb3n queda en graves y medios, m\xc3\xa1s oscura. Al m\xc3\xa1ximo: apagado."));
    setUpKnob (emphasisKnob, id::emphasis, "Emphasis", juce::String::fromUTF8 ("Emphasis: sube (o baja) una zona ANTES de la curva y la devuelve a su sitio DESPU\xc3\x89S. Sin drive no cambia nada; con drive, esa zona es la que m\xc3\xa1s se satura (positivo) o la que se mantiene limpia (negativo)."));
    setUpKnob (emphasisFreqKnob, id::emphasisFreq, "Emph Freq", juce::String::fromUTF8 ("Centro de la zona de emphasis (campana ancha, unas 2 octavas). Ejemplos: 100 Hz = saturar el cuerpo del bajo; 3 kHz = la presencia de una voz o una guitarra."));
    setUpKnob (tiltKnob, id::tilt, "Tilt", juce::String::fromUTF8 ("Tilt: inclina el espectro de la salida alrededor de 1 kHz. Positivo = agudos arriba y graves abajo (m\xc3\xa1s brillante); negativo = m\xc3\xa1s oscuro y c\xc3\xa1lido. Va despu\xc3\xa9s de la curva: cambia el color, no lo que se satura."));
    setUpPositionButton (lowCutPositionButton, lowCutPositionAttachment, id::lowCutPosition);
    setUpPositionButton (highCutPositionButton, highCutPositionAttachment, id::highCutPosition);

    bypassButton.setClickingTogglesState (true);
    bypassButton.setTooltip (juce::String::fromUTF8 ("Escuchar la se\xc3\xb1" "al original. Es el mismo bypass que el de FL "
                                                     "y entra con un fundido de 20 ms (sin clics)."));
    content.addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<ButtonAttachment> (processor.getParameters(), id::bypass, bypassButton);

    oversamplingLabel.setText ("OVERSAMPLING", juce::dontSendNotification);
    oversamplingLabel.setJustificationType (juce::Justification::centredRight);
    oversamplingLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    oversamplingLabel.setColour (juce::Label::textColourId, riptide::gui::theme::dimText);
    content.addAndMakeVisible (oversamplingLabel);
    // Las opciones salen del propio parámetro (Off, 2x, 4x, 8x, 16x): el attachment asocia la opción N con el índice N.
    oversamplingBox.addItemList (processor.getParameters().getParameter (id::oversampling)->getAllValueStrings(), 1);
    oversamplingBox.setTooltip (juce::String::fromUTF8 (
        "Calcula la saturaci\xc3\xb3n a 2, 4, 8 o 16 veces la frecuencia de muestreo para que los arm\xc3\xb3nicos que "
        "pasan de Nyquist no se reflejen como aliasing. M\xc3\xa1s factor = menos aliasing, m\xc3\xa1s CPU y algo "
        "m\xc3\xa1s de latencia (FL la compensa). Off = sin oversampling: \xc3\xbasalo para o\xc3\xad" "r el aliasing."));
    content.addAndMakeVisible (oversamplingBox);
    oversamplingAttachment = std::make_unique<ComboBoxAttachment> (processor.getParameters(), id::oversampling, oversamplingBox);

    auto* modeParameter = processor.getParameters().getParameter (id::mode);
    modeAttachment = std::make_unique<juce::ParameterAttachment> (
        *modeParameter, [this] (float index) { showMode (static_cast<int> (std::lround (index))); });
    for (size_t i = 0; i < modeButtons.size(); ++i)
    {
        auto& button = modeButtons[i];
        button.setButtonText (riptide::dsp::modeNames[i]);
        button.setRadioGroupId (1);
        button.setColour (juce::TextButton::buttonOnColourId, riptide::gui::theme::accent.withAlpha (0.55f));
        button.setTooltip (juce::String::fromUTF8 (modeDescriptions[i]));
        button.onClick = [this, i] { modeAttachment->setValueAsCompleteGesture (static_cast<float> (i)); };
        content.addAndMakeVisible (button);
    }
    modeDescription.setJustificationType (juce::Justification::centred);
    modeDescription.setFont (juce::FontOptions (12.0f));
    modeDescription.setColour (juce::Label::textColourId, riptide::gui::theme::dimText);
    modeDescription.setMinimumHorizontalScale (1.0f);
    content.addAndMakeVisible (modeDescription);
    modeAttachment->sendInitialUpdate();

    // Bandas.
    setUpSectionLabel (bandsSection, "BANDAS");
    auto* bandsParameter = processor.getParameters().getParameter (id::bands);
    bandCountAttachment = std::make_unique<juce::ParameterAttachment> (
        *bandsParameter, [this] (float index) { showBandCount (static_cast<int> (std::lround (index)) + 1); });
    for (size_t i = 0; i < bandCountButtons.size(); ++i)
    {
        auto& button = bandCountButtons[i];
        button.setButtonText (juce::String (i + 1));
        button.setRadioGroupId (2);
        button.setColour (juce::TextButton::buttonOnColourId, riptide::gui::theme::accent.withAlpha (0.55f));
        button.setTooltip (juce::String::fromUTF8 ("N\xc3\xba" "mero de bandas. 1 = toda la se\xc3\xb1" "al junta (como hasta la Fase 5). Con m\xc3\xa1s, cada banda se satura por separado: lo fuerte de una (el bombo, el bajo) ya no empuja ni ensucia a las otras. Cambiarlo hace un fundido corto a silencio."));
        button.onClick = [this, i] { bandCountAttachment->setValueAsCompleteGesture (static_cast<float> (i)); };
        content.addAndMakeVisible (button);
    }

    const auto modeChoices = processor.getParameters().getParameter (id::bandMode[0])->getAllValueStrings();
    for (size_t b = 0; b < bandStrips.size(); ++b)
    {
        auto& strip = bandStrips[b];
        strip.title.setText ("BANDA " + juce::String (b + 1), juce::dontSendNotification);
        strip.title.setJustificationType (juce::Justification::centred);
        strip.title.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        strip.title.setColour (juce::Label::textColourId, riptide::gui::theme::bands[b]); // el color de su zona en el espectro
        content.addAndMakeVisible (strip.title);
        strip.range.setJustificationType (juce::Justification::centred);
        strip.range.setFont (juce::FontOptions (11.0f));
        strip.range.setColour (juce::Label::textColourId, riptide::gui::theme::dimText);
        content.addAndMakeVisible (strip.range);

        strip.mode.addItemList (modeChoices, 1);
        strip.mode.setTooltip (juce::String::fromUTF8 ("Modo de esta banda. Global = el de los botones de arriba. Ejemplo: graves en Tube (cuerpo, pares) y agudos en Tape (suaves) o en Transistor (mordida)."));
        content.addAndMakeVisible (strip.mode);
        strip.modeAttachment = std::make_unique<ComboBoxAttachment> (processor.getParameters(), id::bandMode[b], strip.mode);

        strip.drive.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
        strip.drive.setTooltip (juce::String::fromUTF8 ("Drive de esta banda, sumado al Input. Subirlo satura m\xc3\xa1s solo esta zona; bajarlo la deja m\xc3\xa1s limpia que el resto. Con AUTO GAIN el volumen de la banda se mantiene."));
        content.addAndMakeVisible (strip.drive);
        strip.driveAttachment = std::make_unique<SliderAttachment> (processor.getParameters(), id::bandDrive[b], strip.drive);
        strip.drive.setDoubleClickReturnValue (true, 0.0);

        strip.solo.setClickingTogglesState (true);
        strip.solo.setColour (juce::TextButton::buttonOnColourId, riptide::gui::theme::meterHot.withAlpha (0.6f));
        strip.solo.setTooltip (juce::String::fromUTF8 ("Solo: escuchar solo esta banda (tambi\xc3\xa9n en la original del mix y en el delta). Sirve para ajustar el drive y los cruces de cada banda. Varias en solo = suenan todas ellas."));
        content.addAndMakeVisible (strip.solo);
        strip.soloAttachment = std::make_unique<ButtonAttachment> (processor.getParameters(), id::bandSolo[b], strip.solo);
        strip.mute.setClickingTogglesState (true);
        strip.mute.setColour (juce::TextButton::buttonOnColourId, riptide::gui::theme::meterClip.withAlpha (0.6f));
        strip.mute.setTooltip (juce::String::fromUTF8 ("Mute: quitar esta banda de la salida. Con una banda en solo, el solo manda."));
        content.addAndMakeVisible (strip.mute);
        strip.muteAttachment = std::make_unique<ButtonAttachment> (processor.getParameters(), id::bandMute[b], strip.mute);

        strip.autoGain.setJustificationType (juce::Justification::centred);
        strip.autoGain.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        strip.autoGain.setTooltip (juce::String::fromUTF8 ("Compensaci\xc3\xb3n de AUTO GAIN de esta banda (tenue si AUTO GAIN est\xc3\xa1 apagado)."));
        content.addAndMakeVisible (strip.autoGain);
    }
    for (size_t i = 0; i < crossoverKnobs.size(); ++i)
        setUpKnob (crossoverKnobs[i], id::crossover[i], "Cruce " + juce::String (i + 1),
                   juce::String::fromUTF8 ("Frecuencia que separa las dos bandas vecinas (Linkwitz-Riley, 24 dB/oct: sin saturaci\xc3\xb3n la suma es plana). Si dos cruces quedan a menos de media octava, Riptide los separa: lo que suena se ve debajo del nombre de cada banda."));
    bandCountAttachment->sendInitialUpdate();
    updateBandReadouts();

    // Modulación.
    setUpSectionLabel (modulationSection, juce::String::fromUTF8 ("MODULACI\xc3\x93N"));
    setUpSectionLabel (envelopeTitle, "ENVOLVENTE");
    setUpSectionLabel (lfoTitle, "LFO");
    for (auto* title : { &envelopeTitle, &lfoTitle })
        title->setColour (juce::Label::textColourId, riptide::gui::theme::modulation);
    setUpKnob (envAttackKnob, id::envAttack, "Attack", juce::String::fromUTF8 ("Cu\xc3\xa1nto tarda la envolvente en subir cuando la se\xc3\xb1" "al se pone fuerte. Corto (0.1-2 ms): reacciona a cada transitorio (el golpe del bombo ya sale modulado). Largo (20-100 ms): deja pasar el golpe y reacciona despu\xc3\xa9s."));
    setUpKnob (envReleaseKnob, id::envRelease, "Release", juce::String::fromUTF8 ("Cu\xc3\xa1nto tarda en bajar cuando la se\xc3\xb1" "al se calma (cae 8.7 dB cada vez que pasa este tiempo). Corto: sigue cada nota y puede ondular con los graves; largo: suave, pero tarda en soltar."));
    setUpKnob (envSensitivityKnob, id::envSensitivity, "Sens", juce::String::fromUTF8 ("Sensibilidad: ganancia del detector. La envolvente va de 0 (se\xc3\xb1" "al a -40 dBFS o menos) a 1 (0 dBFS). S\xc3\xba" "bela si tu pista es baja: con +12 dB llega al m\xc3\xa1ximo a -12 dBFS. La barra de arriba muestra cu\xc3\xa1nto marca."));
    setUpRoute (envelopeRoute, id::envTarget, id::envAmount, "la envolvente");

    setUpCaption (lfoShapeLabel, "FORMA");
    lfoShapeBox.addItemList (processor.getParameters().getParameter (id::lfoShape)->getAllValueStrings(), 1);
    lfoShapeBox.setTooltip (juce::String::fromUTF8 ("Forma del LFO. Sine/Triangle: un vaiv\xc3\xa9n suave. Saw: sube (o baja) y vuelve de golpe. Square: salta entre dos valores (redondeado 2 ms, sin clics). Sample & Hold: un valor al azar en cada ciclo."));
    content.addAndMakeVisible (lfoShapeBox);
    lfoShapeAttachment = std::make_unique<ComboBoxAttachment> (processor.getParameters(), id::lfoShape, lfoShapeBox);
    setUpToggle (lfoSyncButton, lfoSyncAttachment, id::lfoSync, juce::String::fromUTF8 ("SYNC encendido: la velocidad es una figura musical (1/4, 1/8 T...) al tempo de FL, y con la canci\xc3\xb3n sonando el LFO cae en el pulso. Apagado: velocidad libre en Hz."));
    lfoSyncButton.onStateChange = [this] { showLfoSync (lfoSyncButton.getToggleState()); };
    setUpKnob (lfoRateKnob, id::lfoRate, "Rate", juce::String::fromUTF8 ("Velocidad del LFO en ciclos por segundo (sin SYNC). 0.1-1 Hz: un movimiento lento que se siente como \"respiraci\xc3\xb3n\"; 4-8 Hz: tr\xc3\xa9molo; m\xc3\xa1s r\xc3\xa1pido, un aleteo."));
    setUpCaption (lfoDivisionLabel, juce::String::fromUTF8 ("DIVISI\xc3\x93N"));
    lfoDivisionBox.addItemList (processor.getParameters().getParameter (id::lfoDivision)->getAllValueStrings(), 1);
    lfoDivisionBox.setTooltip (juce::String::fromUTF8 ("Duraci\xc3\xb3n de un ciclo del LFO en figuras musicales: 1/4 = una negra, 1 bar = un comp\xc3\xa1s. T = tresillo, D = con puntillo."));
    content.addAndMakeVisible (lfoDivisionBox);
    lfoDivisionAttachment = std::make_unique<ComboBoxAttachment> (processor.getParameters(), id::lfoDivision, lfoDivisionBox);
    setUpRoute (lfoRoute, id::lfoTarget, id::lfoAmount, "el LFO");
    showLfoSync (lfoSyncButton.getToggleState());

    modulationHelp.setText (juce::String::fromUTF8 ("Amount 100 % = todo el recorrido de la perilla destino. El arco celeste en esa perilla muestra hasta d\xc3\xb3nde la mueve. Con AUTO GAIN, mover el drive cambia el car\xc3\xa1" "cter y no el volumen."),
                            juce::dontSendNotification);
    modulationHelp.setFont (juce::FontOptions (11.0f));
    modulationHelp.setColour (juce::Label::textColourId, riptide::gui::theme::dimText);
    modulationHelp.setJustificationType (juce::Justification::topLeft);
    content.addAndMakeVisible (modulationHelp);

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

    lastFrameMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (framesPerSecond);
}

RiptideAudioProcessorEditor::~RiptideAudioProcessorEditor()
{
    stopTimer();
    if (settings != nullptr)
        settings->saveIfNeeded();
    setLookAndFeel (nullptr);
}

float RiptideAudioProcessorEditor::initialZoom() const
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

void RiptideAudioProcessorEditor::setZoom (float newZoom)
{
    newZoom = juce::jlimit (minZoom, maxZoom, newZoom);
    setSize (juce::roundToInt (baseWidth * newZoom), juce::roundToInt (baseHeight * newZoom));
}

void RiptideAudioProcessorEditor::showZoomMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Zoom");
    const int current = juce::roundToInt (zoom * 100.0f);
    for (const int step : zoomSteps)
        menu.addItem (step, juce::String (step) + " %", true, step == current);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&zoomButton),
                        [safeThis = juce::Component::SafePointer<RiptideAudioProcessorEditor> (this)] (int result)
                        {
                            if (safeThis != nullptr && result > 0)
                                safeThis->setZoom (static_cast<float> (result) / 100.0f);
                        });
}

void RiptideAudioProcessorEditor::setUpKnob (Knob& knob, const char* parameterId, const juce::String& name,
                                             const juce::String& tooltip)
{
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    knob.slider.setTooltip (tooltip);
    content.addAndMakeVisible (knob.slider);
    knob.attachment = std::make_unique<SliderAttachment> (processor.getParameters(), parameterId, knob.slider);
    // Doble clic = valor por defecto (0 dB en las ganancias, apagado en los cortes).
    auto* parameter = processor.getParameters().getParameter (parameterId);
    knob.slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    knob.label.setColour (juce::Label::textColourId, riptide::gui::theme::dimText);
    content.addAndMakeVisible (knob.label);
}

void RiptideAudioProcessorEditor::setUpPositionButton (juce::TextButton& button, std::unique_ptr<ButtonAttachment>& attachment,
                                                       const char* parameterId)
{
    button.setClickingTogglesState (true);
    button.setColour (juce::TextButton::buttonOnColourId, riptide::gui::theme::accent.withAlpha (0.35f));
    button.setTooltip (juce::String::fromUTF8 ("PRE = el filtro act\xc3\xba" "a antes de la curva (decide qu\xc3\xa9 se satura). POST = act\xc3\xba" "a despu\xc3\xa9s (solo cambia lo que se oye). Cambiarlo cruza los dos en 20 ms, sin clics."));
    // El texto sigue al estado (también cuando lo cambia la automatización o un preset).
    button.onStateChange = [&button] { button.setButtonText (button.getToggleState() ? "POST" : "PRE"); };
    content.addAndMakeVisible (button);
    attachment = std::make_unique<ButtonAttachment> (processor.getParameters(), parameterId, button);
    button.onStateChange();
}

void RiptideAudioProcessorEditor::setUpToggle (juce::TextButton& button, std::unique_ptr<ButtonAttachment>& attachment,
                                               const char* parameterId, const juce::String& tooltip)
{
    button.setClickingTogglesState (true);
    button.setColour (juce::TextButton::buttonOnColourId, riptide::gui::theme::accent.withAlpha (0.55f));
    button.setTooltip (tooltip);
    content.addAndMakeVisible (button);
    attachment = std::make_unique<ButtonAttachment> (processor.getParameters(), parameterId, button);
}

void RiptideAudioProcessorEditor::updateAutoGainReadout()
{
    // Encendido: la compensación que se aplica. Apagado: la que se aplicaría, tenue (útil para ver cuánto cambia el
    // volumen el drive).
    // Con varias bandas cada una tiene la suya (en su columna).
    autoGainReadout.setText (shownBands > 1 ? "por banda" : db (processor.getAutoGainDb()), juce::dontSendNotification);
    autoGainReadout.setColour (juce::Label::textColourId, autoGainButton.getToggleState() ? riptide::gui::theme::accent
                                                                                           : riptide::gui::theme::dimText.withAlpha (0.6f));
}

void RiptideAudioProcessorEditor::showBandCount (int count)
{
    shownBands = juce::jlimit (1, riptide::dsp::maxBands, count);
    bandCountButtons[static_cast<size_t> (shownBands - 1)].setToggleState (true, juce::dontSendNotification);
    // Las bandas y los cruces que no existen se ven apagados (y no se pueden tocar).
    for (size_t b = 0; b < bandStrips.size(); ++b)
    {
        const bool active = static_cast<int> (b) < shownBands;
        auto& strip = bandStrips[b];
        for (juce::Component* component : { static_cast<juce::Component*> (&strip.title), static_cast<juce::Component*> (&strip.range),
                                            static_cast<juce::Component*> (&strip.mode), static_cast<juce::Component*> (&strip.drive),
                                            static_cast<juce::Component*> (&strip.solo), static_cast<juce::Component*> (&strip.mute),
                                            static_cast<juce::Component*> (&strip.autoGain) })
        {
            component->setEnabled (active);
            component->setAlpha (active ? 1.0f : 0.3f);
        }
    }
    for (size_t i = 0; i < crossoverKnobs.size(); ++i)
    {
        const bool active = static_cast<int> (i) < shownBands - 1;
        for (juce::Component* component : { static_cast<juce::Component*> (&crossoverKnobs[i].slider),
                                            static_cast<juce::Component*> (&crossoverKnobs[i].label) })
        {
            component->setEnabled (active);
            component->setAlpha (active ? 1.0f : 0.3f);
        }
    }
    updateBandReadouts();
    content.repaint();
}

void RiptideAudioProcessorEditor::updateBandReadouts()
{
    // El rango de cada banda con los cruces que suenan (ya separados), y la compensación de su auto gain.
    const auto colour = autoGainButton.getToggleState() ? riptide::gui::theme::accent : riptide::gui::theme::dimText.withAlpha (0.6f);
    for (size_t b = 0; b < bandStrips.size(); ++b)
    {
        auto& strip = bandStrips[b];
        const int band = static_cast<int> (b);
        const float low = band == 0 ? 20.0f : processor.getCrossoverHz (band - 1);
        const float high = band >= shownBands - 1 ? 20000.0f : processor.getCrossoverHz (band);
        strip.range.setText (static_cast<int> (b) < shownBands ? hz (low) + " - " + hz (high) : juce::String ("-"),
                             juce::dontSendNotification);
        strip.autoGain.setText ("auto " + db (processor.getAutoGainDb (band)), juce::dontSendNotification);
        strip.autoGain.setColour (juce::Label::textColourId, colour);
    }
}

void RiptideAudioProcessorEditor::setUpRoute (RouteControls& route, const char* targetId, const char* amountId,
                                              const juce::String& source)
{
    setUpCaption (route.targetLabel, "DESTINO");
    route.target.addItemList (processor.getParameters().getParameter (targetId)->getAllValueStrings(), 1);
    route.target.setTooltip (juce::String::fromUTF8 ("Qu\xc3\xa9 perilla mueve ") + source + juce::String::fromUTF8 (". Drive = el Input (cu\xc3\xa1nto satura todo); Band N Drive = solo esa banda; Mix, Emphasis, Emph Freq y Tilt = esas perillas. Cambiarlo hace un fundido de 20 ms."));
    content.addAndMakeVisible (route.target);
    route.targetAttachment = std::make_unique<ComboBoxAttachment> (processor.getParameters(), targetId, route.target);
    setUpKnob (route.amount, amountId, "Amount",
               juce::String::fromUTF8 ("Cu\xc3\xa1nto mueve ") + source + juce::String::fromUTF8 (" al destino. 100 % = todo el recorrido de su perilla (en Drive, 48 dB); 25 % = un cuarto. Negativo = en sentido contrario (con la envolvente: lo fuerte satura MENOS). En 0 no hace nada."));
}

// Etiqueta chica sobre un selector ("DESTINO", "FORMA").
void RiptideAudioProcessorEditor::setUpCaption (juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centredLeft);
    label.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, riptide::gui::theme::dimText);
    content.addAndMakeVisible (label);
}

void RiptideAudioProcessorEditor::showLfoSync (bool sync)
{
    // Con SYNC se elige una figura (división); sin SYNC, la velocidad en Hz. Solo se ve el control que cuenta.
    lfoRateKnob.slider.setVisible (! sync);
    lfoRateKnob.label.setVisible (! sync);
    lfoDivisionBox.setVisible (sync);
    lfoDivisionLabel.setVisible (sync);
}

juce::Slider* RiptideAudioProcessorEditor::targetSlider (riptide::dsp::ModTarget target) noexcept
{
    using riptide::dsp::ModTarget;
    switch (target)
    {
        case ModTarget::drive:        return &inputKnob.slider;
        case ModTarget::mix:          return &mixKnob.slider;
        case ModTarget::emphasis:     return &emphasisKnob.slider;
        case ModTarget::emphasisFreq: return &emphasisFreqKnob.slider;
        case ModTarget::tilt:         return &tiltKnob.slider;
        case ModTarget::band1Drive:
        case ModTarget::band2Drive:
        case ModTarget::band3Drive:
        case ModTarget::band4Drive:
            return &bandStrips[static_cast<size_t> (static_cast<int> (target) - static_cast<int> (ModTarget::band1Drive))].drive;
    }
    return nullptr;
}

void RiptideAudioProcessorEditor::updateModulation()
{
    using riptide::dsp::ModTarget;
    // Barras de las fuentes (solo se redibuja su rectángulo).
    const float envelope = processor.getEnvelopeValue();
    const float lfo = processor.getLfoValue();
    if (std::abs (envelope - shownEnvelope) > 0.002f || std::abs (lfo - shownLfo) > 0.002f)
    {
        shownEnvelope = envelope;
        shownLfo = lfo;
        content.repaint (envelopeBar.expanded (2));
        content.repaint (lfoBar.expanded (2));
    }

    // El arco de cada perilla destino: su valor + la modulación, en las unidades de esa perilla y dentro de su rango.
    const auto& property = riptide::gui::RiptideLookAndFeel::modulationProperty;
    for (int t = 0; t < riptide::dsp::numModTargets; ++t)
    {
        const auto target = static_cast<ModTarget> (t);
        auto* slider = targetSlider (target);
        if (slider == nullptr)
            continue;
        const float offset = processor.getModulation (target);
        if (offset == 0.0f)
        {
            if (slider->getProperties().contains (property))
            {
                slider->getProperties().remove (property);
                slider->repaint();
            }
            continue;
        }
        double value = slider->getValue();
        switch (target)
        {
            case ModTarget::mix:          value += 100.0 * offset; break;             // la perilla está en %
            case ModTarget::emphasisFreq: value *= std::exp2 (static_cast<double> (offset)); break; // octavas
            default:                      value += offset; break;                     // dB
        }
        value = juce::jlimit (slider->getMinimum(), slider->getMaximum(), value);
        const double position = slider->valueToProportionOfLength (value);
        const auto* previous = slider->getProperties().getVarPointer (property);
        if (previous == nullptr || std::abs (static_cast<double> (*previous) - position) > 0.002)
        {
            slider->getProperties().set (property, position);
            slider->repaint();
        }
    }
}

void RiptideAudioProcessorEditor::setUpSectionLabel (juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, riptide::gui::theme::accent);
    content.addAndMakeVisible (label);
}

void RiptideAudioProcessorEditor::showMode (int index)
{
    index = juce::jlimit (0, riptide::dsp::numModes - 1, index);
    modeButtons[static_cast<size_t> (index)].setToggleState (true, juce::dontSendNotification);
    modeDescription.setText (juce::String::fromUTF8 (modeDescriptions[static_cast<size_t> (index)]), juce::dontSendNotification);
}

void RiptideAudioProcessorEditor::timerCallback()
{
    // El tiempo real entre cuadros (no 1/30 s): si Windows retrasa el temporizador, los medidores caen igual de rápido.
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double elapsed = juce::jlimit (0.0, 0.5, (now - lastFrameMs) / 1000.0);
    lastFrameMs = now;

    const int channels = processor.getMeteredChannels();
    inputMeter.update (channels, elapsed);
    outputMeter.update (channels, elapsed);
    updateAutoGainReadout();
    updateBandReadouts();
    updateModulation();

    const auto display = makeDisplayState();
    curveView.setState (display, processor.takeInputPeak(), elapsed);
    spectrumView.update (display, elapsed);
}

riptide::gui::DisplayState RiptideAudioProcessorEditor::makeDisplayState() const
{
    // Lo mismo que hace el motor con los parámetros (Engine::setBandSettings y modulateDrive), para dibujar lo que suena.
    namespace id = riptide::params;
    namespace dsp = riptide::dsp;
    auto& apvts = processor.getParameters();
    const auto value = [&apvts] (const char* parameterId) { return apvts.getRawParameterValue (parameterId)->load(); };

    riptide::gui::DisplayState state;
    state.numBands = shownBands;
    const auto globalMode = static_cast<dsp::Mode> (juce::jlimit (0, dsp::numModes - 1, juce::roundToInt (value (id::mode))));
    const float inputDb = juce::jlimit (-dsp::maxDriveDb, dsp::maxDriveDb, value (id::inputGain) + processor.getModulation (dsp::ModTarget::drive));
    bool anySolo = false;
    for (size_t b = 0; b < static_cast<size_t> (shownBands); ++b)
        anySolo = anySolo || value (id::bandSolo[b]) >= 0.5f;

    for (size_t b = 0; b < state.bands.size(); ++b)
    {
        auto& band = state.bands[b];
        const int bandMode = juce::roundToInt (value (id::bandMode[b]));
        band.mode = bandMode > 0 ? static_cast<dsp::Mode> (juce::jlimit (0, dsp::numModes - 1, bandMode - 1)) : globalMode;
        const auto target = static_cast<dsp::ModTarget> (static_cast<int> (dsp::ModTarget::band1Drive) + static_cast<int> (b));
        band.bandDriveDb = juce::jlimit (-dsp::maxBandDriveDb, dsp::maxBandDriveDb, value (id::bandDrive[b]) + processor.getModulation (target));
        band.driveDb = inputDb + band.bandDriveDb;
        band.audible = anySolo ? value (id::bandSolo[b]) >= 0.5f : value (id::bandMute[b]) < 0.5f;
    }
    for (size_t i = 0; i < state.crossoverHz.size(); ++i)
        state.crossoverHz[i] = processor.getCrossoverHz (static_cast<int> (i));
    return state;
}

void RiptideAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Todo se dibuja en 'content' (paintContent); esto solo cubre el borde que puede quedar al redondear la escala.
    g.fillAll (riptide::gui::theme::background);
}

void RiptideAudioProcessorEditor::paintContent (juce::Graphics& g)
{
    using namespace riptide::gui;
    g.fillAll (theme::background);

    auto header = content.getLocalBounds().removeFromTop (headerHeight).reduced (padding, 0);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("RIPTIDE", header.removeFromLeft (110), juce::Justification::centredLeft);
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("saturador  -  Fase 8", header.withRight (oversamplingLabel.getX() - padding), juce::Justification::centredLeft);

    // Paneles de las perillas y de la modulación, entre los dos medidores (los visores dibujan el suyo).
    auto knobsPanel = mainArea.toFloat();
    for (const auto panel : { knobsPanel, modulationArea.toFloat() })
    {
        g.setColour (theme::panel);
        g.fillRoundedRectangle (panel, 8.0f);
        g.setColour (theme::panelOutline);
        g.drawRoundedRectangle (panel.reduced (0.5f), 8.0f, 1.0f);
    }
    // Separador entre TONO y NIVEL Y MEZCLA.
    g.setColour (theme::panelOutline);
    g.drawVerticalLine ((toneArea.getRight() + levelArea.getX()) / 2, static_cast<float> (toneArea.getY() + 4),
                        static_cast<float> (toneArea.getBottom() - 4));
    paintBands (g);
    paintModulation (g);

    // Flecha del recorrido de la señal: IN → perillas → OUT.
    g.setColour (theme::dimText.withAlpha (0.6f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (juce::String::fromUTF8 ("input \xe2\x86\x92 PRE \xe2\x86\x92 emphasis \xe2\x86\x92 BANDAS (drive \xc2\xb7 modo \xc2\xb7 auto gain) \xe2\x86\x92 de-emph. \xe2\x86\x92 POST \xe2\x86\x92 tilt \xe2\x86\x92 mix \xe2\x86\x92 delta \xe2\x86\x92 output"),
                knobsPanel.removeFromBottom (static_cast<float> (signalFlowHeight + 4)), juce::Justification::centred);
}

void RiptideAudioProcessorEditor::paintModulation (juce::Graphics& g)
{
    using namespace riptide::gui;
    // Separador entre la envolvente y el LFO.
    g.setColour (theme::panelOutline);
    g.drawHorizontalLine (separatorY, static_cast<float> (modulationArea.getX() + 10), static_cast<float> (modulationArea.getRight() - 10));

    // Envolvente: barra de 0 a 1 desde la izquierda. LFO: desde el centro, hacia la derecha si es positivo.
    for (const auto* bar : { &envelopeBar, &lfoBar })
    {
        const auto area = bar->toFloat();
        g.setColour (theme::display);
        g.fillRoundedRectangle (area, 3.0f);
        g.setColour (theme::modulation);
        if (bar == &envelopeBar)
        {
            g.fillRoundedRectangle (area.withWidth (area.getWidth() * juce::jlimit (0.0f, 1.0f, shownEnvelope)), 3.0f);
        }
        else
        {
            const float half = area.getWidth() * 0.5f * juce::jlimit (-1.0f, 1.0f, shownLfo);
            const float centre = area.getCentreX();
            g.fillRect (juce::Rectangle<float> (juce::jmin (centre, centre + half), area.getY(), std::abs (half), area.getHeight()));
            g.setColour (theme::dimText.withAlpha (0.6f));
            g.drawVerticalLine (juce::roundToInt (centre), area.getY(), area.getBottom());
        }
    }
}

void RiptideAudioProcessorEditor::layOutRoute (RouteControls& route, juce::Rectangle<int> area)
{
    // Destino a la izquierda (etiqueta + selector, centrados en la fila) y Amount a la derecha.
    auto knob = area.removeFromRight (area.getWidth() * 2 / 5);
    route.amount.label.setBounds (knob.removeFromTop (16));
    route.amount.slider.setBounds (knob);
    auto left = area.withSizeKeepingCentre (area.getWidth() - 8, 16 + 24);
    route.targetLabel.setBounds (left.removeFromTop (16));
    route.target.setBounds (left.removeFromTop (24));
}

void RiptideAudioProcessorEditor::paintBands (juce::Graphics& g)
{
    using namespace riptide::gui;
    // Fondo de cada columna: las que suenan, más claras; las apagadas, apenas marcadas.
    for (size_t b = 0; b < bandStrips.size(); ++b)
    {
        const auto area = bandStrips[b].area.toFloat();
        const bool active = static_cast<int> (b) < shownBands;
        g.setColour (active ? theme::control.withAlpha (0.7f) : theme::control.withAlpha (0.2f));
        g.fillRoundedRectangle (area, 6.0f);
        g.setColour (active ? theme::panelOutline : theme::panelOutline.withAlpha (0.4f));
        g.drawRoundedRectangle (area.reduced (0.5f), 6.0f, 1.0f);
        // Una franja con el color de la banda (el mismo que su zona en el espectro y su curva).
        g.setColour (theme::bands[b].withAlpha (active ? 0.8f : 0.25f));
        g.fillRoundedRectangle (area.withHeight (3.0f).reduced (8.0f, 0.0f).translated (0.0f, 1.0f), 1.5f);
    }
}

void RiptideAudioProcessorEditor::resized()
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

void RiptideAudioProcessorEditor::layOutContent()
{
    auto header = content.getLocalBounds().removeFromTop (headerHeight).reduced (padding, 8);
    bypassButton.setBounds (header.removeFromRight (90));
    header.removeFromRight (padding);
    zoomButton.setBounds (header.removeFromRight (zoomButtonWidth));
    header.removeFromRight (padding);
    oversamplingBox.setBounds (header.removeFromRight (70));
    oversamplingLabel.setBounds (header.removeFromRight (100));

    auto body = content.getLocalBounds().withTrimmedTop (headerHeight).reduced (padding);
    inputMeter.setBounds (body.removeFromLeft (meterWidth));
    outputMeter.setBounds (body.removeFromRight (meterWidth));
    body.reduce (padding, 0);
    modulationArea = body.removeFromRight (modulationWidth);
    body.removeFromRight (padding);

    // Visores: la curva (cuadrada) y el espectro.
    auto displays = body.removeFromTop (displayHeight);
    curveView.setBounds (displays.removeFromLeft (curveWidth));
    displays.removeFromLeft (padding);
    spectrumView.setBounds (displays);
    body.removeFromTop (padding);
    mainArea = body;

    // Modulación: ENVOLVENTE (título y barra, Attack | Release | Sens, destino y Amount) y LFO (título y barra, forma y
    // SYNC | Rate o división, destino y Amount).
    {
        auto column = modulationArea.reduced (10, padding);
        modulationSection.setBounds (column.removeFromTop (sectionLabelHeight));
        column.removeFromTop (6);
        auto envelopeHeader = column.removeFromTop (20);
        envelopeBar = envelopeHeader.removeFromRight (110).withSizeKeepingCentre (110, 8);
        envelopeTitle.setBounds (envelopeHeader);
        column.removeFromTop (4);
        auto envelopeKnobs = column.removeFromTop (modRowHeight);
        const int third = envelopeKnobs.getWidth() / 3;
        for (auto* knob : { &envAttackKnob, &envReleaseKnob, &envSensitivityKnob })
        {
            auto cell = envelopeKnobs.removeFromLeft (third);
            knob->label.setBounds (cell.removeFromTop (16));
            knob->slider.setBounds (cell);
        }
        column.removeFromTop (6);
        layOutRoute (envelopeRoute, column.removeFromTop (modRowHeight));

        column.removeFromTop (14);
        separatorY = column.getY();
        column.removeFromTop (14);
        auto lfoHeader = column.removeFromTop (20);
        lfoBar = lfoHeader.removeFromRight (110).withSizeKeepingCentre (110, 8);
        lfoTitle.setBounds (lfoHeader);
        column.removeFromTop (4);
        auto lfoRow = column.removeFromTop (modRowHeight);
        auto speed = lfoRow.removeFromRight (lfoRow.getWidth() * 2 / 5);
        lfoRateKnob.label.setBounds (speed.removeFromTop (16));
        lfoRateKnob.slider.setBounds (speed);
        auto division = lfoRateKnob.slider.getBounds().withSizeKeepingCentre (speed.getWidth(), 16 + 24);
        lfoDivisionLabel.setBounds (division.removeFromTop (16));
        lfoDivisionBox.setBounds (division.removeFromTop (24).reduced (2, 0));
        auto shape = lfoRow.withSizeKeepingCentre (lfoRow.getWidth() - 8, 16 + 24 + 6 + 24);
        lfoShapeLabel.setBounds (shape.removeFromTop (16));
        lfoShapeBox.setBounds (shape.removeFromTop (24));
        shape.removeFromTop (6);
        lfoSyncButton.setBounds (shape.removeFromTop (24).removeFromLeft (70));
        column.removeFromTop (6);
        layOutRoute (lfoRoute, column.removeFromTop (modRowHeight));

        column.removeFromTop (20);
        modulationHelp.setBounds (column.removeFromTop (90));
    }

    // Modos: una fila de seis botones (justo debajo de la curva que dibujan) y la descripción del activo.
    auto panel = mainArea.reduced (padding, padding);
    {
        auto line = panel.removeFromTop (modeButtonHeight);
        const int width = line.getWidth() / riptide::dsp::numModes;
        for (auto& button : modeButtons)
            button.setBounds (line.removeFromLeft (width).reduced (2, 0));
        panel.removeFromTop (4);
        modeDescription.setBounds (panel.removeFromTop (modeDescriptionHeight));
    }

    // Tono (cinco perillas en el orden de la señal; debajo de los cortes, su PRE/POST) | nivel y mezcla (Input, Mix,
    // Output y la columna de AUTO GAIN, con su lectura, y DELTA).
    panel.removeFromTop (6);
    {
        auto row = panel.removeFromTop (sectionLabelHeight + toneLevelHeight);
        toneArea = row.removeFromLeft (5 * 92);
        row.removeFromLeft (16);
        levelArea = row;

        auto tone = toneArea;
        toneSection.setBounds (tone.removeFromTop (sectionLabelHeight));
        const int toneWidth = tone.getWidth() / 5;
        for (auto* knob : { &lowCutKnob, &highCutKnob, &emphasisKnob, &emphasisFreqKnob, &tiltKnob })
        {
            auto column = tone.removeFromLeft (toneWidth);
            knob->label.setBounds (column.removeFromTop (18));
            knob->slider.setBounds (column.removeFromTop (toneKnobSize + 20));
            column.removeFromTop (4);
            if (knob == &lowCutKnob)
                lowCutPositionButton.setBounds (column.withSizeKeepingCentre (56, positionButtonHeight));
            else if (knob == &highCutKnob)
                highCutPositionButton.setBounds (column.withSizeKeepingCentre (56, positionButtonHeight));
        }

        auto level = levelArea;
        levelSection.setBounds (level.removeFromTop (sectionLabelHeight).withTrimmedLeft (8));
        auto toggles = level.removeFromRight (toggleWidth + 14).withSizeKeepingCentre (toggleWidth, toggleHeight * 2 + 20 + 12);
        autoGainButton.setBounds (toggles.removeFromTop (toggleHeight));
        autoGainReadout.setBounds (toggles.removeFromTop (20));
        toggles.removeFromTop (12);
        deltaButton.setBounds (toggles.removeFromTop (toggleHeight));
        const int knobWidth = level.getWidth() / 3;
        for (auto* knob : { &inputKnob, &mixKnob, &outputKnob })
        {
            auto column = level.removeFromLeft (knobWidth);
            knob->label.setBounds (column.removeFromTop (18));
            knob->slider.setBounds (column.reduced (0, 2));
        }
    }

    // Bandas: el título con el selector 1-4 a la derecha; debajo, banda | cruce | banda | cruce | ...
    panel.removeFromTop (6);
    auto bandsHeader = panel.removeFromTop (22);
    for (auto it = bandCountButtons.rbegin(); it != bandCountButtons.rend(); ++it)
        it->setBounds (bandsHeader.removeFromRight (30).reduced (2, 0));
    bandsSection.setBounds (bandsHeader);
    panel.removeFromTop (4);
    auto bandRow = panel.removeFromTop (bandStripHeight);
    const int stripWidth = (bandRow.getWidth() - riptide::dsp::maxCrossovers * crossoverColumnWidth) / riptide::dsp::maxBands;
    for (size_t b = 0; b < bandStrips.size(); ++b)
    {
        auto& strip = bandStrips[b];
        strip.area = bandRow.removeFromLeft (stripWidth);
        auto column = strip.area.reduced (6, 6);
        strip.title.setBounds (column.removeFromTop (16));
        strip.range.setBounds (column.removeFromTop (16));
        column.removeFromTop (4);
        strip.mode.setBounds (column.removeFromTop (22));
        column.removeFromTop (4);
        strip.drive.setBounds (column.removeFromTop (bandKnobSize + 18));
        column.removeFromTop (4);
        auto buttons = column.removeFromTop (22);
        strip.solo.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (2, 0));
        strip.mute.setBounds (buttons.reduced (2, 0));
        strip.autoGain.setBounds (column.removeFromTop (18));

        if (b < crossoverKnobs.size())
        {
            auto crossoverColumn = bandRow.removeFromLeft (crossoverColumnWidth);
            auto knobArea = crossoverColumn.withSizeKeepingCentre (crossoverColumnWidth, 18 + bandKnobSize + 20);
            crossoverKnobs[b].label.setBounds (knobArea.removeFromTop (18));
            crossoverKnobs[b].slider.setBounds (knobArea);
        }
    }
}
