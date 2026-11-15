#pragma once
#include <QMainWindow>
#include <QProcess>
#include <QTimer>
#include <QTemporaryDir>
#include <QVector>
#include <memory>
namespace Ui { class MainWindow; }
struct TestCase { QString input, expected; };
struct Task { QString title, description; QVector<TestCase> tests; };
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
private:
    enum class Stage { Idle, Compile, Test };
    void selectTask(int row);
    void saveDraft();
    void check();
    void runTest();
    void collect();
    void finished(int code, QProcess::ExitStatus status);
    void finish(const QString &message);
    Ui::MainWindow *ui;
    QVector<Task> tasks;
    QProcess process;
    QTimer timer;
    std::unique_ptr<QTemporaryDir> temp;
    QString executable;
    QByteArray output, errors;
    Stage stage = Stage::Idle;
    int selected = -1, active = -1, testIndex = 0;
    bool timedOut = false, outputExceeded = false;
};
