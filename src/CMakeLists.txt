# Define the source files
set(SOURCE_FILES "benchmark.cc" "colorprint.cc" "commandlineflags.cc" "sleep.cc" "sysinfo.cc" "walltime.cc")

# Determine the correct regular expression engine to use
if(HAVE_STD_REGEX)
  set(RE_FILES "re_std.cc")
elseif(HAVE_GNU_POSIX_REGEX)
  set(RE_FILES "re_posix.cc")
elseif(HAVE_POSIX_REGEX)
  set(RE_FILES "re_posix.cc")
else()
  set(RE_FILES "")
endif()

# Build the benchmark library
add_library(benchmark SHARED ${SOURCE_FILES} ${RE_FILES})
set_target_properties(benchmark PROPERTIES
  OUTPUT_NAME "benchmark"
  VERSION "1.0"
  SOVERSION "1"
)

# Install target (will install the library to specified CMAKE_INSTALL_PREFIX variable)
install(
  TARGETS benchmark
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  COMPONENT library)
install(
  DIRECTORY "${PROJECT_SOURCE_DIR}/include/benchmark"
  DESTINATION include
  FILES_MATCHING PATTERN "*.*h")
