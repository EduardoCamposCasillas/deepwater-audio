#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "gui/Theme.h"
#include "presets/PresetManager.h"

namespace undertow::gui
{

// Fase 10: panel para guardar el sonido como preset del usuario: nombre, categoría, autor y notas.
// Las notas son lo más importante para aprender: escribir POR QUÉ hiciste cada ajuste te obliga a entenderlo, y
// dentro de un mes te lo recordará. Se abre encima de las páginas (sin ventana modal: dan problemas en los hosts).
class PresetSaveDialog final : public juce::Component
{
public:
    // Devuelve el error a mostrar, o una cadena vacía si se guardó.
    std::function<juce::String (const juce::String& name, const juce::String& category, const juce::String& author,
                                const juce::String& notes)> onSave;
    std::function<bool (const juce::String& name)> exists; // ¿ya hay un preset del usuario con ese nombre?
    std::function<void()> onClose;

    PresetSaveDialog()
    {
        const auto styleEditor = [] (juce::TextEditor& editor) {
            editor.setFont (juce::FontOptions (14.0f));
            editor.setColour (juce::TextEditor::backgroundColourId, theme::display);
            editor.setColour (juce::TextEditor::outlineColourId, theme::panelOutline);
            editor.setColour (juce::TextEditor::focusedOutlineColourId, theme::accent);
            editor.setColour (juce::TextEditor::textColourId, theme::text);
        };
        for (auto* editor : { &nameEditor, &authorEditor, &notesEditor })
        {
            styleEditor (*editor);
            addAndMakeVisible (editor);
        }
        nameEditor.setInputRestrictions (64);
        nameEditor.setTextToShowWhenEmpty (juce::String::fromUTF8 ("Mi sonido"), theme::dimText);
        nameEditor.onTextChange = [this] { updateConfirmation(); };
        nameEditor.onReturnKey = [this] { save(); };
        nameEditor.onEscapeKey = [this] { close(); };
        authorEditor.setInputRestrictions (64);
        authorEditor.setTextToShowWhenEmpty ("(opcional)", theme::dimText);
        authorEditor.onEscapeKey = [this] { close(); };

        notesEditor.setMultiLine (true, true);
        notesEditor.setReturnKeyStartsNewLine (true);
        notesEditor.setScrollbarsShown (true);
        notesEditor.setTextToShowWhenEmpty (juce::String::fromUTF8 ("Qu\xc3\xa9 es este sonido, c\xc3\xb3mo lo construiste "
                                                                   "y por qu\xc3\xa9 est\xc3\xa1 cada ajuste."),
                                            theme::dimText);

        for (const auto* name : presets::categoryNames)
            if (juce::String (name) != "Init")
                categoryBox.addItem (name, categoryBox.getNumItems() + 1);
        addAndMakeVisible (categoryBox);

        for (auto* label : { &nameLabel, &categoryLabel, &authorLabel, &notesLabel })
        {
            label->setFont (juce::FontOptions (12.0f, juce::Font::bold));
            label->setColour (juce::Label::textColourId, theme::dimText);
            addAndMakeVisible (label);
        }

        message.setFont (juce::FontOptions (13.0f));
        message.setColour (juce::Label::textColourId, theme::warm);
        addAndMakeVisible (message);

        saveButton.onClick = [this] { save(); };
        cancelButton.onClick = [this] { close(); };
        addAndMakeVisible (saveButton);
        addAndMakeVisible (cancelButton);
    }

    // Prepara el panel con los datos del preset actual (si era tuyo, guardar = actualizarlo).
    void open (const juce::String& name, const juce::String& category, const juce::String& author, const juce::String& notes)
    {
        nameEditor.setText (name, false);
        authorEditor.setText (author, false);
        notesEditor.setText (notes, false);
        categoryBox.setText (category == "Init" ? juce::String ("Otros") : category, juce::dontSendNotification);
        if (categoryBox.getSelectedId() == 0)
            categoryBox.setText ("Otros", juce::dontSendNotification);
        message.setText ({}, juce::dontSendNotification);
        updateConfirmation();
        nameEditor.grabKeyboardFocus();
        nameEditor.selectAll();
    }

    void paint (juce::Graphics& g) override
    {
        // El panel ocupa toda la página con un fondo oscurecido: se ve que es un paso aparte y no se toca nada detrás.
        g.fillAll (theme::background.withAlpha (0.75f));
        const auto bounds = panelBounds().toFloat().reduced (0.5f);
        g.setColour (theme::panel);
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (theme::panelOutline);
        g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

        g.setColour (theme::text);
        g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        g.drawText ("Guardar preset", titleArea, juce::Justification::centredLeft);
        g.setColour (theme::dimText);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText ("Se guarda en " + folder.getFullPathName(), folderArea, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto area = panelBounds().reduced (16);
        titleArea = area.removeFromTop (26);
        folderArea = area.removeFromTop (20);
        area.removeFromTop (10);

        auto row = area.removeFromTop (20);
        nameLabel.setBounds (row.removeFromLeft (360));
        row.removeFromLeft (12);
        categoryLabel.setBounds (row.removeFromLeft (150));
        row.removeFromLeft (12);
        authorLabel.setBounds (row);

        row = area.removeFromTop (30);
        nameEditor.setBounds (row.removeFromLeft (360));
        row.removeFromLeft (12);
        categoryBox.setBounds (row.removeFromLeft (150));
        row.removeFromLeft (12);
        authorEditor.setBounds (row);
        area.removeFromTop (12);

        auto buttons = area.removeFromBottom (30);
        cancelButton.setBounds (buttons.removeFromRight (100));
        buttons.removeFromRight (8);
        saveButton.setBounds (buttons.removeFromRight (120));
        buttons.removeFromRight (12);
        message.setBounds (buttons);
        area.removeFromBottom (10);

        notesLabel.setBounds (area.removeFromTop (20));
        notesEditor.setBounds (area);
    }

    void setFolder (const juce::File& f) { folder = f; }

private:
    [[nodiscard]] juce::Rectangle<int> panelBounds() const
    {
        return getLocalBounds().withSizeKeepingCentre (juce::jmin (getWidth(), 700), juce::jmin (getHeight(), 440));
    }

    void updateConfirmation()
    {
        // Si el nombre ya existe, el botón lo dice: guardar reemplazará ese preset.
        const bool replaces = exists && exists (nameEditor.getText().trim());
        saveButton.setButtonText (replaces ? "Reemplazar" : "Guardar");
    }

    void save()
    {
        if (! onSave)
            return;
        const auto error = onSave (nameEditor.getText(), categoryBox.getText(), authorEditor.getText(), notesEditor.getText());
        if (error.isNotEmpty())
            message.setText (error, juce::dontSendNotification);
        else
            close();
    }

    void close()
    {
        if (onClose)
            onClose();
    }

    juce::Label nameLabel { {}, "NOMBRE" };
    juce::Label categoryLabel { {}, juce::String::fromUTF8 ("CATEGOR\xc3\x8d" "A") };
    juce::Label authorLabel { {}, "AUTOR" };
    juce::Label notesLabel { {}, juce::String::fromUTF8 ("NOTAS: C\xc3\x93MO SE CONSTRUY\xc3\x93") };
    juce::TextEditor nameEditor, authorEditor, notesEditor;
    juce::ComboBox categoryBox;
    juce::Label message;
    juce::TextButton saveButton { "Guardar" };
    juce::TextButton cancelButton { "Cancelar" };
    juce::File folder;
    juce::Rectangle<int> titleArea, folderArea;
};

} // namespace undertow::gui
