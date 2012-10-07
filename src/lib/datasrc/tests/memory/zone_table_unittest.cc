// Copyright (C) 2012  Internet Systems Consortium, Inc. ("ISC")
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
// REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
// INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
// LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
// OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
// PERFORMANCE OF THIS SOFTWARE.

#include <exceptions/exceptions.h>

#include <util/memory_segment_local.h>

#include <dns/name.h>
#include <dns/rrclass.h>

#include <datasrc/result.h>
#include <datasrc/memory/zone_data.h>
#include <datasrc/memory/zone_table.h>
#include <datasrc/memory/segment_object_holder.h>

#include <gtest/gtest.h>

#include <new>                  // for bad_alloc

using namespace isc::dns;
using namespace isc::datasrc;
using namespace isc::datasrc::memory;
using namespace isc::datasrc::memory::detail;

namespace {
// Memory segment specified for tests.  It normally behaves like a "local"
// memory segment.  If "throw count" is set to non 0 via setThrowCount(),
// it continues the normal behavior up to the specified number of calls to
// allocate(), and throws an exception at the next call.
class TestMemorySegment : public isc::util::MemorySegmentLocal {
public:
    TestMemorySegment() : throw_count_(0) {}
    virtual void* allocate(size_t size) {
        if (throw_count_ > 0) {
            if (--throw_count_ == 0) {
                throw std::bad_alloc();
            }
        }
        return (isc::util::MemorySegmentLocal::allocate(size));
    }
    void setThrowCount(size_t count) { throw_count_ = count; }

private:
    size_t throw_count_;
};

class ZoneTableTest : public ::testing::Test {
protected:
    ZoneTableTest() : zclass_(RRClass::IN()),
                      zname1(Name("example.com")),
                      zname2(Name("example.net")),
                      zname3(Name("example")),
                      zone_table(ZoneTable::create(mem_sgmt_, zclass_))
    {}
    ~ZoneTableTest() {
        if (zone_table != NULL) {
            ZoneTable::destroy(mem_sgmt_, zone_table, zclass_);
        }
    }
    void TearDown() {
        ZoneTable::destroy(mem_sgmt_, zone_table, zclass_);
        zone_table = NULL;
        EXPECT_TRUE(mem_sgmt_.allMemoryDeallocated()); // catch any leak here.
    }
    const RRClass zclass_;
    const Name zname1, zname2, zname3;
    TestMemorySegment mem_sgmt_;
    ZoneTable* zone_table;
};

TEST_F(ZoneTableTest, create) {
    // Test about creating a zone table.  Normal case covers through other
    // tests.  We only check exception safety by letting the test memory
    // segment throw.
    mem_sgmt_.setThrowCount(2);
    EXPECT_THROW(ZoneTable::create(mem_sgmt_, zclass_), std::bad_alloc);
    // This shouldn't cause memory leak (that would be caught in TearDown()).
}

TEST_F(ZoneTableTest, addZone) {
    SegmentObjectHolder<ZoneData, RRClass> holder1(
        mem_sgmt_, ZoneData::create(mem_sgmt_, zname1), zclass_);
    // Normal successful case.
    EXPECT_EQ(result::SUCCESS, zone_table->addZone(mem_sgmt_, zclass_,
                                                   zname1, holder1));
    // It got released by it
    EXPECT_EQ(NULL, holder1.get());

    // Duplicate add doesn't replace the existing data.
    SegmentObjectHolder<ZoneData, RRClass> holder2(
        mem_sgmt_, ZoneData::create(mem_sgmt_, zname1), zclass_);
    EXPECT_EQ(result::EXIST, zone_table->addZone(mem_sgmt_, zclass_,
                                                 zname1, holder2));
    // It releases this one even when we replace the old zone
    EXPECT_EQ(NULL, holder2.get());

    SegmentObjectHolder<ZoneData, RRClass> holder3(
        mem_sgmt_, ZoneData::create(mem_sgmt_, Name("EXAMPLE.COM")),
                                    zclass_);
    // names are compared in a case insensitive manner.
    EXPECT_EQ(result::EXIST, zone_table->addZone(mem_sgmt_, zclass_,
                                                 Name("EXAMPLE.COM"),
                                                 holder3));
    // Add some more different ones.  Should just succeed.
    SegmentObjectHolder<ZoneData, RRClass> holder4(
        mem_sgmt_, ZoneData::create(mem_sgmt_, zname2), zclass_);
    EXPECT_EQ(result::SUCCESS,
              zone_table->addZone(mem_sgmt_, zclass_, zname2, holder4));
    SegmentObjectHolder<ZoneData, RRClass> holder5(
        mem_sgmt_, ZoneData::create(mem_sgmt_, zname3), zclass_);
    EXPECT_EQ(result::SUCCESS,
              zone_table->addZone(mem_sgmt_, zclass_, zname3, holder5));

    // Have the memory segment throw an exception in extending the internal
    // tree.  It still shouldn't cause memory leak (which would be detected
    // in TearDown()).
    SegmentObjectHolder<ZoneData, RRClass> holder6(
        mem_sgmt_, ZoneData::create(mem_sgmt_, Name("example.org")), zclass_);
    mem_sgmt_.setThrowCount(1);
    EXPECT_THROW(zone_table->addZone(mem_sgmt_, zclass_, Name("example.org"),
                                     holder6),
                 std::bad_alloc);
}

TEST_F(ZoneTableTest, findZone) {
    SegmentObjectHolder<ZoneData, RRClass> holder1(
        mem_sgmt_, ZoneData::create(mem_sgmt_, zname1), zclass_);
    ZoneData* zone_data = holder1.get();
    EXPECT_EQ(result::SUCCESS, zone_table->addZone(mem_sgmt_, zclass_, zname1,
                                                   holder1));
    SegmentObjectHolder<ZoneData, RRClass> holder2(
        mem_sgmt_, ZoneData::create(mem_sgmt_, zname2), zclass_);
    EXPECT_EQ(result::SUCCESS,
              zone_table->addZone(mem_sgmt_, zclass_, zname2, holder2));
    SegmentObjectHolder<ZoneData, RRClass> holder3(
        mem_sgmt_, ZoneData::create(mem_sgmt_, zname3), zclass_);
    EXPECT_EQ(result::SUCCESS,
              zone_table->addZone(mem_sgmt_, zclass_, zname3, holder3));

    const ZoneTable::FindResult find_result1 =
        zone_table->findZone(Name("example.com"));
    EXPECT_EQ(result::SUCCESS, find_result1.code);
    EXPECT_EQ(zone_data, find_result1.zone_data);

    EXPECT_EQ(result::NOTFOUND,
              zone_table->findZone(Name("example.org")).code);
    EXPECT_EQ(static_cast<ZoneData*>(NULL),
              zone_table->findZone(Name("example.org")).zone_data);

    // there's no exact match.  the result should be the longest match,
    // and the code should be PARTIALMATCH.
    EXPECT_EQ(result::PARTIALMATCH,
              zone_table->findZone(Name("www.example.com")).code);
    EXPECT_EQ(zone_data,
              zone_table->findZone(Name("www.example.com")).zone_data);

    // make sure the partial match is indeed the longest match by adding
    // a zone with a shorter origin and query again.
    SegmentObjectHolder<ZoneData, RRClass> holder4(
        mem_sgmt_, ZoneData::create(mem_sgmt_, Name("com")), zclass_);
    EXPECT_EQ(result::SUCCESS, zone_table->addZone(mem_sgmt_, zclass_,
                                                   Name("com"), holder4));
    EXPECT_EQ(zone_data,
              zone_table->findZone(Name("www.example.com")).zone_data);
}
}
