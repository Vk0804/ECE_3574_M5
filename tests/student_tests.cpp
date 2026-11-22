// Your M5 tests (specification, Section 6).

#include "aiws/build_manager.hpp"
#include "aiws/document.hpp"
#include "aiws/workspace.hpp"

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTimer>

#include "aiws/gui/main_window.hpp"

#include <QFile>
#include <QLabel>
#include <QTemporaryFile>

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

};

QTEST_MAIN(BuildManagerTests)

#include "student_tests.moc"
