# The picture's pixel loops and the analysis at -O2, the rest of the player at -Os
# (snurra.pro). Measured 2026-11-14, a 4K film at 60 under load: at -Os the
# conversion (src/video.cpp's to_rgb) showed 6 pictures a second, 13 with
# its helpers inlined by force, 20 split across four threads; at -O2 the
# same code shows all 60. -Os makes its per-sample helpers calls and leaves
# the loops unvectorised. The cost: the player's code from 538369 bytes to
# 582313, 44 KB, for the whole file at -O2 -- worth a 4K film's pictures.
#
# A compiler of its own, since qmake has no flags a file: the shared flags,
# then -O2, the last -O winning. Not in snurra.pri: what a project linking
# the library builds with is its own.
# And the beat and key analysis (src/beats.cpp, FFTs over a whole tune):
# a minute of audio took 0.93 s at -Os and 0.47 s at -O2, twice as long for
# Find tempos over a library and twice the battery on a phone.
SNURRA_FAST = $$SNURRA_ROOT/src/video.cpp $$SNURRA_ROOT/src/beats.cpp   # as snurra.pri names them, or -= removes nothing
SOURCES -= $$SNURRA_FAST
snurra_fast.input = SNURRA_FAST
snurra_fast.output = ${QMAKE_VAR_OBJECTS_DIR}${QMAKE_FILE_BASE}$${first(QMAKE_EXT_OBJ)}
snurra_fast.commands = $$QMAKE_CXX -c $(CXXFLAGS) -O2 $(INCPATH) ${QMAKE_FILE_IN} -o ${QMAKE_FILE_OUT}
snurra_fast.dependency_type = TYPE_C
snurra_fast.variable_out = OBJECTS
QMAKE_EXTRA_COMPILERS += snurra_fast
