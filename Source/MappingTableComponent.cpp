#include "MappingTableComponent.h"
#include "PluginProcessor.h"
#include "NoteNameUtils.h"

namespace
{
    //==========================================================================
    // Cell components. Each keeps a row index; when the table refreshes it
    // calls setRow() to point the component at its current row and value
    // (reusing the component instead of recreating it, to preserve focus).

    // Editable text cell for the entry name.
    class NameCellComponent : public juce::Label
    {
    public:
        explicit NameCellComponent (ExpressionMapperAudioProcessor& p)
            : processor (p)
        {
            setEditable (false, true);
            setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
            setJustificationType (juce::Justification::centredLeft);
        }

        void setRow (int newRow)
        {
            row = newRow;
            juce::Label::setText (processor.getEntry (row).name, juce::dontSendNotification);
        }

        void textWasEdited() override
        {
            auto entry = processor.getEntry (row);
            entry.name = getText();
            processor.setEntry (row, entry);
        }

    private:
        ExpressionMapperAudioProcessor& processor;
        int row = 0;
    };

    //==========================================================================
    // A keyboard-focus-free dropdown list shown under a NoteComboCell while the
    // user types. Lives in its own temporary top-level window (like PopupMenu)
    // so the table cells can't clip it and the text field keeps keyboard focus.
    class NoteSuggestionPopup
        : public juce::Component
        , private juce::ListBoxModel
        , private juce::Timer
    {
    public:
        /** Called with the picked note number (only for real matches). */
        std::function<void (int noteNumber)> onPick;

        NoteSuggestionPopup()
            : list ("Suggestions", this)
        {
            list.setRowHeight (20);
            list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff262626));
            addAndMakeVisible (list);
            setOpaque (true);
        }

        ~NoteSuggestionPopup() override { dismissNow(); }

        /** Shows `matchedNotes` directly under `anchor` (or above it if there is
            no room). An empty list shows a disabled "(no match)" row. */
        void show (juce::Component* anchor, const std::vector<int>& matchedNotes)
        {
            notes = matchedNotes;
            anyMatches = ! notes.empty();
            list.updateContent();
            list.deselectAllRows();
            list.repaint();

            const int rowH = list.getRowHeight();
            const int numRows = anyMatches ? (int) notes.size() : 1;
            const int height = juce::jmin (numRows * rowH + 2, 12 * rowH);

            auto area = anchor->getScreenBounds();
            const int width = juce::jmax (area.getWidth(), 140);

            const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (area);
            const auto screenArea = display != nullptr ? display->userBounds.toNearestInt()
                                                       : juce::Rectangle<int> {};

            if (! screenArea.isEmpty())
                area.setX (juce::jlimit (screenArea.getX(), juce::jmax (screenArea.getX(), screenArea.getRight() - width),
                                         area.getX()));

            int y = area.getBottom() + 1;
            if (! screenArea.isEmpty() && y + height > screenArea.getBottom())
                y = area.getY() - height - 1;

            if (! isOnDesktop())
            {
                addToDesktop (juce::ComponentPeer::windowIsTemporary);
                setAlwaysOnTop (true);
            }

            setBounds (area.getX(), y, width, height);
            setVisible (true);
        }

        /** Dismisses, but not while the mouse is heading into the list (a focus
            loss from the text field precedes every click on a row). */
        void requestDismiss()
        {
            if (isOnDesktop() && isVisible() && isMouseOver (true))
                startTimerHz (8);
            else
                dismissNow();
        }

        void dismissNow()
        {
            stopTimer();
            if (isOnDesktop())
                removeFromDesktop();
            setVisible (false);
        }

    private:
        int getNumRows() override { return anyMatches ? (int) notes.size() : 1; }

        void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool rowIsSelected) override
        {
            if (rowIsSelected)
                g.fillAll (juce::Colour (0xff4a6984));
            else
                g.fillAll (juce::Colour (0xff262626));

            g.setFont (juce::FontOptions (14.0f));

            if (! anyMatches)
            {
                g.setColour (juce::Colours::grey);
                g.drawText ("(no match)", 6, 0, width - 8, height, juce::Justification::centredLeft);
                return;
            }

            if (! juce::isPositiveAndBelow (row, (int) notes.size()))
                return;

            g.setColour (rowIsSelected ? juce::Colours::white : juce::Colours::grey.withAlpha (0.9f));
            g.drawText (NoteNameUtils::midiToNameWithNumber (notes[(size_t) row]),
                        6, 0, width - 8, height, juce::Justification::centredLeft);
        }

        void listBoxItemClicked (int row, const juce::MouseEvent&) override
        {
            if (anyMatches && juce::isPositiveAndBelow (row, (int) notes.size()) && onPick)
                onPick (notes[(size_t) row]);
        }

        void timerCallback() override
        {
            if (isMouseOver (true))
                startTimerHz (8);   // keep waiting until the mouse leaves
            else
                dismissNow();
        }

        void resized() override { list.setBounds (getLocalBounds()); }

        juce::ListBox list;
        std::vector<int> notes;
        bool anyMatches = false;
    };

    //==========================================================================
    // Note cell: a plain text editor with an autocomplete dropdown.
    //
    // JUCE's editable ComboBox hides its text field inside a private label and
    // reports stale text while typing (Label::getText() skips the active
    // editor by default), so we own the editor directly: text changes, Return,
    // Escape and focus events are all synchronous and reliable.
    //
    // Typing pops up the live-filtered suggestion list; nothing is committed
    // until editing finishes (Enter / focus loss with a parseable text like
    // "C1", "38", "C1 (36)") or a suggestion is clicked. Unparseable text
    // always snaps back to the current value, so invalid input can never take
    // effect.
    class NoteTextCell : public juce::TextEditor
    {
    public:
        explicit NoteTextCell (ExpressionMapperAudioProcessor& p)
            : processor (p)
        {
            setMultiLine (false, false);
            setSelectAllWhenFocused (true);
            setIndents (4, 0);
            setJustification (juce::Justification::centredLeft);
            setFont (juce::FontOptions (14.0f));
            setColour (backgroundColourId, juce::Colours::transparentBlack);
            setColour (outlineColourId, juce::Colours::transparentBlack);
            setColour (focusedOutlineColourId, juce::Colours::steelblue.withAlpha (0.6f));

            onTextChange = [this] { showSuggestions(); };
            onReturnKey  = [this] { finishEditing (parseOrCurrent(), true); };
            onEscapeKey  = [this] { finishEditing (currentEntryNote(), true); };
            onFocusLost  = [this]
            {
                suggestions.requestDismiss();
                finishEditing (parseOrCurrent(), false);
            };
        }

        /** Clicking into the cell (without typing) shows the full list. */
        void focusGained (juce::Component::FocusChangeType cause) override
        {
            juce::TextEditor::focusGained (cause);   // select-all + caret setup
            showSuggestions();
        }

        ~NoteTextCell() override
        {
            suggestions.onPick = nullptr;
            suggestions.dismissNow();
        }

        void setRow (int newRow)
        {
            row = newRow;
            suggestions.dismissNow();   // e.g. the row was scrolled away
            setText (NoteNameUtils::midiToNameWithNumber (currentEntryNote()),
                     juce::dontSendNotification);
        }

    private:
        int currentEntryNote() const { return processor.getEntry (row).note; }

        int parseOrCurrent() const
        {
            const int parsed = NoteNameUtils::parseNoteText (getText());
            return parsed >= 0 ? parsed : currentEntryNote();
        }

        /** Commits `note` (only when it changed), then always displays its
            canonical label, so valid shortcuts like "c1" normalise to "C1 (36)"
            and invalid text reverts to the current value. */
        void finishEditing (int note, bool dismissNow)
        {
            note = juce::jlimit (0, 127, note);

            auto entry = processor.getEntry (row);
            if (entry.note != note)
            {
                entry.note = note;
                processor.setEntry (row, entry);
            }

            setText (NoteNameUtils::midiToNameWithNumber (note), juce::dontSendNotification);

            if (dismissNow)
                suggestions.dismissNow();
            else
                suggestions.requestDismiss();
        }

        void showSuggestions()
        {
            const auto text = getText().trim();

            std::vector<int> matches;
            if (text.isEmpty())
            {
                for (int n = 0; n < 128; ++n)
                    matches.push_back (n);
            }
            else
            {
                for (int n = 0; n < 128; ++n)
                    if (NoteNameUtils::midiToNameWithNumber (n).containsIgnoreCase (text))
                        matches.push_back (n);
            }

            suggestions.onPick = [this] (int note) { finishEditing (note, true); };
            suggestions.show (this, matches);
        }

        ExpressionMapperAudioProcessor& processor;
        int row = 0;
        NoteSuggestionPopup suggestions;
    };
}

//==============================================================================
MappingTableComponent::MappingTableComponent (ExpressionMapperAudioProcessor& processorToUse)
    : processor (processorToUse)
{
    addAndMakeVisible (table);
    table.setColour (juce::ListBox::outlineColourId, juce::Colours::grey);
    table.setOutlineThickness (1);
    table.getHorizontalScrollBar().setVisible (false);   // no horizontal scroll
    // Column widths are set entirely manually in resized() to guarantee they
    // never exceed the visible viewport width (stretch-to-fit miscalculates
    // at Windows high DPI scaling like 150%).

    setupColumns();

    addAndMakeVisible (addButton);
    addAndMakeVisible (removeButton);
    addAndMakeVisible (clearButton);
    addAndMakeVisible (importButton);
    addAndMakeVisible (exportButton);

    addButton.addListener (this);
    removeButton.addListener (this);
    clearButton.addListener (this);
    importButton.addListener (this);
    exportButton.addListener (this);

    setSize (600, 340);
}

MappingTableComponent::~MappingTableComponent()
{
    addButton.removeListener (this);
    removeButton.removeListener (this);
    clearButton.removeListener (this);
    importButton.removeListener (this);
    exportButton.removeListener (this);
}

void MappingTableComponent::setupColumns()
{
    auto& header = table.getHeader();

    // Proportional initial widths; adjusted to fill the table area exactly on
    // every resize in resized().
    header.addColumn ("Name", nameCol, 140);
    header.addColumn ("Note", noteCol, 200);
}

void MappingTableComponent::resized()
{
    auto area = getLocalBounds();

    auto buttons = area.removeFromBottom (30).reduced (0, 2);
    const int buttonGap = 6;
    const int buttonWidth = 80;

    // Right side: Add / Remove / Clear
    removeButton.setBounds (buttons.removeFromRight (buttonWidth));
    buttons.removeFromRight (buttonGap);
    clearButton.setBounds (buttons.removeFromRight (buttonWidth));
    buttons.removeFromRight (buttonGap);
    addButton.setBounds (buttons.removeFromRight (buttonWidth));
    buttons.removeFromRight (buttonGap);

    // Left side: Import / Export
    importButton.setBounds (buttons.removeFromLeft (buttonWidth));
    buttons.removeFromLeft (buttonGap);
    exportButton.setBounds (buttons.removeFromLeft (buttonWidth));

    table.setBounds (area);

    // Manually distribute column widths. We deliberately use slightly less
    // than the full width so the last column is never clipped — critical for
    // high-DPI (e.g. 150%) where JUCE's internal width calculations drift.
    auto& header = table.getHeader();
    const int avail = juce::jmax (100, area.getWidth() - 4);  // 4px safety margin

    // Percentages: 40 + 55 = 95  (remaining 5% stays empty)
    header.setColumnWidth (nameCol, avail * 40 / 100);
    header.setColumnWidth (noteCol, avail * 55 / 100);
}

void MappingTableComponent::buttonClicked (juce::Button* button)
{
    if (button == &addButton)
    {
        processor.addEntry();
    }
    else if (button == &removeButton)
    {
        const int selected = table.getSelectedRow();
        if (selected >= 0)
            processor.removeEntry (selected);
    }
    else if (button == &clearButton)
    {
        processor.clearEntries();
    }
    else if (button == &importButton)
    {
        juce::FileChooser fc ("Import Mapping", {}, "*.exprmap;*.bwdrm;*.csv");

        if (fc.browseForFileToOpen())
        {
            auto file = fc.getResult();
            if (processor.importFromFile (file))
            {
                // Success
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::AlertWindow::WarningIcon,
                    "Import Failed",
                    "Could not import the selected file.\n"
                    "Expected CSV lines like: Name,Note (e.g. \"Kick,36\" or \"Kick,D1\").");
            }
        }
    }
    else if (button == &exportButton)
    {
        juce::FileChooser fc ("Export Mapping", {}, "*.exprmap");

        if (fc.browseForFileToSave (true))
        {
            auto file = fc.getResult().withFileExtension ("exprmap");
            if (! processor.exportToFile (file))
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::AlertWindow::WarningIcon,
                    "Export Failed",
                    "Could not write to the selected file.");
            }
        }
    }

    refresh();
}

//==============================================================================
int MappingTableComponent::getNumRows()
{
    return processor.getNumEntries();
}

void MappingTableComponent::paintRowBackground (juce::Graphics& g, int /*rowNumber*/, int width, int height, bool rowIsSelected)
{
    if (rowIsSelected)
        g.fillAll (juce::Colours::steelblue.withAlpha (0.3f));
    else
        g.fillAll (juce::Colours::darkgrey.withAlpha (0.2f));

    g.setColour (juce::Colours::grey.withAlpha (0.4f));
    g.drawRect (0, 0, width, height);
}

void MappingTableComponent::paintCell (juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool /*rowIsSelected*/)
{
    // Custom components cover the editable cells, so this is just a fallback.
    g.setColour (juce::Colours::white);

    const auto entry = processor.getEntry (rowNumber);

    juce::String text;
    switch (columnId)
    {
        case nameCol: text = entry.name; break;
        case noteCol: text = NoteNameUtils::midiToNameWithNumber (entry.note); break;
        default: break;
    }

    g.drawText (text, 6, 0, width - 6, height, juce::Justification::centredLeft);
}

juce::Component* MappingTableComponent::refreshComponentForCell (int rowNumber, int columnId, bool /*isRowSelected*/, juce::Component* existingToUpdate)
{
    switch (columnId)
    {
        case nameCol:
        {
            auto* c = dynamic_cast<NameCellComponent*> (existingToUpdate);
            if (c == nullptr)
                c = new NameCellComponent (processor);
            c->setRow (rowNumber);
            return c;
        }
        case noteCol:
        {
            auto* c = dynamic_cast<NoteTextCell*> (existingToUpdate);
            if (c == nullptr)
                c = new NoteTextCell (processor);
            c->setRow (rowNumber);
            return c;
        }
        default:
            break;
    }

    return nullptr;
}

void MappingTableComponent::refresh()
{
    table.updateContent();
    table.repaint();
}
