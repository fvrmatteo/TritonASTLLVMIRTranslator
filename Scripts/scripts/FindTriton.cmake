set(LIBRARY_ROOT "${DEPENDENCIES_FOLDER}/triton")

set(TRITON_FOUND TRUE)
set(TRITON_INCLUDE_DIRS "${LIBRARY_ROOT}/includes")

if(DEFINED WIN32)
  set(LIBRARY_PREFIX "")
  set(LIBRARY_EXTENSION "lib")
else()
  set(LIBRARY_PREFIX "lib")
  set(LIBRARY_EXTENSION "a")
endif()

set(TRITON_LIBRARIES
  ${LIBRARY_ROOT}/${LIBRARY_PREFIX}triton.${LIBRARY_EXTENSION}
)

mark_as_advanced(FORCE TRITON_FOUND)
mark_as_advanced(FORCE TRITON_INCLUDE_DIRS)
mark_as_advanced(FORCE TRITON_LIBRARIES)