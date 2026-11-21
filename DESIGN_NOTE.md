# M4 Design Note
VARSHITHA KODELA
Due Wed Oct 28, 11:59 pm. See specification Section 7.

1. My components are 
- Mainwindow - Owns the Workspace, corpus states, and widgets and has a thread to the GUI 
- CorpusBuilder - starts a build, polls with QTimer and polls for the future and has a thread to the GUI
- run_build() - runs ProcessingCore::rebuild() on workspace copy, copies delay and has a thread to worker (std::async)

2. 

Not Built: 
- BUild clicked -> Building
Building: 
- finished, version matches -> Current 
- finished, Version Matches -> NotBuilt(result discraded)
- failed, Version matches -> NotBuilt 
- document added, Reset -> NotBuilt
Current: 
- document added, Reset -> NotBuilt

workspace_version_ increases on every add and every Reset. A built records it at start. 

3. 
- Document addd during a build. Version goes from 1 to 2, state becoems Notuitl, search,inspect,context are disabled at once. The build finihsed carrying version 1, and the GUI thread check fails, and the corpus is discraded. 
- A reset during a build - same version is discarded. the stop flag is set, Build stays diables until the timer sees the old task finish. Only one task ever exists. 
- Build throws - run_build() catches an exception and retruns teh message thorugh the future. The GUI shows "build faield and leaves the building. 
- Build clicked twice - build is diabled while the task is running


4. The user clicks on Build with2 documents and the version is 1. The GUI thread copies the Workspace into the task and strats std::async. The Worker owns that copy and nothing elese touches it. The worker creates a new processingCore as std::unique_ptr<ProcessingCore>, calls rebuild() on the copy, and puts the pointer in a buildResult with version 1. Only the worker wones it at this point. The result goes through the std::furutr, and ownership moves to the future's shared state. The timer slot on the GUi threat calls future.get() and takes ownership of the BuildResult. checks version 1 agsinst the current verison 1. The GUI thread move assigns the core into mainWindow::Processing_core Mainwindow now owns corpus 1, state is current and the chunk shows. 

5. let the worker read the live workspace and disable add document during builds.