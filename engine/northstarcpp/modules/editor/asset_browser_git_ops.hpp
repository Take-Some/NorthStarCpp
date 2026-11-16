#pragma once
// Safe, bounded Git subprocess contract for project-root asset operations.
// Never invokes a shell or constructs a shell command from file names.
#include <QFileInfo>
#include <QProcess>
#include <QStringList>
namespace northstarcpp::editor::git_ops {
struct Result {bool success=false;QString output;};
inline Result run(QString const& directory,QStringList args) {
    if(directory.isEmpty() || !QFileInfo(directory).isDir())
        return {false,"No project content root configured"};
    QProcess command;
    command.setProgram("git");
    args.prepend(directory);
    args.prepend("-C");
    command.setArguments(args);
    command.setWorkingDirectory(directory);
    command.start();
    if(!command.waitForStarted(2500))
        return {false,"Git unavailable: "+command.errorString()};
    if(!command.waitForFinished(5000)) {
        command.kill();
        command.waitForFinished(1000);
        return {false,"Git command timed out"};
    }
    QByteArray bytes=command.readAllStandardOutput();
    bytes+=command.readAllStandardError();
    if(bytes.size()>1024*1024)bytes.truncate(1024*1024);
    return {command.exitCode()==0 && command.exitStatus()==QProcess::NormalExit,
            QString::fromUtf8(bytes)};
}
}
