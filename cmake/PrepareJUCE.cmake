set(SEENLA_JUCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../third_party/JUCE")
set(SEENLA_JUCE_PATCH "${CMAKE_CURRENT_LIST_DIR}/patches/juce-open-dynamics.patch")
if(NOT EXISTS "${SEENLA_JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE is missing. Run: git submodule update --init --recursive")
endif()
find_package(Git REQUIRED)
execute_process(COMMAND "${GIT_EXECUTABLE}" apply --ignore-whitespace --check "${SEENLA_JUCE_PATCH}"
    WORKING_DIRECTORY "${SEENLA_JUCE_DIR}" RESULT_VARIABLE patch_check
    OUTPUT_QUIET ERROR_QUIET)
if(patch_check EQUAL 0)
    execute_process(COMMAND "${GIT_EXECUTABLE}" apply --ignore-whitespace --whitespace=nowarn "${SEENLA_JUCE_PATCH}"
        WORKING_DIRECTORY "${SEENLA_JUCE_DIR}" RESULT_VARIABLE patch_result ERROR_VARIABLE patch_error)
    if(NOT patch_result EQUAL 0)
        message(FATAL_ERROR "Could not prepare JUCE: ${patch_error}")
    endif()
else()
    execute_process(COMMAND "${GIT_EXECUTABLE}" apply --ignore-whitespace --reverse --check "${SEENLA_JUCE_PATCH}"
        WORKING_DIRECTORY "${SEENLA_JUCE_DIR}" RESULT_VARIABLE reverse_check
        OUTPUT_QUIET ERROR_QUIET)
    if(NOT reverse_check EQUAL 0)
        message(FATAL_ERROR "JUCE does not match the pinned revision/patch. Check the submodule before building.")
    endif()
endif()
