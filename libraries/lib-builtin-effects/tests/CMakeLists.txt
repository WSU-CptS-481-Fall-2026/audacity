#[[
Unit tests for lib-builtin-effects (DTMF tone generation).

The DTMF tests are deliberately free of wxWidgets, so they only need the
header-only code under test and lib-math for sampleCount.
]]

add_unit_test(
   NAME
      lib-builtin-effects-dtmf
   SOURCES
      DtmfToneCharacterizationTests.cpp
      DtmfToneEquivalenceTests.cpp
      DtmfToneLegacyReference.h
   LIBRARIES
      lib-math
)

if( TARGET lib-builtin-effects-dtmf-test )
   target_include_directories( lib-builtin-effects-dtmf-test PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/.." )
endif()
