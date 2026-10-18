#[[
Unit tests for lib-cloud-audiocom
]]

add_unit_test(
   NAME
      lib-cloud-audiocom
   MOCK_PREFS
   SOURCES
      LinkAccountMenuStateTests.cpp
   LIBRARIES
      lib-cloud-audiocom-interface
      lib-import-export-interface
)
