#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"

#include <algorithm>
#include <initializer_list>

namespace theme = tidechart::gui::theme;

namespace
{
juce::String utf8 (const char* text)
{
    return juce::String::fromUTF8 (text);
}

// Las tres pestañas tienen el mismo tamaño: cabe entero en 1280 × 672 útiles (una laptop de 13" a 1080p al 150 %),
// contando la barra de la ventana de FL.
constexpr int pageWidth = 1180;
constexpr int pageHeight = 560;
constexpr int headerHeight = 52;
constexpr int createLeftWidth = 780;   // Crear: TONALIDAD y PROGRESIÓN; a la derecha, MELODÍA Y BAJO con el piano roll
constexpr int settingsLeftWidth = 820; // Ajustes: la fila de arriba de la paleta completa necesita algo más de ancho

const juce::Identifier pageProperty { "page" };   // "create" (o nada: por defecto) / "settings" / "analysis"
const juce::Identifier scaleProperty { "scale" }; // la escala elegida con la esquina (o nada: 1)
constexpr double minScale = 0.5, maxScale = 1.5;

constexpr const char* pageIds[] { "create", "settings", "analysis" };
constexpr int presetBarWidth = 420;
constexpr int messageSeconds = 4;

std::unique_ptr<juce::PropertiesFile> openSettings()
{
    // %APPDATA%\Deepwater\Tidechart.settings (en Windows). Solo guarda preferencias de la interfaz.
    juce::PropertiesFile::Options options;
    options.applicationName = "Tidechart";
    options.filenameSuffix = ".settings";
    options.folderName = "Deepwater";
    options.osxLibrarySubFolder = "Application Support";
    return std::make_unique<juce::PropertiesFile> (options);
}

deepwater::presets::Palette presetPalette()
{
    return { theme::background, theme::panel, theme::panelOutline, theme::display, theme::text, theme::dimText, theme::accent, theme::freeRunning };
}
} // namespace

TidechartMainView::TidechartMainView (TidechartAudioProcessor& p)
    : processor (p),
      paletteView (p.getParameters(),
                   { [&p] (int degree) { p.previewChord (degree); },
                     [&p] (const deepwater::theory::RomanNumeral& numeral) { p.previewNumeral (numeral); },
                     [this] (const deepwater::theory::RomanNumeral& numeral) {
                         if (progressionView.addsOnClick())
                             progressionView.append (numeral);
                     } }),
      progressionView (p.getParameters(),
                       { [&p] (const deepwater::theory::Progression& progression) { p.setProgression (progression); },
                         [&p] (const deepwater::theory::RomanNumeral& numeral) { p.previewNumeral (numeral); },
                         [&p] { p.previewChord (-1); },
                         [&p] (deepwater::theory::Mode mode) {
                             // Una plantilla pone su modo, como si se eligiera en la paleta (con su gesto, para el host).
                             auto* parameter = p.getParameters().getParameter (tidechart::params::keyMode);
                             parameter->beginChangeGesture();
                             parameter->setValueNotifyingHost (parameter->convertTo0to1 (static_cast<float> (mode)));
                             parameter->endChangeGesture();
                         } }),
      keyDetectView ({ [&p] (bool listen) { p.setListening (listen); },
                       [&p] (const deepwater::theory::Key& key) {
                           // Tónica y modo, como si se eligieran en la paleta (cada uno con su gesto, para el host).
                           auto& state = p.getParameters();
                           for (const auto& [id, value] : { std::pair { tidechart::params::keyTonic, key.tonic.pitchClass() },
                                                            std::pair { tidechart::params::keyMode, static_cast<int> (key.mode) } })
                           {
                               auto* parameter = state.getParameter (id);
                               parameter->beginChangeGesture();
                               parameter->setValueNotifyingHost (parameter->convertTo0to1 (static_cast<float> (value)));
                               parameter->endChangeGesture();
                           }
                       } },
                     p.isListening()),
      linesView (p.getParameters(),
                 { [&p] (const tidechart::BassSettings& settings) { p.setBassSettings (settings); },
                   [&p] (const tidechart::MelodySettings& settings) { p.setMelodySettings (settings); },
                   [&p] (bool capture) { p.setCapturing (capture); },
                   [&p] (const deepwater::theory::Line& line) { p.captureMelody (line); },
                   [&p] { p.fixMelody(); },
                   [&p] (tidechart::ExportLayer layer) { return p.exportClip (layer); } }),
      inputMonitor (utf8 ("ENTRADA  \xe2\x80\x94  lo que llega a Tidechart"), theme::input),
      outputMonitor (utf8 ("SALIDA  \xe2\x80\x94  lo que va al instrumento"), theme::accent),
      preferences (openSettings()),
      presets (p.getPresetManager(), presetPalette(), preferences.get())
{
    addAndMakeVisible (transportView);
    addAndMakeVisible (paletteView);
    addAndMakeVisible (progressionView);
    seenProgressionChanges = processor.progressionChangeCount();
    progressionView.setProgression (processor.getProgression());
    addAndMakeVisible (keyDetectView);
    addAndMakeVisible (linesView);
    seenLinesChanges = processor.linesChangeCount();
    linesView.setContext (processor.getProgression(), processor.currentChordSettings (false));
    linesView.setLines (processor.getLines());
    addAndMakeVisible (keyboardView);
    addAndMakeVisible (chordView);
    addAndMakeVisible (inputMonitor);
    addAndMakeVisible (outputMonitor);

    const char* pageNames[] { "Crear", "Ajustes", "An\xc3\xa1lisis" };
    const char* pageTips[] {
        "Lo necesario para crear: tonalidad, acordes, progresi\xc3\xb3n, melod\xc3\xad" "a y bajo, y arrastrarlos al Piano Roll de FL.",
        "El ajuste fino: s\xc3\xa9ptimas, qu\xc3\xa9 teclas tocan los acordes, voicing, texto romano, densidad, rango, capturar, fijar y "
        "variar la melod\xc3\xad" "a. Lo que cambies aqu\xc3\xad sigue sonando en Crear.",
        "Lo que pasa por dentro: el reloj de FL, la tonalidad detectada con su porqu\xc3\xa9, las notas que suenan y el monitor MIDI."
    };
    for (int i = 0; i < 3; ++i)
    {
        auto& button = pageButtons[static_cast<std::size_t> (i)];
        button.setButtonText (utf8 (pageNames[i]));
        button.setTooltip (utf8 (pageTips[i]));
        button.setRadioGroupId (1);
        button.setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < 2 ? juce::Button::ConnectedOnRight : 0));
        button.setColour (juce::TextButton::buttonColourId, theme::control);
        button.setColour (juce::TextButton::buttonOnColourId, theme::accent);
        button.setColour (juce::TextButton::textColourOffId, theme::dimText);
        button.setColour (juce::TextButton::textColourOnId, theme::background);
        button.onClick = [this, i] {
            setPage (static_cast<Page> (i));
            processor.getParameters().state.setProperty (pageProperty, pageIds[i], nullptr);
        };
        addAndMakeVisible (button);
    }

    panicButton.setButtonText (utf8 ("P\xc3\xa1nico"));
    panicButton.setTooltip (utf8 ("Apaga todo lo que suena: un note-off por cada nota que sali\xc3\xb3 de aqu\xc3\xad y no se apag\xc3\xb3, "
                                  "y All Notes Off (CC 123) en los 16 canales por si el instrumento tiene notas colgadas "
                                  "de otro sitio."));
    panicButton.setColour (juce::TextButton::buttonColourId, theme::control);
    panicButton.setColour (juce::TextButton::textColourOffId, theme::text);
    panicButton.onClick = [this] { processor.panic(); };
    addAndMakeVisible (panicButton);

    bypassButton.setButtonText ("Bypass");
    bypassButton.setClickingTogglesState (true);
    bypassButton.setTooltip (utf8 ("Deja pasar el MIDI tal cual, sin acordes: cada tecla suena sola. Es el mismo bypass que el de FL. "
                                   "Al activarlo o quitarlo se apaga lo que sonaba."));
    bypassButton.setColour (juce::TextButton::buttonColourId, theme::control);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, theme::warning);
    bypassButton.setColour (juce::TextButton::textColourOffId, theme::text);
    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.getParameters(),
                                                                                                tidechart::params::bypass, bypassButton);

    // Presets (Fase 9). Al final: el navegador y el panel de guardar quedan por encima de todo lo demás.
    presets.onMessage = [this] (const juce::String& text) { showMessage (text); };
    presets.addTo (*this);

    setSize (pageWidth, pageHeight);
    const auto saved = processor.getParameters().state.getProperty (pageProperty).toString();
    Page initial = Page::create;
    for (int i = 0; i < 3; ++i)
        if (saved == pageIds[i])
            initial = static_cast<Page> (i);
    setPage (initial);
    startTimerHz (30);
}

TidechartMainView::~TidechartMainView()
{
    stopTimer();
}

void TidechartMainView::showMessage (const juce::String& text)
{
    message = text;
    messageExpiry = juce::Time::getApproximateMillisecondCounter() + messageSeconds * 1000;
    repaint (guideArea);
}

void TidechartMainView::setPage (Page newPage)
{
    presets.closePanels(); // el navegador estorba en otra pestaña
    page = newPage;
    pageButtons[static_cast<std::size_t> (page)].setToggleState (true, juce::dontSendNotification);

    const bool create = page == Page::create, settings = page == Page::settings, analysis = page == Page::analysis;
    // Cada panel con el detalle de su pestaña (lo que no se ve sigue sonando como esté).
    paletteView.setSimple (create);
    progressionView.setSimple (create);
    keyDetectView.setSimple (create);
    linesView.setSimple (! settings); // al salir de Ajustes, si capturaba, la captura termina
    linesView.setShowsRoll (create);

    paletteView.setVisible (! analysis);
    progressionView.setVisible (! analysis);
    linesView.setVisible (! analysis);
    keyDetectView.setVisible (! settings);
    chordView.setVisible (! create);
    for (auto* c : std::initializer_list<juce::Component*> { &transportView, &keyboardView, &inputMonitor, &outputMonitor })
        c->setVisible (analysis);

    resized();
    repaint();
}

void TidechartMainView::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    auto header = getLocalBounds().reduced (16, 0).removeFromTop (headerHeight);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("TIDECHART", header.removeFromLeft (150), juce::Justification::centredLeft);

    g.setColour (theme::panel);
    for (auto* c : std::initializer_list<juce::Component*> { &paletteView, &progressionView, &linesView, &transportView })
        if (c->isVisible())
            g.fillRoundedRectangle (c->getBounds().expanded (10, 6).toFloat(), 6.0f);

    if (page == Page::analysis)
    {
        paintAnalysis (g);
        return;
    }
    if (page == Page::settings)
    {
        g.fillRoundedRectangle (chordView.getBounds().expanded (10, 6).toFloat(), 6.0f);
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (11.0f));
        if (message.isNotEmpty())
            g.setColour (theme::accent);
        g.drawFittedText (message.isNotEmpty() ? message
                                               : utf8 ("Lo que cambies aqu\xc3\xad sigue sonando en Crear. Pasa el rat\xc3\xb3n por un control para ver qu\xc3\xa9 hace."),
                          guideArea, juce::Justification::centredLeft, 1, 0.9f);
        return;
    }

    // Crear: la guía (qué hacer ahora) y, debajo, el ruteo.
    auto guideBox = guideArea;
    g.setColour (theme::accent);
    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    g.drawFittedText (message.isNotEmpty() ? message : guide, guideBox.removeFromTop (36), juce::Justification::topLeft, 2, 1.0f);
    guideBox.removeFromTop (6);
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (11.0f));
    g.drawFittedText (utf8 ("Para o\xc3\xadrlo en FL: engranaje de Tidechart \xe2\x86\x92 MIDI \xe2\x86\x92 Output port = 1; en el instrumento "
                            "(Undertow, FLEX\xe2\x80\xa6): MIDI \xe2\x86\x92 Input port = 1."),
                      guideBox.removeFromTop (16), juce::Justification::centredLeft, 1, 0.9f);
}

// Qué hacer ahora, según lo que pasa: el siguiente paso para que suene algo, o qué probar con lo que ya suena.
juce::String TidechartMainView::guideText() const
{
    namespace th = deepwater::theory;
    const auto settings = processor.currentChordSettings (transport.bypassed);
    if (transport.bypassed)
        return utf8 ("Bypass: el MIDI pasa tal cual y no hay acordes. Qu\xc3\xadtalo (arriba a la derecha) para que Tidechart toque.");
    if (transport.listening)
        return utf8 ("Escuchando: toca en tu teclado o dale play a tu canci\xc3\xb3n en FL. Cuando diga \xc2\xab" "clara\xc2\xbb o \xc2\xab"
                     "probable\xc2\xbb, pulsa Usar y la paleta queda en esa tonalidad.");
    if (settings.playMode == tidechart::midi::PlayMode::keys)
        return utf8 ("Mant\xc3\xa9n pulsado un acorde de arriba para o\xc3\xadrlo. En el Piano Roll de Tidechart, las teclas blancas tocan los "
                     "acordes (C = el 1.\xc2\xba). Para o\xc3\xadr la progresi\xc3\xb3n con FL: \xc2\xab" "Progresi\xc3\xb3n (sigue a FL)\xc2\xbb.");
    const auto progression = processor.getProgression();
    if (progression.steps.empty())
        return utf8 ("La progresi\xc3\xb3n est\xc3\xa1 vac\xc3\xad" "a: pulsa \xc2\xab" "Otra idea\xc2\xbb, o enciende \xc2\xab" "Agregar con clic\xc2\xbb "
                     "y elige acordes arriba.");
    if (transport.source != deepwater::dsp::ClockSource::host)
        return utf8 ("Dale play en FL con una nota larga en el Piano Roll de Tidechart (cualquier tecla): suena la progresi\xc3\xb3n, "
                     "siguiendo a FL.");
    if (! transport.progressionSounding)
        return utf8 ("FL reproduce, pero no hay ninguna nota abajo en el Piano Roll de Tidechart: pon una nota larga (cualquier "
                     "tecla) que dure todo el patr\xc3\xb3n.");

    juce::String text = utf8 ("Suena ");
    if (const int step = transport.progressionStep; step >= 0 && step < static_cast<int> (progression.steps.size()))
    {
        const th::Notation notation { true, 5 };
        const auto& numeral = progression.steps[static_cast<std::size_t> (step)].numeral;
        text << "el " << juce::String::fromUTF8 (th::romanNumeralText (numeral, notation).c_str()) << " ("
             << juce::String::fromUTF8 (th::chordSymbol (th::chordFor (numeral, settings.key), notation).c_str()) << "). ";
    }
    else
        text << utf8 ("la progresi\xc3\xb3n. ");
    if (! settings.playBass && ! settings.playMelody)
        return text + utf8 ("Prueba otro ritmo u \xc2\xab" "Otra idea\xc2\xbb; luego enciende Bajo y Melod\xc3\xad" "a a la derecha.");
    return text + utf8 ("Pulsa \xc2\xab" "Otra\xc2\xbb hasta encontrar un bajo y una melod\xc3\xad" "a que te gusten; pasa el rat\xc3\xb3n por una "
                        "nota para ver por qu\xc3\xa9 funciona.");
}

void TidechartMainView::paintAnalysis (juce::Graphics& g)
{
    // Paneles detrás de la tonalidad y del teclado (el del reloj ya está).
    g.setColour (theme::panel);
    g.fillRoundedRectangle (keyDetectView.getBounds().expanded (10, 6).toFloat(), 6.0f);
    g.fillRoundedRectangle (keyboardView.getBounds().getUnion (chordView.getBounds()).withTop (keyboardTitleArea.getY()).expanded (10, 6).toFloat(), 6.0f);

    auto title = keyboardTitleArea;
    g.setColour (theme::accent);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("SUENA AHORA", title.removeFromLeft (110), juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (11.5f));
    const int sounding = transport.soundingCount;
    g.setColour (sounding > 0 ? theme::text : theme::dimText);
    juce::String summary = sounding == 0 ? utf8 ("nada (sin notas colgadas)")
                                         : juce::String (sounding) + (sounding == 1 ? " nota" : " notas");
    summary << utf8 ("   \xc2\xb7   todo apagado ") << static_cast<int> (transport.releases)
            << (transport.releases == 1 ? " vez" : " veces") << utf8 (" (p\xc3\xa1nico, reinicio del audio, cargar el proyecto)");
    g.drawText (summary, title, juce::Justification::centredLeft);
    if ((transport.listening || transport.capturing) && ! transport.bypassed)
    {
        g.setColour (theme::input);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (transport.capturing ? utf8 ("CAPTURANDO (el MIDI pasa tal cual)") : utf8 ("ESCUCHANDO (el MIDI pasa tal cual)"), keyboardTitleArea,
                    juce::Justification::centredRight);
    }
    if (transport.bypassed)
    {
        g.setColour (theme::warning);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText ("BYPASS", keyboardTitleArea, juce::Justification::centredRight);
    }
}

void TidechartMainView::resized()
{
    auto header = getLocalBounds().reduced (16, 0).removeFromTop (headerHeight);
    bypassButton.setBounds (header.removeFromRight (90).reduced (0, 12));
    header.removeFromRight (8);
    panicButton.setBounds (header.removeFromRight (90).reduced (0, 12));

    header.removeFromLeft (150); // el título
    for (auto& button : pageButtons)
        button.setBounds (header.removeFromLeft (110).reduced (0, 12));
    // Presets (Fase 9): en el hueco del medio de la cabecera; el navegador y el panel de guardar tapan la página entera.
    presets.setBounds (header.withSizeKeepingCentre (presetBarWidth, header.getHeight()).reduced (0, 11),
                       getLocalBounds().withTrimmedTop (headerHeight).reduced (16, 8));

    switch (page)
    {
        case Page::create: layoutCreate(); break;
        case Page::settings: layoutSettings(); break;
        case Page::analysis: layoutAnalysis(); break;
    }
}

void TidechartMainView::layoutCreate()
{
    // A la derecha: MELODÍA Y BAJO, todo el alto.
    auto right = getLocalBounds().withTrimmedLeft (createLeftWidth).withTrimmedTop (headerHeight + 4).reduced (0, 6);
    right.removeFromRight (16);
    linesView.setBounds (right.reduced (10, 0).withTrimmedBottom (4));

    auto area = getLocalBounds().withWidth (createLeftWidth).reduced (16, 0);
    area.removeFromTop (headerHeight + 4);
    paletteView.setBounds (area.removeFromTop (200).reduced (10, 0));
    // La detección, en la fila de la tonalidad: es la otra forma de elegirla.
    keyDetectView.setBounds (paletteView.freeTopArea() + paletteView.getPosition());
    area.removeFromTop (18);
    progressionView.setBounds (area.removeFromTop (196).reduced (10, 0));
    area.removeFromTop (16);
    guideArea = area.withTrimmedBottom (10).reduced (10, 0);
}

void TidechartMainView::layoutSettings()
{
    // A la derecha: MELODÍA Y BAJO con todos sus controles (el piano roll se ve en Crear).
    auto right = getLocalBounds().withTrimmedLeft (settingsLeftWidth).withTrimmedTop (headerHeight + 4).reduced (0, 6);
    right.removeFromRight (16);
    linesView.setBounds (right.reduced (10, 0).withTrimmedBottom (4));

    auto area = getLocalBounds().withWidth (settingsLeftWidth).reduced (16, 0);
    area.removeFromTop (headerHeight + 4);
    paletteView.setBounds (area.removeFromTop (192).reduced (10, 0));
    area.removeFromTop (18);
    progressionView.setBounds (area.removeFromTop (192).reduced (10, 0));
    area.removeFromTop (18);
    // El acorde que suena: para oír y ver qué cambian el voicing, las séptimas o el bajo.
    chordView.setBounds (area.removeFromTop (34).reduced (10, 0));
    area.removeFromTop (10);
    guideArea = area.removeFromTop (18).reduced (10, 0);
}

void TidechartMainView::layoutAnalysis()
{
    auto area = getLocalBounds().reduced (16, 0);
    area.removeFromTop (headerHeight + 4);
    transportView.setBounds (area.removeFromTop (40).reduced (10, 0));
    area.removeFromTop (18);

    keyDetectView.setBounds (area.removeFromTop (108).reduced (10, 0));
    area.removeFromTop (18);

    auto keyboard = area.removeFromTop (136).reduced (10, 0);
    keyboardTitleArea = keyboard.removeFromTop (20);
    keyboard.removeFromTop (4);
    chordView.setBounds (keyboard.removeFromBottom (34));
    keyboard.removeFromBottom (6);
    keyboardView.setBounds (keyboard);
    area.removeFromTop (18);

    area.removeFromBottom (10);
    auto left = area.removeFromLeft ((area.getWidth() - 12) / 2);
    area.removeFromLeft (12);
    inputMonitor.setBounds (left);
    outputMonitor.setBounds (area);
}

void TidechartMainView::timerCallback()
{
    const auto before = std::make_tuple (transport.soundingCount, transport.releases, transport.bypassed, transport.listening, transport.capturing);
    if (processor.readTransport (transport))
    {
        transportView.setSnapshot (transport);
        keyboardView.setSounding (transport.soundingChannels);
        chordView.setSounding (transport.soundingChannels);
        paletteView.update (processor.currentChordSettings (transport.bypassed), transport.heldDegrees);
        keyDetectView.update (transport);
        linesView.update (transport);
        if (std::make_tuple (transport.soundingCount, transport.releases, transport.bypassed, transport.listening, transport.capturing) != before)
            repaint (keyboardTitleArea);
    }

    // La tonalidad para nombrar el acorde que suena (aunque el audio no publique nada nuevo, p. ej. con FL parado).
    const auto settings = processor.currentChordSettings (transport.bypassed);
    chordView.setKey (settings.key);

    // La progresión: si cambió (desde aquí o al cargar un proyecto), se vuelve a leer; y dónde está FL en ella.
    if (const auto changes = processor.progressionChangeCount(); changes != seenProgressionChanges)
    {
        seenProgressionChanges = changes;
        progressionView.setProgression (processor.getProgression());
    }
    progressionView.update (settings, transport);

    // Las líneas: si cambiaron (ajustes, captura, regeneradas porque cambió la progresión o la tonalidad, proyecto
    // cargado), se vuelven a leer. El contexto (progresión, tonalidad, capas) para colorear y explicar sus notas.
    linesView.setContext (processor.getProgression(), settings);
    if (const auto changes = processor.linesChangeCount(); changes != seenLinesChanges)
    {
        seenLinesChanges = changes;
        linesView.setLines (processor.getLines());
    }

    presets.update(); // nombre del preset y "*" si se tocó algo (también la progresión y las líneas)
    if (messageExpiry != 0 && juce::Time::getApproximateMillisecondCounter() > messageExpiry)
    {
        messageExpiry = 0;
        message.clear();
        repaint (guideArea);
    }

    if (page == Page::create)
        if (auto text = guideText(); text != guide)
        {
            guide = text;
            repaint (guideArea);
        }

    processor.drainMidiLog ([this] (const tidechart::midi::LoggedEvent& event) {
        (event.output ? outputMonitor : inputMonitor).add (event, transport);
        linesView.observe (event);
    });
    inputMonitor.setNoteCount (processor.notesIn());
    outputMonitor.setNoteCount (processor.notesOut());
    inputMonitor.refresh();
    outputMonitor.refresh();
}

// --- El editor: el contenido escalado ------------------------------------------------------------------------------

TidechartAudioProcessorEditor::TidechartAudioProcessorEditor (TidechartAudioProcessor& p)
    : AudioProcessorEditor (&p), tidechart (p), view (p)
{
    userScale = juce::jlimit (minScale, maxScale, static_cast<double> (p.getParameters().state.getProperty (scaleProperty, 1.0)));
    addAndMakeVisible (view);
    setResizable (true, true);
    applyScale();
}

void TidechartAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

// Lo más grande que cabe en el área útil de la pantalla (sin la barra de tareas), dejando sitio a la barra de la ventana
// del plugin en FL. Sin pantalla (tests sin GUI), 1.
double TidechartAudioProcessorEditor::fitScale() const
{
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    const auto* display = isShowing() ? displays.getDisplayForRect (getScreenBounds()) : displays.getPrimaryDisplay();
    if (display == nullptr || view.getWidth() <= 0 || view.getHeight() <= 0)
        return maxScale;
    const auto area = display->userBounds;
    return std::min ((area.getWidth() - 40.0) / view.getWidth(), (area.getHeight() - 110.0) / view.getHeight());
}

void TidechartAudioProcessorEditor::applyScale()
{
    const int w = view.getWidth(), h = view.getHeight();
    if (w <= 0 || h <= 0)
        return;
    const double s = juce::jlimit (minScale, maxScale, std::min (userScale, fitScale()));
    const juce::ScopedValueSetter<bool> setting (applying, true);
    // Primero los límites (con la proporción de esta vista) y después el tamaño: si no, el constrainer recortaría el
    // tamaño nuevo con los límites de la vista anterior.
    getConstrainer()->setFixedAspectRatio (static_cast<double> (w) / h);
    setResizeLimits (juce::roundToInt (w * minScale), juce::roundToInt (h * minScale), juce::roundToInt (w * maxScale),
                     juce::roundToInt (h * maxScale));
    setSize (juce::roundToInt (w * s), juce::roundToInt (h * s));
    resized(); // por si el tamaño no cambió pero la vista sí
}

void TidechartAudioProcessorEditor::resized()
{
    if (view.getWidth() <= 0)
        return;
    scale = static_cast<float> (getWidth()) / static_cast<float> (view.getWidth());
    view.setTopLeftPosition (0, 0);
    view.setTransform (juce::AffineTransform::scale (scale));
    if (! applying)
    {
        // El usuario arrastró la esquina (o el host cambió el tamaño): esa es la escala elegida.
        userScale = juce::jlimit (minScale, maxScale, static_cast<double> (scale));
        tidechart.getParameters().state.setProperty (scaleProperty, userScale, nullptr);
    }
}
