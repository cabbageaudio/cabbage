/*
 Copyright (C) 2009 Rory Walsh
 
 Cabbage is free software; you can redistribute it
 and/or modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.
 
 Cabbage is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU Lesser General Public License for more details.
 
 You should have received a copy of the GNU Lesser General Public
 License along with Csound; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 02111-1307 USA
 */

#include "CabbagePluginProcessor.h"
#include "CabbagePluginEditor.h"
#include <iostream>


#define MAX_BUFFER_SIZE 1024
//Csound functions like to return 0 when everything is ok..
#define LOGGER 0

//these two lines may need to be copied to top part of csound.h
//#define int32 int
//#define uint32 unsigned int

CabbageLookAndFeel* lookAndFeel;
CabbageLookAndFeelBasic* lookAndFeelBasic;

//juce_ImplementSingleton (IdentArray);


//==============================================================================
// There are two different CabbagePluginAudioProcessor constructors. One for the
// standalone application and the other for the plugin library
//==============================================================================
//#ifdef Cabbage_Build_Standalone
#if defined(Cabbage_Build_Standalone) || defined(CABBAGE_HOST)
//===========================================================
// STANDALONE - CONSTRUCTOR
//===========================================================
CabbagePluginAudioProcessor::CabbagePluginAudioProcessor(String inputfile, bool guiOnOff, int _pluginType)
:backgroundThread ("Audio Recorder Thread"),
activeWriter (nullptr),
csoundStatus(false),
csdFile(File(inputfile)),
showMIDI(false),
csCompileResult(1),
changeMessageType(""),
guiON(false),
currentLine(-99),
noSteps(0),
noPatterns(0),
timeCounter(0),
beat(0),
bpm(120),
patMatrixActive(0),
masterCounter(0),
xyAutosCreated(false),
updateTable(false),
yieldCallbackBool(false),
yieldCounter(10),
isNativeThreadRunning(false),
scoreEvents(),
nativePluginEditor(false),
averageSampleIndex(0),
pluginType(_pluginType),
automationAmp(0),
isAutomator(false),
createLog(false),
automationParamID(-1),
debugMessage(""),
guiRefreshRate(50),
mouseY(0),
mouseX(0),
isWinXP(false),
ksmpsOffset(0),
breakCount(0),
stopProcessing(false),
firstTime(true),
isMuted(false),
isBypassed(false),
vuCounter(0)
{
    //suspendProcessing(true);
    codeEditor = nullptr;
    //backgroundThread.startThread();
    
    setPlayConfigDetails(2, 2, 44100, 512);
    
    
    //set up file logger if needed..
    StringArray tmpArray;
    CabbageGUIClass cAttr;
    
    tmpArray.addLines(File(inputfile).loadFileAsString());
    for(int i=0; i<tmpArray.size() || tmpArray[i].contains("</Cabbage>"); i++)
        if(tmpArray[i].contains("logger("))
        {
            CabbageGUIClass cAttr(tmpArray[i], -99);
            createLog = cAttr.getNumProp(CabbageIDs::logger);
            if(createLog)
            {
                String logFileName = File(inputfile).getParentDirectory().getFullPathName()+String("/")+File(inputfile).getFileNameWithoutExtension()+String("_Log.txt");
                logFile = File(logFileName);
                fileLogger = new FileLogger(logFile, String("Cabbage Log.."));
                Logger::setCurrentLogger(fileLogger);
            }
        }
    
   
#ifndef Cabbage_No_Csound
    //don't start of run Csound in edit mode
    setOpcodeDirEnv();
    csound = nullptr;
#if !defined(AndroidBuild)
    csound = new Csound();
#else
    AlertWindow::showMessageBoxAsync (AlertWindow::WarningIcon,
                                      "Uh-oh",
                                      "Pugin Constructor",
                                      "ok");
    csound = new AndroidCsound();
    csound->setOpenSlCallbacks(); // for android audio to work
#endif
    
    csound->SetHostImplementedMIDIIO(true);

    csound->Reset();
    //Logger::writeToLog(csound->GetEnv("OPCODEDIR64"));
#ifdef CSOUND5
    csound->PreCompile();
#endif
    csound->SetHostData(this);
    //csound->SetMessageCallback(CabbagePluginAudioProcessor::messageCallback);
	csound->CreateMessageBuffer(0);
    csound->SetExternalMidiInOpenCallback(OpenMidiInputDevice);
    csound->SetExternalMidiReadCallback(ReadMidiData);
    csound->SetExternalMidiOutOpenCallback(OpenMidiOutputDevice);
    csound->SetExternalMidiWriteCallback(WriteMidiData);
	csound->SetIsGraphable(0);

    
    csoundChanList = NULL;
    numCsoundChannels = 0;
    csndIndex = 32;
    
    //set up PVS struct
    dataout = new PVSDATEXT;
    
    if(inputfile.isNotEmpty())
    {
        File(inputfile).setAsCurrentWorkingDirectory();
        
        

        csoundParams = new CSOUND_PARAMS();
		#ifndef CABBAGE_HOST
        csoundParams->nchnls_override = this->getNumOutputChannels();
		#endif
        csoundParams->displays = 0;
        csound->SetParams(csoundParams);
        csound->SetOption((char*)"-n");
        csound->SetOption((char*)"-d");

        StringArray lines, includeFiles;
        lines.addLines(File(inputfile).loadFileAsString());
        
        for(int i=0; i<lines.size(); i++)
            if(lines[i].contains("include("))
            {
                CabbageGUIClass cAttr(lines[i], -99);
                for(int y=0; y<cAttr.getStringArrayProp(CabbageIDs::include).size(); y++)
                {
                    String infile = cAttr.getStringArrayPropValue(CabbageIDs::include, y);
                    if(!File::isAbsolutePath(infile))
                    {
                        String file = returnFullPathForFile(infile, File(inputfile).getParentDirectory().getFullPathName());
                        includeFiles.add(file);
                    }
                    else
                        includeFiles.add(infile);
                }
                break;
            }
        
        includeFiles.removeDuplicates(0);
        
        
        csCompileResult = csound->Compile(const_cast<char*>(inputfile.toUTF8().getAddress()));
        
        File(inputfile).getParentDirectory().setAsCurrentWorkingDirectory();

        Logger::writeToLog(inputfile);
        if(csCompileResult==OK)
        {
            firstTime=false;
            //send root directory path to Csound.
            setPlayConfigDetails(getNumberCsoundOutChannels(),
                                 getNumberCsoundOutChannels(),
                                 getCsoundSamplingRate(),
                                 getCsoundKsmpsSize());
            
            guiRefreshRate = getCsoundKsmpsSize()*2;
            
            //simple hack to allow tables to be set up correctly.
            csound->PerformKsmps();
            for(int i=0; i<includeFiles.size(); i++)
            {
                //	csound->CompileOrc(File(includeFiles[i]).loadFileAsString().toUTF8().getAddress());
                //	csound->InputMessage("i\"PROCESSOR\" 0 3600 1");
            }
            
            csound->SetScoreOffsetSeconds(0);
            csound->RewindScore();
            
			char path[4096] = {0};
			
	#ifdef WIN32
			
			csound->GetStringChannel("CSD_PATH", path);
			if(String(path).isNotEmpty())
				File(path).getParentDirectory().setAsCurrentWorkingDirectory();
			else 
				csound->SetChannel("CSD_PATH", File(csdFile).getParentDirectory().getFullPathName().replace("\\", "\\\\").toUTF8().getAddress());
	#else
			csound->GetStringChannel("CSD_PATH", path);
			if(String(path).isNotEmpty())
				File(path).getParentDirectory().setAsCurrentWorkingDirectory();
			else 
				csound->SetChannel("CSD_PATH", File(csdFile).getParentDirectory().getFullPathName().toUTF8().getAddress());
	#endif
				Logger::writeToLog("Csound compiled your file");
            
            //csound->SetYieldCallback(CabbagePluginAudioProcessor::yieldCallback);
            if(csound->GetSpout()==nullptr);
            CSspout = csound->GetSpout();
            CSspin  = csound->GetSpin();
            // numCsoundChannels = csoundListChannels(csound->GetCsound(), &csoundChanList);
            csndIndex = csound->GetKsmps();
            csdKsmps = csound->GetKsmps();
            cs_scale = csound->Get0dBFS();
            csoundStatus = true;
            debugMessageArray.add(CABBAGE_VERSION);
            debugMessageArray.add(String("\n"));
            this->setLatencySamples(csound->GetKsmps());
            updateHostDisplay();
            
        }
        else
        {
            Logger::writeToLog("Csound couldn't compile your file");
            csoundStatus=false;
            //debugMessage = "Csound did not compile correctly. Check for snytax errors by compiling with WinXound";
        }
    }
    else
        Logger::writeToLog("Welcome to Cabbage, problems with input file...");
    
    if(SystemStats::getOperatingSystemType()!=SystemStats::WinXP)
    {
        isWinXP = true;
        String path = File(inputfile).getParentDirectory().getFullPathName();
        String fullFileName;
#if defined(LINUX) || defined(MACOSX)
        fullFileName = path+"/CabbageTemp.wav";
#else
        fullFileName = path+"\\CabbageTemp.wav";
#endif
        tempAudioFile = fullFileName;
        tempAudioFile.replaceWithData(0 ,0);
    }
#endif
    
}
#else

//===========================================================
// PLUGIN - CONSTRUCTOR
//===========================================================
CabbagePluginAudioProcessor::CabbagePluginAudioProcessor():
backgroundThread ("Audio Recorder Thread"),
activeWriter (nullptr),
csoundStatus(false),
showMIDI(false),
csCompileResult(1),
changeMessageType(""),
guiON(false),
currentLine(-99),
noSteps(0),
noPatterns(0),
timeCounter(0),
beat(0),
bpm(120),
patMatrixActive(0),
masterCounter(0),
xyAutosCreated(false),
createLog(false),
updateTable(false),
yieldCallbackBool(false),
yieldCounter(10),
nativePluginEditor(false),
averageSampleIndex(0),
stopProcessing(false),
firstTime(false),
isMuted(false),
isBypassed(false),
vuCounter(0)
{
    //Cabbage plugins always try to load a csd file with the same name as the plugin library.
    //Therefore we need to find the name of the library and append a '.csd' to it.
#ifdef MACOSX
    String osxCSD = File::getSpecialLocation(File::currentApplicationFile).getFullPathName()+String("/Contents/")+File::getSpecialLocation(File::currentApplicationFile).getFileName();
    File thisFile(osxCSD);
    Logger::writeToLog("MACOSX defined OK");
    csdFile = thisFile.withFileExtension(String(".csd")).getFullPathName();
#elif defined(AndroidBuild)
    File inFile(File::getSpecialLocation(File::currentApplicationFile));
    ScopedPointer<InputStream> fileStream;
    fileStream = File(inFile.getFullPathName()).createInputStream();
    ZipFile zipFile (fileStream, false);
    ScopedPointer<InputStream> fileContents;
    fileContents = zipFile.createStreamForEntry(*zipFile.getEntry("assets/AndroidSimpleSynth.csd"));
    File thisFile("/sdcard/Cabbage.csd");
    thisFile.replaceWithText(fileContents->readEntireStreamAsString());
    csdFile = thisFile;
#else
    File thisFile(File::getSpecialLocation(File::currentExecutableFile));
    csdFile = thisFile.withFileExtension(String(".csd")).getFullPathName();
#endif
    
    //Logger::writeToLog(File::getSpecialLocation(File::currentExecutableFile).getFullPathName());
    
    if(csdFile.exists())
    {
        Logger::writeToLog("File exists:"+String(csdFile.getFullPathName()));
        
    }
    else
    {
        Logger::writeToLog("File doesn't exist"+String(csdFile.getFullPathName()));
    }
    
    //File(csdFile.getFullPathName()).setAsCurrentWorkingDirectory();
    csdFile.setAsCurrentWorkingDirectory();
    
    
    
    StringArray tmpArray;
    CabbageGUIClass cAttr;
    
    tmpArray.addLines(csdFile.loadFileAsString());
    for(int i=0; i<tmpArray.size() || tmpArray[i].contains("</Cabbage>"); i++)
        if(tmpArray[i].contains("logger("))
        {
            CabbageGUIClass cAttr(tmpArray[i], -99);
            createLog = cAttr.getNumProp(CabbageIDs::logger);
            if(createLog)
            {
                String logFileName = csdFile.getParentDirectory().getFullPathName()+String("/")+csdFile.getFileNameWithoutExtension()+String("_Log.txt");
                logFile = File(logFileName);
                fileLogger = new FileLogger(logFile, String("Cabbage Log.."));
                Logger::setCurrentLogger(fileLogger);
            }
        }
    
    setOpcodeDirEnv();
    
#ifndef Cabbage_No_Csound
#if !defined(AndroidBuild)
    csound = new Csound();
#else
    csound = new AndroidCsound();
    //csound->setOpenSlCallbacks(); // for android audio to work
#endif
    
    csound->SetHostImplementedMIDIIO(true);
    //csound->Reset();
    //csound->PreCompile();
    csound->SetHostData(this);
    midiOutputBuffer.clear();
    //for host midi to get sent to Csound, don't need this for standalone
    //but might use it in the future for midi mapping to controls
    //csound->SetMessageCallback(CabbagePluginAudioProcessor::messageCallback);
    csound->CreateMessageBuffer(0);
	csound->SetExternalMidiInOpenCallback(OpenMidiInputDevice);
    csound->SetExternalMidiReadCallback(ReadMidiData);
    csound->SetExternalMidiOutOpenCallback(OpenMidiOutputDevice);
    csound->SetExternalMidiWriteCallback(WriteMidiData);
    
    csoundChanList = NULL;
    numCsoundChannels = 0;
    csndIndex = 32;
    startTimer(20);

    csoundParams = new CSOUND_PARAMS();
	#ifndef CABBAGE_HOST
    csoundParams->nchnls_override = this->getNumOutputChannels();
	#endif
    csoundParams->displays = 0;
    csound->SetParams(csoundParams);
    csound->SetOption((char*)"-n");
    csound->SetOption((char*)"-d");

    
    csCompileResult = csound->Compile(const_cast<char*>(csdFile.getFullPathName().toUTF8().getAddress()));
    //csoundSetBreakpointCallback(csound->GetCsound(), breakpointCallback, (void*)this);
    csdFile.getParentDirectory().setAsCurrentWorkingDirectory();
    if(csCompileResult==OK)
    {
        guiRefreshRate = getCsoundKsmpsSize()*2;
        Logger::writeToLog("compiled Ok");
        keyboardState.allNotesOff(0);
        keyboardState.reset();
        //simple hack to allow tables to be set up correctly.
        csound->PerformKsmps();
        csound->SetScoreOffsetSeconds(0);
        csound->RewindScore();
        //set up PVS struct
        dataout = new PVSDATEXT;
        csdKsmps = csound->GetKsmps();
        
        if(csound->GetSpout()==nullptr);
        CSspout = csound->GetSpout();
        CSspin  = csound->GetSpin();
        cs_scale = csound->Get0dBFS();
        //numCsoundChannels = csoundListChannels(csound->GetCsound(), &csoundChanList);
        csndIndex = csound->GetKsmps();
        this->setLatencySamples(csound->GetKsmps());
        updateHostDisplay();
        //soundFilerVector = new MYFLT[csdKsmps];
        csoundStatus = true;
        debugMessageArray.add(VERSION);
        debugMessageArray.add(String("\n"));
		char path[8192] = {0};
			
	#ifdef WIN32
			
			csound->GetStringChannel("CSD_PATH", path);
			if(String(path).isNotEmpty())
				File(path).getParentDirectory().setAsCurrentWorkingDirectory();
			else 
				csound->SetChannel("CSD_PATH", File(csdFile).getParentDirectory().getFullPathName().replace("\\", "\\\\").toUTF8().getAddress());
	#else
			csound->GetStringChannel("CSD_PATH", path);
			if(String(path).isNotEmpty())
				File(path).getParentDirectory().setAsCurrentWorkingDirectory();
			else 
				csound->SetChannel("CSD_PATH", File(csdFile).getParentDirectory().getFullPathName().toUTF8().getAddress());
	#endif
        
        if (getPlayHead() != 0 && getPlayHead()->getCurrentPosition (hostInfo))
        {
            csound->SetChannel(CabbageIDs::hostbpm.toUTF8(), hostInfo.bpm);
            csound->SetChannel(CabbageIDs::timeinseconds.toUTF8(), hostInfo.timeInSeconds);
            csound->SetChannel(CabbageIDs::isplaying.toUTF8(), hostInfo.isPlaying);
            csound->SetChannel(CabbageIDs::isrecording.toUTF8(), hostInfo.isRecording);
            csound->SetChannel(CabbageIDs::hostppqpos.toUTF8(), hostInfo.ppqPosition);
            csound->SetChannel(CabbageIDs::timeinsamples.toUTF8(), hostInfo.timeInSamples);
            csound->SetChannel(CabbageIDs::timeSigDenom.toUTF8(), hostInfo.timeSigDenominator);
            csound->SetChannel(CabbageIDs::timeSigNum.toUTF8(), hostInfo.timeSigNumerator);
        }
        Logger::writeToLog("everything still good...");
    }
    else
    {
        Logger::writeToLog("Csound couldn't compile your file");
        csoundStatus=false;
    }
#endif
    
//    
//    if(SystemStats::getOperatingSystemType()!=SystemStats::WinXP)
//    {
//        isWinXP = true;
//        String path = csdFile.getParentDirectory().getFullPathName();
//        String fullFileName;
//#ifdef LINUX
//        fullFileName = path+"/CabbageTemp.wav";
//#else
//        fullFileName = path+"\\CabbageTemp.wav";
//#endif
//        tempAudioFile = fullFileName;
//        tempAudioFile.replaceWithData(0 ,0);
//    }
    
    
    createGUI(csdFile.loadFileAsString(), true);
    Logger::writeToLog("GUI has been created");
    
}
#endif

//===========================================================
// PLUGIN - DESTRUCTOR
//===========================================================
CabbagePluginAudioProcessor::~CabbagePluginAudioProcessor()
{
	
    deleteAndZero(lookAndFeel);
    deleteAndZero(lookAndFeelBasic);
    Logger::setCurrentLogger (nullptr);
    stopProcessing = true;
    removeAllChangeListeners();
    
#ifndef Cabbage_No_Csound
    
    xyAutomation.clear();
    
    if(csound)
    {
		this->getCallbackLock().enter();
        csound->DeleteChannelList(csoundChanList);
        Logger::writeToLog("about to cleanup Csound");
        //csoundDebugContinue(csound);
        //csound->Cleanup();
        //csound->SetHostImplementedMIDIIO(false);
        
        //csound->Reset();
        //csoundDebuggerClean(csound->GetCsound());
		
        csound = nullptr;
        Logger::writeToLog("Csound cleaned up");
    }
    
    
#endif
}

int CabbagePluginAudioProcessor::performEntireScore()
{
#ifndef Cabbage_No_Csound
    if(!isNativeThreadRunning)
    {
        //csoundPerfThread->Play();
        isNativeThreadRunning = true;
    }
#endif
    return 1;
}

//this callback will be employed when users run with Csound audio IO rather than Cabbage
void CabbagePluginAudioProcessor::YieldCallback(void* data)
{
    CabbagePluginAudioProcessor *cabbage = (CabbagePluginAudioProcessor *)data;
    cabbage->sendOutgoingMessagesToCsound();
    cabbage->updateCabbageControls();
}

//============================================================================
//RECOMPILE CSOUND. THIS IS CALLED FROM THE PLUGIN HOST WHEN UDPATES ARE MADE ON THE FLY
//============================================================================
int CabbagePluginAudioProcessor::reCompileCsound(File file)
{
#ifndef Cabbage_No_Csound
    
    stopProcessing = true;
    getCallbackLock().enter();
	numChannelsChanged();
    midiOutputBuffer.clear();
    //csound->DeleteChannelList(csoundChanList);

	
	//csound->DestroyMessageBuffer();
    //csound->Reset();
    ksmpsOffset = 0;
    breakCount = 0;

    csoundParams = new CSOUND_PARAMS();
	#ifndef CABBAGE_HOST
    csoundParams->nchnls_override =2;
	#endif
    csoundParams->displays = 0;
    csound->SetParams(csoundParams);
    csound->SetOption((char*)"-n");
    csound->SetOption((char*)"-d");

    
    csound->SetHostImplementedMIDIIO(true);
    xyAutosCreated = false;
    //csound->SetMessageCallback(CabbagePluginAudioProcessor::messageCallback);
    numCsoundChannels = 0;
    //csound->SetParams(csoundParams);
    //csound->SetMessageCallback(CabbagePluginAudioProcessor::messageCallback);
	csound->CreateMessageBuffer(0);
    csound->SetExternalMidiInOpenCallback(OpenMidiInputDevice);
    csound->SetExternalMidiReadCallback(ReadMidiData);
    csound->SetExternalMidiOutOpenCallback(OpenMidiOutputDevice);
    csound->SetExternalMidiWriteCallback(WriteMidiData);
    
    CSspout = nullptr;
    CSspin = nullptr;
    
    csCompileResult = csound->CompileCsd(const_cast<char*>(file.getFullPathName().toUTF8().getAddress()));
    file.getParentDirectory().setAsCurrentWorkingDirectory();
    
#ifdef BUILD_DEBUGGER
    for(int i=0; i<breakpointInstruments.size(); i++)
    {
        getCallbackLock().enter();
        if(i==0)
            csoundDebuggerInit(csound->GetCsound());
        csoundSetBreakpointCallback(csound->GetCsound(), breakpointCallback, (void*)this);
        csoundSetInstrumentBreakpoint(csound->GetCsound(), breakpointInstruments[i], 0);
        getCallbackLock().exit();
    }
#endif
    
    StringArray lines, includeFiles;
    lines.addLines(file.loadFileAsString());
    
    for(int i=0; i<lines.size(); i++)
        if(lines[i].contains("include("))
        {
            CabbageGUIClass cAttr(lines[i], -99);
            for(int y=0; y<cAttr.getStringArrayProp(CabbageIDs::include).size(); y++)
            {
                String infile = cAttr.getStringArrayPropValue(CabbageIDs::include, y);
                if(!File::isAbsolutePath(infile))
                {
                    String ifile = returnFullPathForFile(infile, file.getParentDirectory().getFullPathName());
                    includeFiles.add(ifile);
                }
                else
                    includeFiles.add(infile);
            }
            break;
        }
    
    includeFiles.removeDuplicates(0);
    
    
    if(csCompileResult==OK)
    {
        firstTime=false;
        keyboardState.allNotesOff(0);
        keyboardState.reset();
        //simple hack to allow tables to be set up correctly.
        csound->PerformKsmps();
        csound->SetScoreOffsetSeconds(0);
        csound->RewindScore();
        for(int i=0; i<includeFiles.size(); i++)
        {
            //	csound->CompileOrc(File(includeFiles[i]).loadFileAsString().toUTF8().getAddress());
            //	csound->InputMessage("i\"PROCESSOR\" 0 3600 1");
        }
        csndIndex = 0;
        CSspout = csound->GetSpout();
        CSspin  = csound->GetSpin();
        csound->PerformKsmps();
        csound->SetScoreOffsetSeconds(0);
        csound->RewindScore();
        Logger::writeToLog("Csound compiled your file");
        //numCsoundChannels = csoundListChannels(csound->GetCsound(), &csoundChanList);
        cs_scale = csound->Get0dBFS();
        csoundStatus = true;
        debugMessageArray.add(CABBAGE_VERSION);
        debugMessageArray.add(String("\n"));
        //removeAllChangeListeners();
        getCallbackLock().exit();
        
        //init all channels with their init val
        for(int i=0; i<guiCtrls.size(); i++)
        {
            csound->SetChannel( guiCtrls.getReference(i).getStringProp(CabbageIDs::channel).toUTF8(),
                               guiCtrls.getReference(i).getNumProp(CabbageIDs::value));
        }
        
#ifdef WIN32
        csound->SetChannel("CSD_PATH", file.getParentDirectory().getFullPathName().replace("\\", "\\\\").toUTF8().getAddress());
#else
        csound->SetChannel("CSD_PATH", file.getParentDirectory().getFullPathName().toUTF8().getAddress());
#endif
        
        stopProcessing = false;
        
        return csCompileResult;
    }
    else
    {
        Logger::writeToLog("Csound couldn't compile your file");
        csoundStatus=false;
    }
    getCallbackLock().exit();
	
    return csCompileResult;
#endif
}
//===========================================================
// PARSE CSD FILE AND FILL GUI/GUI-LAYOUT VECTORs.
// NO JUCE WIDGETS GET CREATED IN THIS CLASS. ALL
// GUI OBJECTS ARE CREATED ON THE FLY IN THE CABBAGE PLUGIN
// EDITOR FROM INFORMATION HELD IN THE GUICONTROLS VECTOR
//===========================================================
//maybe this should only be done at the end of a k-rate cycle..
void CabbagePluginAudioProcessor::createGUI(String source, bool refresh)
{
    //clear arrays if refresh is set
    if(refresh==true)
    {
        guiLayoutCtrls.clear();
        guiCtrls.clear();
        CabbagePluginAudioProcessorEditor* editor = dynamic_cast<CabbagePluginAudioProcessorEditor*>(this->getActiveEditor());
        if(editor)
        {
            editor->comps.clear();
            editor->layoutComps.clear();
            editor->subPatches.clear();
            editor->popupMenus.clear();
        }
    }
    
    int indexOfLastGUICtrl = guiCtrls.size();
    int indexOfLastLayoutCtrl = guiLayoutCtrls.size();
    
    String warningMessage;
    
    int test=100;
    int checkGUI = isGuiEnabled();
    //setGuiEnabled((false));
    int guiID=0;
    StringArray csdText;
    
    int lines=1;
    String csdLine("");
    csdText.addLines(source.replace("\t", " "));
    bool multiComment = false;
    bool multiLine = false;
    
    //csound->Message("\n===Cabbage Warnings===\n");
    int lineWhichCabbageSectionStarts = 0;
	int lineWhichCabbageSectionEnds = csdText.size();
    for(int i=0; i<csdText.size(); i++)
    {
		if(csdText[i].contains("<Cabbage>"))
			lineWhichCabbageSectionStarts=i;
		else if(csdText[i].contains("</Cabbage>"))
			lineWhichCabbageSectionEnds=i;
	}	
	
	csdText.removeRange(0, lineWhichCabbageSectionStarts);
	csdText.removeRange(lineWhichCabbageSectionEnds+1, 99999);
	
	//cUtils::debug(csdText.size());
	
    for(int i=0; i<csdText.size(); i++)
    {
        String temp;
        //roll any lines with a \ into the next line and remove from csdText array...
        if(csdText[i].contains(" \\"))
        {
            temp = csdText[i+1];
            csdText.remove(i+1);
            csdText.set(i, csdText[i].replace(" \\", " ")+temp);
            //cUtils::debug(csdText[i]);
        }
    }
    
    for(int i=0; i<csdText.size(); i++)
    {
        
        int csdLineNumber=0;
#if (defined(Cabbage_Build_Standalone) || defined(CABBAGE_HOST)) && !defined(AndroidBuild)
        if(!refresh)
        {
            StringArray fullText;
            if(codeEditor)
                fullText.addLines(codeEditor->getAllText());
            else
                fullText.addLines(csdFile.loadFileAsString());
            for(int u=0; u<fullText.size(); u++)
                if(csdText[i].equalsIgnoreCase(fullText[u]))
                {
                    csdLineNumber = u;
                    //Logger::writeToLog("LineNumber:"+String(csdLineNumber)+"\n"+csdText[i]);
                    u=fullText.size();
                }
        }
#endif
        
        if(csdText[i].indexOfWholeWordIgnoreCase(String("</Cabbage>"))==-1)
        {
            if(csdText[i].trim().isNotEmpty())
            {
                csdLine = csdText[i];
                if(refresh)
                    csdLineNumber = i;
                //tidy up string
                csdLine = csdLine.trimStart();
                //csdLine = csdLine.removeCharacters(" \\");
                //csdLine = csdLine.removeCharacters(",\\");
                
                StringArray tokes;
                tokes.addTokens(csdLine.trimEnd(), ", ", "\"");
                
                if(tokes[0].containsIgnoreCase(String("/*")))
                {
                    multiComment = true;
                }
                if(tokes[0].containsIgnoreCase(String("*\\")))
                {
                    multiComment = false;
                }
                
                if(tokes[0].containsIgnoreCase(String(";")))
                {
                    //allows for single line comments
                }
                else if(tokes[0].containsIgnoreCase(String("}")))
                {
                    plantFlag = ""; //reset plantFlag when a closing bracket is found
                    presetFlag = "";
                }
                
                if(tokes[0].containsIgnoreCase("#define"))
                {
                    tokes.removeEmptyStrings();
                    if(tokes.size()>2)
                    {
                        macroText.set("$"+tokes[1], " "+csdLine.substring(csdLine.indexOf(tokes[1])+tokes[1].length())+" ");
                    }
                }
                
                for(int cnt=0;cnt<macroText.size();cnt++)
                {
                    if(csdLine.contains(macroText.getName(cnt).toString()))
                        csdLine = csdLine.replace(macroText.getName(cnt), macroText.getWithDefault(macroText.getName(cnt), "").toString()+" ");
                }
                
                if(!multiComment)
                    //populate the guiLayoutCtrls vector with non-interactive widgets
                    //the host widgets aren't GUI based but they can be added to this
                    //vector too, as can the editor button.
                    if(tokes[0].equalsIgnoreCase(String("form"))
                       ||tokes[0].equalsIgnoreCase(String("image"))
                       ||tokes[0].equalsIgnoreCase(String("socketsend"))
					   ||tokes[0].equalsIgnoreCase(String("socketreceive"))
                       ||tokes[0].equalsIgnoreCase(String("keyboard"))
                       ||tokes[0].equalsIgnoreCase(String("gentable"))
                       ||tokes[0].equalsIgnoreCase(String("csoundoutput"))
                       ||tokes[0].equalsIgnoreCase(String("textbox"))
                       ||tokes[0].equalsIgnoreCase(String("line"))
                       ||tokes[0].equalsIgnoreCase(String("recordbutton"))
                       ||tokes[0].equalsIgnoreCase(String("label"))
                       ||tokes[0].equalsIgnoreCase(String("hostbpm"))
                       ||tokes[0].equalsIgnoreCase(String("hosttime"))
                       ||tokes[0].equalsIgnoreCase(String("hostplaying"))
                       ||tokes[0].equalsIgnoreCase(String("hostppqpos"))
                       ||tokes[0].equalsIgnoreCase(String("vumeter"))
                       ||tokes[0].equalsIgnoreCase(String("patmatrix"))
                       ||tokes[0].equalsIgnoreCase(String("source"))
                       ||tokes[0].equalsIgnoreCase(String("multitab"))
                       ||tokes[0].equalsIgnoreCase(String("infobutton"))
                       ||tokes[0].equalsIgnoreCase(String("filebutton"))
                       ||tokes[0].equalsIgnoreCase(String("soundfiler"))
                       ||tokes[0].equalsIgnoreCase(String("texteditor"))
                       ||tokes[0].equalsIgnoreCase(String("popupmenu"))
                       ||tokes[0].equalsIgnoreCase(String("snapshot"))
                       ||tokes[0].equalsIgnoreCase(String("table"))
                       ||tokes[0].equalsIgnoreCase(String("pvsview"))
                       ||tokes[0].equalsIgnoreCase(String("hostrecording"))
                       ||tokes[0].equalsIgnoreCase(String("directorylist"))
                       ||tokes[0].equalsIgnoreCase(String("transport"))
                       ||tokes[0].equalsIgnoreCase(String("groupbox")))
                    {
                        CabbageGUIClass cAttr(csdLine.trimEnd(), guiID);
                        warningMessage = "";
                        warningMessage << "Line Number:" << csdLineNumber+1 << "\n" << cAttr.getWarningMessages();
                        if(cAttr.getWarningMessages().isNotEmpty())
                            csound->Message(warningMessage.toUTF8().getAddress());
                        
                        //showMessage(csdLine);
                        cAttr.setNumProp(CabbageIDs::lineNumber, csdLineNumber);
                        if(cAttr.getStringProp("native").length()>0)
                        {
                            //create generic plugin editor and break..
                            //setupNativePluginEditor();
                            nativePluginEditor = true;
                            return;
                        }
                        
                        
                        if(cAttr.getNumProp(CabbageIDs::guirefresh)>1)
                            guiRefreshRate = cAttr.getNumProp(CabbageIDs::guirefresh);
                        //showMessage(cAttr.getStringProp("type"));
                        
                        
                        if(tokes[0].equalsIgnoreCase(String("socketsend")))
                        {
							//set up server...
							server = new CabbageInterprocessConnectionServer (*this);
							socketChannelIdentifiers.clear();
							socketChannelValues.clear();
							openInterprocess(false, true, cAttr.getStringProp(CabbageIDs::socketaddress),
															cAttr.getNumProp(CabbageIDs::socketport));
															
                            for(int i=0; i<cAttr.getStringArrayProp(CabbageIDs::channel).size(); i++)
                            {
                                socketChannelIdentifiers.add(cAttr.getStringArrayPropValue("channel", i));
                                Logger::writeToLog(cAttr.getStringArrayPropValue("channel", i));
                                socketChannelValues.set(cAttr.getStringArrayPropValue("channel", i), 0.f);
                            }
							//cUtils::debug(socketChannelValues.size());
                        }
						else if(tokes[0].equalsIgnoreCase(String("socketreceive")))
                        {
							server = new CabbageInterprocessConnectionServer (*this);
							socketChannelIdentifiers.clear();
							socketChannelValues.clear();
							
							openInterprocess(false, false, cAttr.getStringProp(CabbageIDs::socketaddress),
															cAttr.getNumProp(CabbageIDs::socketport));
															
                            for(int i=0; i<cAttr.getStringArrayProp(CabbageIDs::channel).size(); i++)
                            {
                                socketChannelIdentifiers.add(cAttr.getStringArrayPropValue("channel", i));
                                Logger::writeToLog(cAttr.getStringArrayPropValue("channel", i));
                                socketChannelValues.set(cAttr.getStringArrayPropValue("channel", i), 0.f);
                            }
							//cUtils::debug(socketChannelValues.size());
						} 
						
						
                        csdLine = "";
                      
                        
                        //set up stuff for tables
                        if(tokes[0].equalsIgnoreCase(String("table")))
                        {
                            /*                            if(cAttr.getStringArrayProp(CabbageIDs::channel).size()==0)
                             for(int i=0; i<cAttr.getIntArrayProp("tablenumber").size(); i++)
                             cAttr.addDummyChannel("dummy"+String(i));*/
                            
                            for(int i=0; i<cAttr.getStringArrayProp(CabbageIDs::channel).size(); i++)
                                cAttr.addTableChannelValues();
                            
                        }
                        
                        //set up plant flag if needed for other widgets
                        if(cAttr.getStringProp(String("plant")).isNotEmpty())
                        {
                            plantFlag = cAttr.getStringProp(String("plant"));
                        }
                        else if(cAttr.getStringProp(String("reltoplant")).equalsIgnoreCase(String("")))
                            cAttr.setStringProp(String("reltoplant"), plantFlag);
                         						
						
                        //if an array of objects is being set up...
                        if((cAttr.getStringArrayProp(CabbageIDs::identchannelarray).size()>0) &&
						(cAttr.getStringArrayProp(CabbageIDs::channelarray).size()>0))
                        {
                            //showMessage(String(cAttr.getStringArrayProp(CabbageIDs::channelarray).joinIntoString(" ")));
                            //showMessage(String(cAttr.getStringArrayProp(CabbageIDs::channelarray).size()));
                            for(int i=0; i<cAttr.getStringArrayProp(CabbageIDs::channelarray).size(); i++)
                            {
                                CabbageGUIClass copy = cAttr;
                                copy.setStringProp(CabbageIDs::channel, cAttr.getStringArrayProp(CabbageIDs::channelarray).getReference(i));
								copy.setStringProp(CabbageIDs::identchannel, cAttr.getStringArrayProp(CabbageIDs::identchannelarray).getReference(i));
                                //Logger::writeToLog(cAttr.getStringArrayProp(CabbageIDs::channelarray).getReference(i));
                                guiLayoutCtrls.add(copy);
                                guiID++;
                            }
                        }
                        else
                        {
                            guiLayoutCtrls.add(cAttr);
                            guiID++;
                        }
                        
                        if(cAttr.getStringProp("type").containsIgnoreCase("form"))
                            if(cAttr.getStringProp("text").length()>2)
                                setPluginName(cAttr.getStringProp("text"));
                            else if(cAttr.getStringProp("caption").length()>2)
                                setPluginName(cAttr.getStringProp("caption"));
                            else setPluginName("Untitled Cabbage Patch!");
                        
                        //StringArray log = logGUIAttributes(cAttr, String("Non-Interactive"));
                        //debugMessageArray.addArray(logGUIAttributes(cAttr, String("Non-Interactive")));
                        sendChangeMessage();
                        //if instrument uses any of the host widgets, or an xypad, turn
                        //on the timer
                        if(tokes[0].equalsIgnoreCase(String("hostbpm"))
                           ||tokes[0].equalsIgnoreCase(String("hosttime"))
                           ||tokes[0].equalsIgnoreCase(String("hostplaying"))
                           ||tokes[0].equalsIgnoreCase(String("hostppqpos"))
                           ||tokes[0].equalsIgnoreCase(String("hostrecording")))
                            startTimer(20);
                    }
                //populate the guiCtrls vector with interactive widgets
                    else if(tokes[0].equalsIgnoreCase(String("hslider"))
                            ||tokes[0].equalsIgnoreCase(String("hslider2"))
                            ||tokes[0].equalsIgnoreCase(String("hslider3"))
                            ||tokes[0].equalsIgnoreCase(String("rslider"))
                            ||tokes[0].equalsIgnoreCase(String("vslider"))
                            ||tokes[0].equalsIgnoreCase(String("vslider2"))
                            ||tokes[0].equalsIgnoreCase(String("vslider3"))
                            ||tokes[0].equalsIgnoreCase(String("combobox"))
                            ||tokes[0].equalsIgnoreCase(String("checkbox"))
                            ||tokes[0].equalsIgnoreCase(String("numberbox"))
                            ||tokes[0].equalsIgnoreCase(String("xypad"))
                            ||tokes[0].equalsIgnoreCase(String("button")))
                    {
                        
                        CabbageGUIClass cAttr(csdLine.trimEnd(), guiID);
                        warningMessage = "";
                        warningMessage << "Line Number:" << csdLineNumber+1 << "\n" << cAttr.getWarningMessages();
                        if(cAttr.getWarningMessages().isNotEmpty())
                            csound->Message(warningMessage.toUTF8().getAddress());
                        
                        cAttr.setNumProp(CabbageIDs::lineNumber, csdLineNumber);
                        //Logger::writeToLog(csdLine.trimEnd());
                        csdLine = "";
                        //Logger::writeToLog(tokes[0]);
                        //attach widget to plant if need be
                        if(cAttr.getStringProp(String("reltoplant")).equalsIgnoreCase(String("")))
                        {
                            //showMessage(cAttr.getStringProp(String("relToPlant")));
                            cAttr.setStringProp(String("reltoplant"), plantFlag);
                            //showMessage(String("presetFlag:")+presetFlag);
                            //showMessage(cAttr.getStringProp("name"));
                            if(cAttr.getStringProp("preset").length()<1)
                                cAttr.setStringProp(String("preset"), presetFlag.trim());
                            //showMessage(cAttr.getStringProp("preset"));
                        }
                        
                        //xypad contain two control paramters, one for x axis and another for y. As such we add two
                        //to our contorl vector so that plugin hosts display two sliders. We name one of the xypad pads
                        // 'dummy' so that our editor doesn't display it. Our editor only needs to show one xypad.
                        if(tokes[0].equalsIgnoreCase(String("xypad")))
                        {
                            cAttr.setStringProp(CabbageIDs::xychannel, String("X"));
                            cAttr.setNumProp(CabbageIDs::range,  cAttr.getNumProp(CabbageIDs::rangex));
                            cAttr.setNumProp(CabbageIDs::min,  cAttr.getNumProp(CabbageIDs::minx));
                            cAttr.setNumProp(CabbageIDs::max,  cAttr.getNumProp(CabbageIDs::maxx));
                            cAttr.setNumProp(CabbageIDs::value, cAttr.getNumProp(CabbageIDs::valuex));
                            cAttr.setStringProp(String(CabbageIDs::channel), cAttr.getStringProp(CabbageIDs::xchannel));
                            guiCtrls.add(cAttr);
                            
                            cAttr.setStringProp(CabbageIDs::xychannel, String("Y"));
                            cAttr.setNumProp(CabbageIDs::range,  cAttr.getNumProp(CabbageIDs::rangey));
                            cAttr.setNumProp(CabbageIDs::min,  cAttr.getNumProp(CabbageIDs::miny));
                            cAttr.setNumProp(CabbageIDs::max,  cAttr.getNumProp(CabbageIDs::maxy));
                            cAttr.setNumProp(CabbageIDs::value, cAttr.getNumProp(CabbageIDs::valuey));
                            cAttr.setStringProp(String(CabbageIDs::channel), cAttr.getStringProp(CabbageIDs::ychannel));
                            //append 'dummy' to name so the editor know not to display the
                            //second xypad
                            cAttr.setStringProp("name", cAttr.getStringProp(CabbageIDs::name)+String("dummy"));
                            guiCtrls.add(cAttr);
                            guiID++;
                            startTimer(10);
                        }
                        else
                        {
                            //if an array of objects is to be set up enter.....
                            if(cAttr.getStringArrayProp(CabbageIDs::channelarray).size()>0)
                            {
                                //showMessage(String(cAttr.getStringArrayProp(CabbageIDs::channelarray).joinIntoString(" ")));
                                //showMessage(String(cAttr.getStringArrayProp(CabbageIDs::channelarray).size()));
                                for(int i=0; i<cAttr.getStringArrayProp(CabbageIDs::channelarray).size(); i++)
                                {
                                    CabbageGUIClass copy = cAttr;
                                    copy.setStringProp(CabbageIDs::channel, cAttr.getStringArrayProp(CabbageIDs::channelarray).getReference(i));
                                    copy.setStringProp(CabbageIDs::identchannel, cAttr.getStringArrayProp(CabbageIDs::identchannelarray).getReference(i));
                                    //Logger::writeToLog(cAttr.getStringArrayProp(CabbageIDs::channelarray).getReference(i));
                                    guiCtrls.add(copy);
                                    guiID++;
                                }
                            }
                            else
                            {
                                guiCtrls.add(cAttr);
                                guiID++;
                                if(tokes[0].equalsIgnoreCase(String("hslider2")) || tokes[0].equalsIgnoreCase(String("vslider2")))
                                {
                                    cAttr.setStringProp(String(CabbageIDs::channel), cAttr.getStringArrayPropValue(CabbageIDs::channel, 1));
                                    
                                    cAttr.setStringProp("name", cAttr.getStringProp(CabbageIDs::name)+String("dummy"));
                                    guiCtrls.add(cAttr);
                                    guiID++;
                                }
                            }
                            
                        }
                        
                        //debugMessageArray.addArray(logGUIAttributes(cAttr, String("Interactive")));
                        sendChangeMessage();
                    }
                
            }
        }
        else break;
    } //end of scan through entire csd text, control vectors are now populated
    
    //csound->Message("===End of Cabbage warnings===\n");
    //init all channels with their init val, and set parameters
    for(int i=0; i<guiCtrls.size(); i++)
    {
        //		Logger::writeToLog(guiCtrls.getReference(i).getStringProp(CabbageIDs::channel)+": "+String(guiCtrls[i].getNumProp(CabbageIDs::value)));
#ifndef Cabbage_No_Csound
        if(guiCtrls.getReference(i).getStringProp("channeltype")=="string")
            //deal with combobox strings..
            csound->SetChannel(guiCtrls.getReference(i).getStringProp(CabbageIDs::channel).toUTF8(), "");
        //									guiCtrls.getReference(i).getStringArrayPropValue("text", guiCtrls[i].getNumProp(CabbageIDs::value)-1).toUTF8().getAddress());
        else
            csound->SetChannel( guiCtrls.getReference(i).getStringProp(CabbageIDs::channel).toUTF8(), guiCtrls[i].getNumProp(CabbageIDs::value));
#endif
    }
    
    //init all channels with their init val, and set parameters
    for(int i=0; i<guiLayoutCtrls.size(); i++)
    {
        if(guiLayoutCtrls.getReference(i).getStringProp(CabbageIDs::type).equalsIgnoreCase("texteditor"))
            csound->SetChannel(guiLayoutCtrls.getReference(i).getStringProp(CabbageIDs::channel).toUTF8(),
                               guiLayoutCtrls.getReference(i).getStringProp(CabbageIDs::text).toUTF8().getAddress());
#ifndef Cabbage_No_Csound
        if(guiLayoutCtrls.getReference(i).getStringProp(CabbageIDs::identchannel).isNotEmpty())
            //deal with combobox strings..
            csound->SetChannel(guiLayoutCtrls.getReference(i).getStringProp(CabbageIDs::identchannel).toUTF8(), "");
        //									guiCtrls.getReference(i).getStringArrayPropValue("text", guiCtrls[i].getNumProp(CabbageIDs::value)-1).toUTF8().getAddress());
#endif
    }
    
    
#if defined(Cabbage_Build_Standalone) || defined(CABBAGE_HOST)
    
    if(this->getActiveEditor())
    {
        CabbagePluginAudioProcessorEditor* editor = dynamic_cast<CabbagePluginAudioProcessorEditor*>(this->getActiveEditor());
        
        if(refresh)
        {
            editor->comps.clear();
            editor->layoutComps.clear();
            editor->repaint();
            //((CabbagePluginAudioProcessorEditor*)getActiveEditor())->setEditMode(false);
            //editor->setEditMode(false);
        }
        //!this will not work as we are moving through our entire control vector
        for(int i=indexOfLastLayoutCtrl; i<guiLayoutCtrls.size(); i++)
            editor->InsertGUIControls(guiLayoutCtrls[i]);
        for(int i=indexOfLastGUICtrl; i<guiCtrls.size(); i++)
            editor->InsertGUIControls(guiCtrls[i]);
        
        if(!getPreference(appProperties, "ExternalEditor") && refresh)
            editor->setEditMode(checkGUI);
        
    }
    
#endif
}

//============= deal with interprocess communication =========================
void CabbagePluginAudioProcessor::CabbageInterprocessConnection::messageReceived(const MemoryBlock& message)
{
	StringArray data;
	data.addTokens(message.toString(), " ");
	owner.messageQueue.addOutgoingChannelMessageToQueue(data[0], data[1].getFloatValue(), "float");
	//owner.appendMessage ("Connection #" + String (ourNumber) + " - message received: " + message.toString());
}

void CabbagePluginAudioProcessor::openInterprocess (bool asSocket, bool asSender, String address, int port)
{
        closeInterprocess();

        // Make the appropriate bits of UI visible..


        // and try to open the socket or pipe...
        bool openedOk = false;

        if (asSender)
        {
            // if we're connecting to an existing server, we can just create a connection object
            // directly.
            ScopedPointer<CabbageInterprocessConnection> newConnection (new CabbageInterprocessConnection (*this));

            if (asSocket)
            {
                openedOk = newConnection->connectToSocket(address,
                                                           port,
                                                           1000);
            }
            else
            {
                openedOk = newConnection->connectToPipe ("CabbagePipe", 100);
            }

            if (openedOk)
			{
                activeConnections.add (newConnection.release());
				
			}
			else
				csound->Message("Couldn't create connection?");
        }
        else
        {
            // if we're starting up a server, we need to tell the server to start waiting for
            // clients to connect. It'll then create connection objects for us when clients arrive.
            if (asSocket)
            {
                openedOk = server->beginWaitingForSocket (port);

                if (openedOk)
                    csound->Message("Waiting for another app to connect to Cabbage server..");
            }
            else
            {
                ScopedPointer<CabbageInterprocessConnection> newConnection (new CabbageInterprocessConnection (*this));

                openedOk = newConnection->createPipe ("CabbagePipe", 100);

                if (openedOk)
                {
                    csound->Message("Waiting for another app to connect to Cabbage server..");
                    activeConnections.add (newConnection.release());
                }
            }
        }

        if (! openedOk)
        {
			csound->Message("Failed to open pipe...");
        }
}

//============================================================================
//dynamically remove components from editor window, used in EDIT mode
//============================================================================
void CabbagePluginAudioProcessor::removeGUIComponent(int index, String type)
{
    //remove component struct from our abstract vector
    CabbagePluginAudioProcessorEditor* editor = dynamic_cast<CabbagePluginAudioProcessorEditor*>(this->getActiveEditor());
    
    if(type=="interactive")
    {
        //remove GUI abstract structure from vector
        guiCtrls.remove(index);
    }
    else
    {
        //remove GUI abstract structure from vector
        guiLayoutCtrls.remove(index);
    }
    
    editor->updateLayoutEditorFrames();
    editor->repaint();
}

//============================================================================
// start/stop recording of file
//============================================================================
void CabbagePluginAudioProcessor::startRecording ()
{
    if(!isWinXP)
        if (sampleRate > 0)
        {
            // Create an OutputStream to write to our destination file...
            tempAudioFile.deleteFile();
            backgroundThread.startThread();
            ScopedPointer<FileOutputStream> fileStream (tempAudioFile.createOutputStream());
            
            if (fileStream != nullptr)
            {
                // Now create a WAV writer object that writes to our output stream...
                WavAudioFormat wavFormat;
                AudioFormatWriter* writer = wavFormat.createWriterFor(fileStream, sampleRate, 2, 16, StringPairArray(), 0);
                
                if (writer != nullptr)
                {
                    fileStream.release(); // (passes responsibility for deleting the stream to the writer object that is now using it)
                    
                    // Now we'll create one of these helper objects which will act as a FIFO buffer, and will
                    // write the data to disk on our background thread.
                    threadedWriter = new AudioFormatWriter::ThreadedWriter (writer, backgroundThread, 32768);
                    
                    // Reset our recording thumbnail
                    //thumbnail.reset (writer->getNumChannels(), writer->getSampleRate());
                    nextSampleNum = 0;
                    
                    // And now, swap over our active writer pointer so that the audio callback will start using it..
                    const ScopedLock sl (writerLock);
                    activeWriter = threadedWriter;
                }
            }
        }
}

void CabbagePluginAudioProcessor::stopRecording()
{
    if(!isWinXP)
    {
        // First, clear this pointer to stop the audio callback from using our writer object..
        {
            const ScopedLock sl (writerLock);
            activeWriter = nullptr;
        }
        
        threadedWriter = nullptr;
        
        // Now we can delete the writer object. It's done in this order because the deletion could
        // take a little time while remaining data gets flushed to disk, so it's best to avoid blocking
        // the audio callback while this happens.
#if !defined(AndroidBuild)
        FileChooser fc("Save", tempAudioFile.getFullPathName(), "*.wav", UseNativeDialogue);
        //determine whether to poen file or directory
        if(fc.browseForFileToSave(true))
        {
            File selectedFile = fc.getResult();
            tempAudioFile.moveFileTo(selectedFile);
        }
#endif
    }
}


String CabbagePluginAudioProcessor::getCsoundOutput()
{
	const int messageCnt = csound->GetMessageCnt();
	
	if(messageCnt==0)
		return csoundOutput;
		
	for(int i=0;i<messageCnt;i++)
	{
		csoundOutput+=csound->GetFirstMessage();
		csound->PopFirstMessage();
	}	
	
	return csoundOutput;
}

//===========================================================
// CALLBACKS FOR STANDALONE
//===========================================================
//#ifndef Cabbage_No_Csound
//void CabbagePluginAudioProcessor::messageCallback(CSOUND* csound, int /*attr*/,  const char* fmt, va_list args)
//{
//    if(fmt)
//    {
//        CabbagePluginAudioProcessor* ud = (CabbagePluginAudioProcessor *) csoundGetHostData(csound);
//        char msg[MAX_BUFFER_SIZE];
//        vsnprintf(msg, MAX_BUFFER_SIZE, fmt, args);
//        
//        ud->debugMessage += String(msg); //We have to append the incoming msg
//		//const MessageManagerLock mmLock;
//        ud->csoundOutput += ud->debugMessage;
//        ud->debugMessageArray.add(ud->debugMessage);
//        if(ud->createLog)
//            Logger::writeToLog(String(msg).trim());
//        ud->sendChangeMessage();
//        ud->debugMessage = "";
//        ud = nullptr;
//        
//    }
//}
//#endif

#if defined(BUILD_DEBUGGER) && !defined(Cabbage_No_Csound)

void CabbagePluginAudioProcessor::breakpointCallback(CSOUND *csound, debug_bkpt_info_t *bkpt_info, void *userdata)
{
#ifdef BUILD_DEBUGGER
    CabbagePluginAudioProcessor* ud = (CabbagePluginAudioProcessor *) userdata;
    ud->getCallbackLock().enter();
    int ksmpOffset = ud->ksmpsOffset;;
    
    //code based on Andres implementation in CsoundQT...
    
    debug_instr_t *debug_instr = bkpt_info->breakpointInstr;
    
    
    // Copy variable list
    debug_variable_t* vp = bkpt_info->instrVarList;
    
    
    if(ud->breakCount==0)
        ud->breakCount= debug_instr->kcounter;
    else ud->breakCount++;
    String output;
    
    output << "\nBreakpoint at instr " << debug_instr->p1 << "\tNumber of k-cycles into performance: " << ud->breakCount << "\n------------------------------------------------------";;
    
    while (vp)
    {
        output << " \n";
        if (vp->name[0] != '#')
        {
            output << " VarName:"<< vp->name << "\t";;
            if (strcmp(vp->typeName, "i") == 0 || strcmp(vp->typeName, "k") == 0)
            {
                output << " value=" << *((MYFLT *) vp->data);
            }
            else if(strcmp(vp->typeName, "S") == 0)
            {
                output << " value=" << (char*)(vp->data);
            }
            else if (strcmp(vp->typeName, "a") == 0)
            {
                MYFLT *data = (MYFLT *) vp->data;
                output << " value="<< *data;// << *(data + 1) << *(data + 2)<< *(data + 3);
                
            }
            output << " varType[" << vp->typeName << "]";
            
        }
        vp = vp->next;
    }
    
    ud->csoundDebuggerOutput = output;
    csoundDebugFreeVariables(csound, vp);
    //csoundDebugFreeInstrInstances(csound, debug_instr);
    ud->getCallbackLock().exit();
    
#endif
}
#endif
//==============================================================================
#if defined(Cabbage_Build_Standalone) || defined(CABBAGE_HOST)
CabbagePluginAudioProcessor* JUCE_CALLTYPE createCabbagePluginFilter(String inputfile, bool guiOnOff, int pluginType)
{
    return new CabbagePluginAudioProcessor(inputfile, false, pluginType);
}
#else
AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CabbagePluginAudioProcessor();
}
#endif

//==========================================================================
//action listener. Listen to messages being sent form xypad automations
//==========================================================================
void CabbagePluginAudioProcessor::changeListenerCallback(ChangeBroadcaster *source)
{
    float xVal, yVal;
    //is message coming from an xypad
    XYPadAutomation* xyPad = dynamic_cast< XYPadAutomation*>(source);
    if(xyPad)
    {
#ifdef Cabbage_Build_Standalone
        setParameterNotifyingHost(xyPad->paramIndex, xyPad->getXValue());
        setParameterNotifyingHost(xyPad->paramIndex+1, xyPad->getYValue());
        sendChangeMessage();
#else
        if(xyPad->getMinimumXValue()>=0)
            xVal = (xyPad->getXValue()/xyPad->getXRange())+(fabs(xyPad->getMinimumXValue())/xyPad->getXRange());
        else
            xVal = (xyPad->getXValue()/xyPad->getXRange())-(fabs(xyPad->getMinimumXValue())/xyPad->getXRange());
        
        if(xyPad->getMinimumYValue()<=0)
            yVal = (xyPad->getYValue()/xyPad->getYRange())+(fabs(xyPad->getMinimumYValue())/xyPad->getYRange());
        else
            yVal = (xyPad->getYValue()/xyPad->getYRange())-(fabs(xyPad->getMinimumYValue())/xyPad->getYRange());
        
        setParameterNotifyingHost(xyPad->paramIndex, xVal);
        setParameterNotifyingHost(xyPad->paramIndex+1, yVal);
        sendChangeMessage();
#endif
    }
    
    
}

//==============================================================================
// getTable data from Csound so table editor can draw table
//==============================================================================
StringArray CabbagePluginAudioProcessor::getTableStatement(int tableNum)
{
    StringArray fdata;
    fdata.add(String::empty);
#ifndef Cabbage_No_Csound
    if(csCompileResult==OK)
    {
        MYFLT* argsPtr, *temp;
        int noOfArgs = csoundGetTableArgs(csound->GetCsound(), &argsPtr, tableNum);
        if(noOfArgs!=-1)
        {
            int tableSize = csound->GetTable(temp, tableNum);
            fdata.add(String(tableNum));
            fdata.add("0");
            fdata.add(String(tableSize));
            if(noOfArgs==0)
                fdata.add(String(1));
            else
                for(int i=0; i<noOfArgs; i++)
                {
                    fdata.add(String(argsPtr[i]));
                }
        }
    }
#endif
    return fdata;
}
//==============================================================================
const Array<double, CriticalSection> CabbagePluginAudioProcessor::getTable(int tableNum)
{
    Array<double, CriticalSection> points;
    if(csCompileResult==OK)
    {
        int tableSize=0;
#ifndef Cabbage_No_Csound
        MYFLT* temp;
        tableSize = csound->GetTable(temp, tableNum);
#else
        float *temp;
#endif
        if(tableSize>0)
            points = Array<double, CriticalSection>(temp, tableSize);
    }
    return points;
}

const Array<float, CriticalSection> CabbagePluginAudioProcessor::getTableFloats(int tableNum)
{
    Array<float, CriticalSection> points;
    if(csCompileResult==OK)
    {
        points.clear();
        
        int tableSize=0;
#ifndef Cabbage_No_Csound
        
        tableSize = csound->TableLength(tableNum);
        temp.clear();
        //not good if table size is -1!
        if(tableSize<0)
            return points;
        
        temp.reserve(tableSize);
        csound->TableCopyOut(tableNum, &temp[0]);
#else
        float *temp;
#endif
        if(tableSize>0)
            points = Array<float, CriticalSection>(&temp[0], tableSize);
    }
    return points;
}

int CabbagePluginAudioProcessor::checkTable(int tableNum)
{
#ifndef Cabbage_No_Csound
    return  csound->TableLength(tableNum);
#else
    return -1;
#endif
}

//=================================================================================
// Get and Set Parameter methods, called by our editor, and the plugin host...
//=================================================================================
float CabbagePluginAudioProcessor::getParameter (int index)
{
    if(isPositiveAndBelow(index, getGUICtrlsSize()))
    {
        float range = getGUICtrls(index).getNumProp(CabbageIDs::range);
        float min = getGUICtrls(index).getNumProp(CabbageIDs::min);
        //Logger::writeToLog("parameterGet-"+String(index)+String("-Min:")+String(min)+" Range:"+String(range)+ " Val:"+String(getGUICtrls(index).getNumProp(CabbageIDs::value)));
        //Logger::writeToLog("parameterGet:"+String(index)+String(":")+String(guiCtrls[index].getNumProp(CabbageIDs::value)));
        
        /* this gets called at any time by our host or out GUI editor */
        if(index<(int)guiCtrls.size()) //make sure index isn't out of range
        {
#ifndef Cabbage_Build_Standalone
            float val = (getGUICtrls(index).getNumProp(CabbageIDs::value)/range)-(min/range);
            if(getGUICtrls(index).getStringProp(CabbageIDs::type)==CabbageIDs::combobox)
                return (getGUICtrls(index).getNumProp(CabbageIDs::value)/getGUICtrls(index).getNumProp(CabbageIDs::comborange));
            else if(getGUICtrls(index).getStringProp(CabbageIDs::type)==CabbageIDs::checkbox ||
                    getGUICtrls(index).getStringProp(CabbageIDs::type)==CabbageIDs::button)
                return getGUICtrls(index).getNumProp(CabbageIDs::value);
            else
                return (getGUICtrls(index).getNumProp(CabbageIDs::value)/range)-(min/range);
#else
            return guiCtrls[index].getNumProp(CabbageIDs::value);
#endif
        }
        else
            return 0.0f;
    }
    else
    {
        cUtils::debug("getParameter(): Parameter index out of bounds");
        return 0.0f;
    }
}

void CabbagePluginAudioProcessor::setParameter (int index, float newValue)
{
    String stringMessage;
#ifndef Cabbage_No_Csound
    float range, min, max, comboRange;
    //add index of control that was changed to dirty control vector
    dirtyControls.addIfNotAlreadyThere(index);
    //Logger::writeToLog("parameterSet:"+String(newValue));
    if(index<(int)guiCtrls.size())//make sure index isn't out of range
    {
#ifndef Cabbage_Build_Standalone
        //scaling in here because incoming values in plugin mode range from 0-1
        range = getGUICtrls(index).getNumProp(CabbageIDs::range);
        comboRange = getGUICtrls(index).getNumProp(CabbageIDs::comborange);
        //Logger::writeToLog("inValue:"+String(newValue));
        min = getGUICtrls(index).getNumProp(CabbageIDs::min);
        
        if(getGUICtrls(index).getStringProp(CabbageIDs::type)==CabbageIDs::xypad)
            newValue = (jmax(0.f, newValue)*range)+min;
        else if(getGUICtrls(index).getStringProp(CabbageIDs::type)==CabbageIDs::combobox)//combo box value need to be rounded...
            newValue = (newValue*comboRange);
        else if(getGUICtrls(index).getStringProp(CabbageIDs::type)==CabbageIDs::checkbox ||
                getGUICtrls(index).getStringProp(CabbageIDs::type)==CabbageIDs::button)
            range=1;
        else
            newValue = (newValue*range)+min;
        
        
        
#endif
        if(getGUICtrls(index).getStringProp(CabbageIDs::type)==CabbageIDs::combobox &&
           getGUICtrls(index).getStringProp(CabbageIDs::channeltype)==CabbageIDs::stringchannel)
        {
			cUtils::debug(getGUICtrls(index).getStringArrayProp(CabbageIDs::text).size());
            stringMessage = getGUICtrls(index).getStringArrayPropValue(CabbageIDs::text, newValue-1);
            messageQueue.addOutgoingChannelMessageToQueue(guiCtrls.getReference(index).getStringProp(CabbageIDs::channel),
                                                          stringMessage, CabbageIDs::stringchannel);
        }
        else
        {
            messageQueue.addOutgoingChannelMessageToQueue(guiCtrls.getReference(index).getStringProp(CabbageIDs::channel),
                                                          newValue, guiCtrls.getReference(index).getStringProp(CabbageIDs::type));
            
        }
        guiCtrls.getReference(index).setNumProp(CabbageIDs::value, newValue);
    }
#endif
}

//==============================================================================
//this method gets called after a performKsmps() to update our GUI controls
//with messages from Csound. For instance, a user might wish to change the value
//of a GUI slider from Csound by using a chnset opcode. The speed at which this is
//updated can be teaked, so as not to hog resources...
void CabbagePluginAudioProcessor::updateCabbageControls()
{
#ifndef Cabbage_No_Csound
    bool shouldUpdate = false;
    String chanName, channelMessage;
    if(csCompileResult==OK)
    {
		for(int i=0;i<socketChannelIdentifiers.size();i++)
		{
			//showMessage(oscChannelIdentifiers[i]);
			float currentVal = (float)socketChannelValues.getWithDefault(socketChannelIdentifiers[i], -9999);
			float channelValue = csound->GetChannel(socketChannelIdentifiers[i].getCharPointer());
			
			if(currentVal != channelValue)
			{
				socketChannelValues.set(socketChannelIdentifiers[i], channelValue);
				//MemoryBlock messageData (socketChannelValues.getWithDefault(socketChannelIdentifiers[i], ""), sizeof(float));
				//
				const String message (socketChannelIdentifiers[i] + " " + String(channelValue));
				MemoryBlock messageData(message.toUTF8(), message.getNumBytesAsUTF8());
				for (int i = activeConnections.size(); --i >= 0;)
				{
					if (! activeConnections[i]->sendMessage (messageData))
					{
						// the write failed, so indicate that the connection has broken..
						//csound->Message("Cabbage is trying to output a message to another process but can't because it can't find a connection!");
					}
				}

			}
		}		
		
//		{
//			char string[1024] = {0};
//            csound->GetStringChannel("HappyDays", string);
//			const String message = String(string);
//			//cUtils::debug(message);
//			csound->SetChannel("HappyDays", "");
//			
//		}    		
		
        //update all control widgets
        for(int index=0; index<getGUICtrlsSize(); index++)
        {
            if(guiCtrls[index].getStringProp(CabbageIDs::channeltype).equalsIgnoreCase(CabbageIDs::stringchannel))
            {
                //THIS NEEDS TO ALLOW COMBOBOXEX THAT CONTAIN SNAPSHOTS TO UPDATE..
            }
            else
            {
                float value = csound->GetChannel(guiCtrls[index].getStringProp(CabbageIDs::channel).getCharPointer());
                
                if(value!=guiCtrls.getReference(index).getNumProp(CabbageIDs::value))
                {
                    //Logger::writeToLog("Channel:"+guiCtrls[index].getStringProp(CabbageIDs::channel));
                    //Logger::writeToLog("value:"+String(value));
                    guiCtrls.getReference(index).setNumProp(CabbageIDs::value, value);
                    dirtyControls.addIfNotAlreadyThere(index);
                    shouldUpdate = true;
                }
            }
            
            if(guiCtrls[index].getStringProp(CabbageIDs::identchannel).isNotEmpty())
            {
                //if controls has an identifier channel send data from Csound to control
                char string[4096] = {0};
                csound->GetStringChannel(guiCtrls[index].getStringProp(CabbageIDs::identchannel).toUTF8().getAddress(), string);
                channelMessage = String(string);
                if(channelMessage.isNotEmpty())
                {
                    guiCtrls.getReference(index).setStringProp(CabbageIDs::identchannelmessage, channelMessage.trim());
                    guiCtrls.getReference(index).parse(" "+channelMessage, "");
                    dirtyControls.addIfNotAlreadyThere(index);
                    shouldUpdate = true;
                }
                //else
                //guiCtrls.getReference(index).setStringProp(CabbageIDs::identchannelmessage, "");
                //zero channel message so that we don't keep sending the same string
                csound->SetChannel(guiCtrls[index].getStringProp(CabbageIDs::identchannel).toUTF8().getAddress(), "");
            }
        }
        
        //update all layout control widgets
        //currently this is only needed for table widgets as other layout controls
        //don't use channel messages...
        for(int index=0; index<getGUILayoutCtrlsSize(); index++)
        {
            if(guiLayoutCtrls[index].getStringProp(CabbageIDs::type)==CabbageIDs::table)
            {
                for(int y=0; y<guiLayoutCtrls[index].getStringArrayProp(CabbageIDs::channel).size(); y++)
                {
                    //String test = getGUILayoutCtrls(index).getStringArrayPropValue(CabbageIDs::channel, y);
                    float value = csound->GetChannel(guiLayoutCtrls[index].getStringArrayPropValue(CabbageIDs::channel, y).getCharPointer());
                    guiLayoutCtrls[index].setTableChannelValues(y, value);
                    shouldUpdate=true;
                }
            }
            
            if(guiLayoutCtrls[index].getStringProp(CabbageIDs::identchannel).isNotEmpty())
            {
                char string[4096] = {0};
                //guiLayoutCtrls[index].getStringProp(CabbageIDs::identchannel).toUTF8().getAddress()
                csound->GetStringChannel(guiLayoutCtrls[index].getStringProp(CabbageIDs::identchannel).toUTF8().getAddress(), string);
                channelMessage = String(string);
                //Logger::writeToLog(guiLayoutCtrls[index].getStringProp(CabbageIDs::identchannel));
                if(channelMessage.isNotEmpty())
                {
                    guiLayoutCtrls.getReference(index).parse(" "+channelMessage, channelMessage);
                    guiLayoutCtrls.getReference(index).setStringProp(CabbageIDs::identchannelmessage,channelMessage.trim());
                    shouldUpdate=true;
                }
                //else
                //	guiLayoutCtrls.getReference(index).setStringProp(CabbageIDs::identchannelmessage, "");
                //zero channel message so that we don't keep sending the same string
                csound->SetChannel(guiLayoutCtrls[index].getStringProp(CabbageIDs::identchannel).toUTF8().getAddress(), "");
            }
        }
        if(shouldUpdate)
            sendChangeMessage();
    }
#endif
}


//==============================================================================
//this method only gets called when it's safe to do so, i.e., between calls to performKsmps()
//this method sends any channel messages that are in the queue to from Cabbage to Csound
void CabbagePluginAudioProcessor::sendOutgoingMessagesToCsound()
{
#ifndef Cabbage_No_Csound
    if(csCompileResult==OK)
    {
#ifndef Cabbage_Build_Standalone
        if (getPlayHead() != 0 && getPlayHead()->getCurrentPosition (hostInfo))
        {
            csound->SetChannel(CabbageIDs::hostbpm.toUTF8(), hostInfo.bpm);
            csound->SetChannel(CabbageIDs::timeinseconds.toUTF8(), hostInfo.timeInSeconds);
            csound->SetChannel(CabbageIDs::isplaying.toUTF8(), hostInfo.isPlaying);
            csound->SetChannel(CabbageIDs::isrecording.toUTF8(), hostInfo.isRecording);
            csound->SetChannel(CabbageIDs::hostppqpos.toUTF8(), hostInfo.ppqPosition);
            csound->SetChannel(CabbageIDs::timeinsamples.toUTF8(), hostInfo.timeInSamples);
            csound->SetChannel(CabbageIDs::timeSigDenom.toUTF8(), hostInfo.timeSigDenominator);
            csound->SetChannel(CabbageIDs::timeSigNum.toUTF8(), hostInfo.timeSigNumerator);
            
        }
#endif
        
        for(int i=0; i<messageQueue.getNumberOfOutgoingChannelMessagesInQueue(); i++)
        {
            //Logger::writeToLog("MessageType:"+messageQueue.getOutgoingChannelMessageFromQueue(i).type);
            if(messageQueue.getOutgoingChannelMessageFromQueue(i).type=="directoryList")
            {
                for(int y=0; y<scoreEvents.size(); y++)
                    csound->InputMessage(scoreEvents[y].toUTF8());
                //scoreEvents.clear();
            }
            //update Csound function tables with values from table widget
            else if(messageQueue.getOutgoingChannelMessageFromQueue(i).type=="updateTable")
            {
                //Logger::writeToLog(messageQueue.getOutgoingChannelMessageFromQueue(i).fStatement.toUTF8());
                csound->InputMessage(messageQueue.getOutgoingChannelMessageFromQueue(i).fStatement.getCharPointer());
            }
            //catch string messags
            else if(messageQueue.getOutgoingChannelMessageFromQueue(i).type==CabbageIDs::stringchannel)
            {
                Logger::writeToLog(messageQueue.getOutgoingChannelMessageFromQueue(i).channelName);
                Logger::writeToLog(messageQueue.getOutgoingChannelMessageFromQueue(i).stringVal);
                csound->SetChannel(messageQueue.getOutgoingChannelMessageFromQueue(i).channelName.getCharPointer(),
                                   messageQueue.getOutgoingChannelMessageFromQueue(i).stringVal.toUTF8().getAddress());
            }
            else
                csound->SetChannel(messageQueue.getOutgoingChannelMessageFromQueue(i).channelName.getCharPointer(),
                                   messageQueue.getOutgoingChannelMessageFromQueue(i).value);
        }
        
        messageQueue.flushOutgoingChannelMessages();
    }
    
#endif
}


//========================================================================
// Standard plugin methods, getName, getNumParameters, setParamterName, get ProgramName, etc....
//==============================================================================
const String CabbagePluginAudioProcessor::getName() const
{
    return pluginName;
}

int CabbagePluginAudioProcessor::getNumParameters()
{
    return guiCtrls.size();
}

const String CabbagePluginAudioProcessor::getParameterName (int index)
{
    if(index<(int)guiCtrls.size())//make sure index isn't out of range
        return guiCtrls.getReference(index).getStringProp(CabbageIDs::channel);
    else return String::empty;
}

const String CabbagePluginAudioProcessor::getParameterText (int index)
{
    if(index<(int)guiCtrls.size())//make sure index isn't out of range
        return String (guiCtrls.getReference(index).getNumProp(CabbageIDs::value), 2);
    else return String::empty;
}

const String CabbagePluginAudioProcessor::getInputChannelName (int channelIndex) const
{
    if(channelIndex<(int)guiCtrls.size())//make sure index isn't out of range
        return String (channelIndex + 1);
    else return String::empty;
}

const String CabbagePluginAudioProcessor::getOutputChannelName (int channelIndex) const
{
    if(channelIndex<(int)guiCtrls.size())//make sure index isn't out of range
        return String (channelIndex + 1);
    else return String::empty;
}

bool CabbagePluginAudioProcessor::isInputChannelStereoPair (int /*index*/) const
{
    return true;
}

bool CabbagePluginAudioProcessor::isOutputChannelStereoPair (int /*index*/) const
{
    return true;
}

bool CabbagePluginAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool CabbagePluginAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

void CabbagePluginAudioProcessor::setGuiEnabled(bool val)
{
#if (defined(Cabbage_Build_Standalone) || defined(CABBAGE_HOST)) && !defined(AndroidBuild)
    guiON = val;
    CabbagePluginAudioProcessorEditor* editor = dynamic_cast< CabbagePluginAudioProcessorEditor*>(this->getActiveEditor());
    if(editor)
    {
        if(val==false)
            if(getPreference(appProperties, "ExternalEditor"))
                csdFile.replaceWithText(codeEditor->getAllText());
        
    }
#endif
}

int CabbagePluginAudioProcessor::getNumPrograms()
{
    return 0;
}

int CabbagePluginAudioProcessor::getCurrentProgram()
{
    return 0;
}

void CabbagePluginAudioProcessor::setCurrentProgram (int /*index*/)
{
}

const String CabbagePluginAudioProcessor::getProgramName (int /*index*/)
{
    return String::empty;
}

void CabbagePluginAudioProcessor::changeProgramName (int /*index*/, const String& /*newName*/)
{
}

//==============================================================================
void CabbagePluginAudioProcessor::prepareToPlay (double sampRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    //showMessage(String(this->getNumOutputChannels()));
    keyboardState.reset();
    sampleRate = sampRate;
}
//==============================================================================
void CabbagePluginAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
    keyboardState.reset();
}
//==============================================================================
//this following callback only runs in plugin mode, and only when one of the
//host widgets are being used
void CabbagePluginAudioProcessor::timerCallback()
{
#ifndef Cabbage_No_Csound
    for(int y=0; y<xyAutomation.size(); y++)
    {
        if(xyAutomation[y])
            xyAutomation[y]->update();
    }
#endif
}

//==============================================================================
void CabbagePluginAudioProcessor::processBlock (AudioSampleBuffer& buffer, MidiBuffer& midiMessages)
{
    float* audioBuffer;
    int numSamples = buffer.getNumSamples();
    
    if(stopProcessing || isGuiEnabled())
    {
        buffer.clear();
    }
    else
    {
#ifndef Cabbage_No_Csound
		//if no inputs are used clear buffer in case it's not empty..
		if(getNumInputChannels()==0)
			buffer.clear();
				
        if(csCompileResult==OK)
        {
            if(isBypassed==true)
                return;
            
			
			
            keyboardState.processNextMidiBuffer (midiMessages, 0, buffer.getNumSamples(), true);
            midiBuffer = midiMessages;
			//midiBuffer.clear();
            
#if JucePlugin_ProducesMidiOutput
            if(!midiOutputBuffer.isEmpty())
			{
                midiMessages.swapWith(midiOutputBuffer);
			}
			else
				midiMessages.clear();
#endif
            
            for(int i=0; i<numSamples; i++, csndIndex++)
            {
                if(csndIndex == csdKsmps)
                {
                    getCallbackLock().enter();
                    //slow down calls to these functions, no need for them to be firing at k-rate
                    yieldCounter = (yieldCounter>guiRefreshRate) ? 0 : yieldCounter+1;
                    if(yieldCounter==0)
                    {
                        sendOutgoingMessagesToCsound();
                        updateCabbageControls();
                    }
                    
                    
                    csCompileResult = csound->PerformKsmps();
                    
                    if(csCompileResult!=OK)
                        stopProcessing = true;
                    else
                        ksmpsOffset++;
                    
                    getCallbackLock().exit();
                    csndIndex = 0;
                }
                if(csCompileResult==OK)
                {
                    for(int channel = 0; channel < getNumOutputChannels(); channel++ )
                    {
                        audioBuffer = buffer.getWritePointer(channel,0);
                        pos = csndIndex*getNumOutputChannels();
                        float samp = audioBuffer[i]*cs_scale;
						CSspin[channel+pos] = samp;
                        audioBuffer[i] = (CSspout[channel+pos]/cs_scale);
                        
                    }
                }
                else
                    buffer.clear();
                
                
            }
            
            if (activeWriter != 0 && !isWinXP)
                activeWriter->write (buffer.getArrayOfReadPointers(), buffer.getNumSamples());
            
            if(isMuted)
                buffer.clear();
            
            rmsLeft = buffer.getRMSLevel(0, 0, buffer.getNumSamples());
            rmsRight = buffer.getRMSLevel(1, 0, buffer.getNumSamples());
            
        }//if not compiled just mute output
        else
        {
            for(int channel = 0; channel < getNumInputChannels(); channel++)
            {
                audioBuffer = buffer.getWritePointer(channel,0);
                for(int i=0; i<numSamples; i++, csndIndex++)
                {
                    audioBuffer[i]=0;
                }
            }
        }
        
#endif
    }
    
    vuCounter = (vuCounter>5) ? 0 : vuCounter+1;
    if(vuCounter==0)
        sendActionMessage("rmsValues "+String(rmsLeft)+" "+String(rmsRight));
}


//==============================================================================
// MIDI functions
//==============================================================================

#ifndef Cabbage_No_Csound
int CabbagePluginAudioProcessor::OpenMidiInputDevice(CSOUND * csound, void **userData, const char* /*devName*/)
{
    *userData = csoundGetHostData(csound);
    if(!userData)
        cout << "\n\ncan't open midi in\n\n";
    return 0;
}

//==============================================================================
// Reads MIDI input data from host, gets called every time there is MIDI input to our plugin
//==============================================================================
int CabbagePluginAudioProcessor::ReadMidiData(CSOUND* /*csound*/, void *userData,
                                              unsigned char *mbuf, int nbytes)
{
    CabbagePluginAudioProcessor *midiData = (CabbagePluginAudioProcessor *)userData;
    if(!userData)
    {
        cout << "\n\nInvalid";
        return 0;
    }
    int cnt=0;
    
    if(!midiData->midiBuffer.isEmpty() && cnt <= (nbytes - 3))
    {
        MidiMessage message(0xf4, 0, 0, 0);
        MidiBuffer::Iterator i (midiData->midiBuffer);
        int messageFrameRelativeTothisProcess;
        while (i.getNextEvent (message, messageFrameRelativeTothisProcess))
        {
            if(message.isNoteOn())
            {
                *mbuf++ = (unsigned char)0x90 + message.getChannel()-1;
                *mbuf++ = (unsigned char)message.getNoteNumber();
                *mbuf++ = (unsigned char)message.getVelocity();
                cnt += 3;
            }
            else if(message.isNoteOff())
            {
                *mbuf++ = (unsigned char)0x80 + message.getChannel()-1;
                *mbuf++ = (unsigned char)message.getNoteNumber();
                *mbuf++ = (unsigned char)message.getVelocity();
                cnt += 3;
            }
            else if(message.isAllSoundOff())
            {
                *mbuf++ = (unsigned char)0x7B + message.getChannel()-1;
                *mbuf++ = (unsigned char)message.getNoteNumber();
                *mbuf++ = (unsigned char)message.getVelocity();
                cnt += 3;
            }
            else if(message.isController())
            {
                *mbuf++ = (unsigned char)0xB0 + message.getChannel()-1;
                *mbuf++ = (unsigned char)message.getControllerNumber();
                *mbuf++ = (unsigned char)message.getControllerValue();
                cnt += 3;
            }
            else if(message.isProgramChange())
            {
                *mbuf++ = (unsigned char)0xC0 + message.getChannel()-1;
                *mbuf++ = (unsigned char)message.getProgramChangeNumber();
                cnt += 2;
            }
            else if(message.isPitchWheel())
            {
                *mbuf++ = (unsigned char)0xE0 + message.getChannel()-1;
                *mbuf++ = (unsigned char)((pitch_bend >> 7) & 0xFF);
                *mbuf++ = (unsigned char)(pitch_bend & 0xFF);
                cnt += 3;
            }
            
        }
        midiData->midiBuffer.clear();
    }
    return cnt;
}

//==============================================================================
// Opens MIDI output device, adding -QN to your CsOptions will causes this method to be called
// as soon as your plugin loads
//==============================================================================
int CabbagePluginAudioProcessor::OpenMidiOutputDevice(CSOUND * csound, void **userData, const char* /*devName*/)
{
    *userData = csoundGetHostData(csound);
    if(!userData)
        Logger::writeToLog("\n\ncan't open midi out\n\n");
    return 0;
}

//==============================================================================
// Write MIDI data to plugin's MIDI output. Each time Csound outputs a midi message this
// method should be called. Note: you must have -Q set in your CsOptions
//==============================================================================
int CabbagePluginAudioProcessor::WriteMidiData(CSOUND* /*csound*/, void *_userData,
                                               const unsigned char *mbuf, int nbytes)
{
    CabbagePluginAudioProcessor *userData = (CabbagePluginAudioProcessor *)_userData;
    if(!userData)
    {
        Logger::writeToLog("\n\nInvalid");
        return 0;
    }
    
    MidiMessage message(mbuf, nbytes, 0);
    //Logger::writeToLog(String(message.getNoteNumber()));
    userData->midiOutputBuffer.addEvent(message, 0);
    return nbytes;
}

#endif
//==============================================================================
bool CabbagePluginAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

AudioProcessorEditor* CabbagePluginAudioProcessor::createEditor()
{
    return new CabbagePluginAudioProcessorEditor (this);
}

//==============================================================================
void CabbagePluginAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // Here's an example of how you can use XML to make it easy and more robust:
    
    // Create an outer XML element..
    XmlElement xml ("CABBAGE_PLUGIN_SETTINGS");
    
    for(int i=0; i<guiCtrls.size(); i++)
        xml.setAttribute(guiCtrls[i].getStringProp(CabbageIDs::channel),getParameter(i));
    
    for(int i=0; i<guiLayoutCtrls.size(); i++)
        if(guiLayoutCtrls[i].getStringProp(CabbageIDs::type)==CabbageIDs::filebutton)
        {
            //save filebutton last opened file...
            char string[4096] = {0};
            csound->GetStringChannel(guiLayoutCtrls[i].getStringProp(CabbageIDs::channel).toUTF8().getAddress(), string);
            xml.setAttribute ("filebutton_"+guiLayoutCtrls[i].getStringProp(CabbageIDs::channel), String(string));
            Logger::writeToLog("filebutton_"+guiLayoutCtrls[i].getStringProp(CabbageIDs::channel));
            Logger::writeToLog(String(string));
        }
    
    // then use this helper function to stuff it into the binary blob and return it..
    copyXmlToBinary (xml, destData);
}

void CabbagePluginAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
    // This getXmlFromBinary() helper function retrieves our XML from the binary blob..
    ScopedPointer<XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    
    if (xmlState != nullptr)
    {
        // make sure that it's actually our type of XML object..
        if (xmlState->hasTagName ("CABBAGE_PLUGIN_SETTINGS"))
        {
            for(int i=0; i<this->getNumParameters(); i++)
            {
                this->setParameter(i, (float)xmlState->getDoubleAttribute(guiCtrls[i].getStringProp(CabbageIDs::channel)));
            }
            
            
            for(int i=0; i<xmlState->getNumAttributes(); i++)
            {
                //if the instrument uses a filebutton, then retreive the last known
                //file that was loaded with it
                if(xmlState->getAttributeName(i).contains("filebutton_"))
                {
                    String channel = xmlState->getAttributeName(i).substring(11);
                    //Logger::writeToLog(xmlState->getAttributeValue(i));
                    csound->SetChannel(channel.toUTF8().getAddress(), xmlState->getAttributeValue(i).toUTF8().getAddress());
                }
                
            }
        }
    }
}
//==============================================================================
// breakpoint functions
//==============================================================================
void CabbagePluginAudioProcessor::setCsoundInstrumentBreakpoint(int instr, int line=0)
{
#if defined(BUILD_DEBUGGER) && !defined(Cabbage_No_Csound)
    getCallbackLock().enter();
    if(breakpointInstruments.size()==0)
        csoundDebuggerInit(csound->GetCsound());
    csoundSetBreakpointCallback(csound->GetCsound(), breakpointCallback, (void*)this);
    csoundSetInstrumentBreakpoint(csound->GetCsound(), instr, 0);
    breakpointInstruments.add(instr);
    getCallbackLock().exit();
#endif
}

void CabbagePluginAudioProcessor::removeCsoundInstrumentBreakpoint(int instr)
{
#if defined(BUILD_DEBUGGER) && !defined(Cabbage_No_Csound)
    getCallbackLock().enter();
    breakpointInstruments.removeAllInstancesOf(instr);
    if(breakpointInstruments.size()==0)
        breakCount=0;
    csoundDebuggerInit(csound->GetCsound());
    csoundRemoveInstrumentBreakpoint(csound->GetCsound(), instr);
    getCallbackLock().exit();
#endif
}

void CabbagePluginAudioProcessor::continueCsoundDebug()
{
#if defined(BUILD_DEBUGGER) && !defined(Cabbage_No_Csound)
    
    if(breakpointInstruments.size()>0)
    {
        getCallbackLock().enter();
        csoundDebugContinue(csound->GetCsound());
        getCallbackLock().exit();
    }
    
#endif
}

void CabbagePluginAudioProcessor::nextCsoundDebug()
{
#if defined(BUILD_DEBUGGER) && !defined(Cabbage_No_Csound)
    //not yet implemented....
    if(breakpointInstruments.size()>0)
    {
        getCallbackLock().enter();
        //csoundDebugNext(csound->GetCsound());
        getCallbackLock().exit();
    }
    
#endif
}

void CabbagePluginAudioProcessor::cleanCsoundDebug()
{
#if defined(BUILD_DEBUGGER) && !defined(Cabbage_No_Csound)
    csoundClearBreakpoints(csound->GetCsound());
#endif
}
