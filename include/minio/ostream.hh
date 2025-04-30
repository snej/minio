//
// mini/ostream.hh
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
#include "minio/varint.hh"
#include <concepts>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

ASSUME_NONNULL_BEGIN

namespace snej::minio {
#ifdef _WIN32
    constexpr const char* endl = "\r\n";
#else
    constexpr const char* endl = "\n";
#endif


#pragma mark - OUTPUT STREAMS:


    /** Abstract base class of output streams. */
    class ostream {
    public:
        virtual ~ostream() = default;

        /// The total number of bytes written to the stream.
        virtual size_t offset() const =0;

        /// Requests the stream allocate enough space in its buffer for `size` bytes.
        virtual void reserve(size_t size)               { }

        /// Ensures data is written to persistent storage; ignored if non-persistent.
        virtual void flush()                            { }

        ostream& write(const void* src, size_t len);

        ostream& write(const void* begin, const void* end) {
            assert(end >= begin);
            return write(begin, uintptr_t(end) - uintptr_t(begin));
        }

        ostream& write(string_view str);
        ostream& write(std::span<const std::byte> b);

        ostream& writeByte(uint8_t b)                   {return write(&b, 1);}

        ostream& writeInt64(int64_t, int base =10);     ///< Writes a number in ASCII
        ostream& writeUInt64(uint64_t, int base =10);   ///< Writes a number in ASCII
        ostream& writeDouble(double);                   ///< Writes a number in ASCII

        /// Writes a value of type `T` exactly as it's laid out in memory.
        template <typename T>
        ostream& writeRaw(T const* _Nonnull val) {
            write(val, sizeof(T));
            return *this;
        }

        /// Writes a binary varint.
        template <std::unsigned_integral U>
        ostream& writeVarint(U n) {
            uint8_t intBuf[varint::maxSize<U>()];
            unsigned nBytes = varint::putInt(intBuf, n);
            write(intBuf, nBytes);
            return *this;
        }

        /// Writes a string, prefixed with its length as a varint.
        ostream& writeVarString(string_view str);

        /// The number of bytes that `writeVarString` will write.
        static size_t sizeOfVarString(std::string_view str);

    protected:
        static constexpr size_t kDefaultBufferSize = 128;

        /** Internal buffer. ostream writes to this buffer until it overflows. */
        class buffer {
        public:
            buffer() = default;
            buffer(void* start, size_t len)             {set(start, len);}
            void set(void* start, size_t len) {
                _start = _next = (char*)start;
                _avail = len;
            }
            char* data() const pure                     {return _start;}
            size_t offset() const pure                  {return _next - _start;}
            size_t available() const pure               {return _avail;}
            size_t capacity() const pure                {return _next - _start + _avail;}
            string_view contents() const pure           {return {_start, _next};}
            bool write(const void* src, size_t len);

        private:
            char*   _start {};     // Start of buffer
            char*   _next {};      // Next available address in buffer to write to
            size_t  _avail {};     // Number of bytes remaining in buffer
        };

        ostream() = default;
        ostream(void* buffer, size_t len)   
            :_buf(buffer, len) { }
        ostream(void* buffer, void* end)
            :ostream(buffer, uintptr_t(end) - uintptr_t(buffer)) {assert(buffer <= end);}
        ostream(ostream const&) = delete;
        ostream& operator=(ostream const&) = delete;

        /// If a write is too long to fit in the buffer, this method is called instead.
        /// It should process the bytes already in the buffer as well as the ones at `src`,
        /// and reset the buffer.
        virtual void writeOverflow(const void* src, size_t len) =0;

        buffer _buf;
    };


    /** ostream that writes to a string. */
    class ostringstream final : public ostream {
    public:
        ostringstream()                                 {makeBuffer();}
        explicit ostringstream(string s)                :_str(std::move(s)) {makeBuffer();}

        void reserve(size_t n) override;

        size_t offset() const override                  {return _str.size() - _buf.available();}

        string_view view() const                        {return {_str.data(), offset()};}
        string str() const &                            {return _str.substr(0, offset());}
        string str() &&                             {_str.resize(offset()); return std::move(_str);}

        void str(string s)                              {_str = std::move(s); makeBuffer();}

        void clear()                                    {_str.clear(); makeBuffer();}

    private:
        void writeOverflow(const void* src, size_t len) override;
        void makeBuffer();

        string _str;
    };


    /** ostream that writes to a vector<byte>. */
    class ovectorstream final : public ostream {
    public:
        using vec = std::vector<std::byte>;

        ovectorstream()                                 {makeBuffer();}
        explicit ovectorstream(vec v)                   :_vec(std::move(v)) {makeBuffer();}

        void reserve(size_t n) override;

        size_t offset() const override                  {return _vec.size() - _buf.available();}

        std::span<const std::byte> view() const         {return {_vec.data(), offset()};}
        vec vector() const &                    {return {_vec.begin(), _vec.begin() + offset()};}
        vec vector() &&                         {_vec.resize(offset()); return std::move(_vec);}

        void vector(vec v)                              {_vec = std::move(v); makeBuffer();}

        void clear()                                    {_vec.clear(); makeBuffer();}

    private:
        void writeOverflow(const void* src, size_t len) override;
        void makeBuffer();

        vec _vec;
    };


    /** ostream that writes to a fixed-size caller-provided buffer. */
    class omemstream : public ostream {
    public:
        omemstream(void* begin, void* end)               :ostream(begin, end) { }
        omemstream(void* begin, size_t size)             :ostream(begin, size) { }
        explicit omemstream(std::span<char> b)           :omemstream(b.data(), b.size_bytes()) { }

        size_t offset() const override                  {return _buf.offset();}

        size_t available() const                        {return _buf.available();}

        string_view str() const                         {return _buf.contents();}
        std::span<char> buffer() const                  {return {_buf.data(), _buf.offset()};}

        void clear()                                    {_buf.set(_buf.data(), _buf.capacity());}

    protected:
        omemstream() = default;
        void writeOverflow(const void* src, size_t len) override;
    };


    /** ostream that writes to a fixed-size buffer it allocates itself.
        A small buffer lives inside the object; a large one is heap-allocated. */
    template <size_t SIZE>
    class obufferstream : public omemstream {
    public:
        obufferstream()  :omemstream(SIZE > kMaxInlSize ? new char[SIZE] : _inlineBuffer, SIZE) { }
        ~obufferstream() {if constexpr (SIZE > kMaxInlSize) delete[] _buf.data();}
    private:
        static constexpr size_t kMaxInlSize = 128;
        char _inlineBuffer[ (SIZE <= kMaxInlSize) ? SIZE : 1 ];
    };


    /** ostream that writes nothing, but does track its offset. */
    class onullstream final : public ostream {
    public:
        onullstream() = default;
        size_t offset() const override                  {return _offset;}

    private:
        void writeOverflow(const void* src, size_t len) override;
        size_t _offset = 0;
    };


#pragma mark - FILE STREAMS:


    static constexpr size_t kFileStreamBufferSize = 128;

    /** Minimalist file output stream. */
    class ofilestream final : public obufferstream<kFileStreamBufferSize> {
    public:
        explicit ofilestream(std::string const& filename, const char* mode = "w");
        explicit ofilestream(std::string_view filename, const char* mode = "w");
        explicit ofilestream(FILE* f)                      :_fd(f), _owned(false) { }
        ~ofilestream();
        void flush() override;
        size_t offset() const override;

    private:
        friend class ifilestream;
        static FILE* _open(const char* filename, const char* mode);
        void _write(const void* src, size_t len);
        void _writeBuffer();
        void writeOverflow(const void* src, size_t len) override;

        FILE* _fd;
        bool  _owned;
    };




    extern ofilestream cout;       ///< ostream that writes to stdout.
    extern ofilestream cerr;       ///< ostream that writes to stderr.


    inline ostream& operator<< (ostream& o, std::span<const std::byte> b)   {return o.write(b);}
    inline ostream& operator<< (ostream& o, string_view s)     {return o.write(s.data(), s.size());}
    inline ostream& operator<< (ostream& o, char c)            {return o.write(&c, 1);}

    ostream& operator<< (ostream&, const void*);

    template <std::signed_integral INT>
    ostream& operator<< (ostream& o, INT i)             {return o.writeInt64(int64_t(i));}

    template <std::unsigned_integral UINT>
    ostream& operator<< (ostream& o, UINT i)            {return o.writeUInt64(uint64_t(i));}

    inline ostream& operator<< (ostream& o, float f)    {return o.writeDouble(f);}
    inline ostream& operator<< (ostream& o, double d)   {return o.writeDouble(d);}

    
    /** The concept `ostreamable` defines types that can be written to an ostream with `<<`. */
    template <typename T>
    concept ostreamable = requires(ostream& out, T t) { out << t; };
}

ASSUME_NONNULL_END
