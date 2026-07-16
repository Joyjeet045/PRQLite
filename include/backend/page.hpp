#pragma once

#include <cstdint>
#include <string>

namespace db::backend {

constexpr int PAGE_SIZE = 4096;

using PageId = int;

class Page {
public:
    static constexpr int HEADER_SIZE = 12;
    static constexpr int SLOT_SIZE = 4;
    static constexpr int LSN_OFFSET = 4;

    Page() { init(); }

    void init();

    char* data() { return bytes_; }
    const char* data() const { return bytes_; }

    int slotCount() const;
    int freeSpace() const;

    int freeBytes() const;

    std::uint64_t pageLSN() const;
    void setPageLSN(std::uint64_t lsn);

    bool insert(const std::string& record, int& outSlot);

    bool get(int slot, std::string& out) const;

    bool erase(int slot);

    bool update(int slot, const std::string& record);

    bool isLive(int slot) const;

    /* Recovery: idempotently place / clear a record at an exact slot. */
    bool redoSet(int slot, const std::string& record, std::uint64_t lsn);
    void redoClear(int slot, std::uint64_t lsn);

private:
    std::uint16_t readU16(int offset) const;
    void writeU16(int offset, std::uint16_t value);
    std::uint64_t readU64(int offset) const;
    void writeU64(int offset, std::uint64_t value);
    void slotAt(int slot, std::uint16_t& offset, std::uint16_t& length) const;
    void setSlot(int slot, std::uint16_t offset, std::uint16_t length);

    int usableSpace() const;

    void compact();

    char bytes_[PAGE_SIZE];
};

}
