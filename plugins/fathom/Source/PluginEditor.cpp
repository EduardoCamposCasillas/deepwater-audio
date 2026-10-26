#include "PluginEditor.h"
#include "Parameters.h"
#include "PluginProcessor.h"
#include "gui/Keys.h"

namespace
{
constexpr int editorWidth = 1082; // la columna de SALIDA (Fase 6) se sumó a la derecha
constexpr int editorHeight = 714; // Fase 9: la fila de la dinámica en el panel de la banda
constexpr int bandPanelHeight = fathom::gui::BandPanel::preferredHeight;
constexpr int headerHeight = 44;
constexpr int footerHeight = 26;
constexpr int controlsWidth = 110;
constexpr int padding = 12;
constexpr int framesPerSecond = 30;
} // namespace

FathomAudioProcessorEditor::FathomAudioProcessorEditor (FathomAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p), display (p), bandPanel (p), outputPanel (p)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (display);
    addAndMakeVisible (bandPanel);
    // Elegir una banda (en el selector del panel, con un clic en su nodo o al crearla en el visor) la muestra en los dos.
    display.onBandFocused = [this] (int band) { selectBand (band); };
    bandPanel.onBandChosen = [this] (int band) { selectBand (band); };

    // El analizador: lo que se elige en el panel se guarda en el proyecto y lo usa el visor.
    addAndMakeVisible (analyzerPanel);
    analyzerPanel.onViewChanged = [this] {
        processor.setAnalyzerView (analyzerPanel.getView());
        showAnalyzerView (analyzerPanel.getView());
    };
    analyzerPanel.onFreezeChanged = [this] (bool frozen) { display.setFrozen (frozen); };
    analyzerPanel.onClearPeaks = [this] { display.clearPeakHold(); };
    analyzerPanel.setView (processor.getAnalyzerView());
    showAnalyzerView (processor.getAnalyzerView());

    // Deshacer cubre todo lo que se edita en la GUI (visor, panel y salida), no la automatización del host ni el bypass.
    undoButton.setTooltip (juce::String::fromUTF8 ("Deshacer el \xc3\xbaltimo cambio (Ctrl+Z): mover un nodo, una perilla, "
                                                   "crear o borrar una banda. Se acuerda aunque cierres la ventana."));
    redoButton.setTooltip (juce::String::fromUTF8 ("Rehacer lo que deshiciste (Ctrl+Y o Ctrl+Shift+Z)."));
    undoButton.onClick = [this] { processor.getEditHistory().undo(); };
    redoButton.onClick = [this] { processor.getEditHistory().redo(); };
    for (auto* button : { &undoButton, &redoButton })
        addAndMakeVisible (*button);
    setWantsKeyboardFocus (true);

    bypassButton.setClickingTogglesState (true);
    bypassButton.setTooltip (juce::String::fromUTF8 ("Escuchar la se\xc3\xb1" "al original. Es el mismo bypass que el de FL "
                                                     "y entra con un fundido de 20 ms (sin clics)."));
    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<ButtonAttachment> (processor.getParameters(), fathom::params::bypass, bypassButton);

    addAndMakeVisible (outputPanel);

    // Fase 7: respuesta analógica o bilineal (para escuchar y ver qué corrige; con A/B se comparan a volumen igual).
    analogButton.setClickingTogglesState (true);
    analogButton.setColour (juce::TextButton::buttonOnColourId, fathom::gui::theme::accent.withAlpha (0.45f));
    analogButton.setTooltip (juce::String::fromUTF8 (
        "Encendido: las bandas tienen la respuesta de un EQ anal\xc3\xb3gico tambi\xc3\xa9n cerca de Nyquist (la mitad de "
        "la frecuencia de muestreo: 22 kHz a 44.1). Apagado: la bilineal cl\xc3\xa1sica, que \"aplasta\" lo que est\xc3\xa1 "
        "arriba: una campana en 12 kHz sale m\xc3\xa1s angosta y su lado de arriba cae a 0 dB en 22 kHz; un corte de agudos "
        "se hunde de golpe al llegar a Nyquist. Los dos sin latencia. Mira la curva al alternar: abajo de ~5 kHz casi no "
        "cambia. Con A/B y AUTO GAIN se comparan a o\xc3\xad" "do. Con FASE NATURAL no hace falta: siempre es la "
        "anal\xc3\xb3gica (y adem\xc3\xa1s con su fase)."));
    addAndMakeVisible (analogButton);
    analogAttachment = std::make_unique<ButtonAttachment> (processor.getParameters(), fathom::params::analogResponse, analogButton);

    // Comparación A/B: dos versiones completas del EQ; un clic en la otra letra cambia (sin clic en el audio).
    const auto compareTip = juce::String::fromUTF8 (
        "Comparaci\xc3\xb3n A/B: dos versiones completas del EQ (todo menos el bypass). Haz una en A, pasa a B (nace como "
        "copia de A), cambia lo que quieras y alterna para decidir, mejor con AUTO GAIN encendido. Cada lado tiene su "
        "propio deshacer. Se guarda con el proyecto.");
    for (auto* button : { &sideAButton, &sideBButton })
    {
        button->setColour (juce::TextButton::buttonOnColourId, fathom::gui::theme::accent.withAlpha (0.45f));
        button->setTooltip (compareTip);
        button->onClick = [this, button] {
            if ((button == &sideBButton) != (processor.getCompareSide() == 1))
                processor.switchCompareSide();
            showCompareSide();
        };
        addAndMakeVisible (*button);
    }
    copySideButton.setTooltip (juce::String::fromUTF8 ("Copiar el lado activo al otro (los dos quedan iguales): para partir de "
                                                       "lo que tienes y probar una variaci\xc3\xb3n."));
    copySideButton.onClick = [this] { processor.copyToOtherSide(); };
    addAndMakeVisible (copySideButton);
    showCompareSide();

    setSize (editorWidth, editorHeight);
    processor.setAnalyzerActive (true);
    lastFrameSeconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
    startTimerHz (framesPerSecond);
}

FathomAudioProcessorEditor::~FathomAudioProcessorEditor()
{
    processor.setAnalyzerActive (false);
    processor.setSoloBand (-1); // sin la ventana no se ve que hay un solo: no se deja sonando
    setLookAndFeel (nullptr);
}

void FathomAudioProcessorEditor::timerCallback()
{
    // El tiempo real entre cuadros (el temporizador no es exacto): la caída del analizador se mide en dB por segundo.
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double elapsed = juce::jlimit (0.0, 0.2, now - lastFrameSeconds);
    lastFrameSeconds = now;
    // Un proyecto cargado con la ventana abierta trae su propia vista del analizador.
    if (const auto view = processor.getAnalyzerView(); view != shownView)
    {
        analyzerPanel.setView (view);
        showAnalyzerView (view);
    }
    display.update (elapsed);
    bandPanel.refresh();
    outputPanel.refresh();
    if ((processor.getCompareSide() == 1) != sideBButton.getToggleState()) // un proyecto cargado con la ventana abierta
        showCompareSide();
    // Fase 10: con fase natural las bandas son siempre las analógicas (el botón no cambia nada).
    analogButton.setAlpha (processor.getPhaseMode() == fathom::dsp::PhaseMode::natural ? 0.4f : 1.0f);
    undoButton.setEnabled (processor.getEditHistory().canUndo());
    redoButton.setEnabled (processor.getEditHistory().canRedo());
}

bool FathomAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    const auto letter = fathom::gui::commandLetter (key);
    const bool shift = key.getModifiers().isShiftDown();
    if (letter == 'z' && ! shift)
        processor.getEditHistory().undo();
    else if (letter == 'y' || (letter == 'z' && shift))
        processor.getEditHistory().redo();
    else
        return false;
    return true; // la tecla era nuestra aunque no hubiera nada que deshacer (que no la reciba FL)
}

void FathomAudioProcessorEditor::selectBand (int band)
{
    bandPanel.showBand (band);
    display.setSelectedBand (band);
}

void FathomAudioProcessorEditor::showCompareSide()
{
    const bool onB = processor.getCompareSide() == 1;
    sideAButton.setToggleState (! onB, juce::dontSendNotification);
    sideBButton.setToggleState (onB, juce::dontSendNotification);
    copySideButton.setButtonText (juce::String::fromUTF8 (onB ? "B \xe2\x86\x92 A" : "A \xe2\x86\x92 B"));
}

void FathomAudioProcessorEditor::showAnalyzerView (const fathom::analysis::ViewSettings& view)
{
    shownView = view;
    display.setView (view);
}

void FathomAudioProcessorEditor::paint (juce::Graphics& g)
{
    using namespace fathom::gui;
    g.fillAll (theme::background);

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (padding, 0);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("FATHOM", header.removeFromLeft (100), juce::Justification::centredLeft);
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (juce::String::fromUTF8 ("EQ param\xc3\xa9trico  -  Fase 10: modos de fase"), header, juce::Justification::centredLeft);

    g.setColour (theme::dimText.withAlpha (0.6f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (juce::String::fromUTF8 ("Doble clic = crear banda  \xc2\xb7  arrastrar = frecuencia/ganancia (Shift fino, Alt un "
                                        "eje)  \xc2\xb7  rueda = Q  \xc2\xb7  Ctrl+clic o rect\xc3\xa1ngulo = varias  \xc2\xb7  "
                                        "clic en un pico = banda  \xc2\xb7  clic derecho = men\xc3\xba (y SOLO)  \xc2\xb7  Ctrl+Z = deshacer"),
                getLocalBounds().removeFromBottom (footerHeight), juce::Justification::centred);
}

void FathomAudioProcessorEditor::resized()
{
    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (padding, 8);
    bypassButton.setBounds (header.removeFromRight (90));
    header.removeFromRight (24);
    redoButton.setBounds (header.removeFromRight (84));
    header.removeFromRight (6);
    undoButton.setBounds (header.removeFromRight (90));
    header.removeFromRight (24);
    copySideButton.setBounds (header.removeFromRight (62));
    header.removeFromRight (6);
    sideBButton.setBounds (header.removeFromRight (34));
    header.removeFromRight (2);
    sideAButton.setBounds (header.removeFromRight (34));
    header.removeFromRight (24);
    analogButton.setBounds (header.removeFromRight (96));

    auto content = getLocalBounds().withTrimmedTop (headerHeight).withTrimmedBottom (footerHeight).reduced (padding, 0);
    outputPanel.setBounds (content.removeFromRight (controlsWidth));
    content.removeFromRight (padding);
    auto controls = content.removeFromRight (controlsWidth);
    content.removeFromRight (padding);
    bandPanel.setBounds (content.removeFromBottom (bandPanelHeight));
    content.removeFromBottom (padding);
    display.setBounds (content);

    analyzerPanel.setBounds (controls.withTrimmedTop (8));
}
