#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class ExpressionMapperAudioProcessor;

/**
    The editable entry table.

    Columns: | Name | Note |

    The Note cell is a combo box (note names shown as "C3 (60)" etc.); the
    Name cell is an editable label. Add / Remove / Clear / Import / Export
    buttons live below the table. There is deliberately no remapping: each
    entry just gives a display name to a note number.
*/
class MappingTableComponent
    : public juce::Component
    , public juce::TableListBoxModel
    , public juce::Button::Listener
{
public:
    enum ColumnIds
    {
        nameCol = 1,
        noteCol
    };

    explicit MappingTableComponent (ExpressionMapperAudioProcessor& processorToUse);
    ~MappingTableComponent() override;

    void resized() override;

    void buttonClicked (juce::Button*) override;

    // ---- TableListBoxModel ----
    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int rowNumber, int width, int height, bool rowIsSelected) override;
    void paintCell (juce::Graphics&, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
    juce::Component* refreshComponentForCell (int rowNumber, int columnId, bool isRowSelected, juce::Component* existingToUpdate) override;

    /** Rebuild the table rows from the processor's current mapping. */
    void refresh();

private:
    ExpressionMapperAudioProcessor& processor;

    juce::TableListBox table { {}, this };
    juce::TextButton addButton    { "Add" };
    juce::TextButton removeButton { "Remove" };
    juce::TextButton clearButton  { "Clear" };
    juce::TextButton importButton { "Import..." };
    juce::TextButton exportButton { "Export..." };

    void setupColumns();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MappingTableComponent)
};
