/**
 * MaNGOS is a full featured server for World of Warcraft, supporting
 * the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
 *
 * Copyright (C) 2005-2025 MaNGOS <https://www.getmangos.eu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * World of Warcraft, and all World of Warcraft or Warcraft art, images,
 * and lore are copyrighted by Blizzard Entertainment, Inc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "DBCFileLoader.h"
#include <cassert>
#include <cstdint>

/// WDBC header: magic, then recordCount, fieldCount, recordSize, stringSize.
static const size_t DBC_HEADER_SIZE = 20;
static const uint32_t DBC_MAGIC_WDBC = 0x43424457;

DBCFileLoader::DBCFileLoader()
{
    recordSize = 0;
    recordCount = 0;
    fieldCount = 0;
    stringSize = 0;
    data = nullptr;
    stringTable = nullptr;
    fieldsOffset = nullptr;
}

bool DBCFileLoader::Load(const char* filename, const char* fmt)
{
    FILE* f = fopen(filename, "rb");
    if (!f)
    {
        return false;
    }

    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return false;
    }

    const long fileSize = ftell(f);
    if (fileSize < 0 || fseek(f, 0, SEEK_SET) != 0)
    {
        fclose(f);
        return false;
    }

    std::vector<unsigned char> image(static_cast<size_t>(fileSize));
    // An empty DBC cannot carry even a header, so the short-read check below rejects it.
    if (!image.empty() && fread(image.data(), image.size(), 1, f) != 1)
    {
        fclose(f);
        return false;
    }

    fclose(f);
    return LoadFromMemory(image.data(), image.size(), fmt);
}

bool DBCFileLoader::LoadFromMemory(const void* bytes, size_t size, const char* fmt)
{
    // Loading twice must not leak the previous image.
    delete[] data;
    data = nullptr;
    stringTable = nullptr;
    delete[] fieldsOffset;
    fieldsOffset = nullptr;

    if (!bytes || size < DBC_HEADER_SIZE)
    {
        return false;
    }

    const unsigned char* image = static_cast<const unsigned char*>(bytes);

    uint32_t header;
    memcpy(&header, image, 4);
    EndianConvert(header);
    if (header != DBC_MAGIC_WDBC)
    {
        return false;
    }

    memcpy(&recordCount, image + 4, 4);
    EndianConvert(recordCount);
    memcpy(&fieldCount, image + 8, 4);
    EndianConvert(fieldCount);
    memcpy(&recordSize, image + 12, 4);
    EndianConvert(recordSize);
    memcpy(&stringSize, image + 16, 4);
    EndianConvert(stringSize);

    // A zero field count would make fieldsOffset[0] a write past a zero-length array, and
    // the record/string arithmetic below must not wrap. Reject rather than trust the file.
    if (fieldCount == 0 || strlen(fmt) < fieldCount)
    {
        return false;
    }
    if (recordCount != 0 && recordSize > (SIZE_MAX - stringSize) / recordCount)
    {
        return false;
    }

    const size_t payloadSize = size_t(recordSize) * recordCount + stringSize;
    if (payloadSize > size - DBC_HEADER_SIZE)               // truncated file
    {
        return false;
    }

    fieldsOffset = new uint32_t[fieldCount];
    fieldsOffset[0] = 0;
    for (uint32_t i = 1; i < fieldCount; ++i)
    {
        fieldsOffset[i] = fieldsOffset[i - 1];
        if (fmt[i - 1] == 'b' || fmt[i - 1] == 'X')         // byte fields
        {
            fieldsOffset[i] += 1;
        }
        else                                                // 4 byte fields (int32/float/strings)
        {
            fieldsOffset[i] += 4;
        }
    }

    data = new unsigned char[payloadSize];
    stringTable = data + size_t(recordSize) * recordCount;
    memcpy(data, image + DBC_HEADER_SIZE, payloadSize);

    return true;
}

DBCFileLoader::~DBCFileLoader()
{
    delete[] data;
    delete[] fieldsOffset;
}

DBCFileLoader::Record DBCFileLoader::getRecord(size_t id)
{
    assert(data);
    return Record(*this, data + id * recordSize);
}

uint32_t DBCFileLoader::GetFormatRecordSize(const char* format, int32_t* index_pos)
{
    uint32_t recordsize = 0;
    int32_t i = -1;
    for (uint32_t x = 0; format[x]; ++ x)
    {
        switch (format[x])
        {
            case DBC_FF_FLOAT:
                recordsize += sizeof(float);
                break;
            case DBC_FF_INT:
                recordsize += sizeof(uint32_t);
                break;
            case DBC_FF_STRING:
                recordsize += sizeof(char*);
                break;
            case DBC_FF_SORT:
                i = x;
                break;
            case DBC_FF_IND:
                i = x;
                recordsize += sizeof(uint32_t);
                break;
            case DBC_FF_BYTE:
                recordsize += sizeof(uint8_t);
                break;
            case DBC_FF_LOGIC:
                assert(false && "Attempted to load DBC files that do not have field types that match what is in the core. Check DBCfmt.h or your DBC files.");
                break;
            case DBC_FF_NA:
            case DBC_FF_NA_BYTE:
                break;
            default:
                assert(false && "Unknown field format character in DBCfmt.h");
                break;
        }
    }

    if (index_pos)
    {
        *index_pos = i;
    }

    return recordsize;
}

char* DBCFileLoader::AutoProduceData(const char* format, uint32_t& records, char**& indexTable)
{
    /**
     * format STRING, NA, FLOAT,NA,INT <=>
     * struct{
     *     char* field0,
     *     float field1,
     *     int field2
     * } entry;
     *
     * this func will generate  entry[rows] data;
     */

    typedef char* ptr;
    if (strlen(format) != fieldCount)
    {
        return nullptr;
    }

    // get struct size and index pos
    int32_t i;
    uint32_t recordsize = GetFormatRecordSize(format, &i);

    if (i >= 0)
    {
        uint32_t maxi = 0;
        // find max index
        for (uint32_t y = 0; y < recordCount; ++y)
        {
            uint32_t ind = getRecord(y).getUInt(i);
            if (ind > maxi)
            {
                maxi = ind;
            }
        }

        ++maxi;
        records = maxi;
        indexTable = new ptr[maxi];
        memset(indexTable, 0, maxi * sizeof(ptr));
    }
    else
    {
        records = recordCount;
        indexTable = new ptr[recordCount];
    }

    char* dataTable = new char[recordCount * recordsize];

    uint32_t offset = 0;

    for (uint32_t y = 0; y < recordCount; ++y)
    {
        if (i >= 0)
        {
            indexTable[getRecord(y).getUInt(i)] = &dataTable[offset];
        }
        else
        {
            indexTable[y] = &dataTable[offset];
        }

        for (uint32_t x = 0; x < fieldCount; ++x)
        {
            switch (format[x])
            {
                case DBC_FF_FLOAT:
                    *((float*)(&dataTable[offset])) = getRecord(y).getFloat(x);
                    offset += sizeof(float);
                    break;
                case DBC_FF_IND:
                case DBC_FF_INT:
                    *((uint32_t*)(&dataTable[offset])) = getRecord(y).getUInt(x);
                    offset += sizeof(uint32_t);
                    break;
                case DBC_FF_BYTE:
                    *((uint8_t*)(&dataTable[offset])) = getRecord(y).getUInt8(x);
                    offset += sizeof(uint8_t);
                    break;
                case DBC_FF_STRING:
                    *((char**)(&dataTable[offset])) = nullptr; // will replace non-empty or "" strings in AutoProduceStrings
                    offset += sizeof(char*);
                    break;
                case DBC_FF_LOGIC:
                    assert(false && "Attempted to load DBC files that do not have field types that match what is in the core. Check DBCfmt.h or your DBC files.");
                    break;
                case DBC_FF_NA:
                case DBC_FF_NA_BYTE:
                case DBC_FF_SORT:
                    break;
                default:
                    assert(false && "Unknown field format character in DBCfmt.h");
                    break;
            }
        }
    }

    return dataTable;
}

char* DBCFileLoader::AutoProduceStrings(const char* format, char* dataTable)
{
    if (strlen(format) != fieldCount)
    {
        return nullptr;
    }

    char* stringPool = new char[stringSize];
    memcpy(stringPool, stringTable, stringSize);

    uint32_t offset = 0;

    for (uint32_t y = 0; y < recordCount; ++y)
    {
        for (uint32_t x = 0; x < fieldCount; ++x)
        {
            switch (format[x])
            {
                case DBC_FF_FLOAT:
                    offset += sizeof(float);
                    break;
                case DBC_FF_IND:
                case DBC_FF_INT:
                    offset += sizeof(uint32_t);
                    break;
                case DBC_FF_BYTE:
                    offset += sizeof(uint8_t);
                    break;
                case DBC_FF_STRING:
                {
                    // fill only not filled entries
                    char** slot = (char**)(&dataTable[offset]);
                    if (!*slot || !** slot)
                    {
                        const char* st = getRecord(y).getString(x);
                        *slot = stringPool + (st - (const char*)stringTable);
                    }
                    offset += sizeof(char*);
                    break;
                }
                case DBC_FF_LOGIC:
                    assert(false && "Attempted to load DBC files that does not have field types that match what is in the core. Check DBCfmt.h or your DBC files.");
                    break;
                case DBC_FF_NA:
                case DBC_FF_NA_BYTE:
                case DBC_FF_SORT:
                    break;
                default:
                    assert(false && "Unknown field format character in DBCfmt.h");
                    break;
            }
        }
    }

    return stringPool;
}
