//
// mini/format.hh
//
// Copyright 2023-Present Couchbase, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#pragma once
#include "minio/ostream.hh"
#include <concepts>
#include <cstdarg>
#include <stdexcept>
#include <string>
#include <string_view>

/*
 A string formatting API mostly compatible with `std::format`, but optimized for small code size.
 https://en.cppreference.com/w/cpp/utility/format/spec

 It also supports `printf` format strings, with many benefits over regular `printf`:
 - Safer: type mismatches and missing arguments are detected at compile time.
 - Excess arguments are allowed and are formatted and added to the output.
 - Directly returns a `std::string`, or writes to `ostream`s.
 - No need to use `l` modifiers with long integer types.
 - A `bool` argument can be formatted with '%s', resulting in "true" or "false".
 - Doesn't crash if a null pointer is passed to '%s'.
 - Supports `std::string` and `std::string_view` arguments.
 - In fact it supports any type that can be written to an `ostream` with `operator<<`.
 - Probably somewhat faster because parsing is done at compile-time not runtime (not benchmarked!)

 Limitations:
 - Only 10 arguments are allowed. (You can change this by changing FormatString::kMaxSpecs.)
 - Field width and precision are limited to 255.

 `std::format` limitations:
 - You can't create custom formatters that interpret custom field specs. (But you can format
   custom types by implementing `operator<<`.)
 - Arguments can't be reordered (i.e. a field spec like `{nn:}` isn't allowed.)
 - Field widths & alignment are not Unicode-aware; they assume 1 byte == 1 space.
 - Localized variants are unimplemented (using 'L' in a format spec has no effect.)

 `printf` limitations:
 - Field width or precision '*' are not supported.

 Compatibility:
 - If building for Apple platforms, and your deployment version is earlier than macOS 13.4 or
   iOS/watchOS/tvOS 16.3, floating-point formatting specifiers are ignored. This is because the
   floating-point versions of `std::to_chars()` aren't available in older versions of libc++.

 Known bugs:
 - When a number is zero-padded, the zeroes go before the sign character, not afterward.
 - When the alternate ('#') form of a float adds a decimal point, it will go after any exponent,
   when it should go before.
 */

namespace snej::minio {

#pragma mark - ARGUMENT TYPES:


    namespace i { // internal stuff
        // Enumeration identifying the argument types passed to format functions.
        enum class ArgType : uint8_t {
            None = 0, Bool, Char, Int, UInt, Long, ULong, LongLong, ULongLong, Double,
            CString, String, StringView, Pointer, Arg
        };

        struct ostreamableArg;
        using enum ArgType;

        // This maps types to FmtID values. Every formattable type needs an entry here.
        template <typename T> struct Formatting { };
        template<> struct Formatting<bool>              { static constexpr ArgType id = Bool; };
        template<> struct Formatting<char>              { static constexpr ArgType id = Char; };
        template<> struct Formatting<signed char>       { static constexpr ArgType id = Char; };
        template<> struct Formatting<unsigned char>     { static constexpr ArgType id = UInt; };
        template<> struct Formatting<short>             { static constexpr ArgType id = Int; };
        template<> struct Formatting<unsigned short>    { static constexpr ArgType id = UInt; };
        template<> struct Formatting<int>               { static constexpr ArgType id = Int; };
        template<> struct Formatting<unsigned int>      { static constexpr ArgType id = UInt; };
        template<> struct Formatting<long>              { static constexpr ArgType id = Long; };
        template<> struct Formatting<unsigned long>     { static constexpr ArgType id = ULong; };
        template<> struct Formatting<long long>         { static constexpr ArgType id = LongLong; };
        template<> struct Formatting<unsigned long long>{ static constexpr ArgType id = ULongLong;};
        template<> struct Formatting<float>             { static constexpr ArgType id = Double; };
        template<> struct Formatting<double>            { static constexpr ArgType id = Double; };
        template<> struct Formatting<const char*>       { static constexpr ArgType id = CString; };
        template<> struct Formatting<char*>             { static constexpr ArgType id = CString; };
        template<> struct Formatting<const void*>       { static constexpr ArgType id = Pointer; };
        template<> struct Formatting<void*>             { static constexpr ArgType id = Pointer; };
        template<> struct Formatting<std::string>       { static constexpr ArgType id = String; };
        template<> struct Formatting<std::string_view>  { static constexpr ArgType id= StringView;};
        template<> struct Formatting<ostreamableArg>    { static constexpr ArgType id = Arg; };
        template <ostreamable T> struct Formatting<T>   { static constexpr ArgType id = Arg; };

        // Struct that type-erases an `ostreamable` value; passed as arg to formatting fns.
        // Since this is passed through varargs it cannot have a constructor.
        struct ostreamableArg {
            template <ostreamable T>
            static ostreamableArg make(T &&value) {
                return ostreamableArg{._ptr = reinterpret_cast<const void*>(&value),
                           ._write = &writeFn<std::remove_cvref_t<T>>};
            }

            void writeTo(ostream& out) const                        {_write(out, _ptr);}

            using writeFn_t = void (*)(ostream&, const void*);

            template <typename T>
            static void writeFn(ostream& out, const void* ptr)      { out << *(const T*)ptr; }

            const void* _ptr;                       // address of value -- a type-erased `T*`
            writeFn_t   _write;                     // address of `writeFn<T>()`
        };

        // `passArg()` transforms args before they're passed as varargs to `format`.
        template <std::integral T>       auto passArg(T t)  {return t;}
        template <std::floating_point T> auto passArg(T t)  {return t;}
        inline auto passArg(char* t)                        {return t;}
        inline auto passArg(const char* t)                  {return t;}
        inline auto passArg(void* t)                        {return t;}
        inline auto passArg(const void* t)                  {return t;}
        inline auto passArg(std::string const& t)           {return &t;} // pass by reference!
        inline auto passArg(std::string_view const& t)      {return &t;} // pass by reference!

        template <ostreamable T> // ostreamable values are passed as a type-erased struct
        requires (i::Formatting<T>::id == ArgType::Arg)
        ostreamableArg passArg(T const& t)                  {return ostreamableArg::make(t);}
    }


    /** The concept `Formattable` matches the types that can be passed as args to `format`. */
    template <typename T>
    concept Formattable = requires { i::Formatting<std::decay_t<T>>::id; };


    /** A pointer to a C array of argument types, terminated with `None`.
        This is constructed at compile-time and passed to the `format` implementation. */
    using ArgTypeList = i::ArgType const*;


    // `ArgTypes<...>::ids` is an `ArgTypeList` whose items match the template arguments.
    template<Formattable... Args>
    struct ArgTypes {
        static constexpr i::ArgType ids[] {i::Formatting<std::decay_t<Args>>::id... ,
                                           i::ArgType::None};
    };


#pragma mark - FORMAT STRING:


    /** A compiled format string. Functions that are passed format strings by other functions
        instead of being called directly should take a `FormatString const&` parameter; see
        `format_types()` and `vformat_types` as examples. */
    class FormatString {
    public:
        enum class Syntax : uint8_t {printf, fmt};
        consteval FormatString(Syntax syntax, const char* cstr, ArgTypeList argTypes)
        :_impl(parse(syntax, cstr, argTypes)) { }

        string_view get() const     {return _impl._str;}

        enum class align_t : uint8_t {left, center, right};
        enum class sign_t  : uint8_t {minusOnly, minusPlus, minusSpace};
        static constexpr uint8_t kDefaultPrecision = 255;

        // Parsed format specifier
        struct Spec {
            char    type            = 0;                    // data type char; 0 if unspecified
            char    fill            = ' ';                  // fill character
            uint8_t width           = 0;                    // field width
            uint8_t precision       = kDefaultPrecision;    // field precision
            align_t align       :2  = align_t::left;        // field alignment
            sign_t  sign        :2  = sign_t::minusOnly;    // whether/how to add sign character
            bool    alternate   :1  = false;                // use alternate form; type-specific
            bool    localize    :1  = false;                // 'L' specifier in fmt syntax
            friend constexpr bool operator==(Spec const& a, Spec const& b) = default;
        };

        // iterator over format string & specifiers
        class iterator {
        public:
            bool isLiteral() const;
            string_view literal() const;
            Spec const& spec() const                    {return *_pSpec;}
            iterator& operator++ ();
            friend bool operator== (iterator const& a, iterator const& b) {
                return a._pLength == b._pLength;}
        private:
            friend class FormatString;
            explicit iterator(FormatString const& fmt);
            explicit iterator(int8_t const* endLength) :_pLength(endLength) { }

            const char*     _str;
            int8_t const*   _pLength;
            Spec const*     _pSpec;
        };

        iterator begin() const {return iterator(*this);}
        iterator end()   const {return iterator(&_impl._lengths[_impl._nSegments]);}

        void format_types_to(ostream&, ArgTypeList types, ...) const;
        void vformat_types_to(ostream&, ArgTypeList types, va_list) const;
        string format_types( ArgTypeList types, ...) const;
        string vformat_types( ArgTypeList types, va_list) const;

#ifndef NDEBUG
        static FormatString testParse(Syntax syntax, const char* cstr, ArgTypeList argTypes) {
            return FormatString(parse(syntax, cstr, argTypes));
        }
#endif
    private:
        static constexpr size_t kMaxSpecs = 10;         // Max # of parameters in string
        static constexpr size_t kMaxSegments = 2 * kMaxSpecs + 1;

        struct Impl {
            const char* const   _str;                   // the format string
            uint8_t             _nSegments;             // number of pieces; size of _lengths[]
            int8_t              _lengths[kMaxSegments]; // length in bytes of each piece of _str
            Spec                _specs[kMaxSpecs];      // format specs, in order
        };

        explicit FormatString(Impl const& impl) :_impl(impl) { }
        static constexpr Impl parse(Syntax, const char* cstr, ArgTypeList);

        Impl const _impl;
    };

    // Templated subclass of FormatString that takes the arg types as format parameters,
    // so that the ArgTypeList can be passed to the parent constructor for compile-time checking.
    // (The `type_identity` thing is magic; I just copied it from the `std::format_string` docs:
    // https://en.cppreference.com/w/cpp/utility/format/basic_format_string )
    template<FormatString::Syntax S, Formattable... Args>
    class FormatString_ : public FormatString {
    public:
        consteval FormatString_(const char* s) :FormatString(S, s, ArgTypes<Args...>::ids) { }
    };

    /** A format string, templated by the types of the runtime arguments.
        Client-visible formatting functions should take a (reference to) this type,
        using the parameter pack as the template args. See `format()` for example. */
    template<Formattable... Args>
    using FmtFormatString = FormatString_<FormatString::Syntax::fmt, std::type_identity_t<std::decay_t<Args>>...>;


    /** A format string that uses `printf` syntax. */
    template<Formattable... Args>
    using PrintfFormatString = FormatString_<FormatString::Syntax::printf, std::type_identity_t<std::decay_t<Args>>...>;


    /** Configuration parameter: If this is `true`, the `format` functions allow you to pass more
        arguments than are specified in the format string. The extra arguments are printed after
        a ":", with default formatting, and separated by ","s. */
    static constexpr bool kAllowExtraArgs = true;


    /** Writes formatted output to a `mini::ostream`.
        @param out  The stream to write to.
        @param fmt  Format string. Must be a string literal, with `{…}` placeholders for args.
        @param args  Arguments, of any types satisfying `Formattable`. */
    template<Formattable... Args>
    void format_to(ostream& out, FmtFormatString<Args...> const& fmt, Args &&...args) {
        fmt.format_types_to(out, ArgTypes<Args...>::ids, i::passArg(args)...);
    }


    /** Returns a formatted string. This is mostly a drop-in replacement for `std::format()`.
        @param fmt  Format string. Must be a string literal, with `{…}` placeholders for args.
        @param args  Arguments, of any types satisfying `Formattable`. */
    template<Formattable... Args>
    string format(FmtFormatString<Args...> const& fmt, Args &&...args) {
        return fmt.format_types(ArgTypes<Args...>::ids, i::passArg(args)...);
    }


    /** Writes formatted output to a `mini::ostream` using `printf` syntax.
        @param out  The stream to write to.
        @param fmt  Format string. Must be a string literal, with `%` placeholders for args.
        @param args  Arguments, of any types satisfying `Formattable`. */
    template<Formattable... Args>
    void printf(ostream& out, PrintfFormatString<Args...> const& fmt, Args &&...args) {
        fmt.format_types_to(out, ArgTypes<Args...>::ids, i::passArg(args)...);
    }


    /** Returns a formatted string using `printf` syntax.
        @param fmt  Format string. Must be a string literal, with `%` placeholders for args.
        @param args  Arguments, of any types satisfying `Formattable`. */
    template<Formattable... Args>
    string sprintf(PrintfFormatString<Args...> const& fmt, Args &&...args) {
        return fmt.format_types(ArgTypes<Args...>::ids, i::passArg(args)...);
    }


    /** Exception thrown for invalid format specs or invalid arguments.
        Mostly gets thrown _at compile time_, which manifests as a confusing build error about
        a `consteval` function calling a runtime-only function (i.e. `throw`.) */
    class format_error : public std::runtime_error {
    public:
        explicit format_error(const char *msg) :runtime_error(msg) { }
    };

}

// Implementations of the constexpr methods:
#include "format_impl.hh"
