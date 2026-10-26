# CMake generated Testfile for 
# Source directory: /mnt/storage/bachelor/adders/systemc/all_adders/64bit
# Build directory: /mnt/storage/bachelor/adders/systemc/all_adders/build/64bit
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(rca_64bit_BUILD "/usr/bin/cmake" "--build" "/mnt/storage/bachelor/adders/systemc/all_adders/build" "--target" "rca_64bit_sctool")
set_tests_properties(rca_64bit_BUILD PROPERTIES  _BACKTRACE_TRIPLES "/home/kilian/Projects/systemc-compiler/lib/cmake/SVC/svc_target.cmake;248;add_test;/mnt/storage/bachelor/adders/systemc/all_adders/64bit/CMakeLists.txt;10;svc_target;/mnt/storage/bachelor/adders/systemc/all_adders/64bit/CMakeLists.txt;0;")
add_test(rca_64bit_SYN "/mnt/storage/bachelor/adders/systemc/all_adders/build/64bit/rca_64bit_sctool")
set_tests_properties(rca_64bit_SYN PROPERTIES  DEPENDS "rca_64bit_BUILD" WILL_FAIL "FALSE" _BACKTRACE_TRIPLES "/home/kilian/Projects/systemc-compiler/lib/cmake/SVC/svc_target.cmake;253;add_test;/mnt/storage/bachelor/adders/systemc/all_adders/64bit/CMakeLists.txt;10;svc_target;/mnt/storage/bachelor/adders/systemc/all_adders/64bit/CMakeLists.txt;0;")
add_test(cla_64bit_BUILD "/usr/bin/cmake" "--build" "/mnt/storage/bachelor/adders/systemc/all_adders/build" "--target" "cla_64bit_sctool")
set_tests_properties(cla_64bit_BUILD PROPERTIES  _BACKTRACE_TRIPLES "/home/kilian/Projects/systemc-compiler/lib/cmake/SVC/svc_target.cmake;248;add_test;/mnt/storage/bachelor/adders/systemc/all_adders/64bit/CMakeLists.txt;13;svc_target;/mnt/storage/bachelor/adders/systemc/all_adders/64bit/CMakeLists.txt;0;")
add_test(cla_64bit_SYN "/mnt/storage/bachelor/adders/systemc/all_adders/build/64bit/cla_64bit_sctool")
set_tests_properties(cla_64bit_SYN PROPERTIES  DEPENDS "cla_64bit_BUILD" WILL_FAIL "FALSE" _BACKTRACE_TRIPLES "/home/kilian/Projects/systemc-compiler/lib/cmake/SVC/svc_target.cmake;253;add_test;/mnt/storage/bachelor/adders/systemc/all_adders/64bit/CMakeLists.txt;13;svc_target;/mnt/storage/bachelor/adders/systemc/all_adders/64bit/CMakeLists.txt;0;")
add_test(ter_64bit_BUILD "/usr/bin/cmake" "--build" "/mnt/storage/bachelor/adders/systemc/all_adders/build" "--target" "ter_64bit_sctool")
set_tests_properties(ter_64bit_BUILD PROPERTIES  _BACKTRACE_TRIPLES "/home/kilian/Projects/systemc-compiler/lib/cmake/SVC/svc_target.cmake;248;add_test;/mnt/storage/bachelor/adders/systemc/all_adders/64bit/CMakeLists.txt;16;svc_target;/mnt/storage/bachelor/adders/systemc/all_adders/64bit/CMakeLists.txt;0;")
add_test(ter_64bit_SYN "/mnt/storage/bachelor/adders/systemc/all_adders/build/64bit/ter_64bit_sctool")
set_tests_properties(ter_64bit_SYN PROPERTIES  DEPENDS "ter_64bit_BUILD" WILL_FAIL "FALSE" _BACKTRACE_TRIPLES "/home/kilian/Projects/systemc-compiler/lib/cmake/SVC/svc_target.cmake;253;add_test;/mnt/storage/bachelor/adders/systemc/all_adders/64bit/CMakeLists.txt;16;svc_target;/mnt/storage/bachelor/adders/systemc/all_adders/64bit/CMakeLists.txt;0;")
add_test(sdtc_pm_64bit_BUILD "/usr/bin/cmake" "--build" "/mnt/storage/bachelor/adders/systemc/all_adders/build" "--target" "sdtc_pm_64bit_sctool")
set_tests_properties(sdtc_pm_64bit_BUILD PROPERTIES  _BACKTRACE_TRIPLES "/home/kilian/Projects/systemc-compiler/lib/cmake/SVC/svc_target.cmake;248;add_test;/mnt/storage/bachelor/adders/systemc/all_adders/64bit/CMakeLists.txt;19;svc_target;/mnt/storage/bachelor/adders/systemc/all_adders/64bit/CMakeLists.txt;0;")
add_test(sdtc_pm_64bit_SYN "/mnt/storage/bachelor/adders/systemc/all_adders/build/64bit/sdtc_pm_64bit_sctool")
set_tests_properties(sdtc_pm_64bit_SYN PROPERTIES  DEPENDS "sdtc_pm_64bit_BUILD" WILL_FAIL "FALSE" _BACKTRACE_TRIPLES "/home/kilian/Projects/systemc-compiler/lib/cmake/SVC/svc_target.cmake;253;add_test;/mnt/storage/bachelor/adders/systemc/all_adders/64bit/CMakeLists.txt;19;svc_target;/mnt/storage/bachelor/adders/systemc/all_adders/64bit/CMakeLists.txt;0;")
add_test(sdtc_ppm_64bit_BUILD "/usr/bin/cmake" "--build" "/mnt/storage/bachelor/adders/systemc/all_adders/build" "--target" "sdtc_ppm_64bit_sctool")
set_tests_properties(sdtc_ppm_64bit_BUILD PROPERTIES  _BACKTRACE_TRIPLES "/home/kilian/Projects/systemc-compiler/lib/cmake/SVC/svc_target.cmake;248;add_test;/mnt/storage/bachelor/adders/systemc/all_adders/64bit/CMakeLists.txt;22;svc_target;/mnt/storage/bachelor/adders/systemc/all_adders/64bit/CMakeLists.txt;0;")
add_test(sdtc_ppm_64bit_SYN "/mnt/storage/bachelor/adders/systemc/all_adders/build/64bit/sdtc_ppm_64bit_sctool")
set_tests_properties(sdtc_ppm_64bit_SYN PROPERTIES  DEPENDS "sdtc_ppm_64bit_BUILD" WILL_FAIL "FALSE" _BACKTRACE_TRIPLES "/home/kilian/Projects/systemc-compiler/lib/cmake/SVC/svc_target.cmake;253;add_test;/mnt/storage/bachelor/adders/systemc/all_adders/64bit/CMakeLists.txt;22;svc_target;/mnt/storage/bachelor/adders/systemc/all_adders/64bit/CMakeLists.txt;0;")
