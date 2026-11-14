# snurra -- the player, one tile per process (project.md sec 3b).
TEMPLATE = app
TARGET = snurra
QT += widgets network
CONFIG += c++17
QMAKE_CXXFLAGS += -ffunction-sections -fdata-sections
# Size, not speed (the aim: tiny). In the release flags, not beside them:
# qmake puts its -O2 after QMAKE_CXXFLAGS, and the last -O wins, so -Os
# there was never in force (2026-11-13).
QMAKE_CXXFLAGS_RELEASE -= -O2
QMAKE_CXXFLAGS_RELEASE += -Os
QMAKE_LFLAGS += -Wl,--gc-sections
include(snurra.pri)
include(app/fuzznet.pri)
include(app/speed.pri)
SOURCES += src/main.cpp
# The version, from VERSION, the one place it lives: what --version says.
SNURRA_VERSION_TEXT = $$cat($$PWD/VERSION)
DEFINES += SNURRA_VERSION_TEXT=\\\"$$SNURRA_VERSION_TEXT\\\"

android {
	# Qt's own manifest template, changed in one place -- the package, the
	# APP_ID -- so a Qt upgrade can be diffed against the new template.
	ANDROID_PACKAGE_SOURCE_DIR = $$PWD/android
	# The phone's music, for the DJ player's browser, asked for at start
	# (main.cpp); declared in android/AndroidManifest.xml.
	QT += core-private
	# From VERSION through the Makefile, which can do the arithmetic; the
	# fallbacks are for a qmake run that does not come through it.
	isEmpty(SNURRA_VERSION_CODE): SNURRA_VERSION_CODE = 1
	isEmpty(SNURRA_VERSION): SNURRA_VERSION = 0
	isEmpty(SNURRA_TARGET_API): SNURRA_TARGET_API = 36
	ANDROID_VERSION_CODE = $$SNURRA_VERSION_CODE
	ANDROID_VERSION_NAME = $$SNURRA_VERSION
	ANDROID_MIN_SDK_VERSION = 26
	ANDROID_TARGET_SDK_VERSION = $$SNURRA_TARGET_API
}
