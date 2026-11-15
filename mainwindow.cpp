#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>

static const QString starter = QStringLiteral("#include <iostream>\n\nint main()\n{\n    // Напишите решение здесь\n\n    return 0;\n}\n");
static QString shown(const QString &s) { return s.isEmpty() ? QStringLiteral("(пусто)") : s; }

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);
    tasks = {
        {QStringLiteral("1. Сумма двух чисел"), QStringLiteral("Даны два целых числа от -1000000 до 1000000. Выведите их сумму.\n\nВвод: 2 3\nВывод: 5"), {{"2 3\n", "5"}, {"-5 10\n", "5"}, {"0 0\n", "0"}, {"-1000000 -1000000\n", "-2000000"}}},
        {QStringLiteral("2. Чётное или нечётное"), QStringLiteral("Дано целое число от -1000000 до 1000000. Если оно чётное, выведите even, иначе odd.\n\nВвод: 7\nВывод: odd"), {{"7\n", "odd"}, {"0\n", "even"}, {"-4\n", "even"}, {"-3\n", "odd"}}},
        {QStringLiteral("3. Максимум из трёх"), QStringLiteral("Даны три целых числа от -1000000 до 1000000. Выведите наибольшее из них.\n\nВвод: 4 10 7\nВывод: 10"), {{"4 10 7\n", "10"}, {"-5 -2 -9\n", "-2"}, {"3 3 3\n", "3"}, {"0 -1 -2\n", "0"}}}
    };
    for (const Task &task : tasks) ui->taskList->addItem(task.title);
    QString compiler = QSettings().value("compiler").toString();
    if (compiler.isEmpty()) compiler = QStandardPaths::findExecutable("g++");
    ui->compilerEdit->setText(compiler);
    connect(ui->taskList, &QListWidget::currentRowChanged, this, &MainWindow::selectTask);
    connect(ui->codeEditor, &QPlainTextEdit::textChanged, this, &MainWindow::saveDraft);
    connect(ui->compilerEdit, &QLineEdit::textChanged, this, [](const QString &s) { QSettings().setValue("compiler", s); });
    connect(ui->compilerButton, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Выберите компилятор g++"), ui->compilerEdit->text(), QStringLiteral("Программы (*.exe);;Все файлы (*)"));
        if (!file.isEmpty()) ui->compilerEdit->setText(QDir::toNativeSeparators(file));
    });
    connect(ui->resetButton, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, QStringLiteral("Сбросить код"), QStringLiteral("Заменить решение этой задачи начальной заготовкой?")) == QMessageBox::Yes)
            ui->codeEditor->setPlainText(starter);
    });
    connect(ui->checkButton, &QPushButton::clicked, this, &MainWindow::check);
    connect(&process, &QProcess::readyReadStandardOutput, this, &MainWindow::collect);
    connect(&process, &QProcess::readyReadStandardError, this, &MainWindow::collect);
    connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, &MainWindow::finished);
    connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && stage != Stage::Idle) finish(QStringLiteral("Не удалось запустить процесс: ") + process.errorString());
    });
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, this, [this] { timedOut = true; process.kill(); });
    ui->taskList->setCurrentRow(0);
}
MainWindow::~MainWindow() {
    timer.stop();
    if (process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(1000); }
    delete ui;
}
void MainWindow::saveDraft() {
    if (selected >= 0) QSettings().setValue(QString("drafts/%1").arg(selected), ui->codeEditor->toPlainText());
}
void MainWindow::selectTask(int row) {
    if (row < 0 || row >= tasks.size()) return;
    saveDraft(); selected = row;
    ui->taskDescription->setPlainText(tasks[row].description);
    ui->codeEditor->setPlainText(QSettings().value(QString("drafts/%1").arg(row), starter).toString());
    ui->resultOutput->setPlainText(QStringLiteral("Напишите решение и нажмите «Проверить»."));
}
void MainWindow::check() {
    if (stage != Stage::Idle || selected < 0) return;
    QString compiler = ui->compilerEdit->text().trimmed();
    if (!QFileInfo(compiler).isAbsolute()) compiler = QStandardPaths::findExecutable(compiler);
    if (compiler.isEmpty() || !QFileInfo(compiler).isFile()) { ui->resultOutput->setPlainText(QStringLiteral("Компилятор не найден. Выберите файл g++.exe.")); return; }
    temp = std::make_unique<QTemporaryDir>();
    if (!temp->isValid()) { finish(QStringLiteral("Не удалось создать временную папку.")); return; }
    const QString sourcePath = temp->filePath("solution.cpp");
#ifdef Q_OS_WIN
    executable = temp->filePath("solution.exe");
#else
    executable = temp->filePath("solution");
#endif
    QFile file(sourcePath); const QByteArray code = ui->codeEditor->toPlainText().toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(code) != code.size()) { finish(QStringLiteral("Не удалось сохранить код.")); return; }
    file.close();
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("PATH", QFileInfo(compiler).absolutePath() + QDir::listSeparator() + env.value("PATH"));
    process.setProcessEnvironment(env); process.setWorkingDirectory(temp->path());
    active = selected; testIndex = 0; output.clear(); errors.clear(); timedOut = false; outputExceeded = false;
    ui->taskList->setEnabled(false); ui->checkButton->setEnabled(false); ui->resetButton->setEnabled(false);
    ui->compilerEdit->setEnabled(false); ui->compilerButton->setEnabled(false); ui->codeEditor->setReadOnly(true);
    stage = Stage::Compile; ui->resultOutput->setPlainText(QStringLiteral("Компиляция..."));
    process.start(compiler, {"-std=c++17", sourcePath, "-o", executable}); timer.start(30000);
}
void MainWindow::runTest() {
    if (testIndex >= tasks[active].tests.size()) { finish(QStringLiteral("Решение принято! Пройдено тестов: %1 из %1.").arg(testIndex)); return; }
    output.clear(); errors.clear(); timedOut = false; outputExceeded = false; stage = Stage::Test;
    ui->resultOutput->setPlainText(QStringLiteral("Проверка: тест %1 из %2...").arg(testIndex + 1).arg(tasks[active].tests.size()));
    process.start(executable, {}); process.write(tasks[active].tests[testIndex].input.toUtf8()); process.closeWriteChannel(); timer.start(3000);
}
void MainWindow::collect() {
    output += process.readAllStandardOutput(); errors += process.readAllStandardError();
    if (output.size() + errors.size() > 65536 && !outputExceeded) { outputExceeded = true; process.kill(); }
}
void MainWindow::finished(int code, QProcess::ExitStatus status) {
    if (stage == Stage::Idle) return;
    timer.stop(); collect();
    if (timedOut) { finish(stage == Stage::Compile ? QStringLiteral("Превышено время компиляции (30 с).") : QStringLiteral("Тест %1: превышено время выполнения (3 с).").arg(testIndex + 1)); return; }
    if (outputExceeded) { finish(QStringLiteral("Превышен лимит вывода (64 КБ).")); return; }
    if (status != QProcess::NormalExit || code != 0) {
        finish((stage == Stage::Compile ? QStringLiteral("Ошибка компиляции") : QStringLiteral("Тест %1: ошибка выполнения (код %2)").arg(testIndex + 1).arg(code)) + "\n\n" + shown(QString::fromLocal8Bit(errors).trimmed())); return;
    }
    if (stage == Stage::Compile) { runTest(); return; }
    const TestCase &test = tasks[active].tests[testIndex]; const QString actual = QString::fromUtf8(output).trimmed();
    if (actual != test.expected.trimmed()) { finish(QStringLiteral("Тест %1 из %2: неверный ответ.\n\nВвод:\n%3\n\nОжидалось:\n%4\n\nПолучено:\n%5").arg(testIndex + 1).arg(tasks[active].tests.size()).arg(test.input.trimmed(), test.expected, shown(actual))); return; }
    ++testIndex; runTest();
}
void MainWindow::finish(const QString &message) {
    timer.stop(); stage = Stage::Idle; ui->taskList->setEnabled(true); ui->checkButton->setEnabled(true); ui->resetButton->setEnabled(true);
    ui->compilerEdit->setEnabled(true); ui->compilerButton->setEnabled(true); ui->codeEditor->setReadOnly(false);
    ui->resultOutput->setPlainText(message); temp.reset();
}
