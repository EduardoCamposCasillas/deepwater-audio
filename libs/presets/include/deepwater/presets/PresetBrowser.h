#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "deepwater/presets/Palette.h"
#include "deepwater/presets/PresetManager.h"

namespace deepwater::presets
{

// Navegador de presets (Undertow Fase 10). Se abre encima de las páginas (clic en el nombre del preset).
//
//   ┌ Categorías ┐┌ Buscar…           ┐┌ Nombre del preset            ┐
//   │ Todos (22) ││ Bajo Moog     BASS ││ BASS · Fábrica · Undertow    │
//   │ Bass  (7)  ││ Acid Squelch  BASS ││ Cómo se construyó: …         │
//   │ …          ││ …                  ││                    [Cerrar]  │
//
// Un clic en la lista CARGA el preset (para escuchar rápido, con las flechas del teclado también); doble clic o
// Enter lo carga y cierra el navegador. La búsqueda mira el nombre, la categoría y las notas ("FM", "Env 2"…).
class PresetBrowser final : public juce::Component, private juce::ListBoxModel
{
public:
    std::function<void (const Preset&)> onLoad;
    std::function<void()> onClose;
    std::function<void (const juce::String& message)> onMessage;

    PresetBrowser (PresetManager& m, const Palette& colours)
        : manager (m), palette (colours), numFilters (static_cast<size_t> (m.getFormat().categories.size()) + 2)
    {
        // La columna de la categoría en cada fila mide lo que la más larga (al menos 52 px).
        for (const auto& category : m.getFormat().categories)
            categoryColumnWidth = juce::jmax (categoryColumnWidth, juce::roundToInt (juce::GlyphArrangement::getStringWidth (
                                                                       rowCategoryFont(), upperCase (category))) + 4);
        for (size_t i = 0; i < numFilters; ++i)
        {
            auto& button = *filterButtons.add (std::make_unique<juce::TextButton>());
            button.setClickingTogglesState (true);
            button.setRadioGroupId (2);
            button.onClick = [this, i] {
                filter = static_cast<int> (i);
                updateList();
            };
            addAndMakeVisible (button);
        }
        filterButtons[0]->setToggleState (true, juce::dontSendNotification);

        search.setTextToShowWhenEmpty (juce::String::fromUTF8 ("Buscar (nombre, categor\xc3\xad" "a o notas)\xe2\x80\xa6"),
                                       palette.dimText);
        search.setFont (juce::FontOptions (14.0f));
        search.setColour (juce::TextEditor::backgroundColourId, palette.display);
        search.setColour (juce::TextEditor::outlineColourId, palette.panelOutline);
        search.setColour (juce::TextEditor::focusedOutlineColourId, palette.accent);
        search.setColour (juce::TextEditor::textColourId, palette.text);
        search.onTextChange = [this] { updateList(); };
        search.onReturnKey = [this] { closeAfterLoading(); };
        search.onEscapeKey = [this] { close(); };
        addAndMakeVisible (search);

        list.setModel (this);
        list.setRowHeight (rowHeight);
        list.setColour (juce::ListBox::backgroundColourId, palette.display);
        list.setColour (juce::ListBox::outlineColourId, palette.panelOutline);
        list.setOutlineThickness (1);
        list.setWantsKeyboardFocus (true);
        addAndMakeVisible (list);

        title.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, palette.text);
        details.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        details.setColour (juce::Label::textColourId, palette.accent);
        addAndMakeVisible (title);
        addAndMakeVisible (details);

        notes.setMultiLine (true, true);
        notes.setReadOnly (true);
        notes.setScrollbarsShown (true);
        notes.setCaretVisible (false);
        notes.setFont (juce::FontOptions (13.5f));
        notes.setColour (juce::TextEditor::backgroundColourId, palette.display);
        notes.setColour (juce::TextEditor::outlineColourId, palette.panelOutline);
        notes.setColour (juce::TextEditor::focusedOutlineColourId, palette.panelOutline);
        notes.setColour (juce::TextEditor::textColourId, palette.text);
        addAndMakeVisible (notes);

        deleteButton.setTooltip ("Mueve el archivo del preset a la papelera de reciclaje.");
        deleteButton.onClick = [this] { deleteSelected(); };
        addChildComponent (deleteButton);

        closeButton.onClick = [this] { close(); };
        addAndMakeVisible (closeButton);
    }

    // Vuelve a leer la biblioteca (por si el usuario añadió o borró archivos) y selecciona el preset actual.
    void refresh()
    {
        manager.rescan();
        updateCounts();
        updateList();
        if (const auto* current = manager.getCurrentPreset())
            selectPreset (*current);
    }

    void focusList() { list.grabKeyboardFocus(); }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (palette.panel);
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (palette.panelOutline);
        g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

        g.setColour (palette.dimText);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (juce::String::fromUTF8 ("CATEGOR\xc3\x8d" "AS"), categoryCaption, juce::Justification::centredLeft);
        g.drawText ("PRESETS", listCaption, juce::Justification::centredLeft);
        g.drawText (juce::String::fromUTF8 ("C\xc3\x93MO SE CONSTRUY\xc3\x93"), notesCaption, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (padding);

        auto categories = area.removeFromLeft (categoryWidth);
        categoryCaption = categories.removeFromTop (captionHeight);
        for (auto* button : filterButtons)
        {
            button->setBounds (categories.removeFromTop (30).reduced (0, 2));
            if (button == filterButtons[0] || button == filterButtons[filterButtons.size() - 2])
                categories.removeFromTop (8); // separa "Todos" y "Mis presets" de las categorías
        }
        area.removeFromLeft (padding);

        auto middle = area.removeFromLeft (listWidth);
        listCaption = middle.removeFromTop (captionHeight);
        search.setBounds (middle.removeFromTop (30));
        middle.removeFromTop (6);
        list.setBounds (middle);
        area.removeFromLeft (padding);

        auto info = area;
        auto buttons = info.removeFromBottom (30);
        closeButton.setBounds (buttons.removeFromRight (90));
        buttons.removeFromRight (8);
        deleteButton.setBounds (buttons.removeFromRight (150));
        info.removeFromBottom (8);
        title.setBounds (info.removeFromTop (captionHeight + 8).withTrimmedTop (2));
        details.setBounds (info.removeFromTop (20));
        info.removeFromTop (4);
        notesCaption = info.removeFromTop (captionHeight);
        notes.setBounds (info);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            close();
            return true;
        }
        if (key == juce::KeyPress::returnKey)
        {
            closeAfterLoading();
            return true;
        }
        return false;
    }

private:
    static constexpr int padding = 12;
    static constexpr int categoryWidth = 130;
    static constexpr int listWidth = 290;
    static constexpr int captionHeight = 22;
    static constexpr int rowHeight = 26;

    [[nodiscard]] static juce::Font rowCategoryFont() { return juce::FontOptions (10.5f, juce::Font::bold); }

    [[nodiscard]] int userFilter() const noexcept { return static_cast<int> (numFilters) - 1; }

    [[nodiscard]] bool passesFilter (const Preset& preset) const
    {
        if (filter == userFilter() && preset.factory)
            return false;
        if (filter > 0 && filter < userFilter() && preset.categoryIndex() != filter - 1)
            return false;

        const auto text = search.getText().trim();
        return text.isEmpty() || preset.name.containsIgnoreCase (text) || preset.category.containsIgnoreCase (text)
               || preset.notes.containsIgnoreCase (text);
    }

    void updateCounts()
    {
        const auto& all = manager.getPresets();
        for (size_t i = 0; i < numFilters; ++i)
        {
            int count = 0;
            for (const auto& preset : all)
            {
                if (i == 0)
                    ++count;
                else if (static_cast<int> (i) == userFilter())
                    count += preset.factory ? 0 : 1;
                else
                    count += preset.categoryIndex() == static_cast<int> (i) - 1 ? 1 : 0;
            }
            const juce::String name = i == 0                              ? juce::String ("Todos")
                                      : static_cast<int> (i) == userFilter() ? juce::String ("Mis presets")
                                                                             : manager.getFormat().categories[static_cast<int> (i) - 1];
            filterButtons[static_cast<int> (i)]->setButtonText (name + "  (" + juce::String (count) + ")");
        }
    }

    void updateList()
    {
        visible.clear();
        const auto& all = manager.getPresets();
        for (size_t i = 0; i < all.size(); ++i)
            if (passesFilter (all[i]))
                visible.push_back (i);

        list.updateContent();
        list.repaint();

        // Mantener la selección si el preset sigue visible; si no, mostrar el primero (sin cargarlo).
        const auto* shown = shownPreset();
        if (shown == nullptr || ! passesFilter (*shown))
            showInfo (visible.empty() ? nullptr : &all[visible.front()], visible.empty() ? -1 : 0);
        else
            selectPreset (*shown);
    }

    void selectPreset (const Preset& preset)
    {
        const auto& all = manager.getPresets();
        for (size_t row = 0; row < visible.size(); ++row)
            if (all[visible[row]].sameAs (preset))
            {
                showInfo (&all[visible[row]], static_cast<int> (row));
                return;
            }
        showInfo (&preset, -1);
    }

    // Muestra el preset en el panel de la derecha y lo marca en la lista, sin cargarlo.
    void showInfo (const Preset* preset, int row)
    {
        shownName = preset != nullptr ? preset->name : juce::String();
        shownFactory = preset != nullptr && preset->factory;
        confirmingDelete = false;

        const juce::ScopedValueSetter<bool> quiet (selectingProgrammatically, true);
        if (row >= 0)
        {
            list.selectRow (row);
            list.scrollToEnsureRowIsOnscreen (row);
        }
        else
            list.deselectAllRows();

        if (preset == nullptr)
        {
            title.setText ({}, juce::dontSendNotification);
            details.setText ({}, juce::dontSendNotification);
            notes.setText (juce::String::fromUTF8 ("Ning\xc3\xban preset coincide con la b\xc3\xbasqueda."), false);
            deleteButton.setVisible (false);
            return;
        }

        title.setText (preset->name, juce::dontSendNotification);
        juce::String meta = upperCase (preset->category) + juce::String::fromUTF8 ("  \xc2\xb7  ")
                            + (preset->factory ? juce::String::fromUTF8 ("F\xc3\xa1" "brica") : juce::String ("Tuyo"));
        if (preset->author.isNotEmpty())
            meta << juce::String::fromUTF8 ("  \xc2\xb7  ") << preset->author;
        details.setText (meta, juce::dontSendNotification);
        notes.setText (preset->notes.isNotEmpty() ? preset->notes : juce::String ("(Sin notas.)"), false);
        notes.moveCaretToTop (false);

        deleteButton.setButtonText ("Borrar preset");
        deleteButton.setVisible (! preset->factory);
    }

    [[nodiscard]] const Preset* shownPreset()
    {
        for (const auto& preset : manager.getPresets())
            if (preset.factory == shownFactory && preset.name == shownName)
                return &preset;
        return nullptr;
    }

    void loadRow (int row)
    {
        if (row < 0 || row >= static_cast<int> (visible.size()))
            return;
        const auto& preset = manager.getPresets()[visible[static_cast<size_t> (row)]];
        showInfo (&preset, row);
        if (onLoad)
            onLoad (preset);
        list.repaint(); // el preset actual se dibuja resaltado
    }

    void closeAfterLoading()
    {
        const int row = list.getSelectedRow();
        if (row >= 0)
            loadRow (row);
        close();
    }

    void close()
    {
        if (onClose)
            onClose();
    }

    void deleteSelected()
    {
        const auto* preset = shownPreset();
        if (preset == nullptr || preset->factory)
            return;

        // Dos clics: el primero pide confirmación (sin ventanas modales, que dan problemas dentro de un host).
        if (! confirmingDelete)
        {
            confirmingDelete = true;
            deleteButton.setButtonText (juce::String::fromUTF8 ("\xc2\xbf" "Seguro? Pulsa otra vez"));
            return;
        }

        const juce::String name = preset->name;
        const auto result = manager.remove (*preset);
        if (onMessage)
            onMessage (result.wasOk() ? "Preset \"" + name + juce::String::fromUTF8 ("\" movido a la papelera de reciclaje.")
                                      : result.getErrorMessage());
        updateCounts();
        updateList();
    }

    // --- ListBoxModel ---
    int getNumRows() override { return static_cast<int> (visible.size()); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= static_cast<int> (visible.size()))
            return;
        const auto& preset = manager.getPresets()[visible[static_cast<size_t> (row)]];
        const bool current = preset.name == manager.getCurrentName() && manager.getCurrentPreset() == &preset;

        if (selected)
        {
            g.setColour (palette.accent.withAlpha (0.18f));
            g.fillRect (0, 0, width, height);
        }

        auto area = juce::Rectangle<int> (0, 0, width, height).reduced (10, 0);
        g.setFont (rowCategoryFont());
        g.setColour (palette.dimText);
        g.drawText (upperCase (preset.category), area.removeFromRight (categoryColumnWidth), juce::Justification::centredRight);

        if (! preset.factory)
        {
            // Punto de color = preset tuyo.
            g.setColour (palette.secondary);
            g.fillEllipse (area.removeFromLeft (8).toFloat().withSizeKeepingCentre (6.0f, 6.0f));
            area.removeFromLeft (6);
        }

        g.setFont (juce::FontOptions (14.0f, current ? juce::Font::bold : juce::Font::plain));
        g.setColour (current ? palette.accent : palette.text);
        g.drawFittedText (preset.name, area, juce::Justification::centredLeft, 1, 0.8f);
    }

    void selectedRowsChanged (int lastRowSelected) override
    {
        // Clic o flechas del teclado = cargar (escuchar rápido). Al seleccionar por código no se carga nada.
        if (! selectingProgrammatically && lastRowSelected >= 0)
            loadRow (lastRowSelected);
    }

    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
    {
        loadRow (row);
        close();
    }

    void returnKeyPressed (int) override { closeAfterLoading(); }

    PresetManager& manager;
    const Palette palette;
    // Filtro 0 = Todos; 1..N = cada categoría; último = Mis presets (los del usuario, de cualquier categoría).
    const size_t numFilters;
    juce::OwnedArray<juce::TextButton> filterButtons;
    int categoryColumnWidth = 52;
    juce::TextEditor search;
    juce::ListBox list;
    juce::Label title, details;
    juce::TextEditor notes;
    juce::TextButton deleteButton;
    juce::TextButton closeButton { "Cerrar" };

    std::vector<size_t> visible; // índices (en manager.getPresets()) de las filas visibles
    int filter = 0;
    juce::String shownName;
    bool shownFactory = true;
    bool selectingProgrammatically = false;
    bool confirmingDelete = false;
    juce::Rectangle<int> categoryCaption, listCaption, notesCaption;
};

} // namespace deepwater::presets
