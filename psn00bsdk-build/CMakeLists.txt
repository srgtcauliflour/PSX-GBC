cmake_minimum_required(VERSION 3.21)

project(
	aGBe
	LANGUAGES    C ASM
	VERSION      0.3.0
	DESCRIPTION  "aGBe - Game Boy emulator for the PS1 (PSn00bSDK build)"
)

# References the canonical source in ../aGBe directly - no copy-and-sync
# step, unlike the earlier psn00bsdk-smoketest proof-of-concept.
set(AGBE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../aGBe)

psn00bsdk_add_executable(agbe GPREL
	${AGBE_SRC}/main.c
	${AGBE_SRC}/psx.c
	${AGBE_SRC}/gui.c
	${AGBE_SRC}/emu.c
	${AGBE_SRC}/opcodes.c
)
target_include_directories(agbe PRIVATE ${AGBE_SRC})
