#ifndef QUANTMICROSTRUCTURE_FEED_EVENT_FILE_HPP
#define QUANTMICROSTRUCTURE_FEED_EVENT_FILE_HPP

#include "quantMicrostructure/core/event.hpp"

#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <span>
#include <vector>

namespace quantMicrostructure
{

    // An event file is an 8-byte header (the magic "QMEV", then the format
    // version as a 32-bit integer) followed by fixed-size records. Every
    // integer is little-endian and written field by field, so a file does not
    // depend on the machine or the compiler that wrote it.

    inline constexpr std::uint32_t EventFileVersion = 1;
    /// Bytes per event: six 64-bit fields, then the type and the side.
    inline constexpr std::size_t EventRecordSize = 50;

    class EventWriter
    {
      public:
        /// Writes the header. The stream must be binary and outlive the writer.
        explicit EventWriter(std::ostream &out);

        void write(const Event &event);

      private:
        std::ostream &out_;
    };

    class EventReader
    {
      public:
        /// Reads the header; throws std::runtime_error if it is not that of an
        /// event file of a known version.
        explicit EventReader(std::istream &in);

        /// Reads the next event into `event`. Returns false at the end of the
        /// stream; throws std::runtime_error on a truncated or invalid record.
        bool next(Event &event);

      private:
        std::istream &in_;
    };

    void writeEventFile(const std::filesystem::path &path, std::span<const Event> events);
    std::vector<Event> readEventFile(const std::filesystem::path &path);

} // namespace quantMicrostructure

#endif
