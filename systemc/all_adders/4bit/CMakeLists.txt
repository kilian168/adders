#******************************************************************************
# 4-bit adders: regular (RCA), carry-lookahead (CLA), balanced ternary
# (TER), and SD/TC (PM, PPM). Each .cpp is independently synthesizable --
# run `make rca_4bit_sctool` etc. (or the plain `make rca_4bit` for a
# normal SystemC simulation build) once ICSC is installed.
#******************************************************************************
project(all_adders_4bit)

add_executable(rca_4bit rca_glitch.cpp)
svc_target(rca_4bit)

add_executable(cla_4bit cla_glitch.cpp)
svc_target(cla_4bit)

add_executable(ter_4bit ter_glitch.cpp)
svc_target(ter_4bit)

add_executable(sdtc_pm_4bit sdtc_glitch_pm.cpp)
svc_target(sdtc_pm_4bit)

add_executable(sdtc_ppm_4bit sdtc_glitch_ppm.cpp)
svc_target(sdtc_ppm_4bit)
