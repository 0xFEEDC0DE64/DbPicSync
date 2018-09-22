#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <qmath.h>
#include <QDataStream>
#include <QDateTime>
#include <QCryptographicHash>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDirIterator>
#include <QStringBuilder>
#include <QUuid>
#include <QtGlobal>

bool writeBitmap(const QString &filename, const QByteArray &content)
{
    qDebug() << "writeBitmap" << filename;

    QFile file(filename);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        qWarning() << "could not open file" << file.errorString();
        return false;
    }

    quint64 pixels = qCeil(content.length() / 4.0);
    quint32 width = qSqrt(pixels);
    quint32 height = qCeil(pixels / (qreal)width);

    quint32 bitmapSize = width * height * 4;

    {
        QDataStream dataStream(&file);
        dataStream.setByteOrder(QDataStream::LittleEndian);

        //BMP Header
        dataStream << (quint16)0x4D42;            //BM-Header
        dataStream << (quint32)(54 + bitmapSize); //File size
        dataStream << content.length();           //Unused (and abused for content length)
        dataStream << (quint32)54;                //Offset to bitmap data

        //DIB Header
        dataStream << (quint32)40;                //DIP Header size
        dataStream << width;                      //width
        dataStream << height;                     //height
        dataStream << (quint16)1;                 //Number of color planes
        dataStream << (quint16)32;                //Bits per pixel
        dataStream << (quint32)0;                 //No compression;
        dataStream << bitmapSize;                 //Size of bitmap data
        dataStream << (quint32)2835;              //Horizontal print resolution
        dataStream << (quint32)2835;              //Horizontal print resolution
        dataStream << (quint32)0;                 //Number of colors in palette
        dataStream << (quint32)0;                 //Important colors
    }

    file.write(content);
    file.write(QByteArray((width * height * 4) - content.length(), 0));

    return true;
}

bool readBitmap(const QString &filename, QByteArray &content)
{
    qDebug() << "readBitmap" << filename;

    QFile file(filename);
    if(!file.exists())
    {
        qWarning() << "file does not exist";
        return false;
    }
    if(!file.open(QIODevice::ReadOnly))
    {
        qWarning() << "could not open file" << file.errorString();
        return false;
    }

    if(file.size() < 14)
    {
        qWarning() << "not enough bytes";
        return false;
    }

    QDataStream dataStream(&file);
    dataStream.setByteOrder(QDataStream::LittleEndian);

    {
        quint16 bmHeader;
        dataStream >> bmHeader;
        if(bmHeader != 0x4D42)
        {
            qWarning() << "no BM header";
            return false;
        }
    }

    {
        quint32 filesize;
        dataStream >> filesize;
        if(filesize != file.size())
        {
            qWarning() << "file size does not match!";
            return false;
        }
    }

    quint32 usedSize;
    dataStream >> usedSize;

    quint32 offsetBitmapData;
    dataStream >> offsetBitmapData;

    if(!file.seek(offsetBitmapData))
    {
        qWarning() << "could not seek";
        return false;
    }

    content = file.read(usedSize);

    if(content.length() != usedSize)
    {
        qWarning() << "could not read enough for usedSize";
        return false;
    }

    return true;
}

bool emptyDirectory(const QString &path)
{
    QDir dir(path);

    if(!dir.exists())
    {
        qWarning() << "tried to empty non-existant dir" << path;
        return true;
    }

    bool success = true;
    // not empty -- we must empty it first
    QDirIterator di(dir.absolutePath(), QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    while (di.hasNext()) {
        di.next();
        const QFileInfo& fi = di.fileInfo();
        const QString &filePath = di.filePath();
        bool ok;
        if (fi.isDir() && !fi.isSymLink()) {
            ok = QDir(filePath).removeRecursively(); // recursive
        } else {
            ok = QFile::remove(filePath);
            if (!ok) { // Read-only files prevent directory deletion on Windows, retry with Write permission.
                const QFile::Permissions permissions = QFile::permissions(filePath);
                if (!(permissions & QFile::WriteUser))
                    ok = QFile::setPermissions(filePath, permissions | QFile::WriteUser)
                        && QFile::remove(filePath);
            }
        }
        if (!ok)
            success = false;
    }

    if(!success)
        qWarning() << "could not empty" << path;

    return success;
}

bool spread(const QString &sourcePath, const QString &targetPath)
{
    qDebug() << "spread" << sourcePath << targetPath;

    QDir targetDir(targetPath);

    if(!targetDir.mkpath(targetDir.absolutePath()))
    {
        qWarning() << "could not create target dir";
        return false;
    }

    QFileInfo sourceFileInfo(sourcePath);
    if(!sourceFileInfo.exists())
    {
        qWarning() << "source does not exist";
        return false;
    }

    if(sourceFileInfo.isFile())
    {
        bool rewriteIndex = false;

        if(QFile::exists(targetDir.absoluteFilePath("__index.bmp")))
        {
            QByteArray content;
            if(readBitmap(targetDir.absoluteFilePath("__index.bmp"), content))
            {
                QJsonParseError error;
                auto document = QJsonDocument::fromJson(content, &error);
                if(error.error != QJsonParseError::NoError)
                {
                    qWarning() << "index is invalid: error parsing json" << error.errorString();
                    return false;
                }
                if(!document.isObject())
                {
                    qWarning() << "index is invalid: json is not an object";
                    return false;
                }
                auto jsonObject = document.object();

                if(!jsonObject.contains("type"))
                {
                    qWarning() << "index is invalid: json does not contain type";
                    return false;
                }
                auto typeValue = jsonObject.value("type");
                if(typeValue.type() != QJsonValue::String)
                {
                    qWarning() << "index is invalid: json type is not a string";
                    return false;
                }
                auto type = typeValue.toString();

                if(type == "file")
                {
                    //TODO: compare lastmodified and size
                }
                else if(type == "directory")
                {
                    qInfo() << "type changed from file to directory";
                    if(!emptyDirectory(targetDir.absolutePath()))
                        return false;
                    rewriteIndex = true;
                }
                else
                {
                    qWarning() << "index is invalid: unknown type" << type;
                    rewriteIndex = true;
                }
            }
            else
                rewriteIndex = true;
        }
        else
            rewriteIndex = true;

        if(rewriteIndex)
        {
            if(!emptyDirectory(targetPath))
                return false;

            QFile sourceFile(sourcePath);
            if(!sourceFile.open(QIODevice::ReadOnly))
            {
                qWarning() << "could not open source file" << sourceFile.errorString();
                return false;
            }

            QJsonArray parts;

            QCryptographicHash hash(QCryptographicHash::Sha512);
            while(sourceFile.pos() < sourceFile.size())
            {
                QString filename;
                QString completePath;

                do
                {
                    filename = QUuid::createUuid().toString().remove('{').remove('}') % ".bmp";
                    completePath = targetDir.absoluteFilePath(filename);
                }
                while(QFileInfo(completePath).exists());

                QJsonObject part;
                part["filename"] = filename;
                part["startPos"] = sourceFile.pos();

                auto buffer = sourceFile.read(2048 * 2048 * 4);
                hash.addData(buffer);

                part["endPos"] = sourceFile.pos();
                part["length"] = buffer.length();

                if(!writeBitmap(completePath, buffer))
                    return false;

                parts.append(part);
            }

            QJsonObject jsonObject;
            jsonObject["type"] = "file";
            jsonObject["filesize"] = sourceFileInfo.size();

            jsonObject["birthTime"] = sourceFileInfo
#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
                    //deprecated since 5.10
                    .created()
#else
                    .birthTime()
#endif
                    .toMSecsSinceEpoch();
            jsonObject["lastModified"] = sourceFileInfo.lastModified().toMSecsSinceEpoch();
            jsonObject["lastRead"] = sourceFileInfo.lastRead().toMSecsSinceEpoch();
            jsonObject["sha512"] = QString(hash.result().toHex());
            jsonObject["parts"] = parts;
            if(!writeBitmap(targetDir.absoluteFilePath("__index.bmp"), QJsonDocument(jsonObject).toJson(/* QJsonDocument::Compact */))) //amazon has enough storage space!
                return false;
        }
    }
    else if(sourceFileInfo.isDir())
    {
        QDir sourceDir(sourcePath);
        if(!sourceDir.exists())
        {
            qWarning() << "source dir does not exist";
            return false;
        }

        bool rewriteIndex = false;
        QStringList oldEntries;

        if(QFile::exists(targetDir.absoluteFilePath("__index.bmp")))
        {
            QByteArray content;
            if(readBitmap(targetDir.absoluteFilePath("__index.bmp"), content))
            {
                QJsonParseError error;
                auto document = QJsonDocument::fromJson(content, &error);
                if(error.error != QJsonParseError::NoError)
                {
                    qWarning() << "index is invalid: error parsing json" << error.errorString();
                    return false;
                }
                if(!document.isObject())
                {
                    qWarning() << "index is invalid: json is not an object";
                    return false;
                }
                auto jsonObject = document.object();

                if(!jsonObject.contains("type"))
                {
                    qWarning() << "index is invalid: json does not contain type";
                    return false;
                }
                auto typeValue = jsonObject.value("type");
                if(typeValue.type() != QJsonValue::String)
                {
                    qWarning() << "index is invalid: json type is not a string";
                    return false;
                }
                auto type = typeValue.toString();

                if(type == "file")
                {
                    qInfo() << "type changed from directory to file";
                    if(!emptyDirectory(targetDir.absolutePath()))
                        return false;
                    rewriteIndex = true;
                }
                else if(type == "directory")
                {
                    if(!jsonObject.contains("entries"))
                    {
                        qWarning() << "index is invalid: json does not contain entries";
                        rewriteIndex = true;
                    }
                    auto entriesValue = jsonObject.value("entries");
                    if(entriesValue.type() != QJsonValue::Array)
                    {
                        qWarning() << "index is invalid: json entries is not an array";
                        rewriteIndex = true;
                    }
                    auto entries = entriesValue.toArray();

                    for(const auto &value : entries)
                    {
                        if(value.type() != QJsonValue::String)
                        {
                            qWarning() << "index is invalid: json entry is not a string";
                            rewriteIndex = true;
                            break;
                        }

                        oldEntries.append(value.toString());
                    }
                }
                else
                {
                    qWarning() << "index is invalid: unknown type" << type;
                    rewriteIndex = true;
                }
            }
            else
                rewriteIndex = true;
        }
        else
            rewriteIndex = true;

        for(const auto &oldEntry : oldEntries)
        {
            QFileInfo oldFileInfo(sourceDir.absoluteFilePath(oldEntry));
            if(!oldFileInfo.exists())
            {
                qInfo() << "deleted" << sourceDir.absoluteFilePath(oldEntry);
                if(!QDir(targetDir.absoluteFilePath(oldEntry)).removeRecursively())
                {
                    qWarning() << "could not remove dir" << targetDir.absoluteFilePath(oldEntry);
                    return false;
                }
                rewriteIndex = true;
            }
        }

        QJsonArray entries;

        for(auto fileInfo : sourceDir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot))
        {
            if(!oldEntries.contains(fileInfo.fileName()))
            {
                qInfo() << "added" << fileInfo.absoluteFilePath();
                rewriteIndex = true;
            }

            entries.append(fileInfo.fileName());
            if(!spread(fileInfo.absoluteFilePath(), targetDir.absoluteFilePath(fileInfo.fileName())))
                return false;
        }

        if(rewriteIndex)
        {
            QJsonObject jsonObject;
            jsonObject["type"] = "directory";
            jsonObject["entries"] = entries;
            if(!writeBitmap(targetDir.absoluteFilePath("__index.bmp"), QJsonDocument(jsonObject).toJson(/* QJsonDocument::Compact */))) //amazon has enough storage space!
                return false;
        }
    }

    return true;
}

bool compile(const QString &sourcePath, const QString &targetPath)
{
    qDebug() << "compile" << sourcePath << targetPath;

    QFileInfo sourceFileInfo(sourcePath);
    if(!sourceFileInfo.exists())
    {
        qWarning() << "source does not exist";
        return false;
    }
    if(!sourceFileInfo.isDir())
    {
        qWarning() << "source is not a dir";
        return false;
    }

    QDir sourceDir(sourcePath);

    QJsonObject jsonObject;

    {
        QByteArray content;
        if(!readBitmap(sourceDir.absoluteFilePath("__index.bmp"), content))
            return false;

        QJsonParseError error;
        auto document = QJsonDocument::fromJson(content, &error);
        if(error.error != QJsonParseError::NoError)
        {
            qWarning() << "error parsing json" << error.errorString();
            return false;
        }
        if(!document.isObject())
        {
            qWarning() << "json is not an object";
            return false;
        }
        jsonObject = document.object();
    }

    if(!jsonObject.contains("type"))
    {
        qWarning() << "json does not contain type";
        return false;
    }
    auto typeValue = jsonObject.value("type");
    if(typeValue.type() != QJsonValue::String)
    {
        qWarning() << "json type is not a string";
        return false;
    }
    auto type = typeValue.toString();

    if(type == "file")
    {
        QFile targetFile(targetPath);

        if(!targetFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            qWarning() << "could not open file" << targetFile.errorString();
            return false;
        }

        //TODO
    }
    else if(type == "directory")
    {
        QDir targetDir(targetPath);

        if(!targetDir.mkpath(targetDir.absolutePath()))
        {
            qWarning() << "could not create dir";
            return false;
        }

        if(!jsonObject.contains("entries"))
        {
            qWarning() << "json does not contain entries";
            return false;
        }
        auto entriesValue = jsonObject.value("entries");
        if(entriesValue.type() != QJsonValue::Array)
        {
            qWarning() << "json entries is not an array";
            return false;
        }
        auto entries = entriesValue.toArray();

        for(const auto &entryValue : entries)
        {
            if(entryValue.type() != QJsonValue::String)
            {
                qWarning() << "json entry is not a string";
                return false;
            }
            auto entry = entryValue.toString();

            if(!compile(sourceDir.absoluteFilePath(entry), targetDir.absoluteFilePath(entry)))
                return false;
        }
    }
    else
    {
        qWarning() << "unknown type" << type;
        return false;
    }

    return true;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("picsync");
    QCoreApplication::setApplicationVersion("1.0");

    qSetMessagePattern("%{time dd.MM.yyyy HH:mm:ss.zzz} "
                       "["
                       "%{if-debug}DEBUG%{endif}"
                       "%{if-info}INFO%{endif}"
                       "%{if-warning}WARN%{endif}"
                       "%{if-critical}CRIT%{endif}"
                       "%{if-fatal}FATAL%{endif}"
                       "] "
                       "%{function}(): "
                       "%{message}");

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", "Lets you convert any file into pictures. Mostly used in combination with cloud storage."));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption actionOption(QStringList() << "a" << "action", QCoreApplication::translate("main", "Action (spread or compile)"), QCoreApplication::translate("main", "action"));
    parser.addOption(actionOption);

    QCommandLineOption sourceOption(QStringList() << "s" << "source", QCoreApplication::translate("main", "Source file or directory"), QCoreApplication::translate("main", "some_file"));
    parser.addOption(sourceOption);

    QCommandLineOption targetOption(QStringList() << "t" << "target", QCoreApplication::translate("main", "Target directory"), QCoreApplication::translate("main", "some_directory"));
    parser.addOption(targetOption);

    parser.process(app);

    if(!parser.isSet(actionOption))
    {
        qCritical() << "no action set";
        parser.showHelp();
        return -1;
    }

    enum { ActionSpread, ActionCompile } action;

    if(parser.value(actionOption) == "spread")
        action = ActionSpread;
    else if(parser.value(actionOption) == "compile")
        action = ActionCompile;
    else
    {
        qCritical() << "unknown action" << parser.value(actionOption);
        parser.showHelp();
        return -2;
    }

    if(!parser.isSet(sourceOption))
    {
        qCritical() << "source not set";
        parser.showHelp();
        return -3;
    }

    QFileInfo sourceFileInfo(parser.value(sourceOption));
    if(!sourceFileInfo.exists())
    {
        qCritical() << "source" << parser.value(sourceOption) << "does not exist";
        parser.showHelp();
        return -4;
    }
    if(!sourceFileInfo.isFile() && !sourceFileInfo.isDir())
    {
        qCritical() << "source" << parser.value(sourceOption) << "isnt file nor dir";
        parser.showHelp();
        return -5;
    }

    if(!parser.isSet(targetOption))
    {
        qCritical() << "target not set";
        parser.showHelp();
        return -6;
    }

    QFileInfo targetFileInfo(parser.value(targetOption));
    if(targetFileInfo.exists() && !targetFileInfo.isDir())
    {
        qCritical() << "target" << parser.value(targetOption) << "exists and is not a dir";
        parser.showHelp();
        return -7;
    }

    switch(action)
    {
    case ActionSpread:
        if(spread(sourceFileInfo.absoluteFilePath(), targetFileInfo.absoluteFilePath()))
            return 0;
        else
            return -8;
    case ActionCompile:
        if(compile(sourceFileInfo.absoluteFilePath(), targetFileInfo.absoluteFilePath()))
            return 0;
        else
            return -8;
    }

    Q_UNREACHABLE();
}
