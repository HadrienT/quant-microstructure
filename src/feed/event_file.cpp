#include "quantMicrostructure/feed/event_file.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <istream>
#include <ostream>
#include <stdexcept>

namespace quantMicrostructure
{

    namespace
    {

        constexpr std::array<char, 4> Magic{'Q', 'M', 'E', 'V'};
        constexpr std::size_t HeaderSize = 8;

        template <std::size_t Bytes>
        void put(char *at, std::uint64_t value)
        {
            for (std::size_t i = 0; i < Bytes; ++i)
            {
                at[i] = static_cast<char>(static_cast<unsigned char>(value >> (8 * i)));
            }
        }

        template <std::size_t Bytes>
        std::uint64_t get(const char *at)
        {
            std::uint64_t value = 0;
            for (std::size_t i = 0; i < Bytes; ++i)
            {
                value |= static_cast<std::uint64_t>(static_cast<unsigned char>(at[i])) << (8 * i);
            }
            return value;
        }

    } // namespace

    EventWriter::EventWriter(std::ostream &out)
        : out_(out)
    {
        std::array<char, HeaderSize> header{};
        for (std::size_t i = 0; i < Magic.size(); ++i)
        {
            header[i] = Magic[i];
        }
        put<4>(header.data() + 4, EventFileVersion);
        out_.write(header.data(), static_cast<std::streamsize>(header.size()));
    }

    void EventWriter::write(const Event &event)
    {
        std::array<char, EventRecordSize> record{};
        char *at = record.data();
        put<8>(at, static_cast<std::uint64_t>(event.ts));
        put<8>(at + 8, static_cast<std::uint64_t>(event.ts_recv));
        put<8>(at + 16, event.seq);
        put<8>(at + 24, event.order_id);
        put<8>(at + 32, static_cast<std::uint64_t>(event.price));
        put<8>(at + 40, static_cast<std::uint64_t>(event.qty));
        put<1>(at + 48, static_cast<std::uint64_t>(event.type));
        put<1>(at + 49, static_cast<std::uint64_t>(event.side));
        out_.write(record.data(), static_cast<std::streamsize>(record.size()));
    }

    EventReader::EventReader(std::istream &in)
        : in_(in)
    {
        std::array<char, HeaderSize> header{};
        in_.read(header.data(), static_cast<std::streamsize>(header.size()));
        if (in_.gcount() != static_cast<std::streamsize>(header.size()))
        {
            throw std::runtime_error("event file: missing header");
        }
        for (std::size_t i = 0; i < Magic.size(); ++i)
        {
            if (header[i] != Magic[i])
            {
                throw std::runtime_error("event file: not an event file");
            }
        }
        if (get<4>(header.data() + 4) != EventFileVersion)
        {
            throw std::runtime_error("event file: unknown format version");
        }
    }

    bool EventReader::next(Event &event)
    {
        std::array<char, EventRecordSize> record{};
        in_.read(record.data(), static_cast<std::streamsize>(record.size()));
        if (in_.gcount() == 0)
        {
            return false;
        }
        if (in_.gcount() != static_cast<std::streamsize>(record.size()))
        {
            throw std::runtime_error("event file: truncated record");
        }
        const char *at = record.data();
        const std::uint64_t type = get<1>(at + 48);
        const std::uint64_t side = get<1>(at + 49);
        if (type > static_cast<std::uint64_t>(EventType::Clear) || side > static_cast<std::uint64_t>(Side::Sell))
        {
            throw std::runtime_error("event file: invalid record");
        }
        event.ts = static_cast<Timestamp>(get<8>(at));
        event.ts_recv = static_cast<Timestamp>(get<8>(at + 8));
        event.seq = get<8>(at + 16);
        event.order_id = get<8>(at + 24);
        event.price = static_cast<Price>(get<8>(at + 32));
        event.qty = static_cast<Quantity>(get<8>(at + 40));
        event.type = static_cast<EventType>(type);
        event.side = static_cast<Side>(side);
        return true;
    }

    void writeEventFile(const std::filesystem::path &path, std::span<const Event> events)
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            throw std::runtime_error("event file: cannot open " + path.string() + " for writing");
        }
        EventWriter writer(out);
        for (const Event &event : events)
        {
            writer.write(event);
        }
        out.flush();
        if (!out)
        {
            throw std::runtime_error("event file: write to " + path.string() + " failed");
        }
    }

    std::vector<Event> readEventFile(const std::filesystem::path &path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            throw std::runtime_error("event file: cannot open " + path.string());
        }
        EventReader reader(in);
        std::vector<Event> events;
        Event event;
        while (reader.next(event))
        {
            events.push_back(event);
        }
        return events;
    }

} // namespace quantMicrostructure
