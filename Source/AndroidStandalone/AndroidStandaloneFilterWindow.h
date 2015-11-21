/*
  ==============================================================================

   This file is part of the JUCE library.
   Copyright (c) 2013 - Raw Material Software Ltd.

   Permission is granted to use this software under the terms of either:
   a) the GPL v2 (or any later version)
   b) the Affero GPL v3

   Details of these licenses can be found at: www.gnu.org/licenses

   JUCE is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
   A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

   ------------------------------------------------------------------------------

   To release a closed-source product which uses JUCE, commercial licenses are
   available: visit www.juce.com for more information.

  ==============================================================================
*/

#ifndef JUCE_STANDALONEFILTERWINDOW_H_INCLUDED
#define JUCE_STANDALONEFILTERWINDOW_H_INCLUDED

//#include "../Editor/CodeWindow.h"
#include "../CabbageUtils.h"
#include "../Plugin/CabbagePluginProcessor.h"
#include "../Plugin/CabbagePluginEditor.h"
#include "../CabbageAudioDeviceSelectorComponent.h"


//  and make it create an instance of the filter subclass that you're building.
///extern CabbagePluginAudioProcessor* JUCE_CALLTYPE createCabbagePluginFilter(String inputfile, bool guiOnOff, int plugType);

extern AudioProcessor* JUCE_CALLTYPE createPluginFilter(String file);
//==============================================================================
/**
    An object that creates and plays a standalone instance of an AudioProcessor.

    The object will create your processor using the same createPluginFilter()
    function that the other plugin wrappers use, and will run it through the
    computer's audio/MIDI devices using AudioDeviceManager and AudioProcessorPlayer.
*/
class StandalonePluginHolder
{
public:
    /** Creates an instance of the default plugin.

        The settings object can be a PropertySet that the class should use to
        store its settings - the object that is passed-in will be owned by this
        class and deleted automatically when no longer needed. (It can also be null)
    */
    StandalonePluginHolder (PropertySet* settingsToUse)
        : settings (settingsToUse)
    {
        createPlugin("");
        setupAudioDevices();
        reloadPluginState();
        startPlaying();
    }

    ~StandalonePluginHolder()
    {
        deletePlugin();
        shutDownAudioDevices();
    }

    //==============================================================================
    void createPlugin(String file)
    {
        AudioProcessor::setTypeOfNextNewPlugin (AudioProcessor::wrapperType_Standalone);

        if(file.isEmpty())
            processor = createPluginFilter("");
        else
            processor = createPluginFilter(file);

        if(processor == nullptr) // Your createPluginFilter() function must return a valid object!
            cUtils::showMessage("Something not right in plugin");


        //processor->createGUI(tempFile.loadFileAsString(), true);

        AudioProcessor::setTypeOfNextNewPlugin (AudioProcessor::wrapperType_Undefined);

        processor->setPlayConfigDetails (JucePlugin_MaxNumInputChannels,
                                         JucePlugin_MaxNumOutputChannels,
                                         22050, 1024);
    }

    void deletePlugin()
    {
        stopPlaying();
        processor = nullptr;
        Logger::writeToLog("deleting plugin");
    }

    static String getFilePatterns (const String& fileSuffix)
    {
        if (fileSuffix.isEmpty())
            return String();

        return (fileSuffix.startsWithChar ('.') ? "*" : "*.") + fileSuffix;
    }

    //==============================================================================
    void startPlaying()
    {
        player.setProcessor (processor);
    }

    void stopPlaying()
    {
        player.setProcessor (nullptr);
    }

    //==============================================================================
    /** Shows an audio properties dialog box modally. */
    void showAudioSettingsDialog()
    {
        DialogWindow::LaunchOptions o;
        o.content.setOwned (new AudioDeviceSelectorComponent (deviceManager,
                            processor->getNumInputChannels(),
                            processor->getNumInputChannels(),
                            processor->getNumOutputChannels(),
                            processor->getNumOutputChannels(),
                            true, false,
                            true, false));
        o.content->setSize (500, 450);

        o.dialogTitle                   = TRANS("Audio Settings");
        o.dialogBackgroundColour        = Colour (0xfff0f0f0);
        o.escapeKeyTriggersCloseButton  = true;
        o.useNativeTitleBar             = true;
        o.resizable                     = false;

        o.launchAsync();
    }

    void saveAudioDeviceState()
    {
        if (settings != nullptr)
        {
            ScopedPointer<XmlElement> xml (deviceManager.createStateXml());
            settings->setValue ("audioSetup", xml);
        }
    }

    void reloadAudioDeviceState()
    {
        ScopedPointer<XmlElement> savedState;

        if (settings != nullptr)
            savedState = settings->getXmlValue ("audioSetup");

        deviceManager.initialise (processor->getNumInputChannels(),
                                  processor->getNumOutputChannels(),
                                  savedState,
                                  true);
    }

    //==============================================================================
    void savePluginState()
    {
        if (settings != nullptr && processor != nullptr)
        {
            MemoryBlock data;
            processor->getStateInformation (data);

            settings->setValue ("filterState", data.toBase64Encoding());
        }
    }

    void reloadPluginState()
    {
        if (settings != nullptr)
        {
            MemoryBlock data;

            if (data.fromBase64Encoding (settings->getValue ("filterState")) && data.getSize() > 0)
                processor->setStateInformation (data.getData(), (int) data.getSize());
        }
    }

    //==============================================================================
    ScopedPointer<PropertySet> settings;
    ScopedPointer<AudioProcessor> processor;
    //ScopedPointer<CabbagePluginAudioProcessor> processor;
    AudioDeviceManager deviceManager;
    AudioProcessorPlayer player;

private:
    void setupAudioDevices()
    {
        Logger::writeToLog("gotcha!!");
        deviceManager.addAudioCallback (&player);
        deviceManager.addMidiInputCallback (String::empty, &player);

        reloadAudioDeviceState();
    }

    void shutDownAudioDevices()
    {
        saveAudioDeviceState();

        deviceManager.removeMidiInputCallback (String::empty, &player);
        deviceManager.removeAudioCallback (&player);
    }


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StandalonePluginHolder)
};

//==============================================================================
/**
    A class that can be used to run a simple standalone application containing your filter.

    Just create one of these objects in your JUCEApplicationBase::initialise() method, and
    let it do its work. It will create your filter object using the same createPluginFilter() function
    that the other plugin wrappers use.
*/
class StandaloneFilterWindow    : public DocumentWindow,
    public ButtonListener
{
public:


    class FileBrowser : public Component, Button::Listener, public ActionListener
    {
    public:
        class ListboxContents  : public ListBoxModel, public ActionBroadcaster
        {
            // The following methods implement the necessary virtual functions from ListBoxModel,
            // telling the listbox how many rows there are, painting them, etc.
        public:
            ListboxContents()
            {
                File homeDir(String(getenv("EXTERNAL_STORAGE"))+String("/Cabbage/"));
                Array<File> cabbageFiles;
                homeDir.findChildFiles(cabbageFiles, File::findFiles, false, "*.csd");

                for (int i = 0; i < cabbageFiles.size(); ++i)
                    contents.add(cabbageFiles[i].getFullPathName());

            }

            int getNumRows()
            {
                return contents.size();
            }

            void paintListBoxItem (int rowNumber, Graphics& g,
                                   int width, int height, bool rowIsSelected)
            {
                if (rowIsSelected)
                    g.fillAll (Colour(70, 70, 70));

                g.setColour (rowIsSelected ? Colours::white : Colours::green);
                g.setFont (cUtils::getComponentFont());

                g.drawFittedText(File(contents[rowNumber]).getFileNameWithoutExtension(),
                                 5, 0, width, height,
                                 Justification::centred, true);
            }

            void listBoxItemClicked(int row, const MouseEvent &)
            {
                //sendActionMessage(String(row));
            }

            void listBoxItemDoubleClicked(int row, const MouseEvent &)
            {
                sendActionMessage(contents[row]);
            }

        private:
            StringArray contents;

        };

        FileBrowser(StandaloneFilterWindow* ownerWindow): owner(ownerWindow), Component(),
            upButton("Up"), downButton("Down")
        {
            fileListBox.setRowHeight(70);
            fileListBox.setModel (&listBoxModel);
            fileListBox.setMultipleSelectionEnabled (false);
            fileListBox.setColour(ListBox::ColourIds::backgroundColourId, Colour(30, 30, 30));
            addAndMakeVisible (fileListBox);
            listBoxModel.addActionListener(this);
            fileListBox.selectRow(0);
            addAndMakeVisible(&upButton);
            addAndMakeVisible(&downButton);
            upButton.addListener(this);
            downButton.addListener(this);
        }

        ~FileBrowser() {}

        void resized()
        {
            fileListBox.setBounds(0, 0, getWidth(), getHeight()-170);
            upButton.setBounds(100, getHeight()-150, 200, 100);
            downButton.setBounds(getWidth()-300, getHeight()-150, 200, 100);
        }

        void buttonClicked (Button* button) override
        {
            if(button->getName()=="Down")
            {
                fileIndex++;// = (fileIndex<fileListBox.getModel()->getNumRows() ? fileIndex++ : fileListBox.getModel()->getNumRows()-1);
                fileListBox.selectRow(fileIndex);
            }
            else
            {
                fileIndex--;// = (fileIndex>0 ? fileIndex-- : 0);
                fileListBox.selectRow(fileIndex);
            }
        }

        void actionListenerCallback(const String& message)
        {
            owner->loadFile(message);
        }

        TextButton upButton, downButton;
        int fileIndex;
        ListBox fileListBox;
        ListboxContents listBoxModel;
        ScopedPointer<StandaloneFilterWindow> owner;
    };


    //==============================================================================
    /** Creates a window with a given title and colour.
        The settings object can be a PropertySet that the class should use to
        store its settings - the object that is passed-in will be owned by this
        class and deleted automatically when no longer needed. (It can also be null)
    */
    StandaloneFilterWindow (const String& title,
                            Colour backgroundColour,
                            PropertySet* settingsToUse)
        : DocumentWindow (title, backgroundColour, DocumentWindow::minimiseButton | DocumentWindow::closeButton),
          optionsButton ("Open"), lookAndFeel(new CabbageLookAndFeel()),
          editorShowing(true)
    {
        setTitleBarButtonsRequired (0, false);
        Component::setLookAndFeel(lookAndFeel);
        Component::addAndMakeVisible (optionsButton);
        optionsButton.setLookAndFeel(lookAndFeel);
        optionsButton.addListener (this);
        optionsButton.setTriggeredOnMouseDown (true);
        this->setTitleBarHeight(50);
        pluginHolder = new StandalonePluginHolder (settingsToUse);

        Rectangle<int> rect(Desktop::getInstance().getDisplays().getMainDisplay().userArea);
        setSize(rect.getWidth(), rect.getHeight());

        fileBrowser = new FileBrowser(this);
        fileBrowser->setBounds(0, 0, getWidth(), getHeight());

        getProperties().set("colour", Colour(58, 110, 182).toString());
        lookAndFeelChanged();

        createEditorComp();


        if (PropertySet* props = pluginHolder->settings)
        {
            const int x = props->getIntValue ("windowX", -100);
            const int y = props->getIntValue ("windowY", -100);

            if (x != -100 && y != -100)
                setBoundsConstrained (juce::Rectangle<int> (x, y, getWidth(), getHeight()));
            else
                centreWithSize (getWidth(), getHeight());
        }
        else
        {
            centreWithSize (getWidth(), getHeight());
        }
    }

    ~StandaloneFilterWindow()
    {
        if (PropertySet* props = pluginHolder->settings)
        {
            props->setValue ("windowX", getX());
            props->setValue ("windowY", getY());
        }

        pluginHolder->stopPlaying();
        deleteEditorComp();
        pluginHolder = nullptr;
    }

    //==============================================================================
    AudioProcessor* getAudioProcessor() const noexcept
    {
        return pluginHolder->processor;
    }
    AudioDeviceManager& getDeviceManager() const noexcept
    {
        return pluginHolder->deviceManager;
    }

    void createEditorComp()
    {
        setContentOwned (getAudioProcessor()->createEditorIfNeeded(), true);
    }

    void deleteEditorComp()
    {
        if (AudioProcessorEditor* ed = dynamic_cast<AudioProcessorEditor*> (getContentComponent()))
        {
            pluginHolder->processor->editorBeingDeleted (ed);
            clearContentComponent();
        }
    }

    /** Deletes and re-creates the plugin, resetting it to its default state. */
    void resetToDefaultState()
    {
        pluginHolder->stopPlaying();
        deleteEditorComp();
        pluginHolder->deletePlugin();

        if (PropertySet* props = pluginHolder->settings)
            props->removeValue ("filterState");

        pluginHolder->createPlugin("");
        createEditorComp();
        pluginHolder->startPlaying();
    }

    //==============================================================================
    void closeButtonPressed() override
    {
        JUCEApplicationBase::quit();
    }

    void buttonClicked (Button*) override
    {
        clearContentComponent();
        optionsButton.setVisible(false);
        setContentNonOwned(fileBrowser, true);
    }

    void loadFile(String filename)
    {
        File file(filename);
        pluginHolder->stopPlaying();
        deleteEditorComp();
        pluginHolder->deletePlugin();

        if(file.existsAsFile())
        {
            //cUtils::showMessage(file.loadFileAsString());
            pluginHolder->createPlugin(file.getFullPathName());

            createEditorComp();
            pluginHolder->startPlaying();
            clearContentComponent();
            setContentOwned (getAudioProcessor()->createEditorIfNeeded(), true);

            StringArray csdArray;
            csdArray.addLines(file.loadFileAsString());
            for(int i=0; i<csdArray.size(); i++)
            {
                if(csdArray[i].contains("form "))
                {
                    CabbageGUIClass cAttr(csdArray[i], -99);
                    this->getProperties().set("colour", cAttr.getStringProp(CabbageIDs::colour));
                    this->lookAndFeelChanged();
                }
            }
        }
    }

    void resized() override
    {
        DocumentWindow::resized();
        optionsButton.setBounds (8, 6, 130, getTitleBarHeight() - 8);
    }


    ScopedPointer<StandalonePluginHolder> pluginHolder;
    ScopedPointer<CabbageLookAndFeel> lookAndFeel;
    ScopedPointer<FileBrowser> fileBrowser;
    bool editorShowing;

private:
    //==============================================================================
    TextButton optionsButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StandaloneFilterWindow)
};

#endif   // JUCE_STANDALONEFILTERWINDOW_H_INCLUDED
