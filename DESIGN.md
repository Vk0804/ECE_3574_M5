# M4 Decision Record

#M5 Decision Record - 500 words 

See specification Section 8.

Varshitha Kodela 


1) I created the BuildManager class for M4 to move corpus building out of the GUI thread. MainWindow owns the Workspace, ProcessingCore, and BuildManager. When the user clicks Build Corpus, MainWindow::build_corpus() checks that there are documents in the workspace and that another build is not already running. It then passes a copy of the workspace, the build delay, and the current workspace version to BuildManager.

The BuildManager creates a separate std::thread to run ProcessingCore::rebuild(). This keeps the GUI responsive while the corpus is being built. When the build finishes, the worker sends the result back using QMetaObject::invokeMethod() with Qt::QueuedConnection. The completion handler runs on the GUI thread, where the application can safely update the widgets and handle the result. I also made sure that the worker thread is joined when the build finishes and when BuildManager is destroyed. I do not use detached threads.

For CR-1, I added the IpcServer class and the separate aiws_send program. IpcServer handles incoming requests from other programs, and aiws_send allows a user to send a file path to a running AI Workspace application. The server receives the request and passes the file to MainWindow::add_document_file(). This lets files added through IPC use the same process as files added through the GUI, instead of creating a separate way to add documents.

2) The main shared data I considered is the Completion object used by BuildManager. The worker thread writes the build result, success status, error message, and workspace version into this object. It then sends a queued callback to the GUI thread. The GUI thread reads the completion information when handle_completion() runs.

The worker finishes writing to the object before the queued callback handles it, so the GUI thread is not reading the fields while the worker is still changing them. The object is managed with std::shared_ptr, and the actual ProcessingCore result is managed with std::unique_ptr. The worker uses its own copy of the workspace, while the GUI thread continues to own and update the actual workspace and widgets.


3) Most of the M4 implementation followed the design I originally planned in DESIGN_NOTE.md. I kept the overall structure with MainWindow coordinating the application and BuildManager handling the background corpus build. I implemented the worker thread, completion handling, workspace version checking, and thread cleanup as part of that design.

The biggest change after the checkpoint was CR-1. The original design focused on actions happening inside the AI Workspace application, but CR-1 added a way for other programs to send files to it. I added the IpcServer class to receive these requests and created the separate aiws_send program to send file paths. I also updated main.cpp to support starting the application with a custom IPC socket path.




4) For CR-1, I added remote document adding through a Unix domain socket. I created the IpcServer class to listen for requests from other programs and added the separate aiws_send client program to send file paths to the running application.

I also added MainWindow::add_dicument_file(). when teh server receives a request to add a file, it passes the file path to this fucntion. This means files received through IPC go through the same process as files added using the GUI. 

I also updated the application startup code in main.cpp to support the --ipc-socket option and start the IPC server. I did not need to change the corpus-building logic, search functionality, or other processing components because IPC only changes how a document gets into the workspace. Once the document is added, the existing M4 version-checking and corpus-building logic handles the change.


5) AIWS_FAULT_STALE is located in mainWindow::on_build_finished() in the build-result validation logic. The stale build test checks that a build is rejected if the workspace changes or reset is cliked before the build finishes.

AIWS_FAULT_FAILURE is located in the build failure logic. the failure test checks that a failed buil is repoeted to teh GUI and that the application leabes the building state instead. 


6) I used AI to help me understand and plan the M4 and CR-1 implementations, work through different design approaches, and come up with test cases for the new classes. I also used AI to help clean up my grammar for DEISGN.md



Pair Programming: 
Partner Name : Gary Jia 
Meeting: Zoom 
Best form or communication : zoom 
Time: 1:30 pm
End of Session : ~ 2:10 pm

Gary proposed and implemented unreadable_remote_file_keeps_current_corpus, a test checking that a failed Remote Add does not invalidate an existing corpus. We reviewed how the test uses IpcFixture to build a corpus, sends a missing file through the actual aiws_send program, and verifies that the sender returns exit code 1 with an error message. We also checked that the original document, corpus actions, and search functionality remain available after the failed request.

I reviewed Gary's test and suggested changes to the checks, including waiting for the background build to finish and searching again after the rejected request to confirm the application remained usable.

I was the driver for the next part of the session. We reviewed my aiws_send implementation and discussed why a separate client program was needed to send file paths to the running AI Workspace application. Gary helped me work through the 4-byte length prefix design so the receiver knows exactly how many bytes to read for each request. We discussed why this was more reliable than using a delimiter, since file paths can contain spaces and other special characters. I implemented encode_length() and decode_length() to convert the length between an integer representation and the four bytes sent through the socket.

Gary also helped me understand why the client and server responsibilities needed to be separated into two classes rather than handled together. Based on this discussion, I kept aiws_send responsible for sending requests and later created IpcServer to create and manage the Unix domain socket, receive requests, and pass them to the application for processing.

As a pair we worked best over zooma nd sharing our screen while observing the otehr code and design. It worked out for us. 

As a pair, we worked best over Zoom by sharing our screens and reviewing each other's code and design decisions together. This made it easier to understand each other's approaches and work through problems together when we couldn't find a time to meet in person. 