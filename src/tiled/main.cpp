/*
 * main.cpp
 * Copyright 2008-2011, Thorbjørn Lindeijer <thorbjorn@lindeijer.nl>
 * Copyright 2011, Ben Longbons <b.r.longbons@gmail.com>
 * Copyright 2011, Stefan Beller <stefanbeller@googlemail.com>
 *
 * This file is part of Tiled.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "commandlineparser.h"
#include "mainwindow.h"
#include "languagemanager.h"
#include "pluginmanager.h"
#include "mapdocument.h"
#include "mapreader.h"
#include "mapwriterinterface.h"
#include "preferences.h"
#include "tiledapplication.h"
#include "tileset.h"

#include <QDebug>
#include <QFileInfo>
#include <QtPlugin>
#include <QStyle>
#include <QStyleFactory>

#ifdef STATIC_BUILD
Q_IMPORT_PLUGIN(qgif)
Q_IMPORT_PLUGIN(qjpeg)
Q_IMPORT_PLUGIN(qtiff)
#endif

#define STRINGIFY(x) #x
#define AS_STRING(x) STRINGIFY(x)

using namespace Tiled::Internal;

namespace {

class CommandLineHandler : public CommandLineParser
{
public:
    CommandLineHandler();

    bool quit;
    bool showedVersion;
    bool disableOpenGL;
    bool exportMap;

private:
    void showVersion();
    void justQuit();
    void setDisableOpenGL();
    void setExportMap();

    // Convenience wrapper around registerOption
    template <void (CommandLineHandler::*memberFunction)()>
    void option(QChar shortName,
                const QString &longName,
                const QString &help)
    {
        registerOption<CommandLineHandler, memberFunction>(this,
                                                           shortName,
                                                           longName,
                                                           help);
    }
};

} // anonymous namespace


CommandLineHandler::CommandLineHandler()
    : quit(false)
    , showedVersion(false)
    , disableOpenGL(false)
    , exportMap(false)
{
    option<&CommandLineHandler::showVersion>(
                QLatin1Char('v'),
                QLatin1String("--version"),
                QLatin1String("Display the version"));

    option<&CommandLineHandler::justQuit>(
                QChar(),
                QLatin1String("--quit"),
                QLatin1String("Only check validity of arguments"));

    option<&CommandLineHandler::setDisableOpenGL>(
                QChar(),
                QLatin1String("--disable-opengl"),
                QLatin1String("Disable hardware accelerated rendering"));

    option<&CommandLineHandler::setExportMap>(
                QChar(),
                QLatin1String("--export-map"),
                QLatin1String("Export the specified tmx file to target"));
}

void CommandLineHandler::showVersion()
{
    if (!showedVersion) {
        showedVersion = true;
        qWarning() << qPrintable(QApplication::applicationName())
                   << qPrintable(QApplication::applicationVersion());
        quit = true;
    }
}

void CommandLineHandler::justQuit()
{
    quit = true;
}

void CommandLineHandler::setDisableOpenGL()
{
    disableOpenGL = true;
}

void CommandLineHandler::setExportMap()
{
    exportMap = true;
}

int main(int argc, char *argv[])
{
    /*
     * On X11, Tiled uses the 'raster' graphics system by default, because the
     * X11 native graphics system has performance problems with drawing the
     * tile grid.
     */
#ifdef Q_WS_X11
    QApplication::setGraphicsSystem(QLatin1String("raster"));
#endif

    TiledApplication a(argc, argv);

    a.setOrganizationDomain(QLatin1String("dofus.io"));
    a.setApplicationName(QLatin1String("Dofus.io Map Editor"));
#ifdef BUILD_INFO_VERSION
    a.setApplicationVersion(QLatin1String(AS_STRING(BUILD_INFO_VERSION)));
#else
    a.setApplicationVersion(QLatin1String("0.12.0"));
#endif

#ifdef Q_OS_MAC
    a.setAttribute(Qt::AA_DontShowIconsInMenus);
#endif

#if QT_VERSION >= 0x050100
    // Enable support for highres images (added in Qt 5.1, but off by default)
    a.setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

#ifndef Q_OS_WIN
    QString baseName = QApplication::style()->objectName();
    if (baseName == QLatin1String("windows")) {
        // Avoid Windows 95 style at all cost
        if (QStyleFactory::keys().contains(QLatin1String("Fusion"))) {
            baseName = QLatin1String("fusion"); // Qt5
        } else { // Qt4
            // e.g. if we are running on a KDE4 desktop
            QByteArray desktopEnvironment = qgetenv("DESKTOP_SESSION");
            if (desktopEnvironment == "kde")
                baseName = QLatin1String("plastique");
            else
                baseName = QLatin1String("cleanlooks");
        }
        a.setStyle(QStyleFactory::create(baseName));
    }
#endif

    LanguageManager *languageManager = LanguageManager::instance();
    languageManager->installTranslators();

    CommandLineHandler commandLine;

    if (!commandLine.parse(QCoreApplication::arguments()))
        return 0;
    if (commandLine.quit)
        return 0;
    if (commandLine.disableOpenGL)
        Preferences::instance()->setUseOpenGL(false);

    PluginManager::instance()->loadPlugins();

    if (commandLine.exportMap) {
        // Get the path to the source file and target file
        if (commandLine.filesToOpen().length() < 2) {
            qWarning() << qPrintable(QCoreApplication::translate("Command line",
                                                                 "Export syntax is --export-map [format] <tmx file> <target file>"));
            return 1;
        }
        int index = 0;
        const QString *filter = commandLine.filesToOpen().length() > 2 ? &commandLine.filesToOpen().at(index++) : 0;
        const QString &sourceFile = commandLine.filesToOpen().at(index++);
        const QString &targetFile = commandLine.filesToOpen().at(index++);

        // Find the map writer interface for the target file
        Tiled::MapWriterInterface *chosenWriter = 0;
        QString suffix = QFileInfo(targetFile).completeSuffix();
        QList<Tiled::MapWriterInterface*> writers = PluginManager::instance()->interfaces<Tiled::MapWriterInterface>();
        foreach (Tiled::MapWriterInterface *writer, writers) {
            if (filter) {
                if (writer->nameFilters().contains(*filter, Qt::CaseInsensitive)) {
                    chosenWriter = writer;
                }
            }
            else if (!writer->nameFilters().filter(suffix, Qt::CaseInsensitive).isEmpty()) {
                if (chosenWriter) {
                    qWarning() << qPrintable(QCoreApplication::translate("Command line",
                                                                         "Non-unique file extension. Can't determine correct export format."));
                    return 1;
                }
                chosenWriter = writer;
            }
        }
        if (!chosenWriter) {
            qWarning() << qPrintable(QCoreApplication::translate("Command line",
                                                                 "No exporter found for target file."));
            return 1;
        }

        // Load the source file
        Tiled::MapReader reader;
        Tiled::Map *map = reader.readMap(sourceFile);
        if (!map) {
            qWarning() << qPrintable(QCoreApplication::translate("Command line",
                                                                 "Failed to load source map."));
            return 1;
        }

        // Write out the file
        bool success = chosenWriter->write(map, targetFile);

        qDeleteAll(map->tilesets());
        delete map;

        if (!success) {
            qWarning() << qPrintable(QCoreApplication::translate("Command line",
                                                                 "Failed to export map to target file."));
            return 1;
        }
        return 0;
    }

    MainWindow w;
    w.show();

    QObject::connect(&a, SIGNAL(fileOpenRequest(QString)),
                     &w, SLOT(openFile(QString)));

    if (!commandLine.filesToOpen().isEmpty()) {
        foreach (const QString &fileName, commandLine.filesToOpen())
            w.openFile(fileName);
    } else {
        w.openLastFiles();
    }

    return a.exec();
}
