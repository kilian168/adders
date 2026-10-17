# CMake generated Testfile for 
# Source directory: /mnt/storage/bachelor/adders/systemc/all_adders/2bit
# Build directory: /mnt/storage/bachelor/adders/systemc/all_adders/build/2bit
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(rca_2bit_BUILD "/usr/bin/cmake" "--build" "/mnt/storage/bachelor/adders/systemc/all_adders/build" "--target" "rca_2bit_sctool")
set_tests_properties(rca_2bit_BUILD PROPERTIES  _BACKTRACE_TRIPLES "/home/kilian/Projects/systemc-compiler/lib/cmake/SVC/svc_target.cmake;248;add_test;/mnt/storage/bachelor/adders/systemc/all_adders/2bit/CMakeLists.txt;10;svc_target;/mnt/storage/bachelor/adders/systemc/all_adders/2bit/CMakeLists.txt;0;")
add_test(rca_2bit_SYN "/mnt/storage/bachelor/adders/systemc/all_adders/build/2bit/rca_2bit_sctool")
set_tests_properties(rca_2bit_SYN PROPERTIES  DEPENDS "rca_2bit_BUILD" WILL_FAIL "FALSE" _BACKTRACE_TRIPLES "/home/kilian/Projects/systemc-compiler/lib/cmake/SVC/svc_target.cmake;253;add_test;/mnt/storage/bachelor/adders/systemc/all_adders/2bit/CMakeLists.txt;10;svc_target;/mnt/storage/bachelor/adders/systemc/all_adders/2bit/CMakeLists.txt;0;")
add_test(cla_2bit_BUILD "/usr/bin/cmake" "--build" "/mnt/storage/bachelor/adders/systemc/all_adders/build" "--target" "cla_2bit_sctool")
set_tests_properties(cla_2bit_BUILD PROPERTIES  _BACKTRACE_TRIPLES "/home/kilian/Projects/systemc-compiler/lib/cmake/SVC/svc_target.cmake;248;add_test;/mnt/storage/bachelor/adders/systemc/all_adders/2bit/CMakeLists.txt;13;svc_target;/mnt/storage/bachelor/adders/systemc/all_adders/2bit/CMakeLists.txt;0;")
add_test(cla_2bit_SYN "/mnt/storage/bachelor/adders/systemc/all_adders/build/2bit/cla_2bit_sctool")
set_tests_properties(cla_2bit_SYN PROPERTIES  DEPENDS "cla_2bit_BUILD" WILL_FAIL "FALSE" _BACKTRACE_TRIPLES "/home/kilian/Projects/systemc-compiler/lib/cmake/SVC/svc_target.cmake;253;add_test;/mnt/storage/bachelor/adders/systemc/all_adders/2bit/CMakeLists.txt;13;svc_target;/mnt/storage/bachelor/adders/systemc/all_adders/2bit/CMakeLists.txt;0;")
add_test(ter_2bit_BUILD "/usr/bin/cmake" "--build" "/mnt/storage/bachelor/adders/systemc/all_adders/build" "--target" "ter_2bit_sctool")
set_tests_properties(ter_2bit_BUILD PROPERTIES  _BACKTRACE_TRIPLES "/home/kilian/Projects/systemc-compiler/lib/cmake/SVC/svc_target.cmake;248;add_test;/mnt/storage/bachelor/adders/systemc/all_adders/2bit/CMakeLists.txt;16;svc_target;/mnt/storage/bachelor/adders/systemc/all_adders/2bit/CMakeLists.txt;0;")
add_test(ter_2bit_SYN "/mnt/storage/bachelor/adders/systemc/all_adders/build/2bit/ter_2bit_sctool")
set_tests_properties(ter_2bit_SYN PROPERTIES  DEPENDS "ter_2bit_BUILD" WILL_FAIL "FALSE" _BACKTRACE_TRIPLES "/home/kilian/Projects/systemc-compiler/lib/cmake/SVC/svc_target.cmake;253;add_test;/mnt/storage/bachelor/adders/systemc/all_adders/2bit/CMakeLists.txt;16;svc_target;/mnt/storage/bachelor/adders/systemc/all_adders/2bit/CMakeLists.txt;0;")
add_test(sdtc_pm_2bit_BUILD "/usr/bin/cmake" "--build" "/mnt/storage/bachelor/adders/systemc/all_adders/build" "--target" "sdtc_pm_2bit_sctool")
set_tests_properties(sdtc_pm_2bit_BUILD PROPERTIES  _BACKTRACE_TRIPLES "/home/kilian/Projects/systemc-compiler/lib/cmake/SVC/svc_target.cmake;248;add_test;/mnt/storage/bachelor/adders/systemc/all_adders/2bit/CMakeLists.txt;19;svc_target;/mnt/storage/bachelor/adders/systemc/all_adders/2bit/CMakeLists.txt;0;")
add_test(sdtc_pm_2bit_SYN "/mnt/storage/bachelor/adders/systemc/all_adders/build/2bit/sdtc_pm_2bit_sctool")
set_tests_properties(sdtc_pm_2bit_SYN PROPERTIES  DEPENDS "sdtc_pm_2bit_BUILD" WILL_FAIL "FALSE" _BACKTRACE_TRIPLES "/home/kilian/Projects/systemc-compiler/lib/cmake/SVC/svc_target.cmake;253;add_test;/mnt/storage/bachelor/adders/systemc/all_adders/2bit/CMakeLists.txt;19;svc_target;/mnt/storage/bachelor/adders/systemc/all_adders/2bit/CMakeLists.txt;0;")
add_test(sdtc_ppm_2bit_BUILD "/usr/bin/cmake" "--build" "/mnt/storage/bachelor/adders/systemc/all_adders/build" "--target" "sdtc_ppm_2bit_sctool")
set_tests_properties(sdtc_ppm_2bit_BUILD PROPERTIES  _BACKTRACE_TRIPLES "/home/kilian/Projects/systemc-compiler/lib/cmake/SVC/svc_target.cmake;248;add_test;/mnt/storage/bachelor/adders/systemc/all_adders/2bit/CMakeLists.txt;22;svc_target;/mnt/storage/bachelor/adders/systemc/all_adders/2bit/CMakeLists.txt;0;")
add_test(sdtc_ppm_2bit_SYN "/mnt/storage/bachelor/adders/systemc/all_adders/build/2bit/sdtc_ppm_2bit_sctool")
set_tests_properties(sdtc_ppm_2bit_SYN PROPERTIES  DEPENDS "sdtc_ppm_2bit_BUILD" WILL_FAIL "FALSE" _BACKTRACE_TRIPLES "/home/kilian/Projects/systemc-compiler/lib/cmake/SVC/svc_target.cmake;253;add_test;/mnt/storage/bachelor/adders/systemc/all_adders/2bit/CMakeLists.txt;22;svc_target;/mnt/storage/bachelor/adders/systemc/all_adders/2bit/CMakeLists.txt;0;")
