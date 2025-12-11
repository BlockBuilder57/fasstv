function(fasstv_setup_target target)
    target_compile_definitions(${target} PRIVATE "$<$<CONFIG:DEBUG>:FASSTV_DEBUG>")

    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_compile_features(${target} PUBLIC cxx_std_23)

    # some sane compiler flags
    set(_CORE_COMPILE_ARGS -Wall -Wextra)
    set(_CORE_LINKER_ARGS "")

    if("${CMAKE_BUILD_TYPE}" STREQUAL "Release")
        set(_CORE_COMPILE_ARGS ${_CORE_COMPILE_ARGS} -Wall)

        # If on Release use link-time optimizations.
        # On clang we use ThinLTO for even better build performance.
        if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
            set(_CORE_COMPILE_ARGS ${_CORE_COMPILE_ARGS} -flto=thin)
            set(_CORE_LINKER_ARGS ${_CORE_LINKER_ARGS} -flto=thin)
            target_link_options(${target} PRIVATE -fuse-ld=${FASSTV_LINKER} -flto=thin)
        else()
            set(_CORE_COMPILE_ARGS ${_CORE_COMPILE_ARGS} -flto)
            set(_CORE_LINKER_ARGS ${_CORE_LINKER_ARGS} -flto)
            target_link_options(${target} PRIVATE -fuse-ld=${FASSTV_LINKER} -flto)
        endif()

    endif()

    if("asan" IN_LIST FASSTV_BUILD_FEATURES)
        # Error if someone's trying to mix asan and tsan together,
        # they aren't compatible.
        if("tsan" IN_LIST FASSTV_BUILD_FEATURES)
            message(FATAL_ERROR "ASAN and TSAN cannot be used together.")
        endif()

        message(STATUS "Enabling ASAN for target ${target} because it was in FASSTV_BUILD_FEATURES")
        set(_CORE_COMPILE_ARGS ${_CORE_COMPILE_ARGS} -fsanitize=address)
        set(_CORE_LINKER_ARGS ${_CORE_LINKER_ARGS} -fsanitize=address)
    endif()

    if("tsan" IN_LIST FASSTV_BUILD_FEATURES)
        message(STATUS "Enabling TSAN for target ${target} because it was in FASSTV_BUILD_FEATURES")
        set(_CORE_COMPILE_ARGS ${_CORE_COMPILE_ARGS} -fsanitize=thread)
        set(_CORE_LINKER_ARGS ${_CORE_LINKER_ARGS} -fsanitize=thread)
    endif()

    if("ubsan" IN_LIST FASSTV_BUILD_FEATURES)
        message(STATUS "Enabling UBSAN for target ${target} because it was in FASSTV_BUILD_FEATURES")
        set(_CORE_COMPILE_ARGS ${_CORE_COMPILE_ARGS} -fsanitize=undefined)
        set(_CORE_LINKER_ARGS ${_CORE_LINKER_ARGS} -fsanitize=undefined)
    endif()

    target_compile_options(${target} PRIVATE ${_CORE_COMPILE_ARGS})
    target_link_options(${target} PRIVATE ${_CORE_LINKER_ARGS})
endfunction()

function(fasstv_set_alternate_linker)
    find_program(LINKER_EXECUTABLE ld.${FASSTV_LINKER} ${FASSTV_LINKER})
    if(LINKER_EXECUTABLE)
        message(STATUS "Using ${FASSTV_LINKER} as linker for fasstv")
    else()
        message(FATAL_ERROR "Linker ${FASSTV_LINKER} does not exist on your system. Please specify one which does or omit this option from your configure command.")
    endif()
endfunction()
