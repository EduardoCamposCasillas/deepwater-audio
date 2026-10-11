#include "PluginEditor.h"
#include "Parameters.h"
#include "PluginProcessor.h"

namespace
{
constexpr int knobWidth = 90;
constexpr int knobHeight = 110;
constexpr int groupPadding = 12;
constexpr int rowHeight = 174;
constexpr int wavetableColumnWidth = 170;
constexpr int filterDisplayWidth = 280;
constexpr int headerHeight = 32;
constexpr int tabRowHeight = 30;     // Fase 10: las pestañas bajan a su propia fila; arriba va la barra de presets
constexpr int titleAreaWidth = 170;  // "UNDERTOW SYNTH"
constexpr int lfoOptionsWidth = 130;
constexpr int lfoRateWidth = 84;
constexpr int matrixRowHeight = 30;
constexpr int oscillatorColumnWidth = 150; // On (y forma) del sub y del ruido
constexpr int oscillatorViewWidth = 214;   // On, tabla y visor 3D de cada oscilador
constexpr int oscillatorKnobWidth = 64;    // 9 perillas por oscilador: más estrechas que las demás
constexpr int smallKnobWidth = 72;         // sub, ruido, distorsión y chorus
constexpr int tabWidth = 96;
constexpr int effectColumnWidth = 130;     // On (y selectores) de cada efecto
constexpr int chorusColumnWidth = 80;      // el chorus solo tiene On: deja sitio a sus 4 perillas
constexpr int modBarHeight = 40;
constexpr int zoomButtonWidth = 56;
constexpr int voicesLabelWidth = 104;

// El ancho lo marca la fila del filtro: opciones + 4 perillas + un visor de 280 px (igual que en la Fase 4).
constexpr int pageWidth = 2 * groupPadding + wavetableColumnWidth + 4 * knobWidth + filterDisplayWidth;
constexpr int pageHeight = 3 * rowHeight + 2 * groupPadding;

// Tamaño del diseño (zoom 100 %). La ventana real es este tamaño × zoom.
constexpr int baseWidth = pageWidth + 2 * groupPadding;
constexpr int baseHeight = headerHeight + tabRowHeight + 8 + pageHeight + modBarHeight + 4 * groupPadding;

constexpr float minZoom = 0.7f;
constexpr float maxZoom = 2.0f;
constexpr std::array<int, 9> zoomSteps { 70, 80, 90, 100, 115, 130, 150, 175, 200 };
constexpr const char* zoomPropertyId = "uiZoom"; // propiedad del estado (se guarda con el proyecto de FL)
constexpr juce::uint32 messageSeconds = 8;
constexpr const char* authorPropertyId = "presetAuthor"; // último autor escrito al guardar un preset

std::unique_ptr<juce::PropertiesFile> openSettings()
{
    // %APPDATA%\Undertow\Undertow Synth.settings (en Windows). Solo guarda preferencias de la interfaz.
    juce::PropertiesFile::Options options;
    options.applicationName = "Undertow Synth";
    options.filenameSuffix = ".settings";
    options.folderName = "Undertow";
    options.osxLibrarySubFolder = "Application Support";
    return std::make_unique<juce::PropertiesFile> (options);
}
} // namespace

UndertowAudioProcessorEditor::UndertowAudioProcessorEditor (UndertowAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p), router (p.getParameters()), settings (openSettings()),
      presetBrowser (p.getPresetManager())
{
    namespace id = undertow::params;
    auto& apvts = processor.getParameters();
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (content);
    content.setSize (baseWidth, baseHeight);
    content.onModulationDrag = [this] (std::optional<undertow::synth::ModSource> source) {
        if (source.has_value())
        {
            // Arrastrar una fuente es trabajar sobre las perillas: los paneles de presets estorban.
            showPresetBrowser (false);
            showSaveDialog (false);
        }
        for (auto* knob : modulationKnobs)
            knob->setDragInProgress (source);

        // Al soltar sobre una perilla, ella deja su propio mensaje; si se soltó fuera, se quita la pista.
        const auto hint = juce::String::fromUTF8 ("Suelta la fuente sobre una perilla resaltada. Pasa por encima de una "
                                                  "pesta\xc3\xb1" "a para cambiar de p\xc3\xa1gina.");
        if (source.has_value())
            showMessage (hint);
        else if (modBar.getMessage() == hint)
            showMessage ({});
    };

    struct KnobInfo
    {
        const char* parameterId;
        const char* name;
    };
    constexpr std::array<KnobInfo, numEnvelopeKnobs + numVoiceKnobs> infos { {
        { id::attack, "Attack" },
        { id::decay, "Decay" },
        { id::sustain, "Sustain" },
        { id::release, "Release" },
        { id::voices, "Voices" },
        { id::velocity, "Velocity" },
        { id::master, "Master" },
    } };

    // --- Pestañas ---
    // Los literales con tildes se pasan con fromUTF8: un const char* suelto JUCE lo lee como Latin-1.
    modulationTab.setButtonText (juce::String::fromUTF8 ("Modulaci\xc3\xb3n"));
    for (auto* tab : { &oscillatorTab, &warpTab, &soundTab, &modulationTab, &effectsTab, &stringTab })
    {
        tab->setClickingTogglesState (true);
        tab->setRadioGroupId (1);
        content.addAndMakeVisible (tab);
    }
    oscillatorTab.onClick = [this] { showPage (Page::oscillators); };
    warpTab.onClick = [this] { showPage (Page::warp); };
    soundTab.onClick = [this] { showPage (Page::sound); };
    modulationTab.onClick = [this] { showPage (Page::modulation); };
    effectsTab.onClick = [this] { showPage (Page::effects); };
    stringTab.onClick = [this] { showPage (Page::string); };
    content.addChildComponent (oscillatorPage);
    content.addChildComponent (warpPage);
    content.addChildComponent (soundPage);
    content.addChildComponent (modulationPage);
    content.addChildComponent (effectsPage);
    content.addChildComponent (stringPage);
    content.addAndMakeVisible (modBar);

    zoomButton.setTooltip (juce::String::fromUTF8 ("Tama\xc3\xb1o de la ventana. Tambi\xc3\xa9n puedes arrastrar la esquina "
                                                   "inferior derecha."));
    zoomButton.onClick = [this] { showZoomMenu(); };
    content.addAndMakeVisible (zoomButton);

    // --- Fase 10: presets ---
    presetBar.onStep = [this] (int delta) {
        if (const auto* preset = processor.getPresetManager().step (delta))
        {
            showMessage ("Preset: " + preset->name + juce::String::fromUTF8 (" \xc2\xb7 ") + preset->category);
            if (presetBrowser.isVisible())
                presetBrowser.refresh();
        }
        updatePresetBar();
    };
    presetBar.onToggleBrowser = [this] { showPresetBrowser (! presetBrowser.isVisible()); };
    presetBar.onSave = [this] { showSaveDialog (true); };
    presetBar.onMenu = [this] (juce::Component& anchor) { showPresetMenu (anchor); };
    content.addAndMakeVisible (presetBar);

    presetBrowser.onLoad = [this] (const undertow::presets::Preset& preset) { loadPreset (preset); };
    presetBrowser.onClose = [this] { showPresetBrowser (false); };
    presetBrowser.onMessage = [this] (const juce::String& message) {
        showMessage (message);
        updatePresetBar();
    };
    content.addChildComponent (presetBrowser);

    saveDialog.setFolder (processor.getPresetManager().getUserFolder());
    saveDialog.exists = [this] (const juce::String& name) { return processor.getPresetManager().userPresetExists (name); };
    saveDialog.onSave = [this] (const juce::String& name, const juce::String& category, const juce::String& author,
                                const juce::String& notes) -> juce::String {
        const auto result = processor.getPresetManager().save (name, category, author, notes);
        if (result.failed())
            return result.getErrorMessage();
        if (settings != nullptr)
            settings->setValue (authorPropertyId, author.trim());
        showMessage ("Preset guardado: " + name.trim());
        updatePresetBar();
        return {};
    };
    saveDialog.onClose = [this] { showSaveDialog (false); };
    content.addChildComponent (saveDialog);

    // ================= Página "Osciladores": Osc A, Osc B, sub y ruido =================
    for (size_t o = 0; o < oscillators.size(); ++o)
    {
        auto& osc = oscillators[o];
        const auto& ids = id::oscillators[o];
        osc.group.setText (o == 0 ? "Oscilador A" : "Oscilador B");
        oscillatorPage.addAndMakeVisible (osc.group);

        oscillatorPage.addAndMakeVisible (osc.onButton);
        osc.onAttachment = std::make_unique<ButtonAttachment> (apvts, ids.on, osc.onButton);
        setUpComboBox (osc.tableBox, oscillatorPage, ids.wavetable, osc.tableAttachment);
        oscillatorPage.addAndMakeVisible (osc.view);
        osc.view.setPositionParameter (apvts.getParameter (ids.position));
        osc.onParam = apvts.getRawParameterValue (ids.on);
        osc.tableParam = apvts.getRawParameterValue (ids.wavetable);
        osc.positionParam = apvts.getRawParameterValue (ids.position);

        const std::array<KnobInfo, numOscillatorKnobs> oscillatorInfos { {
            { ids.position, "Position" },
            { ids.octave, "Octave" },
            { ids.semitones, "Semi" },
            { ids.fine, "Fine" },
            { ids.level, "Level" },
            { ids.pan, "Pan" },
            { ids.unison, "Unison" },
            { ids.detune, "Detune" },
            { ids.width, "Width" },
        } };
        for (size_t k = 0; k < osc.knobs.size(); ++k)
            setUpKnob (osc.knobs[k], oscillatorPage, oscillatorInfos[k].parameterId, oscillatorInfos[k].name,
                       oscillatorKnobWidth);
    }

    oscillatorPage.addAndMakeVisible (subGroup);
    oscillatorPage.addAndMakeVisible (subOnButton);
    subOnAttachment = std::make_unique<ButtonAttachment> (apvts, id::subOn, subOnButton);
    setUpComboBox (subShapeBox, oscillatorPage, id::subShape, subShapeAttachment);
    setUpKnob (subKnobs[0], oscillatorPage, id::subOctave, "Octave", smallKnobWidth);
    setUpKnob (subKnobs[1], oscillatorPage, id::subLevel, "Level", smallKnobWidth);

    oscillatorPage.addAndMakeVisible (noiseGroup);
    oscillatorPage.addAndMakeVisible (noiseOnButton);
    noiseOnAttachment = std::make_unique<ButtonAttachment> (apvts, id::noiseOn, noiseOnButton);
    setUpKnob (noiseKnobs[0], oscillatorPage, id::noiseLevel, "Level", smallKnobWidth);
    setUpKnob (noiseKnobs[1], oscillatorPage, id::noiseColor, "Color", smallKnobWidth);

    // ================= Página "Warp y FM": una columna por oscilador =================
    for (size_t o = 0; o < warps.size(); ++o)
    {
        auto& warp = warps[o];
        const auto& ids = id::oscillatorWarps[o];
        warp.group.setText (o == 0 ? "Oscilador A" : "Oscilador B");
        warpPage.addAndMakeVisible (warp.group);
        warpPage.addAndMakeVisible (warp.view);
        warp.view.setPositionParameter (apvts.getParameter (id::oscillators[o].position));

        warp.warpLabel.setText ("Warp", juce::dontSendNotification);
        warp.fmLabel.setText ("FM / RM", juce::dontSendNotification);
        for (auto* label : { &warp.warpLabel, &warp.fmLabel })
        {
            styleCaption (*label);
            warpPage.addAndMakeVisible (label);
        }
        setUpComboBox (warp.warpBox, warpPage, ids.warpMode, warp.warpAttachment);
        setUpComboBox (warp.fmBox, warpPage, ids.fmMode, warp.fmAttachment);
        setUpKnob (warp.warpKnob, warpPage, ids.warpAmount, "Amount", knobWidth);
        setUpKnob (warp.fmKnob, warpPage, ids.fmAmount, "Amount", knobWidth);

        warp.warpModeParam = apvts.getRawParameterValue (ids.warpMode);
        warp.warpAmountParam = apvts.getRawParameterValue (ids.warpAmount);
    }

    // ================= Página "Filtro y Amp": filtro, Env 1 y voz =================
    soundPage.addAndMakeVisible (filterGroup);
    soundPage.addAndMakeVisible (envelopeGroup);
    soundPage.addAndMakeVisible (voiceGroup);

    for (size_t i = 0; i < knobs.size(); ++i)
        setUpKnob (knobs[i], soundPage, infos[i].parameterId, infos[i].name, knobWidth);

    soundPage.addAndMakeVisible (filterOnButton);
    filterOnAttachment = std::make_unique<ButtonAttachment> (apvts, id::filter1On, filterOnButton);
    setUpComboBox (filterTypeBox, soundPage, id::filter1Type, filterTypeAttachment);
    setUpComboBox (filterSlopeBox, soundPage, id::filter1Slope, filterSlopeAttachment);

    constexpr std::array<KnobInfo, numFilterKnobs> filterInfos { {
        { id::filter1Cutoff, "Cutoff" },
        { id::filter1Resonance, "Resonance" },
        { id::filter1Drive, "Drive" },
        { id::filter1KeyTrack, "Key Track" },
    } };
    for (size_t i = 0; i < filterKnobs.size(); ++i)
        setUpKnob (filterKnobs[i], soundPage, filterInfos[i].parameterId, filterInfos[i].name, knobWidth);
    soundPage.addAndMakeVisible (filterDisplay);

    // ================= Página "Modulación": LFOs, Env 2 y 3, matriz =================
    for (size_t l = 0; l < lfos.size(); ++l)
    {
        auto& lfo = lfos[l];
        const auto& ids = id::lfos[l];
        lfo.group.setText ("LFO " + juce::String (static_cast<int> (l) + 1));
        modulationPage.addAndMakeVisible (lfo.group);

        setUpComboBox (lfo.shapeBox, modulationPage, ids.shape, lfo.shapeAttachment);
        setUpComboBox (lfo.modeBox, modulationPage, ids.mode, lfo.modeAttachment);
        setUpComboBox (lfo.divisionBox, modulationPage, ids.division, lfo.divisionAttachment);
        modulationPage.addAndMakeVisible (lfo.syncButton);
        lfo.syncAttachment = std::make_unique<ButtonAttachment> (apvts, ids.sync, lfo.syncButton);

        // Rate (Hz) y Division (tempo) ocupan el mismo sitio: el timer muestra uno u otro según Sync.
        setUpKnob (lfo.rateKnob, modulationPage, ids.rate, "Rate", lfoRateWidth);
        lfo.divisionLabel.setText ("Division", juce::dontSendNotification);
        styleCaption (lfo.divisionLabel);
        modulationPage.addChildComponent (lfo.divisionLabel);

        modulationPage.addAndMakeVisible (lfo.display);
        lfo.shapeParam = apvts.getRawParameterValue (ids.shape);
        lfo.syncParam = apvts.getRawParameterValue (ids.sync);
    }

    for (size_t e = 0; e < modEnvelopes.size(); ++e)
    {
        auto& env = modEnvelopes[e];
        const auto& ids = id::modEnvelopes[e];
        env.group.setText ("Env " + juce::String (static_cast<int> (e) + 2));
        modulationPage.addAndMakeVisible (env.group);
        setUpKnob (env.knobs[0], modulationPage, ids.attack, "Attack", knobWidth);
        setUpKnob (env.knobs[1], modulationPage, ids.decay, "Decay", knobWidth);
        setUpKnob (env.knobs[2], modulationPage, ids.sustain, "Sustain", knobWidth);
        setUpKnob (env.knobs[3], modulationPage, ids.release, "Release", knobWidth);
    }

    matrixGroup.setText (juce::String::fromUTF8 ("Matriz de modulaci\xc3\xb3n"));
    modulationPage.addAndMakeVisible (matrixGroup);
    for (size_t s = 0; s < modSlots.size(); ++s)
    {
        auto& slot = modSlots[s];
        const auto& ids = id::modSlots[s];

        slot.number.setText (juce::String (static_cast<int> (s) + 1), juce::dontSendNotification);
        slot.number.setJustificationType (juce::Justification::centred);
        slot.number.setColour (juce::Label::textColourId, juce::Colours::grey);
        modulationPage.addAndMakeVisible (slot.number);

        setUpComboBox (slot.sourceBox, modulationPage, ids.source, slot.sourceAttachment);
        setUpComboBox (slot.destinationBox, modulationPage, ids.destination, slot.destinationAttachment);

        slot.arrow.setText (juce::String::fromUTF8 ("\xe2\x86\x92") /* → */, juce::dontSendNotification);
        slot.arrow.setJustificationType (juce::Justification::centred);
        slot.arrow.setBorderSize ({}); // la flecha ocupa 14 px: con el margen por defecto no cabría
        modulationPage.addAndMakeVisible (slot.arrow);

        slot.amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 22);
        slot.amount.setDoubleClickReturnValue (true, 0.0); // doble clic = amount 0 %
        modulationPage.addAndMakeVisible (slot.amount);
        slot.amountAttachment = std::make_unique<SliderAttachment> (apvts, ids.amount, slot.amount);
    }

    // ================= Página "Efectos": distorsión | chorus, delay, reverb =================
    const auto setUpEffectGroup = [this, &apvts] (EffectGroup& fx, const juce::String& title, const char* onId) {
        fx.group.setText (title);
        effectsPage.addAndMakeVisible (fx.group);
        effectsPage.addAndMakeVisible (fx.onButton);
        fx.onAttachment = std::make_unique<ButtonAttachment> (apvts, onId, fx.onButton);
    };
    setUpEffectGroup (distortionGroup, juce::String::fromUTF8 ("Distorsi\xc3\xb3n"), id::distortionOn);
    setUpEffectGroup (chorusGroup, "Chorus", id::chorusOn);
    setUpEffectGroup (delayGroup, "Delay", id::delayOn);
    setUpEffectGroup (reverbGroup, "Reverb", id::reverbOn);

    setUpComboBox (distortionModeBox, effectsPage, id::distortionMode, distortionModeAttachment);
    const std::array<KnobInfo, 3> distortionInfos { { { id::distortionDrive, "Drive" }, { id::distortionTone, "Tone" },
                                                      { id::distortionMix, "Mix" } } };
    for (size_t k = 0; k < distortionKnobs.size(); ++k)
        setUpKnob (distortionKnobs[k], effectsPage, distortionInfos[k].parameterId, distortionInfos[k].name, smallKnobWidth);

    const std::array<KnobInfo, 4> chorusInfos { { { id::chorusRate, "Rate" }, { id::chorusDepth, "Depth" },
                                                  { id::chorusFeedback, "Feedback" }, { id::chorusMix, "Mix" } } };
    for (size_t k = 0; k < chorusKnobs.size(); ++k)
        setUpKnob (chorusKnobs[k], effectsPage, chorusInfos[k].parameterId, chorusInfos[k].name, smallKnobWidth);

    // Delay: como en los LFO, Time (ms) y Division (tempo) ocupan el mismo sitio y el timer muestra uno u otro.
    effectsPage.addAndMakeVisible (delaySyncButton);
    delaySyncAttachment = std::make_unique<ButtonAttachment> (apvts, id::delaySync, delaySyncButton);
    effectsPage.addAndMakeVisible (delayPingPongButton);
    delayPingPongAttachment = std::make_unique<ButtonAttachment> (apvts, id::delayPingPong, delayPingPongButton);
    setUpKnob (delayTimeKnob, effectsPage, id::delayTime, "Time", knobWidth);
    delayDivisionLabel.setText ("Division", juce::dontSendNotification);
    styleCaption (delayDivisionLabel);
    effectsPage.addChildComponent (delayDivisionLabel);
    setUpComboBox (delayDivisionBox, effectsPage, id::delayDivision, delayDivisionAttachment);
    delaySyncParam = apvts.getRawParameterValue (id::delaySync);
    const std::array<KnobInfo, 3> delayInfos { { { id::delayFeedback, "Feedback" }, { id::delayTone, "Tone" },
                                                 { id::delayMix, "Mix" } } };
    for (size_t k = 0; k < delayKnobs.size(); ++k)
        setUpKnob (delayKnobs[k], effectsPage, delayInfos[k].parameterId, delayInfos[k].name, knobWidth);

    const std::array<KnobInfo, 5> reverbInfos { { { id::reverbSize, "Size" }, { id::reverbDecay, "Decay" },
                                                  { id::reverbDamping, "Damping" }, { id::reverbPreDelay, "Pre-Delay" },
                                                  { id::reverbMix, "Mix" } } };
    for (size_t k = 0; k < reverbKnobs.size(); ++k)
        setUpKnob (reverbKnobs[k], effectsPage, reverbInfos[k].parameterId, reverbInfos[k].name, knobWidth);

    // ================= Página "Cuerda" (Fase 12) =================
    stringGroup.setText ("Cuerda pulsada (Karplus-Strong)");
    stringPage.addAndMakeVisible (stringGroup);
    stringPage.addAndMakeVisible (stringOnButton);
    stringOnAttachment = std::make_unique<ButtonAttachment> (apvts, id::stringOn, stringOnButton);
    const std::array<KnobInfo, 7> stringInfos { { { id::stringOctave, "Octave" },
                                                  { id::stringSemi, "Semi" },
                                                  { id::stringFine, "Fine" },
                                                  { id::stringLevel, "Level" },
                                                  { id::stringDecay, "Decay" },
                                                  { id::stringDamping, "Damping" },
                                                  { id::stringStiffness, "Stiffness" } } };
    for (size_t k = 0; k < stringKnobs.size(); ++k)
        setUpKnob (stringKnobs[k], stringPage, stringInfos[k].parameterId, stringInfos[k].name, knobWidth);
    stringHelp.setText (juce::String::fromUTF8 (
                            "Cada nota puntea la cuerda: un golpe (ruido o impulso) entra en una l\xc3\xadnea de "
                            "retardo que se realimenta y pierde agudos en cada vuelta. Decay = cu\xc3\xa1nto tarda el "
                            "fundamental en caer 60 dB (igual en todas las notas). Damping = cu\xc3\xa1nto antes "
                            "mueren los agudos: poco = met\xc3\xa1lica y brillante (arpa, clavec\xc3\xadn); mucho = "
                            "apagada y redonda (nylon, koto con la palma). Al soltar la tecla la cuerda se apaga como "
                            "con la palma; el Release de Env 1 sigue mandando sobre el final. Suena junto con los "
                            "osciladores, el sub y el ruido, y pasa por el mismo filtro y los efectos. "
                            "Stiffness = rigidez: los arm\xc3\xb3nicos agudos suben (se estiran) como en una cuerda "
                            "gruesa de acero. 0 % = cuerda ideal; 25-50 % = piano (mejor en graves); 75-100 % = campana, "
                            "barra de metal. El fundamental sigue afinado. Se elige al puntear la cuerda en silencio: un "
                            "redisparo con la cuerda sonando conserva su rigidez."),
                        juce::dontSendNotification);
    stringHelp.setJustificationType (juce::Justification::topLeft);
    stringHelp.setColour (juce::Label::textColourId, undertow::gui::theme::dimText);
    stringPage.addAndMakeVisible (stringHelp);

    pluckGroup.setText ("Punteo");
    stringPage.addAndMakeVisible (pluckGroup);
    excitationLabel.setText ("Excitation", juce::dontSendNotification);
    styleCaption (excitationLabel);
    stringPage.addAndMakeVisible (excitationLabel);
    setUpComboBox (excitationBox, stringPage, id::stringExcitation, excitationAttachment);
    excitationParam = apvts.getRawParameterValue (id::stringExcitation);
    const std::array<KnobInfo, 2> pluckInfos { { { id::stringPickPosition, "Pick Pos" }, { id::stringPickTone, "Pick Tone" } } };
    for (size_t k = 0; k < pluckKnobs.size(); ++k)
        setUpKnob (pluckKnobs[k], stringPage, pluckInfos[k].parameterId, pluckInfos[k].name, knobWidth);
    pluckHelp.setText (juce::String::fromUTF8 (
                            "Excitation = con qu\xc3\xa9 se puntea. White Noise: ruido blanco, todos los "
                            "arm\xc3\xb3nicos (el sonido de siempre). Filtered Noise: el ruido pasa por un paso bajo "
                            "(Pick Tone): p\xc3\xba" "a blanda o dedo, ataque m\xc3\xa1s redondo. Impulse: un golpe "
                            "seco por el mismo paso bajo, id\xc3\xa9ntico en cada nota. Pick Position = d\xc3\xb3nde "
                            "se puntea: 0 % sin efecto; cerca del puente (5-15 %) brillante y nasal; 50 % en el "
                            "centro: sin arm\xc3\xb3nicos pares, hueco. Pick Position y Pick Tone se leen al puntear: "
                            "se oyen en la nota siguiente (y la matriz puede moverlos por nota: Velocity \xe2\x86\x92 "
                            "Pick Tone)."),
                       juce::dontSendNotification);
    pluckHelp.setJustificationType (juce::Justification::topLeft);
    pluckHelp.setColour (juce::Label::textColourId, undertow::gui::theme::dimText);
    stringPage.addAndMakeVisible (pluckHelp);

    activeVoicesLabel.setJustificationType (juce::Justification::centredRight);
    activeVoicesLabel.setColour (juce::Label::textColourId, undertow::gui::theme::dimText);
    content.addAndMakeVisible (activeVoicesLabel);

    layoutContent();

    // El zoom se decide ANTES de poner los límites: setResizeLimits ya redimensiona la ventana (aún de 0 × 0) y eso
    // no debe guardarse como el zoom elegido.
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

    oscillatorTab.setToggleState (true, juce::dontSendNotification);
    showPage (Page::oscillators);
    timerCallback(); // estado inicial (p. ej. Rate o Division visibles según Sync) sin esperar al primer tick
    startTimerHz (30); // la GUI consulta el estado; el audio nunca espera a la GUI
}

UndertowAudioProcessorEditor::~UndertowAudioProcessorEditor()
{
    stopTimer();
    if (settings != nullptr)
        settings->saveIfNeeded();
    setLookAndFeel (nullptr);
}

float UndertowAudioProcessorEditor::initialZoom() const
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
        zoomThatFits = juce::jmin (1.0f, 0.92f * area.getWidth() / baseWidth, 0.85f * area.getHeight() / baseHeight);
    }
    return std::floor (zoomThatFits * 20.0f) / 20.0f; // en pasos de 5 %
}

void UndertowAudioProcessorEditor::setZoom (float newZoom)
{
    newZoom = juce::jlimit (minZoom, maxZoom, newZoom);
    setSize (juce::roundToInt (baseWidth * newZoom), juce::roundToInt (baseHeight * newZoom));
}

void UndertowAudioProcessorEditor::showZoomMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Zoom");
    const int current = juce::roundToInt (zoom * 100.0f);
    for (const int step : zoomSteps)
        menu.addItem (step, juce::String (step) + " %", true, step == current);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&zoomButton),
                        [safeThis = juce::Component::SafePointer<UndertowAudioProcessorEditor> (this)] (int result) {
                            if (safeThis != nullptr && result > 0)
                                safeThis->setZoom (static_cast<float> (result) / 100.0f);
                        });
}

void UndertowAudioProcessorEditor::showMessage (const juce::String& message)
{
    modBar.setMessage (message);
    messageExpiry = juce::Time::getApproximateMillisecondCounter() + messageSeconds * 1000;
}

void UndertowAudioProcessorEditor::showPage (Page page)
{
    showPresetBrowser (false);
    showSaveDialog (false);
    oscillatorPage.setVisible (page == Page::oscillators);
    warpPage.setVisible (page == Page::warp);
    soundPage.setVisible (page == Page::sound);
    modulationPage.setVisible (page == Page::modulation);
    effectsPage.setVisible (page == Page::effects);
    stringPage.setVisible (page == Page::string);
}

// ================= Fase 10: presets =================

void UndertowAudioProcessorEditor::loadPreset (const undertow::presets::Preset& preset)
{
    const auto problems = processor.getPresetManager().load (preset);
    if (problems.isEmpty())
        showMessage ("Preset: " + preset.name + juce::String::fromUTF8 (" \xc2\xb7 ") + preset.category);
    else // un preset de otra versión o editado a mano: se carga lo que se entiende y se avisa del resto
        showMessage (preset.name + ": " + juce::String (problems.size()) + " valor(es) no se entendieron (" + problems[0] + ")");
    updatePresetBar();
}

void UndertowAudioProcessorEditor::showPresetBrowser (bool show)
{
    if (show == presetBrowser.isVisible())
        return;
    if (show)
    {
        showSaveDialog (false);
        presetBrowser.refresh();
        presetBrowser.toFront (false);
    }
    presetBrowser.setVisible (show);
    presetBar.setBrowserOpen (show);
    if (show)
        presetBrowser.focusList();
}

void UndertowAudioProcessorEditor::showSaveDialog (bool show)
{
    if (show == saveDialog.isVisible())
        return;
    if (! show)
    {
        saveDialog.setVisible (false);
        return;
    }

    showPresetBrowser (false);
    auto& manager = processor.getPresetManager();
    const juce::String lastAuthor = settings != nullptr ? settings->getValue (authorPropertyId) : juce::String();

    // Si el sonido es un preset tuyo, se propone actualizarlo. Si partiste de uno de fábrica, las notas empiezan
    // diciendo de cuál: así queda escrito qué cambiaste respecto al original.
    saveDialog.toFront (false);
    saveDialog.setVisible (true); // antes de open(): un componente invisible no puede recibir el foco del teclado
    const auto* current = manager.getCurrentPreset();
    if (current != nullptr && ! current->factory)
        saveDialog.open (current->name, current->category, current->author.isNotEmpty() ? current->author : lastAuthor,
                         current->notes);
    else if (current != nullptr && current->category != "Init")
        saveDialog.open ({}, current->category, lastAuthor,
                         juce::String::fromUTF8 ("Basado en \xc2\xab") + current->name
                             + juce::String::fromUTF8 ("\xc2\xbb (f\xc3\xa1" "brica).\n\nQu\xc3\xa9 cambi\xc3\xa9 y por qu\xc3\xa9:\n- "));
    else
        saveDialog.open ({}, "Otros", lastAuthor,
                         juce::String::fromUTF8 ("Qu\xc3\xa9 es:\n\nC\xc3\xb3mo se construy\xc3\xb3:\n- \n\nT\xc3\xb3" "calo:\n"));
}

void UndertowAudioProcessorEditor::showPresetMenu (juce::Component& anchor)
{
    enum Items { init = 1, saveAs, openFolder, rescan };
    juce::PopupMenu menu;
    menu.addSectionHeader ("Presets");
    menu.addItem (init, "Init (empezar desde cero)");
    menu.addItem (saveAs, juce::String::fromUTF8 ("Guardar como\xe2\x80\xa6"));
    menu.addSeparator();
    menu.addItem (openFolder, "Abrir la carpeta de mis presets");
    menu.addItem (rescan, "Volver a leer la carpeta");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor),
                        [safeThis = juce::Component::SafePointer<UndertowAudioProcessorEditor> (this)] (int result) {
                            if (safeThis == nullptr || result == 0)
                                return;
                            auto& manager = safeThis->processor.getPresetManager();
                            switch (result)
                            {
                                case init:
                                    manager.loadInit();
                                    safeThis->showMessage ("Init: todos los controles en su valor por defecto.");
                                    break;
                                case saveAs:
                                    safeThis->showSaveDialog (true);
                                    break;
                                case openFolder:
                                {
                                    const auto folder = manager.getUserFolder();
                                    folder.createDirectory();
                                    folder.startAsProcess(); // abre la carpeta en el Explorador
                                    break;
                                }
                                case rescan:
                                    manager.rescan();
                                    if (safeThis->presetBrowser.isVisible())
                                        safeThis->presetBrowser.refresh();
                                    safeThis->showMessage (juce::String (static_cast<int> (manager.getPresets().size()))
                                                           + " presets en la biblioteca.");
                                    break;
                                default:
                                    break;
                            }
                            safeThis->updatePresetBar();
                        });
}

void UndertowAudioProcessorEditor::updatePresetBar()
{
    auto& manager = processor.getPresetManager();
    const auto* current = manager.getCurrentPreset();
    presetBar.setPreset (current != nullptr ? current->category : juce::String(), manager.getCurrentName(), manager.isModified());
}

void UndertowAudioProcessorEditor::setUpKnob (Knob& knob, juce::Component& page, const char* parameterId,
                                              const juce::String& name, int width)
{
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, width - 6, 20);
    page.addAndMakeVisible (knob.slider);

    knob.label.setText (name, juce::dontSendNotification);
    styleCaption (knob.label);
    page.addAndMakeVisible (knob.label);

    // El attachment sincroniza la perilla con el parámetro en ambos sentidos
    // (y con la automatización de FL) sin que la GUI toque el hilo de audio.
    knob.attachment = std::make_unique<SliderAttachment> (processor.getParameters(), parameterId, knob.slider);

    auto* parameter = processor.getParameters().getParameter (parameterId);
    knob.slider.setParameter (parameter);
    knob.slider.setName (parameter->getName (64)); // "Osc A Level" en vez de "Level": los mensajes son más claros
    // Doble clic = valor por defecto del parámetro.
    knob.slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));

    if (const auto target = undertow::gui::targetForParameter (parameterId))
    {
        knob.slider.setModulationTarget (*target, router, [this] (const juce::String& message) { showMessage (message); });
        modulationKnobs.push_back (&knob.slider);
    }
}

void UndertowAudioProcessorEditor::styleCaption (juce::Label& label)
{
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, undertow::gui::theme::dimText);
}

void UndertowAudioProcessorEditor::setUpComboBox (juce::ComboBox& box, juce::Component& page, const char* parameterId,
                                                  std::unique_ptr<ComboBoxAttachment>& attachment)
{
    // Las opciones deben existir ANTES de crear el attachment (usa el índice del ítem, empezando en 1).
    // Salen del propio parámetro: así la GUI nunca se desincroniza de la lista guardada.
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor.getParameters().getParameter (parameterId)))
        box.addItemList (choice->choices, 1);
    page.addAndMakeVisible (box);
    attachment = std::make_unique<ComboBoxAttachment> (processor.getParameters(), parameterId, box);
}

void UndertowAudioProcessorEditor::layoutKnob (Knob& knob, juce::Rectangle<int> column)
{
    column = column.withHeight (knobHeight + 20);
    knob.label.setBounds (column.removeFromTop (20));
    knob.slider.setBounds (column);
}

void UndertowAudioProcessorEditor::timerCallback()
{
    updatePresetBar();

    const int count = processor.getActiveVoiceCount();
    if (count != lastShownVoiceCount)
    {
        lastShownVoiceCount = count;
        activeVoicesLabel.setText ("Voces sonando: " + juce::String (count), juce::dontSendNotification);
    }

    // --- Modulación: anillos de las perillas, fuentes en uso y mensajes ---
    namespace synth = undertow::synth;
    const auto slots = router.read();
    for (auto* knob : modulationKnobs)
        knob->updateModulation (slots, processor.getModulationDisplay (knob->getModulationDestination()));
    modBar.setUsage (slots);
    if (messageExpiry != 0 && juce::Time::getApproximateMillisecondCounter() > messageExpiry)
    {
        messageExpiry = 0;
        modBar.setMessage ({});
    }
    for (size_t s = 0; s < modSlots.size(); ++s)
        modSlots[s].number.setColour (juce::Label::textColourId, slots[s].isRouted()
                                                                     ? undertow::gui::theme::sourceColour (slots[s].source)
                                                                     : undertow::gui::theme::dimText.withAlpha (0.6f));

    // --- Visores del wavetable ---
    // Se lee el valor del parámetro (no el de la perilla) para que el visor siga también la automatización, y se le
    // suma la modulación de la nota más reciente: con un LFO en Position se ve la onda moverse.
    const auto& bank = processor.getWavetableBank();
    constexpr std::array<std::array<synth::ModDestination, 2>, synth::numOscillators> viewDestinations { {
        { synth::ModDestination::oscAPosition, synth::ModDestination::oscAWarp },
        { synth::ModDestination::oscBPosition, synth::ModDestination::oscBWarp },
    } };
    for (size_t o = 0; o < oscillators.size(); ++o)
    {
        auto& osc = oscillators[o];
        auto& warp = warps[o];
        const auto positionModulation = processor.getModulationDisplay (viewDestinations[o][0]);
        const auto warpModulation = processor.getModulationDisplay (viewDestinations[o][1]);

        undertow::gui::WavetableView::State state;
        state.table = &bank.get (static_cast<int> (osc.tableParam->load()));
        state.position = juce::jlimit (0.0f, 1.0f, osc.positionParam->load() + positionModulation.value_or (0.0f));
        state.warpMode = static_cast<undertow::dsp::WarpMode> (static_cast<int> (warp.warpModeParam->load()));
        state.warpAmount = juce::jlimit (0.0f, 1.0f, warp.warpAmountParam->load() + warpModulation.value_or (0.0f));
        state.enabled = osc.onParam->load() >= 0.5f;
        state.modulated = std::abs (positionModulation.value_or (0.0f)) > 1.0e-4f
                          || std::abs (warpModulation.value_or (0.0f)) > 1.0e-4f;
        osc.view.setState (state);
        warp.view.setState (state);
    }

    const auto filter = processor.readFilterSettings();
    filterDisplay.setResponse (filter.parameters, filter.enabled, processor.getCurrentSampleRate());

    for (size_t l = 0; l < lfos.size(); ++l)
    {
        auto& lfo = lfos[l];
        const bool synced = lfo.syncParam->load() >= 0.5f;
        lfo.rateKnob.slider.setVisible (! synced);
        lfo.rateKnob.label.setVisible (! synced);
        lfo.divisionBox.setVisible (synced);
        lfo.divisionLabel.setVisible (synced);

        lfo.display.setState (static_cast<undertow::dsp::LfoShape> (static_cast<int> (lfo.shapeParam->load())),
                              processor.getLfoDisplayPhase (static_cast<int> (l)));
    }

    const bool delaySynced = delaySyncParam->load() >= 0.5f;
    delayTimeKnob.slider.setVisible (! delaySynced);
    delayTimeKnob.label.setVisible (! delaySynced);
    delayDivisionBox.setVisible (delaySynced);
    delayDivisionLabel.setVisible (delaySynced);

    // Pick Tone no hace nada con White Noise: se atenúa (sigue aceptando modulación, por si se cambia de excitación).
    const bool toneUsed = static_cast<int> (excitationParam->load()) != static_cast<int> (undertow::dsp::StringExcitation::whiteNoise);
    pluckKnobs[1].slider.setAlpha (toneUsed ? 1.0f : 0.4f);
    pluckKnobs[1].label.setAlpha (toneUsed ? 1.0f : 0.4f);
}

void UndertowAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (undertow::gui::theme::background);
}

void UndertowAudioProcessorEditor::Content::paint (juce::Graphics& g)
{
    namespace theme = undertow::gui::theme;
    g.setGradientFill (juce::ColourGradient (theme::background.brighter (0.04f), 0.0f, 0.0f, theme::background, 0.0f,
                                             static_cast<float> (getHeight()), false));
    g.fillAll();

    const auto title = getLocalBounds().withHeight (headerHeight).translated (groupPadding, groupPadding);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (21.0f, juce::Font::bold));
    g.drawText ("UNDERTOW", title, juce::Justification::centredLeft);
    g.setColour (theme::accent);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    const juce::Font titleFont (juce::FontOptions (21.0f, juce::Font::bold));
    const auto titleWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (titleFont, "UNDERTOW"));
    g.drawText ("SYNTH", title.withTrimmedLeft (titleWidth + 6), juce::Justification::centredLeft);
}

void UndertowAudioProcessorEditor::resized()
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

void UndertowAudioProcessorEditor::layoutContent()
{
    auto area = content.getLocalBounds().reduced (groupPadding);

    // Fila 1: título (lo dibuja Content), barra de presets y zoom.
    auto header = area.removeFromTop (headerHeight);
    header.removeFromLeft (titleAreaWidth);
    zoomButton.setBounds (header.removeFromRight (zoomButtonWidth).reduced (0, 3));
    header.removeFromRight (groupPadding);
    presetBar.setBounds (header.reduced (0, 1));
    area.removeFromTop (8);

    // Fila 2: pestañas y contador de voces.
    auto tabRow = area.removeFromTop (tabRowHeight);
    for (auto* tab : { &oscillatorTab, &warpTab, &soundTab, &modulationTab, &effectsTab, &stringTab })
        tab->setBounds (tabRow.removeFromLeft (tabWidth).reduced (2, 1));
    activeVoicesLabel.setBounds (tabRow.removeFromRight (voicesLabelWidth));
    area.removeFromTop (groupPadding);

    modBar.setBounds (area.removeFromBottom (modBarHeight));
    area.removeFromBottom (groupPadding);

    oscillatorPage.setBounds (area);
    warpPage.setBounds (area);
    soundPage.setBounds (area);
    modulationPage.setBounds (area);
    effectsPage.setBounds (area);
    stringPage.setBounds (area);
    presetBrowser.setBounds (area);
    saveDialog.setBounds (area);

    const auto innerOf = [] (juce::Rectangle<int> groupArea) {
        return groupArea.reduced (groupPadding, 0).withTrimmedTop (22).withTrimmedBottom (groupPadding);
    };

    // ================= Página "Osciladores" =================
    {
        auto page = oscillatorPage.getLocalBounds();

        // Filas 1 y 2: Osc A y Osc B. Columna con On y tabla en una línea y el visor 3D debajo; después 9 perillas.
        for (auto& osc : oscillators)
        {
            auto groupArea = page.removeFromTop (rowHeight);
            page.removeFromTop (groupPadding);
            osc.group.setBounds (groupArea);
            auto inner = innerOf (groupArea);

            auto column = inner.removeFromLeft (oscillatorViewWidth);
            inner.removeFromLeft (8);
            auto line = column.removeFromTop (24);
            osc.onButton.setBounds (line.removeFromLeft (56));
            osc.tableBox.setBounds (line);
            column.removeFromTop (6);
            osc.view.setBounds (column);

            for (auto& knob : osc.knobs)
                layoutKnob (knob, inner.removeFromLeft (oscillatorKnobWidth));
        }

        // Fila 3: Sub | Ruido.
        auto row = page.removeFromTop (rowHeight);
        const int halfWidth = (row.getWidth() - groupPadding) / 2;
        const auto layoutSmallGroup = [&] (juce::GroupComponent& group, juce::ToggleButton& onButton,
                                           juce::ComboBox* box, std::array<Knob, 2>& groupKnobs) {
            auto groupArea = row.removeFromLeft (halfWidth);
            row.removeFromLeft (groupPadding);
            group.setBounds (groupArea);
            auto inner = innerOf (groupArea);

            auto column = inner.removeFromLeft (oscillatorColumnWidth);
            onButton.setBounds (column.removeFromTop (24));
            if (box != nullptr)
                box->setBounds (column.removeFromTop (26));

            inner.removeFromLeft ((inner.getWidth() - 2 * smallKnobWidth) / 2); // perillas centradas
            for (auto& knob : groupKnobs)
                layoutKnob (knob, inner.removeFromLeft (smallKnobWidth));
        };
        layoutSmallGroup (subGroup, subOnButton, &subShapeBox, subKnobs);
        layoutSmallGroup (noiseGroup, noiseOnButton, nullptr, noiseKnobs);
    }

    // ================= Página "Warp y FM" =================
    // Dos columnas (A | B): arriba el dibujo del warp; abajo Warp y FM/RM, cada uno con su selector y su perilla.
    {
        auto page = warpPage.getLocalBounds();
        const int halfWidth = (page.getWidth() - groupPadding) / 2;
        for (auto& warp : warps)
        {
            auto groupArea = page.removeFromLeft (halfWidth);
            page.removeFromLeft (groupPadding);
            warp.group.setBounds (groupArea);
            auto inner = innerOf (groupArea);

            auto controls = inner.removeFromBottom (knobHeight + 20 + 28 + 26);
            inner.removeFromBottom (groupPadding);
            warp.view.setBounds (inner);

            const int columnWidth = controls.getWidth() / 2;
            const auto layoutColumn = [] (juce::Rectangle<int> column, juce::Label& label, juce::ComboBox& box, Knob& knob) {
                label.setBounds (column.removeFromTop (20));
                box.setBounds (column.removeFromTop (28).reduced (8, 0));
                column.removeFromTop (6);
                layoutKnob (knob, column.withSizeKeepingCentre (knobWidth, column.getHeight()));
            };
            layoutColumn (controls.removeFromLeft (columnWidth), warp.warpLabel, warp.warpBox, warp.warpKnob);
            layoutColumn (controls, warp.fmLabel, warp.fmBox, warp.fmKnob);
        }
    }

    // ================= Página "Filtro y Amp" =================
    {
        auto page = soundPage.getLocalBounds();

        // Fila 1: filtro (encendido, tipo y pendiente; perillas; curva de respuesta).
        {
            auto groupArea = page.removeFromTop (rowHeight);
            filterGroup.setBounds (groupArea);
            auto inner = innerOf (groupArea);

            auto optionsColumn = inner.removeFromLeft (wavetableColumnWidth).reduced (4, 0);
            filterOnButton.setBounds (optionsColumn.removeFromTop (28));
            optionsColumn.removeFromTop (6);
            filterTypeBox.setBounds (optionsColumn.removeFromTop (28));
            optionsColumn.removeFromTop (6);
            filterSlopeBox.setBounds (optionsColumn.removeFromTop (28));

            for (auto& knob : filterKnobs)
                layoutKnob (knob, inner.removeFromLeft (knobWidth));

            filterDisplay.setBounds (inner.withTrimmedLeft (groupPadding));
        }
        page.removeFromTop (groupPadding);

        // Fila 2: Env 1 y voz.
        auto row = page.removeFromTop (rowHeight);
        const auto layoutGroup = [&] (juce::GroupComponent& group, size_t firstKnob, size_t count) {
            auto groupArea = row.removeFromLeft (static_cast<int> (count) * knobWidth + groupPadding * 2);
            group.setBounds (groupArea);
            auto inner = innerOf (groupArea);
            for (size_t i = firstKnob; i < firstKnob + count; ++i)
                layoutKnob (knobs[i], inner.removeFromLeft (knobWidth));
        };
        layoutGroup (envelopeGroup, 0, numEnvelopeKnobs);
        row.removeFromLeft (groupPadding);
        layoutGroup (voiceGroup, numEnvelopeKnobs, numVoiceKnobs);
    }

    // ================= Página "Modulación" =================
    {
        auto page = modulationPage.getLocalBounds();
        const int halfWidth = (page.getWidth() - groupPadding) / 2;

        // Fila 1: LFO 1 | LFO 2 (opciones; Rate o Division; dibujo de la forma).
        {
            auto row = page.removeFromTop (rowHeight);
            for (auto& lfo : lfos)
            {
                auto groupArea = row.removeFromLeft (halfWidth);
                row.removeFromLeft (groupPadding);
                lfo.group.setBounds (groupArea);
                auto inner = innerOf (groupArea);

                auto options = inner.removeFromLeft (lfoOptionsWidth);
                lfo.shapeBox.setBounds (options.removeFromTop (28));
                options.removeFromTop (6);
                lfo.modeBox.setBounds (options.removeFromTop (28));
                options.removeFromTop (6);
                lfo.syncButton.setBounds (options.removeFromTop (28));

                auto rateColumn = inner.removeFromLeft (lfoRateWidth).withTrimmedLeft (4);
                layoutKnob (lfo.rateKnob, rateColumn);
                lfo.divisionLabel.setBounds (rateColumn.removeFromTop (20));
                lfo.divisionBox.setBounds (rateColumn.removeFromTop (28));

                lfo.display.setBounds (inner.withTrimmedLeft (8));
            }
        }
        page.removeFromTop (groupPadding);

        // Fila 2: Env 2 | Env 3.
        {
            auto row = page.removeFromTop (rowHeight);
            for (auto& env : modEnvelopes)
            {
                auto groupArea = row.removeFromLeft (halfWidth);
                row.removeFromLeft (groupPadding);
                env.group.setBounds (groupArea);
                auto inner = innerOf (groupArea);
                inner.removeFromLeft ((inner.getWidth() - 4 * knobWidth) / 2); // centradas en el grupo
                for (auto& knob : env.knobs)
                    layoutKnob (knob, inner.removeFromLeft (knobWidth));
            }
        }
        page.removeFromTop (groupPadding);

        // Fila 3: matriz, 8 rutas en 2 columnas de 4. Cada fila: número, fuente → destino, amount.
        matrixGroup.setBounds (page);
        auto inner = innerOf (page);
        const int columnWidth = (inner.getWidth() - 2 * groupPadding) / 2;
        const int rowStep = std::min (matrixRowHeight, inner.getHeight() / 4);

        for (size_t s = 0; s < modSlots.size(); ++s)
        {
            auto& slot = modSlots[s];
            const int column = static_cast<int> (s) / 4;
            const int row = static_cast<int> (s) % 4;
            auto line = juce::Rectangle<int> (inner.getX() + column * (columnWidth + 2 * groupPadding),
                                              inner.getY() + row * rowStep, columnWidth, rowStep)
                            .reduced (0, 2);

            slot.number.setBounds (line.removeFromLeft (16));
            slot.sourceBox.setBounds (line.removeFromLeft (104));
            slot.arrow.setBounds (line.removeFromLeft (14));
            slot.destinationBox.setBounds (line.removeFromLeft (128));
            line.removeFromLeft (4);
            slot.amount.setBounds (line);
        }
    }

    // ================= Página "Efectos" =================
    // Fila 1: Distorsión | Chorus. Filas 2 y 3: Delay y Reverb a todo el ancho. En cada grupo, una columna con
    // On (y sus opciones) y después las perillas, en el orden de la cadena de señal.
    {
        auto page = effectsPage.getLocalBounds();
        const auto startGroup = [&innerOf] (EffectGroup& fx, juce::Rectangle<int> groupArea,
                                            int columnWidth = effectColumnWidth) {
            fx.group.setBounds (groupArea);
            auto inner = innerOf (groupArea);
            auto column = inner.removeFromLeft (columnWidth);
            fx.onButton.setBounds (column.removeFromTop (24));
            column.removeFromTop (6);
            return std::pair { inner, column };
        };

        auto row = page.removeFromTop (rowHeight);
        page.removeFromTop (groupPadding);
        const int halfWidth = (row.getWidth() - groupPadding) / 2;
        {
            auto [inner, column] = startGroup (distortionGroup, row.removeFromLeft (halfWidth), effectColumnWidth - 20);
            distortionModeBox.setBounds (column.removeFromTop (26).withTrimmedRight (8));
            for (auto& knob : distortionKnobs)
                layoutKnob (knob, inner.removeFromLeft (smallKnobWidth));
        }
        row.removeFromLeft (groupPadding);
        {
            auto [inner, column] = startGroup (chorusGroup, row, chorusColumnWidth);
            for (auto& knob : chorusKnobs)
                layoutKnob (knob, inner.removeFromLeft (smallKnobWidth));
        }

        {
            auto [inner, column] = startGroup (delayGroup, page.removeFromTop (rowHeight));
            page.removeFromTop (groupPadding);
            delaySyncButton.setBounds (column.removeFromTop (24));
            column.removeFromTop (6);
            delayPingPongButton.setBounds (column.removeFromTop (24));

            auto timeColumn = inner.removeFromLeft (knobWidth).withTrimmedLeft (4);
            layoutKnob (delayTimeKnob, timeColumn);
            delayDivisionLabel.setBounds (timeColumn.removeFromTop (20));
            delayDivisionBox.setBounds (timeColumn.removeFromTop (28));
            for (auto& knob : delayKnobs)
                layoutKnob (knob, inner.removeFromLeft (knobWidth));
        }

        {
            auto [inner, column] = startGroup (reverbGroup, page.removeFromTop (rowHeight));
            for (auto& knob : reverbKnobs)
                layoutKnob (knob, inner.removeFromLeft (knobWidth));
        }
    }

    // ================= Página "Cuerda" =================
    // Una fila: On y después las perillas (afinación, nivel y las tres de la cuerda). Debajo, la explicación.
    {
        auto page = stringPage.getLocalBounds();
        auto groupArea = page.removeFromTop (rowHeight);
        page.removeFromTop (groupPadding);
        stringGroup.setBounds (groupArea);
        auto inner = innerOf (groupArea);
        stringOnButton.setBounds (inner.removeFromLeft (effectColumnWidth).removeFromTop (24));
        for (auto& knob : stringKnobs)
            layoutKnob (knob, inner.removeFromLeft (knobWidth));

        // Fila 2 (Fase 13): el punteo (selector y 2 perillas) y su explicación al lado.
        auto row = page.removeFromTop (rowHeight);
        page.removeFromTop (groupPadding);
        auto pluckArea = row.removeFromLeft (groupPadding * 2 + effectColumnWidth + 2 * knobWidth);
        pluckGroup.setBounds (pluckArea);
        auto pluckInner = innerOf (pluckArea);
        auto column = pluckInner.removeFromLeft (effectColumnWidth).withTrimmedRight (8);
        excitationLabel.setBounds (column.removeFromTop (20));
        excitationBox.setBounds (column.removeFromTop (26));
        for (auto& knob : pluckKnobs)
            layoutKnob (knob, pluckInner.removeFromLeft (knobWidth));
        pluckHelp.setBounds (row.withTrimmedLeft (groupPadding).withTrimmedTop (8));

        stringHelp.setBounds (page.removeFromTop (rowHeight).reduced (groupPadding, 0));
    }
}
