include(CheckCXXSourceCompiles)
include(CheckTypeSize)
include(TestBigEndian)

check_cxx_source_compiles(
    "int main() { __float128 value = 0; (void)value; return 0; }"
    UGRIPPER_FASTCDR_HAVE_FLOAT128
)
check_type_size("long double" UGRIPPER_FASTCDR_SIZEOF_LONG_DOUBLE LANGUAGE CXX)
test_big_endian(UGRIPPER_FASTCDR_IS_BIG_ENDIAN)

set(HAVE_CXX11 1)
set(HAVE_CXX0X 1)
set(HAVE_CXX_CONSTEXPR 1)
if(UGRIPPER_FASTCDR_IS_BIG_ENDIAN)
    set(FASTCDR_IS_BIG_ENDIAN_TARGET 1)
else()
    set(FASTCDR_IS_BIG_ENDIAN_TARGET 0)
endif()
if(UGRIPPER_FASTCDR_HAVE_FLOAT128)
    set(FASTCDR_HAVE_FLOAT128 1)
else()
    set(FASTCDR_HAVE_FLOAT128 0)
endif()
set(FASTCDR_SIZEOF_LONG_DOUBLE ${UGRIPPER_FASTCDR_SIZEOF_LONG_DOUBLE})
set(PROJECT_VERSION_MAJOR 1)
set(PROJECT_VERSION_MINOR 0)
set(PROJECT_VERSION_PATCH 23)
set(PROJECT_VERSION 1.0.23)

configure_file(
    include/fastcdr/config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/fastcdr/config.h
    @ONLY
)

add_library(ugripper_fastcdr STATIC
    src/Cdr.cpp
    src/FastBuffer.cpp
    src/FastCdr.cpp
    src/exceptions/BadParamException.cpp
    src/exceptions/Exception.cpp
    src/exceptions/NotEnoughMemoryException.cpp
)
set_target_properties(ugripper_fastcdr PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)
target_compile_definitions(ugripper_fastcdr PRIVATE FASTCDR_SOURCE)
target_include_directories(ugripper_fastcdr
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}/include
)
add_library(FastCDR::fastcdr ALIAS ugripper_fastcdr)
