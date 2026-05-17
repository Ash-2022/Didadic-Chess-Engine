cmake_minimum_required(VERSION 3.26)
project(chess_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_COLOR_DIAGNOSTICS ON)

# Detect build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

message(STATUS "====================================")
message(STATUS "Chess Engine Build Configuration")
message(STATUS "====================================")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "System: ${CMAKE_SYSTEM_NAME}")
message(STATUS "Processor: ${CMAKE_SYSTEM_PROCESSOR}")

# ============================================
# OPTIMIZATION OPTIONS
# ============================================

option(ENABLE_AVX2 "Enable AVX2 SIMD instructions" ON)
option(ENABLE_BMI2 "Enable BMI2 instructions (PEXT/PDEP)" OFF)
option(ENABLE_LTO "Enable Link-Time Optimization" ON)
option(PRINT_MAGICS "Print magic numbers at startup" OFF)

message(STATUS "AVX2: ${ENABLE_AVX2}")
message(STATUS "BMI2: ${ENABLE_BMI2}")
message(STATUS "LTO: ${ENABLE_LTO}")

# ============================================
# SOURCE FILES
# ============================================

set(ENGINE_SOURCES
    Engine_Core/src/attacks.cpp
    Engine_Core/src/evaluation.cpp
    Engine_Core/src/magics.cpp
    Engine_Core/src/movegen.cpp
    Engine_Core/src/perft.cpp
    Engine_Core/src/position.cpp
    Engine_Core/src/pst.cpp
    Engine_Core/src/search.cpp
    Engine_Core/src/tt_atomic.cpp
    Engine_Core/src/thread_pool.cpp
    Engine_Core/src/uci.cpp
    Engine_Core/src/zobrist.cpp
)

set(ENGINE_HEADERS
    Engine_Core/src/attacks.h
    Engine_Core/src/bitboard.h
    Engine_Core/src/evaluation.h
    Engine_Core/src/history.h
    Engine_Core/src/magics.h
    Engine_Core/src/movegen.h
    Engine_Core/src/perft.h
    Engine_Core/src/position.h
    Engine_Core/src/pst.h
    Engine_Core/src/pst_tables.h
    Engine_Core/src/search.h
    Engine_Core/src/thread_pool.h
    Engine_Core/src/tt_atomic.h
    Engine_Core/src/types.h
    Engine_Core/src/uci.h
    Engine_Core/src/utilities.h
    Engine_Core/src/zobrist.h
)

# ============================================
# CORE LIBRARY
# ============================================

add_library(engine_core STATIC ${ENGINE_SOURCES} ${ENGINE_HEADERS})

target_include_directories(engine_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine_Core/src
)

# ============================================
# COMPILER-SPECIFIC OPTIMIZATIONS
# ============================================

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(STATUS "Applying Clang-specific optimizations")
    
    target_compile_options(engine_core PRIVATE
        # Release optimization flags
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-march=native>
        $<$<CONFIG:Release>:-mtune=native>
        $<$<CONFIG:Release>:-ffast-math>
        $<$<CONFIG:Release>:-funroll-loops>
        $<$<CONFIG:Release>:-fvectorize>
        $<$<CONFIG:Release>:-fslp-vectorize>
        $<$<CONFIG:Release>:-fomit-frame-pointer>
        
        # Debug flags
        $<$<CONFIG:Debug>:-O0>
        $<$<CONFIG:Debug>:-g>
        $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
        $<$<CONFIG:Debug>:-fstandalone-debug>
        
        # RelWithDebInfo (for profiling)
        $<$<CONFIG:RelWithDebInfo>:-O2>
        $<$<CONFIG:RelWithDebInfo>:-g>
        $<$<CONFIG:RelWithDebInfo>:-fno-omit-frame-pointer>
    )
    
    # LTO
    if(ENABLE_LTO)
        target_compile_options(engine_core PRIVATE
            $<$<CONFIG:Release>:-flto=thin>
        )
        target_link_options(engine_core PRIVATE
            $<$<CONFIG:Release>:-flto=thin>
            $<$<CONFIG:Release>:-fuse-ld=lld>
        )
    endif()
    
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    message(STATUS "Applying GCC-specific optimizations")
    
    target_compile_options(engine_core PRIVATE
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-march=native>
        $<$<CONFIG:Release>:-mtune=native>
        $<$<CONFIG:Release>:-ffast-math>
        $<$<CONFIG:Release>:-funroll-loops>
        
        $<$<CONFIG:Debug>:-O0>
        $<$<CONFIG:Debug>:-g>
    )
    
    if(ENABLE_LTO)
        target_compile_options(engine_core PRIVATE
            $<$<CONFIG:Release>:-flto>
        )
        target_link_options(engine_core PRIVATE
            $<$<CONFIG:Release>:-flto>
        )
    endif()
    
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    message(STATUS "Applying MSVC-specific optimizations")
    
    target_compile_options(engine_core PRIVATE
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL>
        $<$<CONFIG:Release>:/favor:AMD64>
        
        $<$<CONFIG:Debug>:/Od>
        $<$<CONFIG:Debug>:/Zi>
    )
    
    if(ENABLE_LTO)
        target_link_options(engine_core PRIVATE
            $<$<CONFIG:Release>:/LTCG>
        )
    endif()
endif()

# ============================================
# SIMD OPTIMIZATIONS
# ============================================

if(ENABLE_AVX2)
    if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        target_compile_options(engine_core PRIVATE /arch:AVX2)
    else()
        target_compile_options(engine_core PRIVATE -mavx2 -mfma)
    endif()
    message(STATUS "AVX2 enabled")
endif()

if(ENABLE_BMI2)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        target_compile_options(engine_core PRIVATE -mbmi2)
    endif()
    message(STATUS "BMI2 enabled")
endif()

# ============================================
# FEATURE FLAGS
# ============================================

if(PRINT_MAGICS)
    add_compile_definitions(PRINT_MAGICS)
endif()

# ============================================
# EXECUTABLE
# ============================================

add_executable(chess_engine Engine_Core/src/main.cpp)
target_link_libraries(chess_engine PRIVATE engine_core)

# ============================================
# INSTALLATION
# ============================================

install(TARGETS chess_engine
        RUNTIME DESTINATION bin)

# ============================================
# CUSTOM TARGETS
# ============================================

# Benchmark target
add_custom_target(bench
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/chess_engine
    DEPENDS chess_engine
    COMMENT "Running chess engine benchmark"
)

# Performance test
add_custom_target(perf-test
    COMMAND ${CMAKE_COMMAND} -E echo "Running performance test..."
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/chess_engine
    DEPENDS chess_engine
    COMMENT "Performance testing"
)

# ============================================
# SUMMARY
# ============================================

message(STATUS "====================================")
message(STATUS "Build Summary:")
message(STATUS "  Source dir: Engine_Core/src")
message(STATUS "  Output: ${CMAKE_CURRENT_BINARY_DIR}/chess_engine")
message(STATUS "  Install: ${CMAKE_INSTALL_PREFIX}/bin/chess_engine")
message(STATUS "====================================")
