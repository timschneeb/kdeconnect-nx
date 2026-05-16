if(NOT DEFINED ENV{DEVKITPRO})
    message(FATAL_ERROR "DEVKITPRO environment variable is not set. Install devkitPro and set DEVKITPRO accordingly.")
endif()

include("$ENV{DEVKITPRO}/cmake/Switch.cmake")
