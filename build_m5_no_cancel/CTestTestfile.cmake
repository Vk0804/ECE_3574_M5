# CMake generated Testfile for 
# Source directory: /workspace
# Build directory: /workspace/build_m5_no_cancel
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(student_tests "/workspace/build_m5_no_cancel/student_tests")
set_tests_properties(student_tests PROPERTIES  ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT "60" _BACKTRACE_TRIPLES "/workspace/CMakeLists.txt;68;add_test;/workspace/CMakeLists.txt;0;")
add_test(ipc_tests "/workspace/build_m5_no_cancel/ipc_tests")
set_tests_properties(ipc_tests PROPERTIES  ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT "60" _BACKTRACE_TRIPLES "/workspace/CMakeLists.txt;82;add_test;/workspace/CMakeLists.txt;0;")
