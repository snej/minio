//
// MiniOStream.cc
//
// Copyright © 2024 Jens Alfke. All rights reserved.
//

#include "minio/ostream.hh"
#include "minio/istream.hh"
#include <charconv>
#include <cstdio>

namespace snej::minio {


#pragma mark - OSTREAM:


    bool ostream::buffer::write(const void* src, size_t len) {
        if (len > _avail) [[unlikely]]
            return false;
        ::memcpy(_next, src, len);
        _next += len;
        _avail -= len;
        return true;
    }


    ostream& ostream::write(const void* src, size_t len) {
        if (!_buf.write(src, len)) [[unlikely]]
            writeOverflow(src, len);
        return *this;
    }

    ostream& ostream::write(std::span<const std::byte> b) {
        return write((const char*)b.data(), b.size());
    }

    ostream& ostream::write(std::string_view str)   {
        return write(str.data(), str.size());
    }

    ostream& ostream::writeInt64(int64_t i, int base) {
        char buf[20];
        auto [ptr, ec] = std::to_chars(&buf[0], &buf[sizeof(buf)], i, base);
        return write(&buf[0], size_t(ptr - buf));
    }

    ostream& ostream::writeUInt64(uint64_t i, int base) {
        char buf[20];
        auto result = std::to_chars(&buf[0], &buf[sizeof(buf)], i, base);
        return write(&buf[0], result.ptr - buf);
    }

    ostream& ostream::writeDouble(double f) {
        char buf[30];
#ifdef __APPLE__ // Apple's libc++ didn't add this method until later
        if (__builtin_available(macOS 13.4, iOS 16.3, tvOS 16.3, watchOS 9.3, *)) {
#endif
            auto result = std::to_chars(&buf[0], &buf[sizeof(buf)], f);
            return write(&buf[0], result.ptr - buf);
#ifdef __APPLE__
        } else {
            snprintf(buf, sizeof(buf), "%g", f);
            return write(buf);
        }
#endif
    }

    ostream& operator<< (ostream& out, const void* ptr) {
        return out.write("0x").writeUInt64(uintptr_t(ptr), 16);
    }

    ostream& ostream::writeVarString(string_view str) {
        writeVarint(str.size()).write(str);
        return *this;
    }

    size_t ostream::sizeOfVarString(std::string_view str) {
        return varint::sizeOfInt(str.size()) + str.size();
    }


    void omemstream::writeOverflow(const void* src, size_t len) {
        throw std::runtime_error("omemstream overflow");
    }


    void onullstream::writeOverflow(const void* src, size_t len) {
        _offset += len;
    }


#pragma mark - OSTRINGSTREAM:


    void ostringstream::makeBuffer() {
        // The buffer comes after the current contents of the string:
        auto size = _str.size();
        _str.resize(size + kDefaultBufferSize);
        _buf.set(&_str[size], kDefaultBufferSize);
    }


    void ostringstream::reserve(size_t reserved) {
        size_t actualSize = offset();
        if (_str.size() < actualSize + reserved) {
            _str.resize(actualSize + reserved);
            _buf.set(&_str[actualSize], reserved);
        }
    }


    void ostringstream::writeOverflow(const void* src, size_t len) {
        size_t actualSize = _str.size() - _buf.available();
        _str.resize(actualSize + len + kDefaultBufferSize);
        ::memcpy(&_str[actualSize], src, len);
        _buf.set(&_str[actualSize + len], kDefaultBufferSize);
    }


#pragma mark - OVECTORSTREAM:


    void ovectorstream::makeBuffer() {
        // The buffer comes after the current contents of the string:
        auto size = _vec.size();
        _vec.resize(size + kDefaultBufferSize);
        _buf.set(&_vec[size], kDefaultBufferSize);
    }


    void ovectorstream::reserve(size_t reserved) {
        size_t actualSize = offset();
        if (_vec.size() < actualSize + reserved) {
            _vec.resize(actualSize + reserved);
            _buf.set(&_vec[actualSize], reserved);
        }
    }


    void ovectorstream::writeOverflow(const void* src, size_t len) {
        size_t actualSize = _vec.size() - _buf.available();
        _vec.resize(actualSize + len + kDefaultBufferSize);
        ::memcpy(&_vec[actualSize], src, len);
        _buf.set(&_vec[actualSize + len], kDefaultBufferSize);
    }


#pragma mark - ISTREAM:


    size_t istream::buffer::read(void* dst, size_t len) {
        len = std::min(len, _avail);
        ::memcpy(dst, _next, len);
        _next += len;
        _avail -= len;
        return len;
    }


    bool istream::buffer::skip(size_t len) {
        if (len < _avail) [[unlikely]]
            return false;
        _next += len;
        _avail -= len;
        return true;
    }


    size_t istream::read(void* dst, size_t len) {
        size_t n = _buf.read(dst, len);
        if (n < len) [[unlikely]] {
            auto x = readUnbuffered((char*)dst + n, len - n);
            if (x > 0)
                fillBuffer();
            n += x;
        }
        return n;
    }


    void istream::readFull(void* dst, size_t maxLen) {
        if (read(dst, maxLen) < maxLen) [[unlikely]]
            throw std::runtime_error("unexpected EOF");
    }


    std::string istream::readVarString() {
        auto size = readVarint<size_t>();
        std::string str(size, 0);
        if (size > 0) [[likely]]
            readFull(str.data(), size);
        return str;
    }

    string_view imemstream::readFullNoCopy(size_t size) {
        const char* start = next();
        if (!_buf.skip(size)) [[unlikely]]
            throw std::runtime_error("unexpected EOF");
        return string_view (start, size);
    }


#pragma mark - FILE STREAMS:


    ofilestream cout(stdout);
    ofilestream cerr(stderr);


    FILE* ofilestream::_open(const char* filename, const char* mode) {
        FILE* fd = fopen(filename, mode);
        if (!fd)
            throw std::runtime_error("I/O error opening file " + string(filename));
        return fd;
    }

    ofilestream::ofilestream(string const& filename, const char* mode)
    :_fd(_open(filename.c_str(), mode))
    ,_owned(true)
    { }

    ofilestream::~ofilestream() {
        if (_owned && _fd)
            fclose(_fd);
    }

    void ofilestream::_write(const void* src, size_t len) {
        if (fwrite(src, len, 1, _fd) < len) [[unlikely]]
            throw std::runtime_error("I/O error writing to file");
    }

    void ofilestream::_writeBuffer() {
        if (string_view cont = _buf.contents(); !cont.empty()) {
            _write(cont.data(), cont.size());
            omemstream::clear();
        }
    }

    void ofilestream::flush() {
        _writeBuffer();
        if (fflush(_fd) != 0) [[unlikely]]
            throw std::runtime_error("I/O error writing to file");
    }

    void ofilestream::writeOverflow(const void* src, size_t len) {
        _writeBuffer();
        _write(src, len);
    }

    size_t ofilestream::offset() const {
        return ftell(_fd) + _buf.offset();
    }


    ifilestream::ifilestream(string const& filename, const char* mode)
    :_fd(ofilestream::_open(filename.c_str(), mode))
    ,_owned(true)
    { }

    ifilestream::~ifilestream() {
        if (_owned && _fd)
            fclose(_fd);
    }

    size_t ifilestream::offset() const {
        return ftello(_fd) - _buf.available();
    }

    bool ifilestream::eof() const noexcept {
        if (_buf.available() > 0)
            return false;
        else if (feof(_fd))
            return true;
        else {
            const_cast<ifilestream*>(this)->fillBuffer();
            return _buf.available() == 0;
        }
    }

    size_t ifilestream::readUnbuffered(void* dst, size_t maxLen) {
        auto len = fread(dst, maxLen, 1, _fd);
        if (len < maxLen && !feof(_fd)) [[unlikely]]
            throw std::runtime_error("I/O error reading file");
        return len;
    }

    void ifilestream::fillBuffer() {
        assert(_buf.available() == 0);
        size_t n = readUnbuffered(_buffer.data(), _buffer.size());
        _buf.set(_buffer.data(), n);
    }

}
