# CPack evaluates this once per generator, keeping archive paths portable.
if(CPACK_GENERATOR STREQUAL "DEB")
  set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
else()
  set(CPACK_PACKAGING_INSTALL_PREFIX "/")
endif()
