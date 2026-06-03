# 1.1

 - more tests and examples: code/TemplateStaticCall.cpp, code/TypeTemplateModule.cpp
 - Linux kernel 6.19 is noe used instead of v5 in linux tests
 - some bugs fixed in the Tribe RISC-V CPU L1/L2 cache and checkpoints after stress-ng testing on Linux
 - _system_clock global variable is mandatory now to define in .cpp source of the project

# 1.0

 - lots of tests in examples/ and tests/
 - Tribe RISC-V CPU is developed in tribe/ folder and has it's own tests/ and linux/ folders for running Linux demo in simulator
 - experimental backwards SV->CPP conversion tool hdlcpp/
 - wrote documents "best_practice" and "tribe_cpu"
 - added CHANGELOG.md
