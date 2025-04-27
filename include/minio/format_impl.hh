//
// MiniFormat_Impl.hh
//
// Copyright 2025-Present Couchbase, Inc. All rights reserved.
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


// This header is included only by MiniFormat.hh; don't include it directly.
// It contains constexpr function bodies declared in MiniFormat, which have to be in a header.

namespace snej::minio {
    namespace i {
        static constexpr char kDefaultTypeCharForArgType[] = " scdddddd sssp ";
        static_assert(std::size(kDefaultTypeCharForArgType) == size_t(ArgType::Arg) + 2);

        static constexpr const char* kValidTypeCharsForArgType[15] = {
            nullptr,
            "sbBdoxX",      // Bool
            "cbBdoxX",      // Char
            "bBcdoxX", "bBcdoxX", "bBcdoxX", "bBcdoxX", "bBcdoxX", "bBcdoxX",  // integers
            "aAeEfFgG",     // Double
            "s", "s", "s",  // strings
            "pP",           // Pointer
            "s"             // Arg
        };

        static constexpr const char* kValidPrintfTypeCharsForArgType[15] = {
            nullptr,
            "sduoxX",       // Bool
            "cdiuoxX",      // Char
            "cdiuoxX", "cdiuoxX", "cdiuoxX", "cdiuoxX", "cdiuoxX", "cdiuoxX",  // integers
            "aAeEfFgG",     // Double
            "s", "s", "s",  // strings
            "pP",           // Pointer
            "s"             // Arg
        };


        static constexpr void require(bool condition, const char* exceptionMessage) {
            if (!condition) throw format_error(exceptionMessage);
        }

        // constexpr and non-localized versions of ctypes fns
        static constexpr bool isdigit(char c)   {return c >= '0' && c <= '9';}
        static constexpr bool isalpha(char c)   {return (c >= 'A' && c <= 'Z') ||
                                                        (c >= 'a' && c <= 'z');}
        static constexpr int digittoint(char c) {return c - '0';}

        static constexpr uint8_t parseUint(const char*& str) {
            unsigned w = 0;
            while (isdigit(*str)) {
                w = 10 * w + digittoint(*str++);
                require(w <= 0xFF, "Format width/precision too large");
            }
            return uint8_t(w);
        }

        constexpr void parseTypeCode(FormatString::Spec& spec, char t, const char* validTypes) {
            spec.type = t;
            i::require(i::isalpha(t), "invalid format spec: invalid type character");
            if (validTypes) {
                while (*validTypes && *validTypes != t)
                    ++validTypes;
                i::require(*validTypes, "invalid format spec: invalid spec for argument");
            }
        }

        // Parse a C++20 std::format specifier:
        constexpr void parseFmtItem(FormatString::Spec& spec, const char* str, i::ArgType argType) {
            using namespace i;
            // https://en.cppreference.com/w/cpp/utility/format/formatter
            // Set a default type based on the arg type:
            if (char c = kDefaultTypeCharForArgType[uint8_t(argType)]; c != ' ')
                spec.type = c;
            // Numbers default to right alignment:
            if (argType >= ArgType::Int && argType <= ArgType::Double)
                spec.align = FormatString::align_t::right;

            // parse argument number, if any:
            for (; *str != ':'; ++str) {
                if (*str == '}') return; // -> empty spec `{}`
                require(isdigit(*str), "invalid format spec: invalid arg number "
                                       "(did you forget the ':'?)");
                throw format_error("invalid format spec: arg numbers not supported "
                                   "(or did you forget the ':'?)");  //TODO: Implement arg numbers
            }
            ++str;
            if (str[0] == '}') return; // -> empty spec `{:}`

            // parse fill and align:
            char alignChar = 0;
            if (char c = str[0]; c == '<' || c == '^' || c == '>') {
                spec.fill = ' ';
                alignChar = c;
                str += 1;
            } else if (char c1 = str[1]; c1 == '<' || c1 == '^' || c1 == '>') {
                spec.fill = str[0];
                alignChar = c1;
                str += 2;
            }
            switch (alignChar) {
            case '<':   spec.align = FormatString::align_t::left; break;
            case '^':   spec.align = FormatString::align_t::center; break;
            case '>':   spec.align = FormatString::align_t::right; break;
            }

            // parse sign:
            switch (*str++) {
            case '}': return;
            case '-': spec.sign = FormatString::sign_t::minusOnly;  break;
            case '+': spec.sign = FormatString::sign_t::minusPlus;  break;
            case ' ': spec.sign = FormatString::sign_t::minusSpace; break;
            default:  --str; break;
            }

            // parse '#' and '0':
            if (*str == '#') {
                spec.alternate = true;
                ++str;
            }
            if (*str == '0' && alignChar == 0) {
                spec.fill = '0';
                spec.align = FormatString::align_t::right;
                ++str;
            }

            // parse field width:
            if (isdigit(*str))
                spec.width = parseUint(str);
            // parse precision:
            if (*str == '.') {
                require(isdigit(*++str), "invalid format spec: invalid precision");
                spec.precision = parseUint(str);
            }
            // parse "localized" specifier:
            if (*str == 'L') {
                spec.localize = true;
                ++str;
            }
            // parse type code:
            if (*str != '}') {
                parseTypeCode(spec, *str++, kValidTypeCharsForArgType[uint8_t(argType)]);
                require(*str == '}', "invalid format spec: unknown chars at end");
            }
        }


        // Parse a printf format specifier:
        constexpr const char* parsePrintfItem(FormatString::Spec& spec, const char* str, i::ArgType argType) {
            using namespace i;
            // https://en.cppreference.com/w/c/io/fprintf

            // "one or more flags that modify the behavior of the conversion":
            spec.align = FormatString::align_t::right;
            for (bool readFlags = true; readFlags; str++) {
                switch (*str) {
                case '-':   spec.align = FormatString::align_t::left; break;
                case '+':   spec.sign = FormatString::sign_t::minusPlus; break;
                case ' ':   if (spec.sign == FormatString::sign_t::minusOnly)  // '+' overrides ' '
                    spec.sign = FormatString::sign_t::minusSpace;
                    break;
                case '#':   spec.alternate = true; break;
                case '0':   spec.fill = '0'; break;
                default:    --str; readFlags = false; break;
                }
            }
            if (spec.align == FormatString::align_t::left)
                spec.fill = ' ';        // '0' flag is ignored if '-' is present

            // parse field width:
            if (isdigit(*str))
                spec.width = uint8_t(parseUint(str));
            else
                require(*str != '*', "invalid format spec: field width '*' is not supported");

            // parse precision:
            if (*str == '.') {
                if (isdigit(*++str)) {
                    spec.precision = parseUint(str);
                } else {
                    require(*str++ == '*', "invalid format spec: invalid precision");
                    // Allow "%.*s" because it's used for string_views and slices
                    require(*str == 's', "invalid format spec: precision '*' is not supported");
                }
            }

            // "parse" (skip and ignore) length modifiers:
            char c = *str;
            while (c == 'h' || c == 'l' || c == 'j' || c == 'z' || c == 't' || c == 'L')
                c = *++str;

            // parse type code:
            parseTypeCode(spec, *str++, kValidPrintfTypeCharsForArgType[uint8_t(argType)]);
            return str;
        }

    }


    constexpr FormatString::Impl FormatString::parse(Syntax syntax,
                                                             const char* cstr,
                                                             ArgTypeList argTypes)
    {
        using namespace i;
        using enum Syntax;

        Impl impl {._str = cstr};
        size_t lastPos = 0, pos;
        unsigned nSpecs = 0;
        auto iArg = argTypes;

        // subroutine to append a string segment to `_lengths`
        enum SegmentType {literal = 1, escape = 0, specifier = -1};
        auto addSegment = [&](SegmentType type) {
            if (pos > lastPos) {
                require(impl._nSegments < kMaxSegments, "Sorry, too many format specifiers");
                require(pos - lastPos <= 0x7F, "Sorry, format string is too long");
                impl._lengths[impl._nSegments++] = int8_t((pos - lastPos) * type);
                lastPos = pos;
            }
        };

        const char* delims = (syntax == fmt) ? "{}" : "%";
        string_view str(cstr);
        while (string::npos != (pos = str.find_first_of(delims, lastPos))) {
            addSegment(literal);
            SegmentType type = specifier;
            if (syntax == fmt) {
                // C++20 std::format / fmt syntax:
                if (str[pos] == '}') {
                    // "}}" is an escape and should be emitted as "}". Otherwise a "} is a syntax error:
                    require(pos + 1 < str.size() && str[pos + 1] == '}', "Invalid '}' in format string");
                    pos += 2;
                    type = escape;
                } else if (pos + 1 < str.size() && str[pos + 1] == '{') {
                    // "{{" is an escape, emitted as "{":
                    pos += 2;
                    type = escape;
                } else {
                    // OK, we have a format specifier!
                    auto endPos = str.find('}', pos + 1);
                    require(endPos != string::npos, "Unclosed format specifier");
                    require(nSpecs < kMaxSpecs, "Too many format specifiers");
                    require(*iArg != ArgType::None, "More format specifiers than arguments");
                    parseFmtItem(impl._specs[nSpecs++], &str[pos + 1], *iArg++);
                    pos = endPos + 1;
                }
            } else {
                // printf syntax:
                require(pos + 1 < str.size(), "Invalid '%' at end of format string");
                if (str[pos + 1] == '%') {
                    // "%%" is a single literal percent
                    pos += 2;
                    type = escape;
                } else {
                    require(*iArg != ArgType::None, "More format specifiers than arguments");
                    const char* next = parsePrintfItem(impl._specs[nSpecs++], &str[pos + 1], *iArg++);
                    pos = next - cstr;
                }
            }
            addSegment(type);
        }

        // Add the last literal segment, if non-empty:
        pos = str.size();
        addSegment(literal);
        return impl;
    }
}
