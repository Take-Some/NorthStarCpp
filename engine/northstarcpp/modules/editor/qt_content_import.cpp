#include "qt_content_import.hpp"
#include "qt_content_import_merge.hpp"
#include "qt_content_browser_internal.hpp"
#include "../../../../tools/nac_pack_wave.hpp"

#include <northstarcpp/shared_assets.hpp>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeData>
#include <QMap>
#include <QLockFile>
#include <QSaveFile>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>

#include <stdexcept>

namespace northstarcpp::editor::content_import {
namespace {
using browser_internal::native;

enum class Kind { texture, model, audio, properties, nativeAsset, loose, unsupported };

Kind kindFor(QString const& file) {
    static QStringList const textures{"png","jpg","jpeg","bmp","tga","dds"};
    static QStringList const models{"obj","fbx","glb","gltf"};
    static QStringList const audio{"wav","ogg","opus","flac","mp3","aif","aiff","m4a"};
    static QStringList const containers{"ytd","ydd","ydr","nac","ymat","ymt",
        "ycd","ybn","ypt","ytyp","yscd","yfd","nui","nsp"};
    static QStringList const loose{"nscene","nscn","nprefab","js","mjs","ts",
        "json","xml","hlsl","glsl","vert","frag","txt","md","yaml","yml"};
    auto const name=QFileInfo(file).fileName().toLower();
    auto const suffix=QFileInfo(name).suffix();
    if(textures.contains(suffix))return Kind::texture;
    if(models.contains(suffix))return Kind::model;
    if(audio.contains(suffix))return Kind::audio;
    if(name.endsWith(".ytyp.xml"))return Kind::properties;
    if(containers.contains(suffix))return Kind::nativeAsset;
    if(loose.contains(suffix))return Kind::loose;
    return Kind::unsupported;
}

[[noreturn]] void fail(QString const& message) {
    throw std::runtime_error(message.toUtf8().toStdString());
}

QString entryName(QString name) {
    static QRegularExpression const invalid("[^a-z0-9_]+");
    name=name.toLower().replace(invalid,"_");
    if(name.isEmpty() || name=="_")name="asset";
    return name.left(100);
}

QString findTool(QString const& name, QString const& toolbeltPath) {
    QString executable=name;
#ifdef Q_OS_WIN
    executable+=".exe";
#endif
    auto const configured=qEnvironmentVariable("NORTHSTAR_IMPORT_TOOLS");
    auto const appDir=QCoreApplication::applicationDirPath();
    QStringList directories;
    if(QDir::isAbsolutePath(configured))directories.append(configured);
    QDir dir(appDir);
    for(int level=0;level<7;++level) {
        directories.append(dir.absolutePath());
        directories.append(dir.filePath("asset-import-tools"));
        if(!toolbeltPath.isEmpty())
            directories.append(dir.filePath("Tools/toolbelt/"+toolbeltPath+"/bin"));
        if(!dir.cdUp())break;
    }
    for(auto const& base:directories) {
        QFileInfo const candidate(QDir(base).filePath(executable));
        if(candidate.isFile() && candidate.isExecutable())return candidate.absoluteFilePath();
    }
    auto const onPath=QStandardPaths::findExecutable(executable);
    if(!onPath.isEmpty())return onPath;
    fail("Install "+name+" in the NorthStar Toolbelt or set NORTHSTAR_IMPORT_TOOLS.");
}

void runTool(QString const& name, QString const& location, QStringList const& args,
             QString const& workDir) {
    QProcess tool;
    tool.setWorkingDirectory(workDir);
    tool.setProcessChannelMode(QProcess::MergedChannels);
    tool.setStandardInputFile(QProcess::nullDevice());
    tool.start(findTool(name,location),args);
    if(!tool.waitForStarted(5000))fail(name+": "+tool.errorString());
    if(!tool.waitForFinished(120000)) {
        tool.kill();
        tool.waitForFinished();
        fail(name+" timed out.");
    }
    if(tool.exitStatus()!=QProcess::NormalExit || tool.exitCode()!=0) {
        auto const details=QString::fromUtf8(tool.readAll().right(2400)).trimmed();
        fail(name+" failed ("+QString::number(tool.exitCode())+"). "+details);
    }
}

std::vector<std::uint8_t> bytesOf(QString const& path) {
    QFile input(path);
    if(!input.open(QIODevice::ReadOnly) || input.size()<=0)
        fail("Cannot read NEF8 asset "+QFileInfo(path).fileName()+".");
    auto const length=input.size();
    // This path reads cooked NEF8 dictionaries. Source assets reach their
    // importers or QFile::copy without an editor file-size gate.
    if(length>256ll*1024*1024)
        fail("NEF8 dictionary exceeds the 256 MiB encoded container capacity.");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    if(input.read(reinterpret_cast<char*>(bytes.data()),length)!=length || !input.atEnd())
        fail("Incomplete asset read.");
    return bytes;
}

void encodeAudio(QStringList const& files, QStringList const& names,
                 QString const& output, QDir const& stage) {
    std::vector<nac::Track> tracks;
    std::size_t used=0;
    for(qsizetype i=0;i<files.size();++i) {
        auto const name=names[i].toStdString();
        auto const id=static_cast<std::uint32_t>(i+1);
        auto loadTrack=[&](QString const& wave) {
            used=(used+15u)&~std::size_t{15u};
            auto track=nac::pack::load_wave(native(wave),name,id,nac::max_pcm_bytes-used);
            used+=track.samples.size()*sizeof(std::int16_t);
            tracks.push_back(std::move(track));
        };
        if(QFileInfo(files[i]).suffix().compare("wav",Qt::CaseInsensitive)==0) {
            try {
                loadTrack(files[i]);
                continue;
            } catch(nac::pack::UnsupportedWaveEncoding const&) {
                // Decode alternate WAV encodings using the same backend as compressed audio.
            }
        }
        auto const wave=stage.filePath(QString("track-%1.wav").arg(i));
        runTool("ffmpeg","third_party/ffmpeg",
                {"-nostdin","-hide_banner","-loglevel","error","-protocol_whitelist","file",
                 "-i",files[i],"-map","0:a:0","-ac","2","-ar","48000","-c:a","pcm_s16le",wave},
                stage.absolutePath());
        loadTrack(wave);
    }
    std::vector<std::uint8_t> bytes;
    std::string error;
    if(!nac::encode(tracks,bytes,error))fail(QString::fromStdString(error));
    QFile file(output);
    if(!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
       file.write(reinterpret_cast<char const*>(bytes.data()),qsizetype(bytes.size()))!=
           qsizetype(bytes.size()) || !file.flush())
        fail("Cannot write NAC.");
    file.close();
    std::vector<nac::TrackInfo> verified;
    if(!nac::inspect_file(native(output),verified,error) || verified.size()!=tracks.size())
        fail("NAC validation failed. "+QString::fromStdString(error));
}

QString suffixFor(Kind kind, bool singleDrawable, QString const& source) {
    switch(kind) {
    case Kind::texture:return "ytd";
    case Kind::model:return singleDrawable?"ydr":"ydd";
    case Kind::audio:return "nac";
    case Kind::properties:return "ytyp";
    default:return QFileInfo(source).suffix();
    }
}

QString importPack(Kind kind, QStringList const& files, QString const& name,
                   Options const& options) {
    QString const suffix=suffixFor(kind,options.singleDrawable && files.size()==1,files.front());
    bool const copy=kind==Kind::nativeAsset || kind==Kind::loose;
    QString const outputName=!options.dictionary.isEmpty()?QFileInfo(options.dictionary).fileName():
        copy?QFileInfo(files.front()).fileName():name+"."+suffix;
    QDir const outputDir(options.destination);
    bool const update=!options.dictionary.isEmpty();
    auto const output=update?options.dictionary:outputDir.filePath(outputName);
    if(!update && (QFileInfo::exists(output) || QFileInfo(output).isSymLink()))
        fail(outputName+" already exists.");
    QTemporaryDir temporary(outputDir.filePath(".northstar-import-XXXXXX"));
    if(!temporary.isValid())fail("Cannot create the import staging directory.");
    QDir const stage(temporary.path());
    auto const cooked=stage.filePath(outputName);

    QStringList names;
    for(auto const& file:files) {
        auto const base=entryName(QFileInfo(file).completeBaseName());
        QString candidate=base;
        for(int i=2;names.contains(candidate);++i)
            candidate=base+"_"+QString::number(i);
        names.append(candidate);
    }

    if(kind==Kind::texture) {
        QStringList args{"pack"};
        for(qsizetype i=0;i<files.size();++i)
            args << "--texture" << names[i]+"="+files[i];
        args << "--output" << cooked;
        runTool("northstar-ytd-packer","first_party/northstar/ytd_packer",args,stage.absolutePath());
        if(shared_assets::load_textures_from_bytes(bytesOf(cooked),outputName.toStdString())
               .size()!=static_cast<std::size_t>(files.size()))
            fail("YTD texture count mismatch.");
    } else if(kind==Kind::model) {
        QStringList args{"pack"};
        for(qsizetype i=0;i<files.size();++i) {
            auto source=files[i];
            auto const suffix=QFileInfo(source).suffix().toLower();
            QFile fbx(source);
            bool const binaryFbx=suffix=="fbx" && fbx.open(QIODevice::ReadOnly) &&
                fbx.peek(21).startsWith("Kaydara FBX Binary");
            fbx.close();
            if(binaryFbx) {
                auto const converted=stage.filePath(names[i]);
                runTool("FBX2glTF","third_party/fbx2gltf",
                        {"-b","-i",source,"-o",converted},stage.absolutePath());
                source=converted+".glb";
            } else if(names[i]!=entryName(QFileInfo(source).completeBaseName())) {
                fail("Choose unique model base names before grouping. Duplicate: "+
                     QFileInfo(source).fileName());
            }
            args << "--input" << source;
        }
        if(files.size()==1)args << "--entry" << names.front();
        args << "--output" << cooked;
        runTool("northstar-ydd-packer","first_party/northstar/ydd_packer",args,stage.absolutePath());
        if(shared_assets::load_drawables_from_bytes(bytesOf(cooked),outputName.toStdString())
               .size()!=static_cast<std::size_t>(files.size()))
            fail("Drawable count mismatch.");
    } else if(kind==Kind::audio) {
        encodeAudio(files,names,cooked,stage);
    } else if(kind==Kind::properties) {
        runTool("northstar-ytyp-packer","first_party/northstar/ytyp_packer",
                {"compile","--root",QFileInfo(files.front()).absolutePath(),
                 "--input",files.front(),"--output",cooked},stage.absolutePath());
        runTool("northstar-ytyp-packer","first_party/northstar/ytyp_packer",
                {"inspect","--input",cooked},stage.absolutePath());
    } else if(!QFile::copy(files.front(),cooked)) {
        fail("Unable to copy "+outputName+".");
    }
    if(update) {
        QLockFile lock(outputDir.filePath("."+outputName+".northstar-import.lock"));
        if(!lock.tryLock(1500))fail("This dictionary is busy.");
        QFileInfo const current(output);
        if(!current.isFile() || current.isSymLink() ||
           current.canonicalFilePath()!=output)
            fail("The destination dictionary moved during import.");
        if(kind==Kind::model) {
            auto const stem=current.completeBaseName();
            if(outputDir.exists(stem+".skeleton.ymt") || outputDir.exists(stem+".ymt"))
                fail("This YDD has a companion rig. Import into a separate YDD to preserve its bindings.");
        }
        auto const original=bytesOf(output);
        auto const result=appendDictionary(original,bytesOf(cooked),outputName.toStdString());
        if(QFileInfo(output).isSymLink() || bytesOf(output)!=original)
            fail("The dictionary changed during import. Retry against the current file.");
        QSaveFile commit(output);
        commit.setDirectWriteFallback(false);
        if(!commit.open(QIODevice::WriteOnly) ||
           commit.write(reinterpret_cast<char const*>(result.data()),qsizetype(result.size()))!=
               qsizetype(result.size()) || !commit.commit())
            fail("Unable to update "+outputName+" atomically. "+commit.errorString());
    } else if(!QFileInfo(cooked).isFile() || !QFile::rename(cooked,output)) {
        fail("Unable to publish "+outputName+". Check access and filename conflicts.");
    }
    return outputName;
}
} // namespace

QStringList localFiles(QMimeData const& mime) {
    QStringList paths;
    for(auto const& url:mime.urls())
        if(url.isLocalFile())paths.append(url.toLocalFile());
    paths.removeDuplicates();
    return paths;
}

bool supported(QString const& file) {return kindFor(file)!=Kind::unsupported;}

bool appendable(QString const& file) {
    auto const ext=QFileInfo(file).suffix().toLower();
    return ext=="ytd" || ext=="ydd" || ext=="nac";
}

bool compatible(QString const& source,QString const& dictionary) {
    if(dictionary.isEmpty())return supported(source);
    auto const ext=QFileInfo(dictionary).suffix().toLower();
    auto const kind=kindFor(source);
    return (ext=="ytd" && kind==Kind::texture) ||
        (ext=="ydd" && kind==Kind::model) ||
        (ext=="nac" && kind==Kind::audio);
}

QString destination(QString const& root, QString const& relativeFolder) {
    if(root.isEmpty() || QDir::isAbsolutePath(relativeFolder))return {};
    auto const base=QFileInfo(root).canonicalFilePath();
    if(base.isEmpty())return {};
    auto const target=QFileInfo(QDir(base).filePath(relativeFolder));
    if(!target.isDir() &&
       (!target.isFile() || target.isSymLink() || !appendable(target.fileName())))return {};
    auto const resolved=target.canonicalFilePath();
    auto const relative=QDir(base).relativeFilePath(resolved);
    if(resolved.isEmpty() || QDir::isAbsolutePath(relative) ||
       relative==".." || relative.startsWith("../"))return {};
    return resolved;
}

QString fileFilter() {
    return "All Files (*);;Textures and Models (*.png *.jpg *.jpeg *.bmp *.tga *.dds "
           "*.obj *.fbx *.glb *.gltf);;Audio (*.wav *.ogg *.opus *.flac *.mp3 *.aif *.aiff *.m4a)";
}

Result run(QStringList const& sources, Options const& options) {
    Result result;
    QMap<Kind,QStringList> groups;
    bool const update=!options.dictionary.isEmpty();
    auto process=[&](Kind kind,QStringList const& files,QString const& name) {
        try {
            auto const output=importPack(kind,files,name,options);
            (update?result.updated:result.created).append(output);
        } catch(std::exception const& error) {
            result.errors.append(name+": "+QString::fromUtf8(error.what()));
        }
    };
    for(auto const& source:sources) {
        QFileInfo const file(source);
        auto const kind=kindFor(source);
        if(!file.isFile() || !file.isReadable()) {
            result.errors.append(file.fileName()+": source file cannot be read.");
            continue;
        }
        if(!compatible(source,options.dictionary)) {
            result.errors.append(file.fileName()+": format is incompatible with the destination.");
            continue;
        }
        auto const grouped=kind==Kind::texture || kind==Kind::model || kind==Kind::audio;
        if(update || (grouped && !options.groupName.isEmpty()))
            groups[kind].append(file.absoluteFilePath());
        else {
            auto name=file.completeBaseName();
            if(kind==Kind::properties)name.chop(5); // Remove the remaining .ytyp.
            process(kind,{file.absoluteFilePath()},entryName(name));
        }
    }
    for(auto it=groups.cbegin();it!=groups.cend();++it) {
        auto const name=update?QFileInfo(options.dictionary).completeBaseName():
            entryName(options.groupName);
        process(it.key(),it.value(),name);
    }
    return result;
}
} // namespace northstarcpp::editor::content_import
