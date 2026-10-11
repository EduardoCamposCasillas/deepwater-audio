#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "deepwater/presets/Palette.h"
#include "deepwater/presets/PresetBar.h"
#include "deepwater/presets/PresetBrowser.h"
#include "deepwater/presets/PresetManager.h"
#include "deepwater/presets/PresetSaveDialog.h"

namespace deepwater::presets
{

// Todo lo de presets que va en el editor de un plugin: la barra de la cabecera, el navegador y el panel de guardar, y
// lo que pasa al pulsar cada cosa (cargar, siguiente/anterior, guardar con las notas ya empezadas, menú ···). Así cada
// editor solo decide DÓNDE van (setBounds) y cómo muestra los mensajes (onMessage).
//
//   PresetPanel presets { manager, palette, settings };
//   presets.addTo (content);                       // en el constructor del editor
//   presets.setBounds (cabecera, zonaDeLasPáginas);  // en el layout
//   presets.update();                              // en el temporizador de la GUI (nombre y "*")
class PresetPanel
{
public:
    // 'settings' = preferencias del editor (recuerda el último autor escrito al guardar); puede ser nullptr.
    PresetPanel (PresetManager& m, const Palette& palette, juce::PropertiesFile* preferences)
        : manager (m), settings (preferences), bar (palette), browser (m, palette), saveDialog (palette, m.getFormat())
    {
        bar.onStep = [this] (int delta) {
            if (const auto* preset = manager.step (delta))
            {
                message ("Preset: " + preset->name + juce::String::fromUTF8 (" \xc2\xb7 ") + preset->category);
                if (browser.isVisible())
                    browser.refresh();
            }
            update();
        };
        bar.onToggleBrowser = [this] { showBrowser (! browser.isVisible()); };
        bar.onSave = [this] { showSaveDialog (true); };
        bar.onMenu = [this] (juce::Component& anchor) { showMenu (anchor); };

        browser.onLoad = [this] (const Preset& preset) { load (preset); };
        browser.onClose = [this] { showBrowser (false); };
        browser.onMessage = [this] (const juce::String& text) {
            message (text);
            update();
        };

        saveDialog.setFolder (manager.getUserFolder());
        saveDialog.exists = [this] (const juce::String& name) { return manager.userPresetExists (name); };
        saveDialog.onSave = [this] (const juce::String& name, const juce::String& category, const juce::String& author,
                                    const juce::String& notes) -> juce::String {
            const auto result = manager.save (name, category, author, notes);
            if (result.failed())
                return result.getErrorMessage();
            if (settings != nullptr)
                settings->setValue (authorPropertyId, author.trim());
            message ("Preset guardado: " + name.trim());
            update();
            return {};
        };
        saveDialog.onClose = [this] { showSaveDialog (false); };
    }

    // Mensajes para el usuario ("Preset: …", avisos al cargar, resultado de guardar o borrar).
    std::function<void (const juce::String&)> onMessage;

    // La barra queda visible; el navegador y el panel de guardar, ocultos y por encima del resto.
    void addTo (juce::Component& parent)
    {
        parent.addAndMakeVisible (bar);
        parent.addChildComponent (browser);
        parent.addChildComponent (saveDialog);
    }

    // barArea: la barra (en la cabecera). panelArea: lo que tapan el navegador y el panel de guardar (las páginas).
    void setBounds (juce::Rectangle<int> barArea, juce::Rectangle<int> panelArea)
    {
        bar.setBounds (barArea);
        browser.setBounds (panelArea);
        saveDialog.setBounds (panelArea);
    }

    // Nombre, categoría y "*" (modificado). Barato: llamarlo desde el temporizador de la GUI.
    void update()
    {
        const auto* current = manager.getCurrentPreset();
        bar.setPreset (current != nullptr ? current->category : juce::String(), manager.getCurrentName(), manager.isModified());
    }

    // Cambiar de página o empezar a trabajar en las perillas cierra los paneles (estorban).
    void closePanels()
    {
        showBrowser (false);
        showSaveDialog (false);
    }

    [[nodiscard]] bool isOpen() const noexcept { return browser.isVisible() || saveDialog.isVisible(); }

    void showBrowser (bool show)
    {
        if (show == browser.isVisible())
            return;
        if (show)
        {
            showSaveDialog (false);
            browser.refresh();
            browser.toFront (false);
        }
        browser.setVisible (show);
        bar.setBrowserOpen (show);
        if (show)
            browser.focusList();
    }

    void showSaveDialog (bool show)
    {
        if (show == saveDialog.isVisible())
            return;
        if (! show)
        {
            saveDialog.setVisible (false);
            return;
        }

        showBrowser (false);
        const juce::String lastAuthor = settings != nullptr ? settings->getValue (authorPropertyId) : juce::String();
        const auto& categories = manager.getFormat().categories;

        // Si el sonido es un preset tuyo, se propone actualizarlo. Si partiste de uno de fábrica, las notas empiezan
        // diciendo de cuál: así queda escrito qué cambiaste respecto al original.
        saveDialog.toFront (false);
        saveDialog.setVisible (true); // antes de open(): un componente invisible no puede recibir el foco del teclado
        const auto* current = manager.getCurrentPreset();
        if (current != nullptr && ! current->factory)
            saveDialog.open (current->name, current->category, current->author.isNotEmpty() ? current->author : lastAuthor,
                             current->notes);
        else if (current != nullptr && current->categoryIndex() != 0) // de fábrica, pero no Init
            saveDialog.open ({}, current->category, lastAuthor,
                             juce::String::fromUTF8 ("Basado en \xc2\xab") + current->name
                                 + juce::String::fromUTF8 ("\xc2\xbb (f\xc3\xa1" "brica).\n\nQu\xc3\xa9 cambi\xc3\xa9 y por qu\xc3\xa9:\n- "));
        else
            saveDialog.open ({}, categories[categories.size() - 1], lastAuthor,
                             juce::String::fromUTF8 ("Qu\xc3\xa9 es:\n\nC\xc3\xb3mo se construy\xc3\xb3:\n- \n\nT\xc3\xb3" "calo:\n"));
    }

private:
    static constexpr const char* authorPropertyId = "presetAuthor"; // último autor escrito al guardar un preset

    void message (const juce::String& text)
    {
        if (onMessage)
            onMessage (text);
    }

    void load (const Preset& preset)
    {
        const auto problems = manager.load (preset);
        if (problems.isEmpty())
            message ("Preset: " + preset.name + juce::String::fromUTF8 (" \xc2\xb7 ") + preset.category);
        else // un preset de otra versión o editado a mano: se carga lo que se entiende y se avisa del resto
            message (preset.name + ": " + juce::String (problems.size()) + " valor(es) no se entendieron (" + problems[0] + ")");
        update();
    }

    void showMenu (juce::Component& anchor)
    {
        enum Items { init = 1, saveAs, openFolder, rescan };
        juce::PopupMenu menu;
        menu.addSectionHeader ("Presets");
        menu.addItem (init, "Init (empezar desde cero)");
        menu.addItem (saveAs, juce::String::fromUTF8 ("Guardar como\xe2\x80\xa6"));
        menu.addSeparator();
        menu.addItem (openFolder, "Abrir la carpeta de mis presets");
        menu.addItem (rescan, "Volver a leer la carpeta");

        // El menú es asíncrono: si el editor se cerró antes de elegir, la barra (miembro de este objeto) ya no existe.
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor),
                            [this, alive = juce::Component::SafePointer<PresetBar> (&bar)] (int result) {
                                if (alive == nullptr || result == 0)
                                    return;
                                switch (result)
                                {
                                    case init:
                                        manager.loadInit();
                                        message ("Init: todos los controles en su valor por defecto.");
                                        break;
                                    case saveAs:
                                        showSaveDialog (true);
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
                                        if (browser.isVisible())
                                            browser.refresh();
                                        message (juce::String (static_cast<int> (manager.getPresets().size()))
                                                 + " presets en la biblioteca.");
                                        break;
                                    default:
                                        break;
                                }
                                update();
                            });
    }

    PresetManager& manager;
    juce::PropertiesFile* settings;
    PresetBar bar;
    PresetBrowser browser;
    PresetSaveDialog saveDialog;

    JUCE_DECLARE_NON_COPYABLE (PresetPanel)
};

} // namespace deepwater::presets
