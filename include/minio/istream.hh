//
// mini/istream.hh
//
// Copyright © 2024 Jens Alfke. All rights reserved.
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
#include "varint.hh"
#include <concepts>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

ASSUME_NONNULL_BEGIN

namespace snej::minio {

    /** Abstract base class of input streams. */
    class istream {
    public:
        virtual ~istream() = default;

        /// Current position in the stream (number of bytes read.)
        virtual size_t offset() const =0;

        /// True if at the end of the data.
        virtual bool eof() const noexcept =0;

        /// Reads up to `maxLen` bytes, copying to `dst`.
        size_t read(void* dst, size_t maxLen);

        size_t read(std::span<char> dst)                {return read(dst.data(), dst.size_bytes());}
        size_t read(std::span<std::byte> dst)           {return read(dst.data(), dst.size_bytes());}

        void readFull(void* dst, size_t maxLen);
        void readFull(std::span<char> dst)              {readFull(dst.data(), dst.size_bytes());}
        void readFull(std::span<std::byte> dst)         {readFull(dst.data(), dst.size_bytes());}

        template <typename T>
        T readRaw() {
            T val;
            readFull(&val, sizeof(T));
            return val;
        }

        template <typename T>
        void readRaw(T* _Nonnull val) {
            readFull(val, sizeof(T));
        }

        template <std::unsigned_integral U>
        U readVarint() {
            uint8_t intBuf[varint::maxSize<U>()];
            readFull(&intBuf[0], 1);
            if (size_t rem = varint::remainingSize(intBuf[0]); rem > 0) {
                readFull(&intBuf[1], rem);
                return varint::getInt<U>(std::span{&intBuf[0], 1 + rem});
            } else {
                return intBuf[0];
            }
        }

        std::string readVarString();

    protected:
        istream() = default;
        istream(const void* start, size_t len)          :_buf(start, len) { }
        istream(istream const&) = delete;
        istream& operator=(istream const&) = delete;

        /// An unbuffered read, from the data source directly to the destination.
        virtual size_t readUnbuffered(void* dst, size_t len) =0;

        /// Called when the buffer is emptied; implementation should set `_buf` to the next data.
        virtual void fillBuffer() =0;

        /** Internal buffer. istream reads from this buffer until it hits the end. */
        class buffer {
        public:
            buffer() = default;
            buffer(const void* start, size_t len)       {set(start, len);}
            void set(const void* start, size_t len) {
                _start = _next = (char*)start;
                _avail = len;
            }
            const char* next() const pure               {return _next;}
            size_t offset() const pure                  {return _next - _start;}
            size_t available() const pure               {return _avail;}
            size_t capacity() const pure                {return _next - _start + _avail;}
            string_view contents() const pure           {return {_next, _avail};}
            size_t read(void* dst, size_t len);
            bool skip(size_t len);

        private:
            const char*   _start {};   // Start of buffer
            const char*   _next {};    // Next available address in buffer to read from
            size_t        _avail {};   // Number of bytes remaining to read
        };

        buffer _buf;
    };


    /** istream that reads from memory. Caller is responsible for keeping the memory valid. */
    class imemstream : public istream {
    public:
        imemstream(const void* start, size_t len)       :istream(start, len) {}
        explicit imemstream(std::span<const std::byte> s) :istream(s.data(), s.size_bytes()) {}
        explicit imemstream(std::string_view s)         :istream(s.data(), s.size()) {}

        bool eof() const noexcept override              {return _buf.available() == 0;}
        size_t offset() const override                  {return _buf.offset();}

        /// A pointer to the next byte that will be read.
        const char* next() const                        {return _buf.next();}

        /// Resets the stream to a new memory range.
        virtual void reset(const void* start, size_t len = SIZE_MAX) {_buf.set(start, len);}
        void reset(string_view s)                       {reset(s.data(), s.size());}

        /// Same as `readFull`, but returns the address range without copying it.
        virtual string_view readFullNoCopy(size_t size);

        /// Same as `readVarString`, but doesn't copy the string.
        string_view readVarStringView()               {return readFullNoCopy(readVarint<size_t>());}

    protected:
        imemstream() = default;
        explicit imemstream(string&&) = delete;
        explicit imemstream(std::vector<std::byte>&&) = delete;
        size_t readUnbuffered(void*, size_t) override     {return 0;}
        void fillBuffer() override                      { }
    };


    /** istream that reads from a (copied) string. */
    class istringstream final : public imemstream {
    public:
        explicit istringstream(string_view s)        {reset(s);}
        explicit istringstream(string s)             {reset(std::move(s));}
        void reset(const void* s, size_t n) override {reset(string((const char*)s, n));}
        void reset(string_view s)                    {imemstream::reset(s);}
        void reset(string s)                         {_str = std::move(s); imemstream::reset(_str.data(), _str.size());}
    private:
        std::string _str;
    };


    /** Minimalist file input stream. */
    class ifilestream final : public istream {
    public:
        explicit ifilestream(std::string const& filename, const char* mode = "r");
        explicit ifilestream(std::string_view filename, const char* mode = "r");
        explicit ifilestream(FILE* f)                      :_fd(f), _owned(false) { }
        ~ifilestream();

        size_t offset() const override;
        bool eof() const noexcept override; //FIXME: Doesn't return true until after truncated read

    private:
        size_t readUnbuffered(void*, size_t) override;
        void fillBuffer() override;

        FILE* _fd;
        bool  _owned;
        std::array<char,kFileStreamBufferSize> _buffer;
    };

}

ASSUME_NONNULL_END
