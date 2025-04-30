cmake_minimum_required(VERSION 3.23)

# MacOS deployment target must be set before the `project` call
if (NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 14.0 CACHE INTERNAL "")
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64")
endif()

project( minio
        VERSION     0.1.0
        DESCRIPTION "Small C++ I/O, formatting, logging library"
        LANGUAGES   CXX
)


set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)


if (MSVC)
    # MSVC:
    add_definitions(-DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0A00 -DNOMINMAX)
    add_compile_options(
            "/wd4068;/wd4100;/wd4244;/wd4267"
            # /W4         # I think this means 'turn on lots of warnings'
            # /wd4068     # ignore unknown pragma
            # /wd4100     # ignore unused fn parameters
            # /wd4244     # ignore implicit sign conversion
            # /wd4267     # ignore implicit integer truncation
    )
else()
    # Clang & GCC:
    add_compile_options(
            -Werror
            -Wall
            -Wpedantic
            -Wno-assume             # Lots of bogus "argument to '__builtin_assume' has side effects"
            -Wno-unknown-pragmas
    )
    if (CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
        # GCC-specific:
        add_compile_options(
                -Wno-psabi          # suppress annoying GCC ABI warning
                -Wno-sign-compare   # apparently comes with `pedantic` in GCC
                -D_FORTIFY_SOURCE=2 # static+dynamic buffer-overflow checks
        )
    else()
        # Clang-specific:
        add_compile_options(
                -Wno-nullability-extension
                -Wno-gnu-zero-variadic-macro-arguments
                -Wno-gnu-conditional-omitted-operand    # Allow `x ?: y`
                -Wno-gnu-statement-expression           # Allow `({...})`
                -Wno-gnu-statement-expression-from-macro-expansion
        )
    endif()
endif()


add_library(minio STATIC
        src/format.cc
        src/logger.cc
        src/stream.cc
        src/string_utils.cc
)

target_include_directories(minio PUBLIC
    include
)


# INSTALLATION:

target_sources(minio PUBLIC FILE_SET HEADERS
    BASE_DIRS include
    FILES
    include/minio/base.hh
    include/minio/format.hh
    include/minio/format_impl.hh
    include/minio/istream.hh
    include/minio/logger.hh
    include/minio/ostream.hh
    include/minio/streamfwd.hh
    include/minio/string_utils.hh
    include/minio/varint.hh
)

install(TARGETS minio
    FILE_SET HEADERS
    ARCHIVE
)
