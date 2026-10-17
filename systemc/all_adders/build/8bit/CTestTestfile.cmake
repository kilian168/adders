# CMake generated Testfile for 
# Source directory: /mnt/storage/bachelor/adders/systemc/all_adders/8bit
# Build directory: /mnt/storage/bachelor/adders/systemc/all_adders/build/8bit
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(rca_8bit_BUILD "/usr/bin/cmake" "--build" "/mnt/storage/bachelor/adders/systemc/all_adders/build" "--target" "rca_8bit_sctool")
set_tests_properties(rca_8bit_BUILD PROPERTIES  _BACKTRACE_TRIPLES "/home/kilian/Projects/systemc-compiler/lib/cmake/SVC/svc_target.cmake;248;add_test;/mnt/storage/bachelor/adders/systemc/all_adders/8bit/CMakeLists.txt;10;svc_target;/mnt/storage/bachelor/adders/systemc/all_adders/8bit/CMakeLists.txt;0;")
add_test(rca_8bit_SYN "/mnt/storage/bachelor/adders/systemc/all_adders/build/8bit/rca_8bit_sctool")
set_tests_properties(rca_8bit_SYN PROPERTIES  DEPENDS "rca_8bit_BUILD" WILL_FAIL "FALSE" _BACKTRACE_TRIPLES "/home/kilian/Projects/systemc-compiler/lib/cmake/SVC/svc_target.cmake;253;add_test;/mnt/storage/bachelor/adders/systemc/all_adders/8bit/CMakeLists.txt;10;svc_target;/mnt/storage/bachelor/adders/systemc/all_adders/8bit/CMakeLists.txt;0;")
add_test(cla_8bit_BUILD "/usr/bin/cmake" "--build" "/mnt/storage/bachelor/adders/systemc/all_adders/build" "--target" "cla_8bit_sctool")
set_tests_properties(cla_8bit_BUILD PROPERTIES  _BACKTRACE_TRIPLES "/home/kilian/Projects/systemc-compiler/lib/cmake/SVC/svc_target.cmake;248;add_test;/mnt/storage/bachelor/adders/systemc/all_adders/8bit/CMakeLists.txt;13;svc_target;/mnt/storage/bachelor/adders/systemc/all_adders/8bit/CMakeLists.txt;0;")
add_test(cla_8bit_SYN "/mnt/storage/bachelor/adders/systemc/all_adders/build/8bit/cla_8bit_sctool")
set_tests_properties(cla_8bit_SYN PROPERTIES  DEPENDS "cla_8bit_BUILD" WILL_FAIL "FALSE" _BACKTRACE_TRIPLES "/home/kilian/Projects/systemc-compiler/lib/cmake/SVC/svc_target.cmake;253;add_test;/mnt/storage/bachelor/adders/systemc/all_adders/8bit/CMakeLists.txt;13;svc_target;/mnt/storage/bachelor/adders/systemc/all_adders/8bit/CMakeLists.txt;0;")
add_test(ter_8bit_BUILD "/usr/bin/cmake" "--build" "/mnt/storage/bachelor/adders/systemc/all_adders/build" "--target" "ter_8bit_sctool")
set_tests_properties(ter_8bit_BUILD PROPERTIES  _BACKTRACE_TRIPLES "/home/kilian/Projects/systemc-compiler/lib/cmake/SVC/svc_target.cmake;248;add_test;/mnt/storage/bachelor/adders/systemc/all_adders/8bit/CMakeLists.txt;16;svc_target;/mnt/storage/bachelor/adders/systemc/all_adders/8bit/CMakeLists.txt;0;")
add_test(ter_8bit_SYN "/mnt/storage/bachelor/adders/systemc/all_adders/build/8bit/ter_8bit_sctool")
set_tests_properties(ter_8bit_SYN PROPERTIES  DEPENDS "ter_8bit_BUILD" WILL_FAIL "FALSE" _BACKTRACE_TRIPLES "/home/kilian/Projects/systemc-compiler/lib/cmake/SVC/svc_target.cmake;253;add_test;/mnt/storage/bachelor/adders/systemc/all_adders/8bit/CMakeLists.txt;16;svc_target;/mnt/storage/bachelor/adders/systemc/all_adders/8bit/CMakeLists.txt;0;")
add_test(sdtc_pm_8bit_BUILD "/usr/bin/cmake" "--build" "/mnt/storage/bachelor/adders/systemc/all_adders/build" "--target" "sdtc_pm_8bit_sctool")
set_tests_properties(sdtc_pm_8bit_BUILD PROPERTIES  _BACKTRACE_TRIPLES "/home/kilian/Projects/systemc-compiler/lib/cmake/SVC/svc_target.cmake;248;add_test;/mnt/storage/bachelor/adders/systemc/all_adders/8bit/CMakeLists.txt;19;svc_target;/mnt/storage/bachelor/adders/systemc/all_adders/8bit/CMakeLists.txt;0;")
add_test(sdtc_pm_8bit_SYN "/mnt/storage/bachelor/adders/systemc/all_adders/build/8bit/sdtc_pm_8bit_sctool")
set_tests_properties(sdtc_pm_8bit_SYN PROPERTIES  DEPENDS "sdtc_pm_8bit_BUILD" WILL_FAIL "FALSE" _BACKTRACE_TRIPLES "/home/kilian/Projects/systemc-compiler/lib/cmake/SVC/svc_target.cmake;253;add_test;/mnt/storage/bachelor/adders/systemc/all_adders/8bit/CMakeLists.txt;19;svc_target;/mnt/storage/bachelor/adders/systemc/all_adders/8bit/CMakeLists.txt;0;")
add_test(sdtc_ppm_8bit_BUILD "/usr/bin/cmake" "--build" "/mnt/storage/bachelor/adders/systemc/all_adders/build" "--target" "sdtc_ppm_8bit_sctool")
set_tests_properties(sdtc_ppm_8bit_BUILD PROPERTIES  _BACKTRACE_TRIPLES "/home/kilian/Projects/systemc-compiler/lib/cmake/SVC/svc_target.cmake;248;add_test;/mnt/storage/bachelor/adders/systemc/all_adders/8bit/CMakeLists.txt;22;svc_target;/mnt/storage/bachelor/adders/systemc/all_adders/8bit/CMakeLists.txt;0;")
add_test(sdtc_ppm_8bit_SYN "/mnt/storage/bachelor/adders/systemc/all_adders/build/8bit/sdtc_ppm_8bit_sctool")
set_tests_properties(sdtc_ppm_8bit_SYN PROPERTIES  DEPENDS "sdtc_ppm_8bit_BUILD" WILL_FAIL "FALSE" _BACKTRACE_TRIPLES "/home/kilian/Projects/systemc-compiler/lib/cmake/SVC/svc_target.cmake;253;add_test;/mnt/storage/bachelor/adders/systemc/all_adders/8bit/CMakeLists.txt;22;svc_target;/mnt/storage/bachelor/adders/systemc/all_adders/8bit/CMakeLists.txt;0;")
