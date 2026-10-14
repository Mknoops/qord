#pragma once

#include <QObject>
#include <QString>

// Test-only backend: constructing any keychain job fails the regression test.
// This also ensures a regression cannot modify the developer's real keychain.
namespace QKeychain {
enum Error { NoError, EntryNotFound };

class Job : public QObject
{
    Q_OBJECT
public:
    explicit Job(const QString &) { qFatal("Mock mode constructed a keychain job"); }
    void setAutoDelete(bool) {}
    void setKey(const QString &) {}
    void setTextData(const QString &) {}
    void start() {}
    Error error() const { return NoError; }
    QString errorString() const { return {}; }
    QString textData() const { return {}; }
signals:
    void finished(Job *);
};

class WritePasswordJob : public Job { public: using Job::Job; };
class ReadPasswordJob : public Job { public: using Job::Job; };
class DeletePasswordJob : public Job { public: using Job::Job; };
} // namespace QKeychain
