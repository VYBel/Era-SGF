#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <zlib.h>

namespace erasgf {
// InkView grayscale bitmaps and PNG both pack low-depth samples MSB first.
// Encode only grayscale formats; never reinterpret a colour bitmap as gray.
inline std::string grayscale_png(unsigned width, unsigned height, unsigned depth,
                                 unsigned stride, const unsigned char* pixels) {
    if (!pixels || !width || !height || width > 8192 || height > 8192 ||
        (depth != 1 && depth != 2 && depth != 4 && depth != 8))
        throw std::runtime_error("Unsupported screenshot bitmap format");
    const std::size_t row = (static_cast<std::size_t>(width) * depth + 7) / 8;
    if (stride < row || (row + 1) * height > 64 * 1024 * 1024)
        throw std::runtime_error("Invalid screenshot bitmap dimensions");
    std::string raw((row + 1) * height, '\0'); // PNG filter 0 for every row.
    for (unsigned y = 0; y < height; ++y)
        std::memcpy(&raw[y * (row + 1) + 1], pixels + static_cast<std::size_t>(y) * stride, row);
    uLongf length = compressBound(static_cast<uLong>(raw.size()));
    std::string compressed(length, '\0');
    if (compress2(reinterpret_cast<Bytef*>(&compressed[0]), &length,
                  reinterpret_cast<const Bytef*>(raw.data()), raw.size(), Z_DEFAULT_COMPRESSION) != Z_OK)
        throw std::runtime_error("PNG compression failed");
    compressed.resize(length);
    auto be32 = [](std::string& out, std::uint32_t value) {
        for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<char>(value >> shift));
    };
    std::string png("\x89PNG\r\n\x1a\n", 8);
    auto chunk = [&](const char* type, const std::string& data) {
        be32(png, static_cast<std::uint32_t>(data.size()));
        const auto start = png.size();
        png.append(type, 4); png += data;
        be32(png, crc32(0, reinterpret_cast<const Bytef*>(png.data() + start),
                       static_cast<uInt>(4 + data.size())));
    };
    std::string header;
    be32(header, width); be32(header, height);
    header.push_back(static_cast<char>(depth));
    header.append(4, '\0'); // grayscale, deflate, filter method 0, no interlace.
    chunk("IHDR", header); chunk("IDAT", compressed); chunk("IEND", {});
    return png;
}
} // namespace erasgf
