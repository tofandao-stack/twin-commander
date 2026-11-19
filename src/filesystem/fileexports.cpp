#include "filesystem/pathencoding.h"
#include "filesystem/fileexports.h"
#include "ui/panels/directorymodel.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUuid>
#include <functional>
namespace FileExports {
using namespace NativeFs;
QByteArray selectionData(const std::vector<fs::path> &paths)
{
    QJsonArray names;for(const auto &path:paths)names.append(QString::fromLatin1(QByteArray::fromStdString(path.filename().native()).toBase64()));
    return QJsonDocument(QJsonObject{{"format","TwinCommander-selection-1"},{"namesBase64",names}}).toJson();
}
std::vector<fs::path> selectionPaths(const QByteArray &data, const fs::path &directory)
{
    QJsonParseError error;const auto doc=QJsonDocument::fromJson(data,&error);
    if(error.error!=QJsonParseError::NoError||doc.object().value("format")!="TwinCommander-selection-1")throw Error("Not a Twin Commander selection file");
    if(!doc.object().value("namesBase64").isArray())throw Error("Selection file has no filename list");
    const auto names=doc.object().value("namesBase64").toArray();if(names.size()>200000)throw Error("Selection file too large");
    std::vector<fs::path> result;
    for(const auto &name:names){const auto decoded=QByteArray::fromBase64Encoding(name.toString().toLatin1(),QByteArray::AbortOnBase64DecodingErrors);if(!decoded)throw Error("Invalid selection filename encoding");const auto raw=decoded.decoded.toStdString();fs::path path(raw);
        if(raw.empty()||raw.find('\0')!=std::string::npos||path.is_absolute()||path.has_parent_path()||path=="."||path=="..")throw Error("Selection contains an unsafe filename");
        result.push_back(directory/path);}
    return result;
}
void exportNew(const fs::path &path, const QByteArray &data)
{
    FileDescriptor parent(::open(path.parent_path().c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC));
    const auto temporary=".tc-export-"+QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    try{FileDescriptor output(::openat(parent.value,temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600));
        qsizetype offset=0;while(offset<data.size()){const auto size=::write(output.value,data.constData()+offset,data.size()-offset);if(size<0&&errno==EINTR)continue;check(static_cast<int>(size));if(!size)throw Error("Incomplete export write");offset+=size;}
        check(::fsync(output.value));renameExclusive(parent.value,temporary,path.filename().native());
    }catch(...){::unlinkat(parent.value,temporary.c_str(),0);throw;}
}
QByteArray readSmall(const fs::path &path, size_t limit)
{
    const auto before=inspect(path);if(!S_ISREG(before.st_mode)||before.st_size<0||static_cast<quint64>(before.st_size)>limit)throw Error("Requires a regular file within the size limit");
    FileDescriptor input(::open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));if(!same(before,inspectFd(input.value)))throw Error("File changed before reading");
    QByteArray bytes;std::array<char,65536> buffer{};while(true){const auto size=::read(input.value,buffer.data(),buffer.size());if(size<0&&errno==EINTR)continue;check(static_cast<int>(size));if(!size)break;if(static_cast<size_t>(bytes.size()+size)>limit)throw Error("File exceeds size limit");bytes.append(buffer.data(),size);}verify(path,input.value,before);return bytes;
}
QString fileList(const std::vector<fs::path> &paths,bool recursive,const std::shared_ptr<LocalFiles::Control> &control)
{
    QString output;size_t count=0;std::function<void(const fs::path &,int)> visit=[&](const fs::path &path,int depth){LocalFiles::checkpoint(control);if(++count>200000||depth>128||output.size()>8*1024*1024)throw Error("File-list limit exceeded");const auto st=inspect(path);output+=displayPath(path)+(S_ISDIR(st.st_mode)?"/":"")+"\n";++control->completed;if(recursive&&S_ISDIR(st.st_mode))for(const auto &child:fs::directory_iterator(path))visit(child.path(),depth+1);};
    for(const auto &path:paths)visit(path,0);
    return output;
}
static QByteArray digest(const fs::path &path,QCryptographicHash::Algorithm algorithm,const std::shared_ptr<LocalFiles::Control> &control)
{
    const auto before=inspect(path);if(!S_ISREG(before.st_mode))throw Error("Checksum requires regular files");FileDescriptor input(::open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));if(!same(before,inspectFd(input.value)))throw Error("File changed before hashing");QCryptographicHash hash(algorithm);std::array<char,128*1024> buffer{};
    while(true){LocalFiles::checkpoint(control);const auto size=::read(input.value,buffer.data(),buffer.size());if(size<0&&errno==EINTR)continue;check(static_cast<int>(size));if(!size)break;hash.addData(QByteArrayView(buffer.data(),size));control->bytes+=size;}verify(path,input.value,before);++control->completed;return hash.result().toHex();
}
QByteArray checksumManifest(const std::vector<fs::path> &paths,QCryptographicHash::Algorithm algorithm,const std::shared_ptr<LocalFiles::Control> &control)
{
    if(paths.empty()||paths.size()>100000)throw Error("Select 1 to 100000 regular files");
    QByteArray result;const auto parent=paths.front().parent_path();
    for(const auto &path:paths){const auto name=QByteArray::fromStdString(path.filename().native());if(path.parent_path()!=parent||name.contains('\n')||name.contains('\r')||name.contains('\\'))throw Error("Checksum export requires sibling files without newline or backslash names");result+=digest(path,algorithm,control)+"  "+name+"\n";}return result;
}
QString verifyManifest(const fs::path &manifest,const std::shared_ptr<LocalFiles::Control> &control)
{
    const auto data=readSmall(manifest);QStringList output;int valid=0,failed=0;
    for(auto line:data.split('\n')){LocalFiles::checkpoint(control);if(line.endsWith('\r'))line.chop(1);if(line.isEmpty())continue;const auto separator=line.indexOf(' ');const auto expected=line.left(separator);const auto name=line.mid(separator+2);const auto algorithm=expected.size()==32?QCryptographicHash::Md5:expected.size()==40?QCryptographicHash::Sha1:QCryptographicHash::Sha256;
        const bool hex=QRegularExpression("^[0-9a-fA-F]+$").match(QString::fromLatin1(expected)).hasMatch();fs::path relative(name.toStdString());
        if(separator<0||(expected.size()!=32&&expected.size()!=40&&expected.size()!=64)||!hex||line.size()<separator+3||(line[separator+1]!=' '&&line[separator+1]!='*')||name.contains('\0')||relative.is_absolute()||relative.has_parent_path()||relative=="."||relative=="..")throw Error("Unsupported or unsafe checksum manifest line (only sibling GNU MD5/SHA1/SHA256 entries supported)");
        try{const bool ok=digest(manifest.parent_path()/relative,algorithm,control)==expected.toLower();output<<(QString(ok?"OK  ":"FAILED  ")+displayPath(relative));ok?++valid:++failed;}catch(const Error &error){++failed;output<<displayPath(relative)+": "+QString::fromLocal8Bit(error.what());}}
    if(valid+failed==0)throw Error("Checksum manifest contains no entries");
    output<<QString("%1 verified; %2 failed").arg(valid).arg(failed);return output.join('\n');
}
}
