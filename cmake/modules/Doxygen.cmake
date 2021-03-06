# generate documentation on 'make doxygen-doc'
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/doc)

find_package(Doxygen)
set_package_properties(Doxygen PROPERTIES
    DESCRIPTION "Documentation generator"
    URL "http://doxygen.org"
    PURPOSE "Needed to generate the API documentation")

if(DOXYGEN_FOUND)
    find_program(QHELPGENERATOR_EXECUTABLE qhelpgenerator)
    mark_as_advanced(QHELPGENERATOR_EXECUTABLE)

    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(QHELPGENERATOR DEFAULT_MSG QHELPGENERATOR_EXECUTABLE)

    set(QT_TAGS_FILE     ${QT_DOC_DIR}/qtcore/qtcore.tags)
    if(EXISTS ${QT_TAGS_FILE})
        find_package(Perl QUIET)

        if (NOT PERL_FOUND)
            message(WARNING "Perl was not found. Qt crosslinks in uploaded docs won't be valid.")
        endif ()
    else()
        message(WARNING "html/qt.tags not found in ${QT_DOC_DIR}. Set the QT_DOC_DIR variable to
point to its location to enable crosslinking.")
        unset(QT_TAGS_FILE)
    endif()

    set(abs_top_builddir ${CMAKE_BINARY_DIR})
    set(abs_top_srcdir   ${CMAKE_SOURCE_DIR})
    set(GENERATE_HTML    YES)
    set(GENERATE_RTF     NO)
    set(GENERATE_CHM     NO)
    set(GENERATE_CHI     NO)
    set(GENERATE_LATEX   NO)
    set(GENERATE_MAN     NO)
    set(GENERATE_XML     NO)
    set(GENERATE_QHP     ${QHELPGENERATOR_FOUND})
    configure_file(doxygen.cfg.in ${CMAKE_BINARY_DIR}/doxygen.cfg)
    add_custom_target(doxygen-doc
        ${DOXYGEN_EXECUTABLE} ${CMAKE_BINARY_DIR}/doxygen.cfg
        SOURCES
            doxygen.cfg.in
            TelepathyQt/account-capability-filter.dox
            TelepathyQt/and-filter.dox
            TelepathyQt/async-model.dox
            TelepathyQt/callbacks.dox
            TelepathyQt/examples.dox
            TelepathyQt/filter.dox
            TelepathyQt/generic-capability-filter.dox
            TelepathyQt/generic-property-filter.dox
            TelepathyQt/groups.dox
            TelepathyQt/main.dox
            TelepathyQt/method-invocation-context.dox
            TelepathyQt/not-filter.dox
            TelepathyQt/or-filter.dox
            TelepathyQt/shared-ptr.dox
    )
else()
    # Suppress cmake policy CMP0046 warnings.
    # This target is being used as a dependency in other targets,
    # so it always needs to be available, even if empty.
    add_custom_target(doxygen-doc)
endif()
