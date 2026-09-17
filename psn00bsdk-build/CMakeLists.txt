cmake_minimum_required(VERSION 3.21)

project(
	psxgbc
	LANGUAGES    C ASM
	VERSION      0.3.0
	DESCRIPTION  "PSX-GBC - Game Boy/Game Boy Color emulator for the PS1 (PSn00bSDK build)"
)

# References the canonical source in ../psx-gbc directly - no copy-and-sync
# step, unlike the earlier psn00bsdk-smoketest proof-of-concept.
set(PSXGBC_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../psx-gbc)

psn00bsdk_add_executable(psxgbc GPREL
	${PSXGBC_SRC}/main.c
	${PSXGBC_SRC}/psx.c
	${PSXGBC_SRC}/gui.c
	${PSXGBC_SRC}/emu.c
	${PSXGBC_SRC}/opcodes.c
)
target_include_directories(psxgbc PRIVATE ${PSXGBC_SRC})
