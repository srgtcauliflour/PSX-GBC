cmake_minimum_required(VERSION 3.21)

project(
	aGBe-core-smoketest
	LANGUAGES    C ASM
	VERSION      0.1.0
	DESCRIPTION  "aGBe CPU/MBC core smoke test on real PSn00bSDK toolchain"
)

psn00bsdk_add_executable(smoketest GPREL main.c emu.c opcodes.c)
