set(BUILD_WITH_HDF5 FALSE CACHE BOOL "Add support for reading HDF5 files.")
if (BUILD_WITH_HDF5)
	define_module(ted BINARY SOURCES ted.cpp LINKS evaluation inference imageprocessing hdf5)
else()
	define_module(ted BINARY SOURCES ted.cpp LINKS evaluation inference imageprocessing)
endif()
