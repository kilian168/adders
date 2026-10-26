#******************************************************************************
# 64-bit adders: regular (RCA), carry-lookahead (CLA), balanced ternary
# (TER), and SD/TC (PM, PPM). Each .cpp is independently synthesizable --
# run `make rca_64bit_sctool` etc. (or the plain `make rca_64bit` for a
# normal SystemC simulation build) once ICSC is installed.
#******************************************************************************
project(all_adders_64bit)

add_executable(rca_64bit rca_glitch.cpp)
svc_target(rca_64bit)

add_executable(cla_64bit cla_glitch.cpp)
svc_target(cla_64bit)

add_executable(ter_64bit ter_glitch.cpp)
svc_target(ter_64bit)

add_executable(sdtc_pm_64bit sdtc_glitch_pm.cpp)
svc_target(sdtc_pm_64bit)

add_executable(sdtc_ppm_64bit sdtc_glitch_ppm.cpp)
svc_target(sdtc_ppm_64bit)
