# Called via cmake -P (with -DSOURCE_DIR=... -DTEMPLATE=... -DOUTPUT=...)
# or via include() with those variables set beforehand.
find_program(_git git)
if(_git)
    execute_process(
        COMMAND "${_git}" rev-parse --short HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE GIT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
    )
    execute_process(
        COMMAND "${_git}" describe --tags --abbrev=0
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE GIT_LAST_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
    )
    execute_process(
        COMMAND "${_git}" describe --tags --dirty=-dirty
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
    )
    unset(_git)
endif()

if(NOT GIT_COMMIT_HASH)
    set(GIT_COMMIT_HASH "unknown")
endif()
if(NOT GIT_LAST_TAG)
    set(GIT_LAST_TAG "unknown")
endif()
if(NOT GIT_DESCRIBE)
    set(GIT_DESCRIBE "unknown")
endif()

configure_file("${TEMPLATE}" "${OUTPUT}" @ONLY)