# CMake module to use prebuilt static libraries instead of rebuilding dependencies
# This significantly speeds up incremental builds by reusing heavy dependencies
#
# Usage: Add -DUSE_PREBUILT_LIBS=ON to your cmake command

option(USE_PREBUILT_LIBS "Use prebuilt static libraries for heavy dependencies" OFF)

if(USE_PREBUILT_LIBS)
    message(STATUS "=== Using Prebuilt Libraries ===")

    set(PREBUILT_LIB_DIR "${CMAKE_SOURCE_DIR}/prebuilt_libs")
    set(PREBUILT_INCLUDES_DIR "${CMAKE_SOURCE_DIR}/prebuilt_includes")

    if(NOT EXISTS "${PREBUILT_LIB_DIR}")
        message(FATAL_ERROR
            "Prebuilt libraries directory not found: ${PREBUILT_LIB_DIR}\n"
            "Please run './copy_prebuilt_libs.sh' after your first successful build.")
    endif()

    # Override TensorFlow Lite Micro build
    # We need to check if the target already exists before creating it
    if(NOT TARGET tensorflow-lite-micro)
        find_library(TENSORFLOW_LITE_MICRO_LIB
            NAMES tensorflow-lite-micro
            PATHS ${PREBUILT_LIB_DIR}
            NO_DEFAULT_PATH
            REQUIRED)

        if(TENSORFLOW_LITE_MICRO_LIB)
            add_library(tensorflow-lite-micro STATIC IMPORTED GLOBAL)
            set_target_properties(tensorflow-lite-micro PROPERTIES
                IMPORTED_LOCATION ${TENSORFLOW_LITE_MICRO_LIB}
            )

            # Add include directories if available
            if(EXISTS "${PREBUILT_INCLUDES_DIR}/tensorflow")
                target_include_directories(tensorflow-lite-micro INTERFACE
                    ${PREBUILT_INCLUDES_DIR}
                    ${PREBUILT_INCLUDES_DIR}/tensorflow
                )
            endif()

            if(EXISTS "${PREBUILT_INCLUDES_DIR}/flatbuffers")
                target_include_directories(tensorflow-lite-micro INTERFACE
                    ${PREBUILT_INCLUDES_DIR}/flatbuffers
                )
            endif()

            message(STATUS "✓ Using prebuilt tensorflow-lite-micro: ${TENSORFLOW_LITE_MICRO_LIB}")

            # Set a flag to skip TensorFlow build in tensorflow_lite_micro.cmake
            set(TENSORFLOW_LITE_MICRO_PREBUILT TRUE CACHE INTERNAL "")
        endif()
    endif()

    # Override CMSIS-NN build if available
    if(NOT TARGET cmsisnn AND NOT TARGET cmsis-nn)
        find_library(CMSIS_NN_LIB
            NAMES cmsisnn cmsis-nn
            PATHS ${PREBUILT_LIB_DIR}
            NO_DEFAULT_PATH)

        if(CMSIS_NN_LIB)
            add_library(cmsisnn STATIC IMPORTED GLOBAL)
            set_target_properties(cmsisnn PROPERTIES
                IMPORTED_LOCATION ${CMSIS_NN_LIB}
            )
            message(STATUS "✓ Using prebuilt CMSIS-NN: ${CMSIS_NN_LIB}")
        endif()
    endif()

    # Override CMSIS-DSP build if available
    if(NOT TARGET CMSISDSP)
        find_library(CMSIS_DSP_LIB
            NAMES CMSISDSP cmsisdsp
            PATHS ${PREBUILT_LIB_DIR}
            NO_DEFAULT_PATH)

        if(CMSIS_DSP_LIB)
            add_library(CMSISDSP STATIC IMPORTED GLOBAL)
            set_target_properties(CMSISDSP PROPERTIES
                IMPORTED_LOCATION ${CMSIS_DSP_LIB}
            )
            message(STATUS "✓ Using prebuilt CMSIS-DSP: ${CMSIS_DSP_LIB}")
        endif()
    endif()

    message(STATUS "================================")
else()
    message(STATUS "Building all dependencies from source (USE_PREBUILT_LIBS=OFF)")
endif()
