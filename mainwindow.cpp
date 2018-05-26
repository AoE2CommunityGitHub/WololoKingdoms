#include <iostream>
#include <set>
#include <map>
#include <algorithm>
#include <cctype>
#include <string>
#include <sstream>
#include <windows.h>
#include <ShellAPI.h>

#include <chrono>
#include <thread>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string/replace.hpp>
#include "paths.h"
#include "conversions.h"

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "dialog.h"
#include <QWhatsThis>
#include <QPoint>
#include <QProgressBar>
#include <QSettings>
#include <QFileDialog>
#include "sdk/public/steam/steam_api.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    initialize();
}

MainWindow::~MainWindow()
{
    delete ui;
}

int MainWindow::initialize() {

    logFile = std::ofstream("prelog.txt");
    QDialog* dialog;
    resourceDir = fs::path("resources\\");
    readSettings();
    ui->label->setWordWrap(true);
    steamPath = getSteamPath();
    boost::replace_all(steamPath,"/","\\");
    HDPath = getHDPath(steamPath);
    HDPath.make_preferred();
    if(HDPath == fs::path()) {
        this->ui->label->setText(translation["noSteamInstallation"].c_str());
        dialog = new Dialog(this,translation["noSteamInstallation"],translation["errorTitle"]);
        dialog->exec();
        log("NoSteam. Path: "+steamPath);
        allowRun = false;
        return -1;
    }
    checkSteamApi();
    setInstallDirectory(getOutPath(HDPath).string());
    this->ui->installDirectory->setText(outPath.string().c_str());
    QObject::connect( this->ui->directoryDialogButton, &QPushButton::clicked, this, [this]() {
        this->ui->installDirectory->setText(QFileDialog::getExistingDirectory(this, "Select Install Directory"));
        setInstallDirectory(this->ui->installDirectory->text().toStdString());
    } );

    QObject::connect( this->ui->installDirectory, &QLineEdit::editingFinished, this, [this]() {
        setInstallDirectory(this->ui->installDirectory->text().toStdString());
        updateUI();
    } );

    this->ui->restrictedCivMods->setEnabled(!this->ui->useExe->isChecked());
    QObject::connect( this->ui->useExe, &QRadioButton::toggled, this, [this]() {
        this->ui->restrictedCivMods->setEnabled(!this->ui->useExe->isChecked());
        updateUI();
    } );

    if(QCoreApplication::arguments().back() != "-s") {
        callExternalExe(std::wstring(L"WKUpdater.exe"));
        exit(EXIT_FAILURE);
    }

    changeLanguage();
    this->ui->label->setText((baseModName+" version " + version).c_str());

    readDataModList();

    //Language selection dropdown
    QObject::connect( this->ui->languageChoice, static_cast<void (QComboBox::*)(const QString &)>(&QComboBox::currentIndexChanged), this, [this]() {
        switch(this->ui->languageChoice->currentIndex()) {
            case 0: language = "br"; break;
            case 1: language = "de"; break;
            case 2: language = "en"; break;
            case 3: language = "es"; break;
            case 4: language = "fr"; break;
            case 5: language = "it"; break;
            case 6: language = "jp"; break;
            case 7: language = "ko"; break;
            case 8: language = "nl"; break;
            case 9: language = "ru"; break;
            case 10: language = "zh"; break;
            case 11: language = "zht"; break;
            default: language = "en";
        }
        changeLanguage();
    } );

    //Patch selection dropdown.
    QObject::connect( this->ui->patchSelection, static_cast<void (QComboBox::*)(const QString &)>(&QComboBox::currentIndexChanged), this, [this]() {
        changeModPatch();
    } );

    setButtonWhatsThis(this->ui->hotkeyTip,"hotkeyTip");
    setButtonWhatsThis(this->ui->tooltipTip,"tooltipTip");
    setButtonWhatsThis(this->ui->patchSelectionTip,"patchSelectionTip");
    setButtonWhatsThis(this->ui->flagsTip,"flagsTip");
    setButtonWhatsThis(this->ui->installTypeTip,"installTypeTip");
    setButtonWhatsThis(this->ui->modsTip,"modsTip");
    setButtonWhatsThis(this->ui->mapsTip,"mapsTip");
    setButtonWhatsThis(this->ui->customMapsTip,"customMapsTip");
    setButtonWhatsThis(this->ui->directoryTip,"directoryTip");
    setButtonWhatsThis(this->ui->restrictedCivTip,"restrictedCivTip");

    if(fs::exists("player1.hki")) {
        this->ui->hotkeyChoice->setDisabled(true);
        this->ui->hotkeyChoice->setItemText(0,translation["customHotkeys"].c_str());
        this->ui->hotkeyTip->setDisabled(true);
    }

    //Checkbox en-/disabling the patch selection dropdown
    QObject::connect( this->ui->usePatch, &QCheckBox::clicked, this, [this]() {
        if(this->ui->usePatch->isChecked()) {
            this->ui->patchSelection->setDisabled(false);
            this->ui->hotkeyChoice->setDisabled(true);
            this->ui->useGrid->setDisabled(true);
            this->ui->useMonks->setDisabled(true);
            this->ui->usePw->setDisabled(true);
            this->ui->useWalls->setDisabled(true);
            this->ui->copyMaps->setDisabled(true);
            this->ui->copyCustomMaps->setDisabled(true);
            this->ui->restrictedCivMods->setDisabled(true);
        } else {
            this->ui->patchSelection->setDisabled(true);
            this->ui->hotkeyChoice->setDisabled(false);
            this->ui->useGrid->setDisabled(false);
            this->ui->useMonks->setDisabled(false);
            this->ui->usePw->setDisabled(false);
            this->ui->useWalls->setDisabled(false);
            this->ui->copyMaps->setDisabled(false);
            this->ui->copyCustomMaps->setDisabled(false);
            this->ui->restrictedCivMods->setDisabled(false);
        }
        changeModPatch();
    } );

    QObject::connect( this->ui->runButton, &QPushButton::clicked, this, &MainWindow::runConverter);

    updateUI();
    return 0;
}

void MainWindow::runConverter() {

    writeSettings();
    if(bar == NULL) {
        bar = new QProgressBar();
        this->ui->verticalLayout->addWidget(bar);
    } else {
        bar->setValue(0);
        bar->repaint();
    }
    qApp->processEvents();
    logFile.close();
    logFile = std::ofstream("log.txt");
    WKSettings* settings = new WKSettings(this->ui->useVoobly->isChecked(), this->ui->useExe->isChecked(),
        this->ui->useBoth->isChecked(), this->ui->useMonks->isChecked(), this->ui->usePw->isChecked(),
        this->ui->useWalls->isChecked(), this->ui->copyMaps->isChecked(), this->ui->copyCustomMaps->isChecked(),
        this->ui->restrictedCivMods->isChecked(),this->ui->useNoSnow->isChecked(), this->ui->fixFlags->isChecked(),
        this->ui->replaceTooltips->isChecked(), this->ui->useGrid->isChecked(), installDir, language, version, dlcLevel,
        this->ui->usePatch->isChecked() ? this->ui->patchSelection->currentIndex() : -1, this->ui->hotkeyChoice->currentIndex(),
        HDPath, outPath, vooblyDir, upDir, dataModList);
    WKConverter* converter = new WKConverter(this, settings);
    try {
        converter->run();
    } catch (std::exception const & e) {
        createDialog(translate("dialogException")+std::string()+e.what(),translate("errorTitle"));

        log(e.what());
        setInfo(translate("error"));
    }
    catch (std::string const & e) {
        createDialog(translate("dialogException")+e,translate("errorTitle"));

        log(e);
        setInfo(translate("error"));
    }
    logFile.close();
}

void MainWindow::log(std::string logMessage) {
    logFile << logMessage << std::endl;
}

void MainWindow::setInfo(std::string info){
    this->ui->label->setText(info.c_str());
    this->ui->label->repaint();
}
void MainWindow::createDialog(std::string info){
    QDialog* dialog = new Dialog(this,info.c_str());
    dialog->exec();
}
void MainWindow::createDialog(std::string info, std::string title){
    QDialog* dialog = new Dialog(this,info.c_str(),title.c_str());
    dialog->exec();
}

void MainWindow::setProgress(int i){
    if(i < 0)
        i = 0;
    else if (i > 100)
        i = 100;
    bar->setValue(i);
    bar->repaint();
}

void MainWindow::increaseProgress(int i){
    if(i < 0)
        i = 0;
    int value = bar->value() + i;
    if (value > 100)
        value = 100;
    bar->setValue(value);
    bar->repaint();
}

std::string MainWindow::translate(std::string line) {
    return translation[line];
}

void MainWindow::setInstallDirectory(std::string directory) {
    if(!fs::exists(directory)) {
        directory = getOutPath(HDPath).make_preferred().string();
    }
    outPath = fs::path(directory);
    outPath.make_preferred();

    switch (dlcLevel) {
        case 3: vooblyDir = outPath / ("Voobly Mods\\AOC\\Data Mods\\"+baseModName);
                upDir = outPath / ("Games\\"+baseModName);
                break;
        case 2: vooblyDir = outPath / ("Voobly Mods\\AOC\\Data Mods\\"+baseModName+" AK");
                upDir = outPath / ("Games\\"+baseModName+" AK");
                break;
        case 1: vooblyDir = outPath / ("Voobly Mods\\AOC\\Data Mods\\"+baseModName+" FE");
                upDir = outPath / ("Games\\"+baseModName+" FE");
                break;
    }


    nfzUpOutPath = upDir / "Player.nfz";
    nfzVooblyOutPath = vooblyDir / "Player.nfz";

    if(!fs::exists(outPath/"age2_x1")) {
        this->ui->label->setText(translation["noAoC"].c_str());
        QDialog* dialog = new Dialog(this,translation["noAoC"],translation["errorTitle"]);
        log("No Aoc. Path: "+(outPath/"age2_x1").string());
        dialog->exec();
        allowRun = false;
    } else {
        this->ui->label->setText((baseModName+" version " + version).c_str());
        allowRun = true;
    }
    updateUI();

    if(!fs::exists(vooblyDir)) {
        this->ui->usePatch->setDisabled(true);
        this->ui->usePatch->setChecked(false);
    } else {
        this->ui->usePatch->setDisabled(false);
    }

}

void MainWindow::setButtonWhatsThis(QPushButton* button, std::string title) {
    const char * questionIcon = "resources\\question.png";
    //WhatsThis for the special maps option
    button->setIcon(QIcon(questionIcon));
    button->setIconSize(QSize(16,16));
    button->setWhatsThis(translation[title].c_str());
    QObject::connect( button, &QPushButton::clicked, this, [this, button]() {
            QWhatsThis::showText(button->mapToGlobal(QPoint(0,0)),button->whatsThis());
    } );

}

void MainWindow::callExternalExe(std::wstring exe) {
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    ZeroMemory( &si, sizeof(si) );
    si.cb = sizeof(si);
    ZeroMemory( &pi, sizeof(pi) );
    wchar_t cmdLineString[exe.length()+1];
    wcscpy(cmdLineString, exe.c_str());
    CreateProcess( NULL, cmdLineString, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi );
    CloseHandle( pi.hProcess );
    CloseHandle( pi.hThread );
}

void MainWindow::readDataModList() {
    /*
     * Read the info which Data Mods are included from a file
     */
    std::ifstream dataModFile("resources\\patches\\dataModList.txt");
    int id = 0;
    std::string line;
    while(std::getline(dataModFile, line)) {
        std::tuple<std::string,std::string,std::string,int,std::string> info;
        int index = line.find(',');
        std::get<0>(info) = line.substr(0,index);
        line = line.substr(index+1, std::string::npos);
        index = line.find(',');
        std::get<1>(info) = line.substr(0,index);
        line = line.substr(index+1, std::string::npos);
        index = line.find(',');
        std::get<2>(info) = line.substr(0,index);
        line = line.substr(index+1, std::string::npos);
        index = line.find(',');
        if(index != std::string::npos) {
            std::get<3>(info) = std::atoi(line.substr(0,index).c_str());
            std::get<4>(info) = line.substr(index+1,std::string::npos);
        } else {
            std::get<3>(info) = std::atoi(line.substr(index+1,std::string::npos).c_str());
            std::get<4>(info) = "";
        }
        dataModList[id] = info;
        this->ui->patchSelection->addItem(std::get<0>(info).c_str());
        id++;
    }
    dataModFile.close();
}

void MainWindow::checkSteamApi() {
    QDialog* dialog;
    SteamAPI_Init();
    if(!SteamApps()) {
        // open steam
        callExternalExe(strtowstr(steamPath) + L"\\Steam.exe");
        SteamAPI_Init();
    }
    int tries = 0;
    while(!SteamApps()) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        SteamAPI_Init();
        tries++;
        if(tries>12)
            break;
    }
    if(!SteamApps()) {
        if(!SteamAPI_Init()) {
            log("noSteamApi. Path: "+HDPath.string()+" Steam Path: "+steamPath);
            this->ui->label->setText(translation["noSteamApi"].c_str());
            dialog = new Dialog(this,translation["noSteamApi"].c_str(),translation["errorTitle"]);
        } else {
            log("noSteamApi. Path: "+HDPath.string()+" Steam Path: "+steamPath);
            this->ui->label->setText(translation["noSteam"].c_str());
            dialog = new Dialog(this,translation["noSteam"],translation["errorTitle"]);
        }
        dialog->exec();
        allowRun = false;
    } else if(SteamApps()->BIsDlcInstalled(239550)) {
        if(SteamApps()->BIsDlcInstalled(355950)) {
            if(SteamApps()->BIsDlcInstalled(488060))
                dlcLevel = 3;
            else {
                dlcLevel = 2;
                dialog = new Dialog(this,translation["noRajas"]);
                dialog->exec();
            }
        } else {
            dlcLevel = 1;
            dialog = new Dialog(this,translation["noAK"]);
            dialog->exec();
        }
    } else {
        log("noSteamApi. Path: "+HDPath.string()+" Steam Path: "+steamPath);
        this->ui->label->setText(translation["noFE"].c_str());
        dialog = new Dialog(this,translation["noFE"],translation["errorTitle"]);
        dialog->exec();
        allowRun = false;
    }
    SteamAPI_Shutdown();
}

void MainWindow::writeSettings()
{
    QSettings settings("Jineapple", "WololoKingdoms Installer");
    settings.setValue("copyMaps", this->ui->copyMaps->isChecked());
    settings.setValue("copyCustomMaps", this->ui->copyCustomMaps->isChecked());
    settings.setValue("useExe", this->ui->useExe->isChecked());
    settings.setValue("useVoobly", this->ui->useVoobly->isChecked());
    settings.setValue("useBoth", this->ui->useBoth->isChecked());
    settings.setValue("fixFlags", this->ui->fixFlags->isChecked());
    settings.setValue("useGrid", this->ui->useGrid->isChecked());
    settings.setValue("useNoSnow", this->ui->useNoSnow->isChecked());
    settings.setValue("usePw", this->ui->usePw->isChecked());
    settings.setValue("useMonks", this->ui->useMonks->isChecked());
    settings.setValue("useWalls", this->ui->useWalls->isChecked());
    settings.setValue("replaceTooltips", this->ui->replaceTooltips->isChecked());
    settings.setValue("usePatch", this->ui->usePatch->isChecked());
    settings.setValue("restrictedCivMods", this->ui->restrictedCivMods->isChecked());
    settings.setValue("languageChoice", this->ui->languageChoice->currentIndex());
    settings.setValue("patchSelection", this->ui->patchSelection->currentIndex());
}

void MainWindow::readSettings()
{
    QSettings settings("Jineapple", "WololoKingdoms Installer");
    if(settings.contains("copyMaps")) {
        this->ui->copyMaps->setChecked(settings.value("copyMaps").toBool());
        this->ui->copyCustomMaps->setChecked(settings.value("copyCustomMaps").toBool());
        this->ui->useExe->setChecked(settings.value("useExe").toBool());
        this->ui->useBoth->setChecked(settings.value("useBoth").toBool());
        this->ui->useVoobly->setChecked(settings.value("useVoobly").toBool());
        this->ui->fixFlags->setChecked(settings.value("fixFlags").toBool());
        this->ui->useGrid->setChecked(settings.value("useGrid").toBool());
        this->ui->usePw->setChecked(settings.value("usePw").toBool());
        this->ui->useWalls->setChecked(settings.value("useWalls").toBool());
        this->ui->useNoSnow->setChecked(settings.value("useNoSnow").toBool());
        this->ui->replaceTooltips->setChecked(settings.value("replaceTooltips").toBool());
        this->ui->usePatch->setChecked(settings.value("usePatch").toBool());
        this->ui->useMonks->setChecked(settings.value("useMonks").toBool());
        this->ui->restrictedCivMods->setChecked(settings.value("restrictedCivMods").toBool());
        this->ui->languageChoice->setCurrentIndex(settings.value("languageChoice").toInt());
        this->ui->patchSelection->setCurrentIndex(settings.value("patchSelection").toInt());
    }
}

void MainWindow::changeModPatch() {
    /*
     * Triggered when the data mod dropdown is changed. Switches the folders for files to be
     * created in between the regular "WololoKingdoms (FE/AK)" folders and a seperate folder for data mods.
     */
	modName = "WK ";
	patch = this->ui->usePatch->isChecked()?this->ui->patchSelection->currentIndex():-1;

    std::string dlcExtension = dlcLevel == 3?"":dlcLevel==2?" AK":" FE";
    modName += std::get<0>(dataModList[patch]);
    if(std::get<3>(dataModList[patch]) % 2 == 1) {
        modName += dlcExtension;
    }

    if(patch == -1) {
        vooblyDir = vooblyDir.parent_path() / (baseModName+dlcExtension);
        upDir = upDir.parent_path() / (baseModName+dlcExtension);
	} else {
		vooblyDir = vooblyDir.parent_path() / modName;
        upDir = upDir.parent_path() / modName;
	}
	nfzUpOutPath = upDir / "Player.nfz";
    nfzVooblyOutPath = vooblyDir / "Player.nfz";
	updateUI();
}

void MainWindow::changeLanguage() {
    /*
     * Loads the contents of <language>.txt into memory (If it exists). This is used to change the language of the installer ui,
     * as well as some special in-game lines (Terrain names in the scenario editor, some fixes for faulty lines in the original language files)
     */
	std::string line;
    std::ifstream translationFile("resources\\"+language+".txt");
	while (std::getline(translationFile, line)) {
        /*
         *  \\\\n -> \\n, means we want a \n in the text files for aoc
         *  If no such line is found, it might be a line for the installer itself, where we want actual linebreaks,
         * so replace \\n -> \n with a linebreak
         */
        if(line.find("\\\\n") == std::string::npos)
            boost::replace_all(line, "\\n", "\n");
		int index = line.find('=');
        std::string key = line.substr(0, index);
        translation[key] = line.substr(index+1, std::string::npos);
	}
    translationFile.close();
	this->ui->runButton->setText(translation["runButton"].c_str());
	this->ui->replaceTooltips->setText(translation["replaceTooltips"].c_str());
    this->ui->useExe->setText(translation["useExe"].c_str());
    this->ui->useVoobly->setText(translation["useVoobly"].c_str());
    this->ui->useBoth->setText(translation["useBoth"].c_str());
    this->ui->copyMaps->setText(translation["copyMaps"].c_str());
    this->ui->copyCustomMaps->setText(translation["copyCustomMaps"].c_str());
	this->ui->useGrid->setText(translation["useGrid"].c_str());
	this->ui->usePw->setText(translation["usePw"].c_str());
	this->ui->useWalls->setText(translation["useWalls"].c_str());
    this->ui->usePatch->setText(translation["usePatch"].c_str());
    this->ui->useMonks->setText(translation["useMonks"].c_str());
    this->ui->useNoSnow->setText(translation["useNoSnow"].c_str());
    this->ui->installLabel->setText(translation["installLabel"].c_str());
	this->ui->hotkeyChoice->setItemText(1,translation["hotkeys1"].c_str());
	this->ui->hotkeyChoice->setItemText(2,translation["hotkeys2"].c_str());
    this->ui->hotkeyChoice->setItemText(3,translation["hotkeys3"].c_str());
	updateUI();
}

void MainWindow::updateUI() {
    /*
     * First, we check if a hotkey file already exists. If it doesn't, the user has to select an option or won't be able to run
     * In either case, we also check if the user has the appropriate dlcLevel. That's >0 for regular WK and
     * 3 for any data mod based on WK (to avoid further fragmentation)
     */
    if ((this->ui->useExe->isChecked() && fs::exists(nfzUpOutPath)) || fs::exists(nfzVooblyOutPath)) {
		this->ui->hotkeyChoice->setItemText(0,translation["hotkeys0"].c_str());
        this->ui->hotkeyChoice->setStyleSheet("");
        if(allowRun)
			this->ui->runButton->setDisabled(false);
        else
            this->ui->runButton->setDisabled(true);
	} else {
		this->ui->hotkeyChoice->setItemText(0,translation["hotkeyChoice"].c_str());
        if(!this->ui->usePatch->isChecked()) {
			this->ui->runButton->setDisabled(true);
            this->ui->hotkeyChoice->setStyleSheet("border-style: solid; border-width: 2px; border-color: red;");
        }
		QObject::connect( this->ui->hotkeyChoice, static_cast<void (QComboBox::*)(const QString &)>(&QComboBox::currentIndexChanged), this, [this]{
            if (this->ui->hotkeyChoice->currentIndex() != 0 && allowRun) {
				this->ui->runButton->setDisabled(false);
                this->ui->hotkeyChoice->setStyleSheet("");
            } else {
				this->ui->runButton->setDisabled(true);
                this->ui->hotkeyChoice->setStyleSheet("border-style: solid; border-width: 2px; border-color: red;");
            }
		} );
	}
    /*
     * Based on whether a <language>.ini file exists, we can offer
     * modded tooltips to be enabled
     */
    fs::path patchFolder;
    if((std::get<3>(dataModList[patch]) / 2) % 2 == 1)
        patchFolder = resourceDir/("patches\\"+std::get<0>(dataModList[patch])+"\\");
    else
        patchFolder = resourceDir;

    if(!fs::exists(patchFolder/(language+".ini"))) {
		this->ui->replaceTooltips->setEnabled(false);
		this->ui->replaceTooltips->setChecked(false);
	} else {
		this->ui->replaceTooltips->setEnabled(true);
	}
	qApp->processEvents();
}
