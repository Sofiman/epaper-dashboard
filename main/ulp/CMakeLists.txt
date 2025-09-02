cmake_minimum_required(VERSION 3.16)

set(lib_path "${CMAKE_CURRENT_LIST_DIR}/../../components")
set(ulp_app_name, "ulp_eink_dashboard")
# Project/target name is passed from the main project to allow IDF to have a dependency on this target
# as well as embed the binary into the main app
project(${ULP_APP_NAME})
add_executable(${ULP_APP_NAME} main.c "${lib_path}/sensirion_common/sensirion_common.c" "${lib_path}/sht4x/sht4x.c" "${lib_path}/scd4x/scd4x.c")

# Import the ULP project helper functions
include(IDFULPProject)

# Apply default compile options
ulp_apply_default_options(${ULP_APP_NAME})

# Apply default sources provided by the IDF ULP component
ulp_apply_default_sources(${ULP_APP_NAME})

# Add targets for building the binary, as well as the linkerscript which exports ULP shared variables to the main app
ulp_add_build_binary_targets(${ULP_APP_NAME})

# Everything below this line is optional and can be used to customize the build process

add_compile_definitions(
    "SHT4x_LP_CORE_I2C" "SCD4x_LP_CORE_I2C"
)

# Create a custom library
#add_library(sht4x STATIC "${lib_path}/sht4x/sht4x.c")
target_include_directories(${ULP_APP_NAME} PRIVATE "${lib_path}/sht4x/include")
target_include_directories(${ULP_APP_NAME} PRIVATE "${lib_path}/scd4x/include")

# Link the library
#target_link_libraries(${ULP_APP_NAME} PRIVATE sht4x)

# Set custom compile flags
#target_compile_options(${ULP_APP_NAME} PRIVATE -msave-restore)
