cmake_minimum_required(VERSION 3.25)
if(POLICY CMP0184)
    cmake_policy(SET CMP0184 NEW)
endif()
project(Moulang VERSION 0.4.0 LANGUAGES C CXX)

set(MOULANG_MSVC_INCLUDE_PREFIX "" CACHE STRING "Override the localized MSVC /showIncludes prefix for Ninja")
if(CMAKE_GENERATOR MATCHES "^Ninja" AND MOULANG_MSVC_INCLUDE_PREFIX)
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${MOULANG_MSVC_INCLUDE_PREFIX}")
endif()

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
    message(FATAL_ERROR "Moulang Phase 1 requires an x64 Windows MSVC toolchain.")
endif()

find_package(LLVM 22.1.8 EXACT REQUIRED CONFIG)
if(LLVM_PACKAGE_VERSION VERSION_LESS 22)
    message(FATAL_ERROR "Moulang requires LLVM 22 or newer.")
endif()
if(NOT "X86" IN_LIST LLVM_TARGETS_TO_BUILD)
    message(FATAL_ERROR "The LLVM package must include the X86 target.")
endif()
message(STATUS "Using LLVM ${LLVM_PACKAGE_VERSION} from ${LLVM_DIR}")
llvm_map_components_to_libnames(moulang_llvm core target passes X86)

# The official Windows archive can reference the release builder's DIA SDK.
if(TARGET LLVMDebugInfoPDB)
    get_target_property(moulang_pdb_deps LLVMDebugInfoPDB INTERFACE_LINK_LIBRARIES)
    foreach(dependency IN LISTS moulang_pdb_deps)
        if(IS_ABSOLUTE "${dependency}" AND dependency MATCHES "[/\\\\]diaguids\\.lib$"
                AND NOT EXISTS "${dependency}")
            find_library(MOULANG_DIAGUIDS NAMES diaguids REQUIRED
                HINTS "$ENV{VSINSTALLDIR}/DIA SDK/lib/amd64")
            list(TRANSFORM moulang_pdb_deps REPLACE "^.*[/\\\\]diaguids\\.lib$" "${MOULANG_DIAGUIDS}")
            set_target_properties(LLVMDebugInfoPDB PROPERTIES INTERFACE_LINK_LIBRARIES "${moulang_pdb_deps}")
            break()
        endif()
    endforeach()
endif()

set(MOULANG_MINGW_ROOT "${CMAKE_SOURCE_DIR}/.toolchain/llvm-mingw-20260616-ucrt-x86_64"
    CACHE PATH "Verified LLVM-MinGW 20260616 UCRT x86_64 installation")
get_filename_component(moulang_llvm_tools "${LLVM_DIR}/../../../bin" ABSOLUTE)
set(MOULANG_LINKER "${moulang_llvm_tools}/ld.lld.exe")
set(MOULANG_LLVM_LIB "${moulang_llvm_tools}/llvm-lib.exe")
set(MOULANG_MINGW_CXX "${MOULANG_MINGW_ROOT}/bin/clang++.exe")
set(moulang_mingw_lib "${MOULANG_MINGW_ROOT}/x86_64-w64-mingw32/lib")
set(moulang_builtins "${MOULANG_MINGW_ROOT}/lib/clang/22/lib/windows/libclang_rt.builtins-x86_64.a")
foreach(moulang_tool IN ITEMS MOULANG_LINKER MOULANG_LLVM_LIB MOULANG_MINGW_CXX)
    if(NOT EXISTS "${${moulang_tool}}")
        message(FATAL_ERROR "Missing ${moulang_tool}. Install the pinned toolchains first.")
    endif()
endforeach()
execute_process(COMMAND "${MOULANG_MINGW_CXX}" --version
    OUTPUT_VARIABLE moulang_mingw_version RESULT_VARIABLE moulang_mingw_status)
if(NOT moulang_mingw_status EQUAL 0 OR NOT moulang_mingw_version MATCHES "clang version 22[.]1[.]8 ")
    message(FATAL_ERROR "The runtime compiler must be LLVM-MinGW with Clang 22.1.8.")
endif()
configure_file(cmake/toolchain.hpp.in generated/toolchain.hpp @ONLY)

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("/experimental:deterministic;/pathmap:C:/moulang-probe=."
    MOULANG_HAS_PATHMAP)
function(moulang_apply_build_privacy target)
    # Keep Debug and RelWithDebInfo available for local debugging.
    set_property(TARGET ${target} PROPERTY MSVC_DEBUG_INFORMATION_FORMAT
        "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
    if(MOULANG_HAS_PATHMAP)
        cmake_path(NATIVE_PATH CMAKE_SOURCE_DIR NORMALIZE moulang_source_prefix)
        cmake_path(NATIVE_PATH CMAKE_BINARY_DIR NORMALIZE moulang_build_prefix)
        target_compile_options(${target} PRIVATE
            "$<$<CONFIG:Release,MinSizeRel>:/experimental:deterministic>"
            "$<$<CONFIG:Release,MinSizeRel>:/pathmap:${moulang_source_prefix}=.>"
            "$<$<CONFIG:Release,MinSizeRel>:/pathmap:${moulang_build_prefix}=./build>")
    endif()
endfunction()

set(moulang_runtime_directory "${CMAKE_BINARY_DIR}/runtime-gnu")
set(moulang_runtime_archive "${CMAKE_BINARY_DIR}/bin/moulang_runtime.lib")
file(MAKE_DIRECTORY "${moulang_runtime_directory}" "${CMAKE_BINARY_DIR}/bin")
set(moulang_runtime_flags --target=x86_64-w64-windows-gnu -std=c++20 -O2 -DNDEBUG -fno-exceptions -fno-rtti
    -fno-stack-protector -ffunction-sections -fdata-sections
    "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=." "-ffile-prefix-map=${CMAKE_BINARY_DIR}=./build")
string(TOUPPER "${CMAKE_BUILD_TYPE}" moulang_build_type)
set(moulang_arc_configuration Release)
if(moulang_build_type STREQUAL "DEBUG")
    set(moulang_arc_configuration Debug)
    list(APPEND moulang_runtime_flags -DMOULANG_ARC_DEBUG)
endif()
add_custom_command(OUTPUT "${moulang_runtime_archive}"
    COMMAND "${MOULANG_MINGW_CXX}" ${moulang_runtime_flags} -c
        "${CMAKE_SOURCE_DIR}/runtime/src/runtime.cpp" -o runtime.obj
    COMMAND "${MOULANG_MINGW_CXX}" ${moulang_runtime_flags} -c
        "${CMAKE_SOURCE_DIR}/runtime/src/real.cpp" -o real.obj
    COMMAND "${MOULANG_MINGW_CXX}" ${moulang_runtime_flags} -c
        "${CMAKE_SOURCE_DIR}/runtime/src/standard.cpp" -o standard.obj
    COMMAND "${MOULANG_MINGW_CXX}" ${moulang_runtime_flags} -c
        "${CMAKE_SOURCE_DIR}/runtime/src/startup.cpp" -o startup.obj
    COMMAND "${MOULANG_LLVM_LIB}" /NOLOGO "/OUT:${moulang_runtime_archive}"
        runtime.obj real.obj standard.obj startup.obj
    WORKING_DIRECTORY "${moulang_runtime_directory}"
    DEPENDS runtime/src/runtime.cpp runtime/src/real.cpp runtime/src/standard.cpp runtime/src/startup.cpp
        "${MOULANG_MINGW_CXX}" "${MOULANG_LLVM_LIB}"
    VERBATIM)
add_custom_target(moulang_runtime_build DEPENDS "${moulang_runtime_archive}")
add_library(moulang_runtime STATIC IMPORTED GLOBAL)
set_target_properties(moulang_runtime PROPERTIES IMPORTED_LOCATION "${moulang_runtime_archive}")
add_dependencies(moulang_runtime moulang_runtime_build)

add_executable(moulang
    src/cli/main.cpp
    src/cli/diagnostics.cpp
    src/source/source.cpp
    src/source/program.cpp
    src/lexer/lexer.cpp
    src/parser/parser.cpp
    src/semantic/semantic.cpp
    src/semantic/tiki.cpp
    src/codegen/llvm/object.cpp
    src/driver/windows.cpp
)
target_compile_features(moulang PRIVATE cxx_std_20)
target_compile_options(moulang PRIVATE /W4 /utf-8)
if(moulang_arc_configuration STREQUAL "Debug")
    target_compile_definitions(moulang PRIVATE MOULANG_ARC_DEBUG)
endif()
target_include_directories(moulang PRIVATE include "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_include_directories(moulang SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_link_libraries(moulang PRIVATE ${moulang_llvm})
moulang_apply_build_privacy(moulang)
target_link_options(moulang PRIVATE
    "$<$<CONFIG:Release,MinSizeRel>:/DEBUG:NONE>"
    "/PDBALTPATH:moulang.pdb")
set_target_properties(moulang PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    MSVC_RUNTIME_LIBRARY MultiThreaded)
add_dependencies(moulang moulang_runtime_build)

set(moulang_runtime_files
    libc++.a libmingw32.a libunwind.a libmoldname.a libmingwex.a libmsvcrt.a
    libadvapi32.a libshell32.a libuser32.a libkernel32.a crt2.o crtbegin.o crtend.o)
set(moulang_copy_commands)
set(moulang_link_inputs "${MOULANG_LINKER}" "${moulang_builtins}")
foreach(moulang_file IN LISTS moulang_runtime_files)
    set(moulang_input "${moulang_mingw_lib}/${moulang_file}")
    if(NOT EXISTS "${moulang_input}")
        message(FATAL_ERROR "The pinned runtime installation lacks ${moulang_file}.")
    endif()
    list(APPEND moulang_link_inputs "${moulang_input}")
    list(APPEND moulang_copy_commands COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${moulang_input}" "$<TARGET_FILE_DIR:moulang>/toolchain/lib/${moulang_file}")
    install(FILES "${moulang_input}" DESTINATION bin/toolchain/lib)
endforeach()
add_custom_command(TARGET moulang POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "$<TARGET_FILE_DIR:moulang>/toolchain/bin" "$<TARGET_FILE_DIR:moulang>/toolchain/lib"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${MOULANG_LINKER}"
        "$<TARGET_FILE_DIR:moulang>/toolchain/bin/ld.lld.exe"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${moulang_builtins}"
        "$<TARGET_FILE_DIR:moulang>/toolchain/lib/libclang_rt.builtins-x86_64.a"
    ${moulang_copy_commands}
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/third_party/licenses"
        "$<TARGET_FILE_DIR:moulang>/toolchain/licenses"
    VERBATIM)
set_property(TARGET moulang APPEND PROPERTY LINK_DEPENDS ${moulang_link_inputs})

install(TARGETS moulang RUNTIME DESTINATION bin)
install(FILES "${moulang_runtime_archive}" DESTINATION bin)
install(FILES "${MOULANG_LINKER}" DESTINATION bin/toolchain/bin RENAME ld.lld.exe)
install(FILES "${moulang_builtins}" DESTINATION bin/toolchain/lib)
install(DIRECTORY third_party/licenses/ DESTINATION bin/toolchain/licenses)

include(CTest)
if(BUILD_TESTING)
    find_program(MOULANG_POWERSHELL NAMES pwsh powershell REQUIRED)
    add_test(NAME moulang.integration COMMAND "${MOULANG_POWERSHELL}"
        -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tests/integration/compiler.ps1"
        -Compiler "$<TARGET_FILE:moulang>" -Hello "${CMAKE_SOURCE_DIR}/examples/hello.m"
        -TestRoot "${CMAKE_BINARY_DIR}/integration" -ExpectedVersion "${PROJECT_VERSION}")
    set_tests_properties(moulang.integration PROPERTIES TIMEOUT 45)
    add_test(NAME moulang.string-escapes COMMAND "${MOULANG_POWERSHELL}"
        -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tests/integration/string-escapes.ps1"
        -Compiler "$<TARGET_FILE:moulang>" -TestRoot "${CMAKE_BINARY_DIR}/string-escapes")
    set_tests_properties(moulang.string-escapes PROPERTIES TIMEOUT 45)
    add_test(NAME moulang.text COMMAND "${MOULANG_POWERSHELL}"
        -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tests/integration/text.ps1"
        -Compiler "$<TARGET_FILE:moulang>" -TestRoot "${CMAKE_BINARY_DIR}/text")
    set_tests_properties(moulang.text PROPERTIES TIMEOUT 45)
    add_test(NAME moulang.diagnostics COMMAND "${MOULANG_POWERSHELL}"
        -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tests/integration/diagnostics.ps1"
        -Compiler "$<TARGET_FILE:moulang>" -TestRoot "${CMAKE_BINARY_DIR}/diagnostics")
    set_tests_properties(moulang.diagnostics PROPERTIES TIMEOUT 45)
    add_test(NAME moulang.print-block COMMAND "${MOULANG_POWERSHELL}"
        -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tests/integration/print-block.ps1"
        -Compiler "$<TARGET_FILE:moulang>" -TestRoot "${CMAKE_BINARY_DIR}/print-block")
    set_tests_properties(moulang.print-block PROPERTIES TIMEOUT 45)
    add_test(NAME moulang.string-lists COMMAND "${MOULANG_POWERSHELL}"
        -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tests/integration/string-lists.ps1"
        -Compiler "$<TARGET_FILE:moulang>" -TestRoot "${CMAKE_BINARY_DIR}/string-lists")
    set_tests_properties(moulang.string-lists PROPERTIES TIMEOUT 45)
    add_test(NAME moulang.nested-lists COMMAND "${MOULANG_POWERSHELL}"
        -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tests/integration/nested-lists.ps1"
        -Compiler "$<TARGET_FILE:moulang>" -TestRoot "${CMAKE_BINARY_DIR}/nested-lists")
    set_tests_properties(moulang.nested-lists PROPERTIES TIMEOUT 45)
    add_test(NAME moulang.text-files COMMAND "${MOULANG_POWERSHELL}"
        -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tests/integration/text-files.ps1"
        -Compiler "$<TARGET_FILE:moulang>" -TestRoot "${CMAKE_BINARY_DIR}/text-files")
    set_tests_properties(moulang.text-files PROPERTIES TIMEOUT 45)
    add_test(NAME moulang.long COMMAND "${MOULANG_POWERSHELL}"
        -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tests/integration/long.ps1"
        -Compiler "$<TARGET_FILE:moulang>" -TestRoot "${CMAKE_BINARY_DIR}/long")
    set_tests_properties(moulang.long PROPERTIES TIMEOUT 90)
    add_test(NAME moulang.integer-casts COMMAND "${MOULANG_POWERSHELL}"
        -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tests/integration/integer-casts.ps1"
        -Compiler "$<TARGET_FILE:moulang>" -TestRoot "${CMAKE_BINARY_DIR}/integer-casts")
    set_tests_properties(moulang.integer-casts PROPERTIES TIMEOUT 90)
    add_test(NAME moulang.standard-library COMMAND "${MOULANG_POWERSHELL}"
        -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tests/integration/standard-library.ps1"
        -Compiler "$<TARGET_FILE:moulang>" -TestRoot "${CMAKE_BINARY_DIR}/standard-library"
        -Configuration "${moulang_arc_configuration}")
    set_tests_properties(moulang.standard-library PROPERTIES TIMEOUT 90)
    add_test(NAME moulang.source-extensions COMMAND "${MOULANG_POWERSHELL}"
        -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tests/integration/source-extensions.ps1"
        -Compiler "$<TARGET_FILE:moulang>" -TestRoot "${CMAKE_BINARY_DIR}/source-extensions")
    set_tests_properties(moulang.source-extensions PROPERTIES TIMEOUT 90)
    add_test(NAME moulang.tiki COMMAND "${MOULANG_POWERSHELL}"
        -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tests/integration/tiki.ps1"
        -Compiler "$<TARGET_FILE:moulang>" -TestRoot "${CMAKE_BINARY_DIR}/tiki")
    set_tests_properties(moulang.tiki PROPERTIES TIMEOUT 45)
    add_test(NAME moulang.arc-tiki COMMAND "${MOULANG_POWERSHELL}"
        -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tests/integration/arc-tiki.ps1"
        -Compiler "$<TARGET_FILE:moulang>" -TestRoot "${CMAKE_BINARY_DIR}/arc-tiki"
        -Configuration "${moulang_arc_configuration}")
    set_tests_properties(moulang.arc-tiki PROPERTIES TIMEOUT 90)
    add_test(NAME moulang.relocation COMMAND "${MOULANG_POWERSHELL}"
        -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tests/integration/relocation.ps1"
        -Compiler "$<TARGET_FILE:moulang>" -Examples "${CMAKE_SOURCE_DIR}/examples"
        -TestRoot "${CMAKE_BINARY_DIR}/relocation")
    set_tests_properties(moulang.relocation PROPERTIES TIMEOUT 90)
    add_test(NAME moulang.arc COMMAND "${MOULANG_POWERSHELL}"
        -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tests/integration/arc.ps1"
        -Compiler "$<TARGET_FILE:moulang>" -Cxx "${MOULANG_MINGW_CXX}"
        -TestRoot "${CMAKE_BINARY_DIR}/arc" -Configuration "${moulang_arc_configuration}")
    set_tests_properties(moulang.arc PROPERTIES TIMEOUT 90)
    add_test(NAME moulang.text-foreach COMMAND "${MOULANG_POWERSHELL}"
        -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tests/integration/text-foreach.ps1"
        -Compiler "$<TARGET_FILE:moulang>" -TestRoot "${CMAKE_BINARY_DIR}/text-foreach"
        -Configuration "${moulang_arc_configuration}")
    set_tests_properties(moulang.text-foreach PROPERTIES TIMEOUT 45)
    foreach(moulang_collection_test IN ITEMS list-foreach ascii-index int-lists)
        add_test(NAME moulang.${moulang_collection_test} COMMAND "${MOULANG_POWERSHELL}"
            -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/tests/integration/${moulang_collection_test}.ps1"
            -Compiler "$<TARGET_FILE:moulang>" -TestRoot "${CMAKE_BINARY_DIR}/${moulang_collection_test}"
            -Configuration "${moulang_arc_configuration}")
        set_tests_properties(moulang.${moulang_collection_test} PROPERTIES TIMEOUT 45)
    endforeach()
    add_test(NAME moulang.static-runtime COMMAND "${MOULANG_POWERSHELL}"
        -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tests/integration/static-runtime.ps1"
        -Compiler "$<TARGET_FILE:moulang>" -ReadObject "${moulang_llvm_tools}/llvm-readobj.exe"
        -Examples "${CMAKE_SOURCE_DIR}/examples" -TestRoot "${CMAKE_BINARY_DIR}/static-runtime")
    add_test(NAME moulang.toolchain-integrity COMMAND "${MOULANG_POWERSHELL}"
        -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tests/integration/toolchain-integrity.ps1"
        -Installer "${CMAKE_SOURCE_DIR}/tools/Install-Toolchains.ps1"
        -LockFile "${CMAKE_SOURCE_DIR}/tools/toolchains.json" -TestRoot "${CMAKE_BINARY_DIR}/toolchain-integrity")
    add_test(NAME moulang.install-integrity COMMAND "${MOULANG_POWERSHELL}"
        -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tests/integration/install-integrity.ps1"
        -Verifier "${CMAKE_SOURCE_DIR}/tools/Verify-Install.ps1" -TestRoot "${CMAKE_BINARY_DIR}/install-integrity")
    add_test(NAME moulang.privacy-scanner COMMAND "${MOULANG_POWERSHELL}"
        -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tests/integration/artifact-privacy.ps1"
        -Scanner "${CMAKE_SOURCE_DIR}/tools/Check-ArtifactPrivacy.ps1"
        -TestRoot "${CMAKE_BINARY_DIR}/privacy-scanner")
    if(MOULANG_HAS_PATHMAP AND CMAKE_BUILD_TYPE MATCHES "^(Release|MinSizeRel)$")
        foreach(moulang_artifact IN ITEMS moulang moulang_runtime)
            add_test(NAME moulang.privacy-${moulang_artifact} COMMAND "${MOULANG_POWERSHELL}"
                -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tools/Check-ArtifactPrivacy.ps1"
                -Artifact "$<TARGET_FILE:${moulang_artifact}>"
                -ForbiddenPrefix "${CMAKE_SOURCE_DIR}")
        endforeach()
        add_library(moulang_privacy_fixture OBJECT tests/integration/privacy-source.cpp)
        moulang_apply_build_privacy(moulang_privacy_fixture)
        add_test(NAME moulang.privacy-source COMMAND "${MOULANG_POWERSHELL}"
            -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tools/Check-ArtifactPrivacy.ps1"
            -Artifact "$<TARGET_OBJECTS:moulang_privacy_fixture>"
            -ForbiddenPrefix "${CMAKE_SOURCE_DIR}")
    endif()
endif()
