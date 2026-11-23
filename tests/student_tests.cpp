// Your M5 tests (specification, Section 6).

#include "aiws/build_manager.hpp"
#include "aiws/document.hpp"
#include "aiws/workspace.hpp"

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTimer>

#include "aiws/gui/main_window.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryFile>

#include <memory>
#include <vector>


class BuildManagerTests : public QObject {
    Q_OBJECT

    private:
    static QString currentStatus(aiws::gui::MainWindow& window) {
        const auto labels = window.findChildren<QLabel*>();

        for (QLabel* label : labels) {
            if (label->text().startsWith("Current status:")) {
                return label->text();
            }
        }

        return {};
    }

    static bool createTempDocument(QTemporaryFile& file) {
        if (!file.open()) {
            return false;
        }

        file.write("alpha beta gamma delta epsilon");
        file.flush();
        file.close();

        return true;
    }

// ---- helpers for the M5 tests ---------------------------------------

    // Number of threads in this process (Linux /proc, as in the course container).
    static int threadCount() {
        return QDir(QStringLiteral("/proc/self/task"))
            .entryList(QDir::Dirs | QDir::NoDotAndDotDot)
            .size();
    }

    static aiws::Workspace makeWorkspace(int documents) {
        aiws::Workspace workspace;
        for (int i = 0; i < documents; ++i) {
            workspace.add_document(aiws::Document(
                "doc" + std::to_string(i), "doc" + std::to_string(i) + ".txt",
                "alpha beta gamma delta epsilon"));
        }
        return workspace;
    }

    static QPushButton* findButton(aiws::gui::MainWindow& window,
                                   const QString& text) {
        for (QPushButton* button : window.findChildren<QPushButton*>()) {
            if (button->text() == text) {
                return button;
            }
        }
        return nullptr;
    }

    static QString corpusStatus(aiws::gui::MainWindow& window) {
        for (QLabel* label : window.findChildren<QLabel*>()) {
            if (label->text().startsWith("Corpus:")) {
                return label->text();
            }
        }
        return {};
    }

    using TempFiles = std::vector<std::unique_ptr<QTemporaryFile>>;

    // Adds `count` distinct temporary documents; the files must outlive the window.
    static bool addTempDocuments(aiws::gui::MainWindow& window,
                                 TempFiles& files, int count) {
        for (int i = 0; i < count; ++i) {
            files.push_back(std::make_unique<QTemporaryFile>());
            if (!createTempDocument(*files.back()) ||
                !window.add_document_file(files.back()->fileName())) {
                return false;
            }
        }
        return true;
    }

    static bool invokeSlot(aiws::gui::MainWindow& window, const char* name) {
        return QMetaObject::invokeMethod(&window, name, Qt::DirectConnection);
    }

private slots:

    void shutdownWhenIdleCompletes() {
        aiws::BuildManager manager;

        manager.shutdown();

        QVERIFY(!manager.is_building());
    }

    void successfulBuild() {
        aiws::Workspace workspace;
        workspace.add_document(
            aiws::Document("doc1", "test.txt",
                           "alpha beta gamma delta epsilon"));

        aiws::BuildManager manager;
        QSignalSpy finished(
            &manager, &aiws::BuildManager::build_finished);

        QVERIFY(finished.isValid());
        QVERIFY(manager.start_build(workspace, 0, 7));

        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);

        const QList<QVariant> result_signal = finished.takeFirst();
        QVERIFY(result_signal.at(0).toBool());

        QCOMPARE(manager.completed_version(), std::size_t(7));

        auto result = manager.take_result();
        QVERIFY(result != nullptr);
        QVERIFY(result->chunk_count() > 0);
        QVERIFY(!manager.is_building());
    }

    void duplicateDocumentCausesFailure() {
        aiws::Workspace workspace;

        // Duplicate IDs cause ProcessingCore::rebuild() to throw.
        workspace.add_document(
            aiws::Document("duplicate", "first.txt", "alpha beta"));

        workspace.add_document(
            aiws::Document("duplicate", "second.txt", "gamma delta"));

        aiws::BuildManager manager;
        QSignalSpy finished(
            &manager, &aiws::BuildManager::build_finished);

        QVERIFY(manager.start_build(workspace, 0, 1));

        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);

        const QList<QVariant> result_signal = finished.takeFirst();

        QVERIFY(!result_signal.at(0).toBool());
        QVERIFY(!result_signal.at(1).toString().isEmpty());
        QVERIFY(!manager.is_building());
        QVERIFY(manager.take_result() == nullptr);
    }

    void rejectsConcurrentBuild() {
        aiws::Workspace workspace;
        workspace.add_document(
            aiws::Document("doc1", "test.txt", "alpha beta gamma"));

        aiws::BuildManager manager;
        QSignalSpy finished(
            &manager, &aiws::BuildManager::build_finished);

        QVERIFY(manager.start_build(workspace, 150, 1));

        // A second build must not start while the first is running.
        QVERIFY(!manager.start_build(workspace, 0, 2));

        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
        QVERIFY(finished.takeFirst().at(0).toBool());
    }

        void cancelledBuildDoesNotBlockReplacement() {
        aiws::Workspace workspace;
        workspace.add_document(
            aiws::Document("doc1", "test.txt", "alpha beta gamma"));

        aiws::BuildManager manager;
        QSignalSpy finished(
            &manager, &aiws::BuildManager::build_finished);

        // Start a build with a long simulated delay.
        QVERIFY(manager.start_build(workspace, 2000, 1));

        // Give the worker time to pick up the first request.
        QTest::qWait(50);

        // Cancel it and immediately submit a replacement.
        manager.cancel_build();
        QVERIFY(!manager.is_building());
        QVERIFY(manager.start_build(workspace, 0, 2));

        // The replacement must not wait for the obsolete build's delay.
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 1000);

        QVERIFY(finished.takeFirst().at(0).toBool());
        QCOMPARE(manager.completed_version(), std::size_t(2));
        QVERIFY(!manager.is_building());
    }

    void eventLoopRemainsResponsive() {
        aiws::Workspace workspace;
        workspace.add_document(
            aiws::Document("doc1", "test.txt", "alpha beta gamma"));

        aiws::BuildManager manager;
        QSignalSpy finished(
            &manager, &aiws::BuildManager::build_finished);

        bool timer_fired = false;

        QVERIFY(manager.start_build(workspace, 300, 1));

        // This timer should fire while the worker is still building.
        QTimer::singleShot(20, this, [&timer_fired]() {
            timer_fired = true;
        });

        QTRY_VERIFY_WITH_TIMEOUT(timer_fired, 1000);
        QVERIFY(manager.is_building());

        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
        QVERIFY(finished.takeFirst().at(0).toBool());
    }

    void repeatedBuildsWork() {
        aiws::Workspace workspace;
        workspace.add_document(
            aiws::Document("doc1", "test.txt", "alpha beta gamma"));

        aiws::BuildManager manager;

        for (std::size_t version = 1; version <= 3; ++version) {
            QSignalSpy finished(
                &manager, &aiws::BuildManager::build_finished);

            QVERIFY(manager.start_build(workspace, 0, version));

            QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
            QVERIFY(finished.takeFirst().at(0).toBool());

            QCOMPARE(manager.completed_version(), version);
            QVERIFY(manager.take_result() != nullptr);
            QVERIFY(!manager.is_building());
        }
    }



    void unreadableFileIsRejected() {
        aiws::gui::MainWindow window;

        QTemporaryFile temp;
        QVERIFY(temp.open());

        const QString missing_path = temp.fileName();
        temp.close();

        QVERIFY(QFile::remove(missing_path));

        QVERIFY(!window.add_document_file(missing_path));

        // An empty workspace must not allow a corpus build.
        QVERIFY(QMetaObject::invokeMethod(
            &window, "build_corpus", Qt::DirectConnection));

        QVERIFY(currentStatus(window).contains(
            "Add documents before building"));
    }

    void addingDocumentDuringBuildRejectsStaleResult() {
        aiws::gui::MainWindow window;
        window.set_build_delay_ms(300);

        QTemporaryFile first;
        QVERIFY(createTempDocument(first));
        QVERIFY(window.add_document_file(first.fileName()));

        // Start the background build.
        QVERIFY(QMetaObject::invokeMethod(
            &window, "build_corpus", Qt::DirectConnection));

        // Change the workspace before the build completes.
        QTemporaryFile second;
        QVERIFY(createTempDocument(second));
        QVERIFY(window.add_document_file(second.fileName()));

        // The first build must be rejected as outdated.
        QTRY_VERIFY_WITH_TIMEOUT(
            currentStatus(window).contains("Workspace changed"),
            3000);

        // The application must allow a fresh build.
        window.set_build_delay_ms(0);

        QVERIFY(QMetaObject::invokeMethod(
            &window, "build_corpus", Qt::DirectConnection));

        QTRY_VERIFY_WITH_TIMEOUT(
            currentStatus(window).contains("Corpus built successfully"),
            3000);
    }

    void resetDuringBuildRejectsStaleResult() {
        aiws::gui::MainWindow window;
        window.set_build_delay_ms(300);

        QTemporaryFile document;
        QVERIFY(createTempDocument(document));
        QVERIFY(window.add_document_file(document.fileName()));

        // Start the background build.
        QVERIFY(QMetaObject::invokeMethod(
            &window, "build_corpus", Qt::DirectConnection));

        // Reset while the worker is still building.
        QVERIFY(QMetaObject::invokeMethod(
            &window, "reset_application", Qt::DirectConnection));

        // The old build must not become the current corpus.
        QTRY_VERIFY_WITH_TIMEOUT(
            currentStatus(window).contains("Workspace changed"),
            3000);

        // Reset should leave an empty workspace.
        QVERIFY(QMetaObject::invokeMethod(
            &window, "build_corpus", Qt::DirectConnection));

        QVERIFY(currentStatus(window).contains(
            "Add documents before building"));
    }

    void windowCanBeDestroyedDuringBuild() {
        auto* window = new aiws::gui::MainWindow();
        window->set_build_delay_ms(200);

        QTemporaryFile document;
        QVERIFY(createTempDocument(document));
        QVERIFY(window->add_document_file(document.fileName()));

        QVERIFY(QMetaObject::invokeMethod(
            window, "build_corpus", Qt::DirectConnection));

        // Destruction must join the worker safely.
        delete window;

        QVERIFY(true);
    }

    // =====================================================================
    // R1: one worker thread for every build
    // =====================================================================

    void r1_allBuildsShareOneWorkerThread() {
        const aiws::Workspace workspace = makeWorkspace(1);
        aiws::BuildManager manager;
        QSignalSpy finished(&manager, &aiws::BuildManager::build_finished);

        // First build: afterwards the worker exists, so this is the baseline.
        QVERIFY(manager.start_build(workspace, 0, 1));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
        const int baseline = threadCount();

        // Completed, cancelled, and replaced builds must not add threads.
        for (std::size_t version = 2; version <= 7; ++version) {
            QVERIFY(manager.start_build(workspace, version % 2 == 0 ? 100 : 0,
                                        version));
            if (version % 2 == 0) {
                QTest::qWait(20);
                manager.cancel_build();
            } else {
                QTRY_VERIFY_WITH_TIMEOUT(!manager.is_building(), 3000);
            }
            QCOMPARE(threadCount(), baseline);
        }
    }

    // =====================================================================
    // R2: handoff without waiting; outdated requests are never built
    // =====================================================================

    void r2_startBuildDoesNotWaitForTheBuild() {
        const aiws::Workspace workspace = makeWorkspace(6);
        aiws::BuildManager manager;

        QElapsedTimer timer;
        timer.start();
        QVERIFY(manager.start_build(workspace, 500, 1));  // build takes ~3 s
        QVERIFY(timer.elapsed() < 200);
        QVERIFY(manager.is_building());

        manager.cancel_build();
    }

    void r2_outdatedRequestIsNeverBuilt() {
        aiws::BuildManager manager;
        QSignalSpy finished(&manager, &aiws::BuildManager::build_finished);

        QVERIFY(manager.start_build(makeWorkspace(1), 400, 1));
        QTest::qWait(50);
        manager.cancel_build();                              // version 1 is outdated
        QVERIFY(manager.start_build(makeWorkspace(2), 0, 2));

        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 1500);
        QTest::qWait(700);  // longer than version 1 would have needed

        QCOMPARE(finished.count(), 1);                       // only version 2 reports
        QCOMPARE(manager.completed_version(), std::size_t(2));
        QVERIFY(manager.take_result() != nullptr);
    }

    // =====================================================================
    // R3: cancellation is prompt and the cancelled build never becomes current
    // =====================================================================

    void r3_cancelStopsWithinAboutOneDocument() {
        aiws::BuildManager manager;
        QSignalSpy finished(&manager, &aiws::BuildManager::build_finished);

        QVERIFY(manager.start_build(makeWorkspace(6), 500, 1));  // ~3 s if left alone
        QTest::qWait(50);

        QElapsedTimer timer;
        timer.start();
        manager.cancel_build();
        QVERIFY(manager.start_build(makeWorkspace(1), 0, 2));

        // The replacement must not wait for the five remaining documents.
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 1500);
        QVERIFY(timer.elapsed() < 1000);
        QCOMPARE(manager.completed_version(), std::size_t(2));
    }

    void r3_cancelledBuildNeverBecomesCurrent() {
        aiws::BuildManager manager;
        QSignalSpy finished(&manager, &aiws::BuildManager::build_finished);

        QVERIFY(manager.start_build(makeWorkspace(2), 200, 1));
        QTest::qWait(50);
        manager.cancel_build();

        QTest::qWait(700);  // longer than the whole cancelled build

        QCOMPARE(finished.count(), 0);
        QVERIFY(manager.take_result() == nullptr);
        QCOMPARE(manager.completed_version(), std::size_t(0));
        QVERIFY(!manager.is_building());
    }

    // =====================================================================
    // R4: Exit stops the worker promptly, mid-build as well as idle
    // =====================================================================

    void r4_shutdownDuringBuildIsPrompt() {
        aiws::BuildManager manager;
        QVERIFY(manager.start_build(makeWorkspace(6), 500, 1));
        QTest::qWait(50);

        QElapsedTimer timer;
        timer.start();
        manager.shutdown();                                  // joins the worker

        QVERIFY(timer.elapsed() < 1000);                     // ~one document
        QVERIFY(!manager.is_building());
    }

    // =====================================================================
    // R5: State pattern behaviour, observed through the window
    // =====================================================================

    void state_readyBuildEntersBuildingThenReturnsToReady() {
        aiws::gui::MainWindow window;
        window.set_build_delay_ms(300);
        TempFiles files;
        QVERIFY(addTempDocuments(window, files, 1));

        QPushButton* build = findButton(window, "Build Corpus");
        QVERIFY(build != nullptr);
        QVERIFY(build->isEnabled());                         // Ready

        QVERIFY(invokeSlot(window, "build_corpus"));
        QVERIFY(!build->isEnabled());                        // Building
        QVERIFY(corpusStatus(window).contains("Building"));

        QTRY_VERIFY_WITH_TIMEOUT(
            currentStatus(window).contains("Corpus built successfully"), 3000);
        QVERIFY(build->isEnabled());                         // back to Ready
        QVERIFY(corpusStatus(window).contains("chunks"));
    }

    void state_buildingRejectsSecondBuildRequest() {
        aiws::gui::MainWindow window;
        window.set_build_delay_ms(300);
        TempFiles files;
        QVERIFY(addTempDocuments(window, files, 1));

        QVERIFY(invokeSlot(window, "build_corpus"));
        QVERIFY(invokeSlot(window, "build_corpus"));         // decision 1: rejected
        QVERIFY(currentStatus(window).contains("already running"));

        QTRY_VERIFY_WITH_TIMEOUT(
            currentStatus(window).contains("Corpus built successfully"), 3000);
    }

    void state_buildingWorkspaceChangeCancelsAndReturnsToReady() {
        aiws::gui::MainWindow window;
        window.set_build_delay_ms(2000);                     // would take 4 s
        TempFiles files;
        QVERIFY(addTempDocuments(window, files, 2));
        QVERIFY(invokeSlot(window, "build_corpus"));
        QTest::qWait(50);  // let the worker take the request: the build is running

        QVERIFY(addTempDocuments(window, files, 1));         // remote-add style change

        QVERIFY(currentStatus(window).contains("Workspace changed"));
        QVERIFY(corpusStatus(window).contains("Out of date"));
        QVERIFY(findButton(window, "Build Corpus")->isEnabled());  // Ready again

        // The next build must not wait for the cancelled one.
        window.set_build_delay_ms(0);
        QVERIFY(invokeSlot(window, "build_corpus"));
        QTRY_VERIFY_WITH_TIMEOUT(
            currentStatus(window).contains("Corpus built successfully"), 1500);
    }

    void state_buildingResetCancelsAndReturnsToReady() {
        aiws::gui::MainWindow window;
        window.set_build_delay_ms(2000);
        TempFiles files;
        QVERIFY(addTempDocuments(window, files, 2));
        QVERIFY(invokeSlot(window, "build_corpus"));
        QTest::qWait(50);  // let the worker take the request: the build is running

        QVERIFY(invokeSlot(window, "reset_application"));

        QVERIFY(currentStatus(window).contains("Workspace changed"));
        QVERIFY(!findButton(window, "Build Corpus")->isEnabled());  // empty workspace

        window.set_build_delay_ms(0);
        QVERIFY(addTempDocuments(window, files, 1));
        QVERIFY(invokeSlot(window, "build_corpus"));
        QTRY_VERIFY_WITH_TIMEOUT(
            currentStatus(window).contains("Corpus built successfully"), 1500);
    }

    void state_exitDuringBuildIsPromptAndIgnoresLaterEvents() {
        aiws::gui::MainWindow window;
        window.set_build_delay_ms(500);
        TempFiles files;
        QVERIFY(addTempDocuments(window, files, 6));         // ~3 s build
        QVERIFY(invokeSlot(window, "build_corpus"));
        QTest::qWait(50);

        QElapsedTimer timer;
        timer.start();
        window.request_exit_after_build();                   // Building -> Exiting
        QVERIFY(timer.elapsed() < 1000);                     // R4

        // Exiting ignores every event: the status text must not change.
        QVERIFY(addTempDocuments(window, files, 1));
        const QString before = currentStatus(window);
        QVERIFY(before.contains("Added"));
        QVERIFY(invokeSlot(window, "build_corpus"));
        QCOMPARE(currentStatus(window), before);
        QVERIFY(invokeSlot(window, "reset_application"));
        QCOMPARE(currentStatus(window), before);
    }

    void state_exitWhenIdleIsPrompt() {
        aiws::gui::MainWindow window;
        QElapsedTimer timer;
        timer.start();
        window.request_exit_after_build();                   // Ready -> Exiting
        QVERIFY(timer.elapsed() < 1000);
    }

};

QTEST_MAIN(BuildManagerTests)

#include "student_tests.moc"
