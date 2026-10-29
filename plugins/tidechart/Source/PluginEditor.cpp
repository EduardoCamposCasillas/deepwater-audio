#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"

#include <initializer_list>

namespace theme = tidechart::gui::theme;

namespace
{
juce::String utf8 (const char* text)
{
    return juce::String::fromUTF8 (text);
}

// Vista completa: la columna de la Fase 5 a la izquierda; a la derecha, MELODÍA Y BAJO.
constexpr int fullWidth = 1360;
constexpr int fullLeftWidth = 900;
constexpr int fullHeight = 960;

// Vista simple.
constexpr int simpleWidth = 1180;
constexpr int simpleLeftWidth = 780;
constexpr int simpleHeight = 560;

const juce::Identifier viewProperty { "view" }; // "simple" (o nada: por defecto) / "full"
} // namespace

TidechartAudioProcessorEditor::TidechartAudioProcessorEditor (TidechartAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
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
                   [&p] { p.fixMelody(); } }),
      inputMonitor (utf8 ("ENTRADA  \xe2\x80\x94  lo que llega a Tidechart"), theme::input),
      outputMonitor (utf8 ("SALIDA  \xe2\x80\x94  lo que va al instrumento"), theme::accent)
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

    viewButton.setTooltip (utf8 ("Vista simple: lo necesario para encontrar acordes, progresiones y melod\xc3\xad" "as. Vista completa: "
                                 "todos los controles (s\xc3\xa9ptimas, voicing, densidad, rango, capturar, variar), el reloj de FL, "
                                 "lo que suena y el monitor MIDI. Lo que cambies en una se ve en la otra."));
    viewButton.setColour (juce::TextButton::buttonColourId, theme::control);
    viewButton.setColour (juce::TextButton::textColourOffId, theme::text);
    viewButton.onClick = [this] {
        setSimple (! simple);
        processor.getParameters().state.setProperty (viewProperty, simple ? "simple" : "full", nullptr);
    };
    addAndMakeVisible (viewButton);

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

    setSimple (processor.getParameters().state.getProperty (viewProperty, "simple").toString() != "full");
    startTimerHz (30);
}

TidechartAudioProcessorEditor::~TidechartAudioProcessorEditor()
{
    stopTimer();
}

void TidechartAudioProcessorEditor::setSimple (bool shouldBeSimple)
{
    simple = shouldBeSimple;
    viewButton.setButtonText (simple ? "Vista completa" : "Vista simple");
    for (auto* c : std::initializer_list<juce::Component*> { &transportView, &keyboardView, &chordView, &inputMonitor, &outputMonitor })
        c->setVisible (! simple);
    paletteView.setSimple (simple);
    progressionView.setSimple (simple);
    keyDetectView.setSimple (simple);
    linesView.setSimple (simple);
    if (simple)
        setSize (simpleWidth, simpleHeight);
    else
        setSize (fullWidth, fullHeight);
    resized(); // por si el tamaño no cambió (al abrir)
    repaint();
}

void TidechartAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    auto area = getLocalBounds().reduced (16, 0);
    auto header = area.removeFromTop (52);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("TIDECHART", header.removeFromLeft (150), juce::Justification::centredLeft);
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (utf8 ("acordes y melod\xc3\xad" "as por tonalidad"), header, juce::Justification::centredLeft);

    g.setColour (theme::panel);
    for (auto* c : std::initializer_list<juce::Component*> { &paletteView, &progressionView, &linesView })
        g.fillRoundedRectangle (c->getBounds().expanded (10, 6).toFloat(), 6.0f);

    if (! simple)
    {
        paintFull (g);
        return;
    }

    // La guía: qué hacer ahora; debajo, el ruteo.
    auto guideBox = guideArea;
    g.setColour (theme::accent);
    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    g.drawFittedText (guide, guideBox.removeFromTop (36), juce::Justification::topLeft, 2, 1.0f);
    guideBox.removeFromTop (6);
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (11.0f));
    g.drawFittedText (utf8 ("Para o\xc3\xadrlo en FL: engranaje de Tidechart \xe2\x86\x92 MIDI \xe2\x86\x92 Output port = 1; en el instrumento "
                            "(Undertow, FLEX\xe2\x80\xa6): MIDI \xe2\x86\x92 Input port = 1."),
                      guideBox.removeFromTop (16), juce::Justification::centredLeft, 1, 0.9f);
}

// Qué hacer ahora, según lo que pasa: el siguiente paso para que suene algo, o qué probar con lo que ya suena.
juce::String TidechartAudioProcessorEditor::guideText() const
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

void TidechartAudioProcessorEditor::paintFull (juce::Graphics& g)
{
    // Paneles detrás del reloj, de la tonalidad y del teclado.
    g.setColour (theme::panel);
    g.fillRoundedRectangle (transportView.getBounds().expanded (10, 4).toFloat(), 6.0f);
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

    auto footer = getLocalBounds().withWidth (fullLeftWidth).reduced (16, 0).removeFromBottom (34);
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (11.5f));
    g.drawFittedText (utf8 ("Ruteo en FL: engranaje de Tidechart \xe2\x86\x92 MIDI \xe2\x86\x92 Output port = 1. En el instrumento que debe sonar "
                            "(Undertow, FLEX\xe2\x80\xa6): MIDI \xe2\x86\x92 Input port = 1. Las notas van en el Piano Roll de Tidechart."),
                      footer, juce::Justification::centredLeft, 2);
}

void TidechartAudioProcessorEditor::resized()
{
    auto all = getLocalBounds().reduced (16, 0);
    auto header = all.removeFromTop (52);
    bypassButton.setBounds (header.removeFromRight (90).reduced (0, 12));
    header.removeFromRight (8);
    panicButton.setBounds (header.removeFromRight (90).reduced (0, 12));
    header.removeFromRight (8);
    viewButton.setBounds (header.removeFromRight (130).reduced (0, 12));

    if (simple)
        layoutSimple();
    else
        layoutFull();
}

void TidechartAudioProcessorEditor::layoutSimple()
{
    // A la derecha: MELODÍA Y BAJO, todo el alto.
    auto right = getLocalBounds().withTrimmedLeft (simpleLeftWidth).withTrimmedTop (56).reduced (0, 6);
    right.removeFromRight (16);
    linesView.setBounds (right.reduced (10, 0).withTrimmedBottom (4));

    auto area = getLocalBounds().withWidth (simpleLeftWidth).reduced (16, 0);
    area.removeFromTop (56);
    paletteView.setBounds (area.removeFromTop (200).reduced (10, 0));
    // La detección, en la fila de la tonalidad: es la otra forma de elegirla.
    keyDetectView.setBounds (paletteView.freeTopArea() + paletteView.getPosition());
    area.removeFromTop (18);
    progressionView.setBounds (area.removeFromTop (196).reduced (10, 0));
    area.removeFromTop (16);
    guideArea = area.withTrimmedBottom (10).reduced (10, 0);
}

void TidechartAudioProcessorEditor::layoutFull()
{
    // A la derecha, de arriba abajo: MELODÍA Y BAJO.
    auto right = getLocalBounds().withTrimmedLeft (fullLeftWidth).withTrimmedTop (56).reduced (0, 6);
    right.removeFromRight (16);
    linesView.setBounds (right.reduced (10, 0).withTrimmedBottom (4));

    auto area = getLocalBounds().withWidth (fullLeftWidth).reduced (16, 0);
    area.removeFromTop (52);

    area.removeFromTop (4);
    transportView.setBounds (area.removeFromTop (40).reduced (10, 0));
    area.removeFromTop (18);

    paletteView.setBounds (area.removeFromTop (192).reduced (10, 0));
    area.removeFromTop (18);

    progressionView.setBounds (area.removeFromTop (192).reduced (10, 0));
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

    area.removeFromBottom (34);
    area.removeFromBottom (8);
    auto left = area.removeFromLeft ((area.getWidth() - 12) / 2);
    area.removeFromLeft (12);
    inputMonitor.setBounds (left);
    outputMonitor.setBounds (area);
}

void TidechartAudioProcessorEditor::timerCallback()
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

    if (simple)
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
