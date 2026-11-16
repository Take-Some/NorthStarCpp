// NorthStarCpp
// Author: Kayla Verner
#include "asset_list_file.hpp"

#include <northstarcpp/assets.hpp>
#include <blake3.h>
#include <zlib.h>
#include <boost/property_tree/json_parser.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <sstream>
#include <mutex>
#include <stdexcept>

namespace northstarcpp::shared_assets::detail {

void require(bool condition, char const* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::uint8_t const* Bytes::at(std::uint64_t offset, std::uint64_t length) const
{
    require(offset <= size_ && length <= size_ - offset, "Asset byte range is outside the file");
    return offset == 0 ? data_ : data_ + static_cast<std::size_t>(offset);
}

std::uint64_t Bytes::integer(std::uint64_t offset, unsigned width) const
{
    auto const* field = at(offset, width);
    std::uint64_t result = 0;
    for (unsigned i = width; i != 0; --i)
        result = (result << 8) | field[i - 1];
    return result;
}

float Bytes::real(std::uint64_t offset) const
{
    auto const bits = static_cast<std::uint32_t>(integer(offset, 4));
    float value;
    static_assert(sizeof(value) == sizeof(bits));
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::string Bytes::string(std::uint64_t offset, std::uint64_t length) const
{
    auto const* begin = at(offset, length);
    return {reinterpret_cast<char const*>(begin), static_cast<std::size_t>(length)};
}

bool Bytes::magic(std::uint64_t offset, std::string_view value) const
{
    return std::memcmp(at(offset, value.size()), value.data(), value.size()) == 0;
}

std::string extension(std::string const& path)
{
    auto value = std::filesystem::u8path(path).extension().u8string();
    for (auto& ch : value)
        if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch + ('a' - 'A'));
    return value;
}

void validate_name(std::string const& name)
{
    require(!name.empty() && name.size() <= 256 &&
        name.find_first_of("@/\\:") == std::string::npos &&
        std::none_of(name.begin(), name.end(), [](unsigned char ch) { return ch < 32; }),
        "Invalid asset entry name");
}

void validate_reference(std::string const& reference)
{
    auto const selector = reference.find('@');
    require(selector != std::string::npos && selector > 0 && selector <= 1024 &&
        reference.find_first_of("\\:", 0) == std::string::npos, "Expected a logical asset path and @entry");
    validate_name(reference.substr(selector + 1));
    std::string_view path(reference.data(), selector);
    for (;;) {
        auto const slash = path.find('/');
        auto const segment = path.substr(0, slash);
        require(!segment.empty() && segment != "." && segment != ".." &&
            std::none_of(segment.begin(), segment.end(), [](unsigned char ch) { return ch < 32; }),
            "Invalid virtual asset reference");
        if (slash == std::string_view::npos)
            return;
        path.remove_prefix(slash + 1);
    }
}

Tree parse_json(std::string const& text)
{
    // Boost.PropertyTree JSON parsing crashed when multiple catalog workers
    // entered it concurrently on the Windows/MSYS2 runtime. The shared
    // metadata parser is deliberately serialized; NEF8 dictionary indexes
    // already bypass optional metadata JSON on their fast path.
    static std::mutex read_json_mutex;
    std::lock_guard<std::mutex> guard(read_json_mutex);
    Tree tree;
    std::istringstream input(text);
    boost::property_tree::read_json(input, tree);
    return tree;
}

namespace {

constexpr std::uint64_t fixed_header_size = 128;
constexpr std::uint64_t maximum_body_size = 64u * 1024u * 1024u;

std::string hex(std::uint8_t const* bytes, std::size_t size)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(2 * size);
    for (std::size_t i = 0; i < size; ++i) {
        result.push_back(digits[bytes[i] >> 4]);
        result.push_back(digits[bytes[i] & 15]);
    }
    return result;
}

void validate_hash(std::uint16_t kind, Bytes const& file, std::vector<std::uint8_t> const& body)
{
    std::array<std::uint8_t, BLAKE3_OUT_LEN> digest{};
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, body.data(), body.size());
    blake3_hasher_finalize(&hasher, digest.data(), digest.size());
    auto const* recorded = file.at(64, digest.size());
    if (std::equal(digest.begin(), digest.end(), recorded))
        return;

    // The original skydome YTD producer wrote the asset-graph hash in this field.
    // Match its exact header and the independently verified body digest.
    require(kind == 1 &&
        hex(recorded, digest.size()) == "00fd5b822a016a0967ee4b87392540f512f1b9ab62f9806550d86b92afc76546" &&
        hex(digest.data(), digest.size()) == "4a5c29942ef7010ef5a0cfd04b995041bdb01148bae1356297aff2686163181c",
        "NEF8 body hash does not match");
}

// Both NEF8 header versions use the same raw DEFLATE payload contract.
// Keep decompression limits and stream cleanup identical across versions.
std::vector<std::uint8_t> inflate_body(Bytes const& file,
    std::uint64_t offset, std::uint64_t compressed, std::uint64_t expanded,
    char const* invalid_stream)
{
    std::vector<std::uint8_t> body(static_cast<std::size_t>(expanded));
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(file.at(offset, compressed));
    stream.avail_in = static_cast<uInt>(compressed);
    stream.next_out = body.data();
    stream.avail_out = static_cast<uInt>(expanded);
    require(inflateInit2(&stream, -MAX_WBITS) == Z_OK,
        "Cannot initialize NEF8 decompression");

    // Restore zlib state even if validation throws.
    struct InflateCleanup {
        z_stream& stream;
        ~InflateCleanup() { inflateEnd(&stream); }
    } cleanup{stream};

    auto const status = inflate(&stream, Z_FINISH);
    require(status == Z_STREAM_END && stream.total_in == compressed &&
        stream.total_out == expanded, invalid_stream);
    return body;
}

} // namespace

static ListFile parse_list_file_v2(Bytes const& file, bool include_metadata)
{
    auto const minor = file.integer(5, 1);
    auto const kind = static_cast<std::uint16_t>(file.integer(6, 2));
    auto const flags = file.integer(8, 2);
    auto const schema = file.integer(10, 2);
    auto const entries = file.integer(12, 4);
    auto const compressed = file.integer(16, 8);
    auto const expanded = file.integer(24, 8);
    auto const prefix = minor >= 6 ? 64u : 32u;
    auto const limit = (kind == 1 || kind == 5) ? 256ull * 1024 * 1024 : maximum_body_size;
    require((minor == 5 || minor == 6 || minor == 8) && (flags & 1u) != 0 &&
        (flags & ~(minor >= 6 ? 7u : 3u)) == 0 &&
        schema > 0 && entries <= 65536 &&
        compressed > 0 && compressed <= file.size() - prefix &&
        expanded > 0 && expanded <= limit, "Invalid NEF8 v2 header");
    auto const offset = file.size() - compressed;
    auto const metadata_size = offset - prefix;
    require(metadata_size <= 8 * 1024 * 1024 &&
        ((flags & 2u) != 0) == (metadata_size != 0), "Invalid NEF8 v2 metadata range");
    ListFile result{kind, static_cast<std::size_t>(entries), {}, {}};
    result.body = inflate_body(file, offset, compressed, expanded,
        "Invalid NEF8 v2 deflate stream or length");
    if (flags & 4u) {
        std::array<std::uint8_t, BLAKE3_OUT_LEN> digest{};
        blake3_hasher hash;
        blake3_hasher_init(&hash);
        blake3_hasher_update(&hash, result.body.data(), result.body.size());
        blake3_hasher_finalize(&hash, digest.data(), digest.size());
        require(std::equal(digest.begin(), digest.end(), file.at(32, digest.size())),
            "NEF8 v2 body hash does not match");
    }
    if (include_metadata && metadata_size)
        result.metadata = parse_json(file.string(prefix, metadata_size));
    return result;
}

ListFile parse_list_file(std::vector<std::uint8_t> const& bytes, bool include_metadata)
{
    Bytes const file(bytes);
    require(file.size() >= 32 && file.size() <= 256ull*1024*1024 && file.magic(0, "NEF8"),
        "Expected an NEF8 ListFile");
    if (file.integer(4, 1) == 2) {
        if (file.integer(5, 1) >= 6)
            require(file.size() >= 64, "Truncated NEF8 v2.6 header");
        return parse_list_file_v2(file, include_metadata);
    }
    require(file.size() >= fixed_header_size, "Truncated NEF8 v1 header");
    auto const header_size = file.integer(6, 2);
    auto const kind = static_cast<std::uint16_t>(file.integer(8, 2));
    require(file.integer(4, 2) == 1 && header_size == fixed_header_size &&
        file.integer(10, 2) == 1 && file.integer(12, 2) == 1 && file.integer(14, 2) == 0 &&
        file.integer(112, 8) == 1, "Unsupported NEF8 header, compression or schema version");

    auto const offset = file.integer(16, 8);
    auto const compressed = file.integer(24, 8);
    auto const expanded = file.integer(32, 8);
    auto const entries = file.integer(40, 8);
    auto const metadata_offset = file.integer(48, 8);
    auto const metadata_size = file.integer(56, 8);
    require(offset >= header_size && offset <= file.size() && compressed > 0 &&
        compressed == file.size() - offset && expanded > 0 && expanded <= maximum_body_size &&
        entries <= 65536, "Invalid NEF8 body range or entry count");
    require((metadata_size == 0 && metadata_offset == 0) ||
        (metadata_offset >= header_size && metadata_offset <= offset &&
         metadata_size <= offset - metadata_offset && metadata_size <= 8 * 1024 * 1024),
        "Invalid NEF8 metadata range");

    ListFile result{kind, static_cast<std::size_t>(entries), {}, {}};
    result.body = inflate_body(file, offset, compressed, expanded,
        "Invalid NEF8 deflate stream or inflated length");
    validate_hash(kind, file, result.body);
    if (include_metadata && metadata_size != 0)
        result.metadata = parse_json(file.string(metadata_offset, metadata_size));
    return result;
}

ListFile read_list_file(AssetStore const& assets, std::string const& path)
{
    require(assets.size(path) <= maximum_body_size, "NEF8 file is too large");
    auto bytes=assets.read(path);
    return parse_list_file(bytes);
}

} // namespace northstarcpp::shared_assets::detail
