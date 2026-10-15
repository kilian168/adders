#******************************************************************************
# 2-bit adders: regular (RCA), carry-lookahead (CLA), balanced ternary
# (TER), and SD/TC (PM, PPM). Each .cpp is independently synthesizable --
# run `make rca_2bit_sctool` etc. (or the plain `make rca_2bit` for a
# normal SystemC simulation build) once ICSC is installed.
#******************************************************************************
project(all_adders_2bit)

add_executable(rca_2bit rca_glitch.cpp)
svc_target(rca_2bit)

add_executable(cla_2bit cla_glitch.cpp)
svc_target(cla_2bit)

add_executable(ter_2bit ter_glitch.cpp)
svc_target(ter_2bit)

add_executable(sdtc_pm_2bit sdtc_glitch_pm.cpp)
svc_target(sdtc_pm_2bit)

add_executable(sdtc_ppm_2bit sdtc_glitch_ppm.cpp)
svc_target(sdtc_ppm_2bit)
