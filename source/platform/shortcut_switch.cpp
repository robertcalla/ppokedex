// Installs a HOME menu shortcut (forwarder) for this homebrew, directly on the console.
//
// Adapted from Sphaira's on-console forwarder installer (sphaira/source/owo.cpp,
// https://github.com/ITotalJustice/sphaira, GPLv3). The title is made of three NCAs:
//   Program: exefs with the loader in romfs:/forwarder (nx-hbloader) + romfs with the .nro path,
//   Control: NACP + icon taken from our own .nro,
//   Meta:    the content meta record.
// Sections are not encrypted; only the NCA header is, with the header key derived on the console by
// the spl service. Installing requires signature patches (sigpatches), like any forwarder.
#include "shortcut.h"

#include <switch.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../core/bytes.h"
#include "platform.h"

namespace plat {

namespace {

constexpr u32 NCA3_MAGIC = 0x3341434E;
constexpr u32 IVFC_MAX_LEVEL = 6;
constexpr u32 IVFC_HASH_BLOCK_SIZE = 0x4000;
constexpr u32 PFS0_EXEFS_HASH_BLOCK_SIZE = 0x10000;
constexpr u32 PFS0_META_HASH_BLOCK_SIZE = 0x1000;
constexpr u32 PFS0_PADDING_SIZE = 0x200;
constexpr u32 ROMFS_ENTRY_EMPTY = 0xFFFFFFFF;
constexpr u32 ROMFS_FILEPARTITION_OFS = 0x200;

constexpr u8 HEADER_KEK_SRC[0x10] = {0x1F, 0x12, 0x91, 0x3A, 0x4A, 0xCB, 0xF0, 0x0D,
                                     0x4C, 0xDE, 0x3A, 0xF6, 0xD5, 0x23, 0x88, 0x2A};
constexpr u8 HEADER_KEY_SRC[0x20] = {0x5A, 0x3E, 0xD8, 0x4F, 0xDE, 0xC0, 0xD8, 0x26, 0x31, 0xF7, 0xE2,
                                     0x5D, 0x19, 0x7B, 0xF5, 0xD0, 0x1C, 0x9B, 0x7B, 0xFA, 0xF6, 0x28,
                                     0x18, 0x3D, 0x71, 0xF6, 0x4D, 0x73, 0xF1, 0x50, 0xB9, 0xD2};

// ---------------------------------------------------------------------------------------------
// NCA structures

struct SectionTableEntry {
    u32 media_start_offset;
    u32 media_end_offset;
    u8 _0x8[0x4];
    u8 _0xC[0x4];
};

struct LayerRegion {
    u64 offset;
    u64 size;
};

struct HierarchicalSha256Data {
    u8 master_hash[0x20];
    u32 block_size;
    u32 layer_count;
    LayerRegion hash_layer;
    LayerRegion pfs0_layer;
    LayerRegion unused_layers[3];
    u8 _0x78[0x80];
};

#pragma pack(push, 1)
struct IvfcLevel {
    u64 logical_offset;
    u64 hash_data_size;
    u32 block_size; // log2
    u32 _0x14;
};
#pragma pack(pop)

struct InfoLevelHash {
    u32 max_layers;
    IvfcLevel levels[6];
    u8 signature_salt[0x20];
};

struct IntegrityMetaInfo {
    u32 magic;
    u32 version;
    u32 master_hash_size;
    InfoLevelHash info_level_hash;
    u8 master_hash[0x20];
    u8 _0xE0[0x18];
};
static_assert(sizeof(HierarchicalSha256Data) == 0xF8);
static_assert(sizeof(IntegrityMetaInfo) == 0xF8);

struct FsHeader {
    u16 version;
    u8 fs_type;
    u8 hash_type;
    u8 encryption_type;
    u8 metadata_hash_type;
    u8 _0x6[0x2];
    union {
        HierarchicalSha256Data hierarchical_sha256_data;
        IntegrityMetaInfo integrity_meta_info;
    } hash_data;
    u8 patch_info[0x40];
    u64 section_ctr;
    u8 spares_info[0x30];
    u8 compression_info[0x28];
    u8 meta_data_hash_data_info[0x30];
    u8 reserved[0x30];
};
static_assert(sizeof(FsHeader) == 0x200);

struct NcaHeader {
    u8 rsa_fixed_key[0x100];
    u8 rsa_npdm[0x100];
    u32 magic;
    u8 distribution_type;
    u8 content_type;
    u8 old_key_gen;
    u8 kaek_index;
    u64 size;
    u64 program_id;
    u32 context_id;
    u32 sdk_version;
    u8 key_gen;
    u8 sig_key_gen;
    u8 _0x222[0xE];
    FsRightsId rights_id;
    SectionTableEntry fs_table[4];
    u8 fs_header_hash[4][0x20];
    u8 key_area[4][0x10];
    u8 _0x340[0xC0];
    FsHeader fs_header[4];
};
static_assert(sizeof(NcaHeader) == 0xC00);

enum { ContentType_Program = 0, ContentType_Meta = 1, ContentType_Control = 2 };
enum { FsType_RomFS = 0, FsType_PFS0 = 1 };
enum { HashType_HierarchicalSha256 = 2, HashType_HierarchicalIntegrity = 3 };
enum { EncryptionType_None = 1 };

struct CnmtHeader {
    u64 title_id;
    u32 title_version;
    u8 meta_type;
    u8 _0xD;
    NcmContentMetaHeader meta_header;
    u8 install_type;
    u8 _0x17;
    u32 required_sys_version;
    u8 _0x1C[0x4];
};
static_assert(sizeof(CnmtHeader) == 0x20);

struct ContentMetaData {
    NcmContentMetaHeader header;
    NcmApplicationMetaExtendedHeader extended;
    NcmContentInfo infos[3];
};

struct ContentStorageRecord {
    NcmContentMetaKey key;
    u8 storage_id;
    u8 padding[0x7];
};

struct NpdmMeta {
    u32 magic;
    u32 signature_key_generation;
    u32 _0x8;
    u8 flags;
    u8 _0xD;
    u8 main_thread_priority;
    u8 main_thread_core_num;
    u32 _0x10;
    u32 sys_resource_size;
    u32 version;
    u32 main_thread_stack_size;
    char title_name[0x10];
    char product_code[0x10];
    u8 _0x40[0x30];
    u32 aci0_offset;
    u32 aci0_size;
    u32 acid_offset;
    u32 acid_size;
};

struct NpdmAcid {
    u8 rsa_sig[0x100];
    u8 rsa_pub[0x100];
    u32 magic;
    u32 size;
    u8 version;
    u8 _0x209[0x1];
    u8 _0x20A[0x2];
    u32 flags;
    u64 program_id_min;
    u64 program_id_max;
    u32 fac_offset;
    u32 fac_size;
    u32 sac_offset;
    u32 sac_size;
    u32 kac_offset;
    u32 kac_size;
    u8 _0x238[0x8];
};

struct NpdmAci0 {
    u32 magic;
    u8 _0x4[0xC];
    u64 program_id;
    u8 _0x18[0x8];
    u32 fac_offset;
    u32 fac_size;
    u32 sac_offset;
    u32 sac_size;
    u32 kac_offset;
    u32 kac_size;
    u8 _0x38[0x8];
};

// ---------------------------------------------------------------------------------------------
// Buffers and file entries

struct Buf {
    std::vector<u8> buf;
    u64 offset = 0;
    void write(const void* data, u64 size) {
        if (offset + size >= buf.size()) buf.resize(offset + size);
        std::memcpy(buf.data() + offset, data, size);
        offset += size;
    }
    void seek(u64 to) { offset = to; }
    u64 tell() const { return offset; }
};

struct FileEntry {
    std::string name;
    std::vector<u8> data;
};
using FileEntries = std::vector<FileEntry>;

void addFile(FileEntries& entries, const char* name, const void* data, u64 size) {
    FileEntry e;
    e.name = name;
    e.data.assign(static_cast<const u8*>(data), static_cast<const u8*>(data) + size);
    entries.push_back(std::move(e));
}

struct NcaEntry {
    std::vector<u8> data;
    u8 type;
    u8 hash[SHA256_HASH_SIZE];
    NcaEntry(std::vector<u8> d, u8 t) : data(std::move(d)), type(t) { sha256CalculateHash(hash, data.data(), data.size()); }
};

u64 writePadding(Buf& buf, u64 off, u64 block) {
    const u64 size = block - (off % block);
    if (size) {
        std::vector<u8> padding(size);
        buf.write(padding.data(), padding.size());
    }
    return size;
}

u32 align32(u32 v, u32 a) { return (v + (a - 1)) & ~(a - 1); }
u64 align64(u64 v, u64 a) { return (v + (a - 1)) & ~(a - 1); }

// ---------------------------------------------------------------------------------------------
// RomFS (all files in the root directory)

u32 pathHash(u32 parent, const u8* path, u32 start, u32 len) {
    u32 hash = parent ^ 123456789;
    for (u32 i = 0; i < len; i++) {
        hash = (hash >> 5) | (hash << 27);
        hash ^= path[start + i];
    }
    return hash;
}

u32 hashTableCount(u32 entries) {
    if (entries < 3) return 3;
    if (entries < 19) return entries | 1;
    u32 count = entries;
    while (count % 2 == 0 || count % 3 == 0 || count % 5 == 0 || count % 7 == 0 || count % 11 == 0 ||
           count % 13 == 0 || count % 17 == 0)
        count++;
    return count;
}

std::vector<u8> buildRomfs(const FileEntries& entries, u64* outSize) {
    // Same layout as Sphaira: file entries in insertion order, first file first, the rest after it
    // in reverse order of insertion (its sibling list insertion).
    const u32 n = u32(entries.size());
    std::vector<u32> order; // logical file order
    if (n) {
        order.push_back(0);
        for (u32 i = n - 1; i >= 1; i--) order.push_back(i);
    }

    std::vector<u32> entryOffset(n);
    u64 fileTableSize = 0;
    for (u32 k = 0; k < n; k++) {
        entryOffset[order[k]] = u32(fileTableSize);
        fileTableSize += sizeof(romfs_file) + align32(u32(entries[order[k]].name.length() - 1), 4);
    }
    std::vector<u64> dataOffset(n);
    u64 partition = 0;
    for (u32 k = 0; k < n; k++) {
        partition = align64(partition, 0x10);
        dataOffset[order[k]] = partition;
        partition += entries[order[k]].data.size();
    }

    const u32 dirHashCount = hashTableCount(1), fileHashCount = hashTableCount(n);
    std::vector<u32> dirHash(dirHashCount, ROMFS_ENTRY_EMPTY), fileHash(fileHashCount, ROMFS_ENTRY_EMPTY);
    std::vector<u8> fileTable(fileTableSize, 0), dirTable(sizeof(romfs_dir), 0);

    for (u32 k = 0; k < n; k++) {
        u32 i = order[k];
        auto* fe = reinterpret_cast<romfs_file*>(fileTable.data() + entryOffset[i]);
        fe->parent = 0;
        u32 sibling = k + 1 < n ? entryOffset[order[k + 1]] : ROMFS_ENTRY_EMPTY;
        fe->sibling = sibling;
        fe->dataOff = dataOffset[i];
        fe->dataSize = entries[i].data.size();
        const u32 nameLen = u32(entries[i].name.length() - 1);
        const u32 h = pathHash(0, reinterpret_cast<const u8*>(entries[i].name.c_str()), 1, nameLen);
        fe->nextHash = fileHash[h % fileHashCount];
        fileHash[h % fileHashCount] = entryOffset[i];
        fe->nameLen = nameLen;
        std::memcpy(fe->name, entries[i].name.c_str() + 1, nameLen);
    }
    auto* de = reinterpret_cast<romfs_dir*>(dirTable.data());
    de->parent = 0;
    de->sibling = ROMFS_ENTRY_EMPTY;
    de->childDir = ROMFS_ENTRY_EMPTY;
    de->childFile = n ? entryOffset[order[0]] : ROMFS_ENTRY_EMPTY;
    const u32 h = pathHash(0, nullptr, 0, 0);
    de->nextHash = dirHash[h % dirHashCount];
    dirHash[h % dirHashCount] = 0;
    de->nameLen = 0;

    romfs_header header{};
    header.headerSize = sizeof(header);
    header.fileHashTableSize = fileHashCount * 4;
    header.fileTableSize = fileTableSize;
    header.dirHashTableSize = dirHashCount * 4;
    header.dirTableSize = sizeof(romfs_dir);
    header.fileDataOff = ROMFS_FILEPARTITION_OFS;
    header.dirHashTableOff = align64(partition + ROMFS_FILEPARTITION_OFS, 4);
    header.dirTableOff = header.dirHashTableOff + header.dirHashTableSize;
    header.fileHashTableOff = header.dirTableOff + header.dirTableSize;
    header.fileTableOff = header.fileHashTableOff + header.fileHashTableSize;

    Buf buf;
    buf.write(&header, sizeof(header));
    for (u32 i = 0; i < n; i++) {
        buf.seek(dataOffset[i] + ROMFS_FILEPARTITION_OFS);
        buf.write(entries[i].data.data(), entries[i].data.size());
    }
    buf.seek(header.dirHashTableOff);
    buf.write(dirHash.data(), dirHash.size() * 4);
    buf.write(dirTable.data(), dirTable.size());
    buf.write(fileHash.data(), fileHash.size() * 4);
    buf.write(fileTable.data(), fileTable.size());

    buf.seek(buf.buf.size());
    *outSize = buf.tell();
    writePadding(buf, buf.tell(), IVFC_HASH_BLOCK_SIZE);
    return buf.buf;
}

// ---------------------------------------------------------------------------------------------
// PFS0 and NCA sections

std::vector<u8> buildPfs0(const FileEntries& entries) {
    struct Header { u32 magic, total_files, string_table_size, padding; } header{};
    struct FileTable { u64 data_offset, data_size; u32 name_offset, padding; };
    std::vector<FileTable> table(entries.size());
    std::vector<char> strings;
    u64 stringOff = 0, dataOff = 0;
    for (size_t i = 0; i < entries.size(); i++) {
        table[i] = {dataOff, entries[i].data.size(), u32(stringOff), 0};
        strings.resize(stringOff + entries[i].name.length() + 1);
        std::memcpy(strings.data() + stringOff, entries[i].name.c_str(), entries[i].name.length() + 1);
        dataOff += entries[i].data.size();
        stringOff += entries[i].name.length() + 1;
    }
    strings.resize((strings.size() + 0x1F) & ~0x1F);
    header.magic = 0x30534650;
    header.total_files = u32(entries.size());
    header.string_table_size = u32(strings.size());
    Buf buf;
    buf.write(&header, sizeof(header));
    buf.write(table.data(), sizeof(FileTable) * table.size());
    buf.write(strings.data(), strings.size());
    for (auto& e : entries) buf.write(e.data.data(), e.data.size());
    return buf.buf;
}

std::vector<u8> hashBlocks(const std::vector<u8>& src, u32 block, bool pad) {
    Buf buf;
    u8 hash[SHA256_HASH_SIZE];
    u64 readSize = block;
    for (u64 i = 0; i < src.size(); i += readSize) {
        if (i + readSize >= src.size()) readSize = src.size() - i;
        sha256CalculateHash(hash, src.data() + i, readSize);
        buf.write(hash, sizeof(hash));
    }
    if (pad) writePadding(buf, buf.tell(), IVFC_HASH_BLOCK_SIZE);
    return buf.buf;
}

void writeSection(NcaHeader& h, u8 index, u64 start, u64 end) {
    auto& s = h.fs_table[index];
    s.media_start_offset = u32(start / 0x200);
    s.media_end_offset = u32(end / 0x200);
    s._0x8[0] = 0x1;
}

void writePfs0Section(NcaHeader& h, u8 index, const FileEntries& entries, u32 blockSize, Buf& buf) {
    const auto pfs0 = buildPfs0(entries);
    const auto table = hashBlocks(pfs0, blockSize, false);
    u8 master[SHA256_HASH_SIZE];
    sha256CalculateHash(master, table.data(), table.size());

    buf.write(table.data(), table.size());
    const u64 padding = writePadding(buf, table.size(), PFS0_PADDING_SIZE);
    auto& fs = h.fs_header[index];
    fs.hash_data.hierarchical_sha256_data.pfs0_layer.offset = table.size() + padding;
    fs.hash_data.hierarchical_sha256_data.pfs0_layer.size = pfs0.size();
    buf.write(pfs0.data(), pfs0.size());
    writePadding(buf, buf.tell(), 0x200);

    const u64 start = index == 0 ? sizeof(NcaHeader) : u64(h.fs_table[index - 1].media_end_offset) * 0x200;
    writeSection(h, index, start, buf.tell());
    fs.hash_type = HashType_HierarchicalSha256;
    fs.fs_type = FsType_PFS0;
    fs.version = 2;
    fs.hash_data.hierarchical_sha256_data.layer_count = 2;
    fs.hash_data.hierarchical_sha256_data.block_size = blockSize;
    fs.encryption_type = EncryptionType_None;
    fs.hash_data.hierarchical_sha256_data.hash_layer.size = table.size();
    std::memcpy(fs.hash_data.hierarchical_sha256_data.master_hash, master, sizeof(master));
    sha256CalculateHash(h.fs_header_hash[index], &fs, sizeof(fs));
}

void writeRomfsSection(NcaHeader& h, u8 index, const FileEntries& entries, Buf& buf) {
    auto& fs = h.fs_header[index];
    auto& meta = fs.hash_data.integrity_meta_info;
    auto& levels = meta.info_level_hash.levels;

    std::vector<u8> ivfc[IVFC_MAX_LEVEL];
    ivfc[5] = buildRomfs(entries, &levels[5].hash_data_size);
    for (int b = 4; b >= 0; b--) {
        ivfc[b] = hashBlocks(ivfc[b + 1], IVFC_HASH_BLOCK_SIZE, true);
        levels[b].hash_data_size = ivfc[b].size();
        levels[b].block_size = 0x0E;
    }
    levels[0].logical_offset = 0;
    for (int i = 1; i <= 5; i++) levels[i].logical_offset = levels[i - 1].logical_offset + levels[i - 1].hash_data_size;
    for (const auto& lv : ivfc) buf.write(lv.data(), lv.size());
    writePadding(buf, buf.tell(), 0x200);

    sha256CalculateHash(meta.master_hash, ivfc[0].data(), ivfc[0].size());
    const u64 start = index == 0 ? sizeof(NcaHeader) : u64(h.fs_table[index - 1].media_end_offset) * 0x200;
    writeSection(h, index, start, buf.tell());
    fs.hash_type = HashType_HierarchicalIntegrity;
    fs.fs_type = FsType_RomFS;
    fs.version = 2;
    meta.magic = 0x43465649; // IVFC
    meta.version = 0x20000;
    meta.master_hash_size = SHA256_HASH_SIZE;
    meta.info_level_hash.max_layers = 7;
    fs.encryption_type = EncryptionType_None;
    levels[5].block_size = 0x0E;
    sha256CalculateHash(h.fs_header_hash[index], &fs, sizeof(fs));
}

void finishNca(NcaHeader& h, u64 tid, const u8* headerKey, u8 type, Buf& buf) {
    h.magic = NCA3_MAGIC;
    h.distribution_type = 0; // system
    h.content_type = type;
    h.program_id = tid;
    h.sdk_version = 0x000C1100;
    h.size = buf.tell();
    Aes128XtsContext ctx{};
    aes128XtsContextCreate(&ctx, headerKey, headerKey + 0x10, true);
    u8 sector = 0;
    for (u64 pos = 0; pos < 0xC00; pos += 0x200) {
        aes128XtsContextResetSector(&ctx, sector++, true);
        aes128XtsEncrypt(&ctx, reinterpret_cast<u8*>(&h) + pos, reinterpret_cast<const u8*>(&h) + pos, 0x200);
    }
    buf.seek(0);
    buf.write(&h, sizeof(h));
}

// ---------------------------------------------------------------------------------------------
// NPDM / NACP patching

bool patchKernelCap(std::vector<u8>& npdm, u32 off, u32 size, u32 bitmask, u32 value) {
    const u32 pattern = BIT(bitmask) - 1;
    const u32 mask = BIT(bitmask) | pattern;
    for (u32 i = 0; i < size; i += 4) {
        u32 cap;
        std::memcpy(&cap, npdm.data() + off + i, sizeof(cap));
        if ((cap & mask) == pattern) {
            cap = value | pattern;
            std::memcpy(npdm.data() + off + i, &cap, sizeof(cap));
            return true;
        }
    }
    return false;
}

bool patchNpdm(std::vector<u8>& npdm, u64 tid) {
    NpdmMeta meta{};
    NpdmAci0 aci0{};
    NpdmAcid acid{};
    if (npdm.size() < sizeof(meta)) return false;
    std::memcpy(&meta, npdm.data(), sizeof(meta));
    std::memcpy(&aci0, npdm.data() + meta.aci0_offset, sizeof(aci0));
    std::memcpy(&acid, npdm.data() + meta.acid_offset, sizeof(acid));
    std::memset(meta.title_name, 0, sizeof(meta.title_name));
    std::strncpy(meta.title_name, "Application", sizeof(meta.title_name) - 1);
    constexpr u8 kAddressSpaceShift = 1, kAddressSpaceMask = 0x7 << kAddressSpaceShift;
    meta.flags = u8((meta.flags & ~kAddressSpaceMask) | (2 << kAddressSpaceShift)); // 39-bit address space
    aci0.program_id = tid;
    acid.program_id_min = tid;
    acid.program_id_max = tid;

    // Three CPU cores, priorities 28-59 (same as Sphaira's default).
    const u32 flags = ((((2u << 8) | 0u) << 6 | 28u) << 6 | 59u) << 4;
    bool ok = patchKernelCap(npdm, meta.aci0_offset + aci0.kac_offset, aci0.kac_size, 3, flags) &&
              patchKernelCap(npdm, meta.acid_offset + acid.kac_offset, acid.kac_size, 3, flags);

    // Atmosphère 1.8.0+ (HOS 19) moved the debug flag; enable it there, as Sphaira does.
    bool svcDebug = false;
    if (R_SUCCEEDED(splInitialize())) {
        u64 ver = 0;
        if (R_SUCCEEDED(splGetConfig(SplConfigItem(65000), &ver))) svcDebug = (ver >> 40) >= MAKEHOSVERSION(1, 8, 0);
        splExit();
    }
    if (svcDebug) {
        patchKernelCap(npdm, meta.aci0_offset + aci0.kac_offset, aci0.kac_size, 16, BIT(19));
        patchKernelCap(npdm, meta.acid_offset + acid.kac_offset, acid.kac_size, 16, BIT(19));
    }
    std::memcpy(npdm.data(), &meta, sizeof(meta));
    std::memcpy(npdm.data() + meta.aci0_offset, &aci0, sizeof(aci0));
    std::memcpy(npdm.data() + meta.acid_offset, &acid, sizeof(acid));
    return ok;
}

void patchNacp(NacpStruct& nacp, u64 tid) {
    nacp.startup_user_account = 0;
    nacp.user_account_switch_lock = 0;
    nacp.add_on_content_registration_type = 1;
    nacp.screenshot = 0;
    nacp.video_capture = 2;
    nacp.logo_type = 2;
    nacp.logo_handling = 0;
    nacp.data_loss_confirmation = 0;
    nacp.required_network_service_license_on_launch = 0;
    nacp.application_error_code_category = 0;
    nacp.presence_group_id = tid;
    nacp.save_data_owner_id = tid;
    nacp.pseudo_device_id_seed = tid;
    nacp.add_on_content_base_id = tid ^ 0x1000;
    for (auto& id : nacp.local_communication_id) id = tid;
    nacp.play_log_policy = 0;
    nacp.play_log_query_capability = 0;
    nacp.user_account_save_data_size = 0;
    nacp.user_account_save_data_journal_size = 0;
    nacp.device_save_data_size = 0;
    nacp.device_save_data_journal_size = 0;
    nacp.user_account_save_data_size_max = 0;
    nacp.user_account_save_data_journal_size_max = 0;
    nacp.device_save_data_size_max = 0;
    nacp.device_save_data_journal_size_max = 0;
}

// Reads the NACP and the icon embedded in a .nro (asset section after the executable).
bool readNroAssets(const std::string& path, NacpStruct& nacp, std::vector<u8>& icon, std::string& error) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        error = "não foi possível abrir " + path;
        return false;
    }
    NroStart start;
    NroHeader header;
    NroAssetHeader assets;
    bool ok = std::fread(&start, sizeof(start), 1, f) == 1 && std::fread(&header, sizeof(header), 1, f) == 1 &&
              header.magic == NROHEADER_MAGIC && std::fseek(f, header.size, SEEK_SET) == 0 &&
              std::fread(&assets, sizeof(assets), 1, f) == 1 && assets.magic == NROASSETHEADER_MAGIC &&
              assets.nacp.size >= sizeof(NacpStruct) && assets.icon.size > 0;
    if (ok) {
        icon.resize(assets.icon.size);
        ok = std::fseek(f, header.size + assets.nacp.offset, SEEK_SET) == 0 &&
             std::fread(&nacp, sizeof(nacp), 1, f) == 1 &&
             std::fseek(f, header.size + assets.icon.offset, SEEK_SET) == 0 &&
             std::fread(icon.data(), 1, icon.size(), f) == icon.size();
    }
    std::fclose(f);
    if (!ok) error = "o arquivo .nro não tem ícone/NACP válidos";
    return ok;
}

u64 titleIdFor(const std::string& nroPath) {
    u64 hash[SHA256_HASH_SIZE / sizeof(u64)];
    const std::string data = nroPath + nroPath; // path + argv (argv is the path itself)
    sha256CalculateHash(hash, data.data(), data.length());
    return 0x0500000000000000ull | (hash[0] & 0x00FFFFFFFFFFF000ull);
}

std::string resultText(const char* what, Result rc) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s (erro 0x%X, %04u-%04u)", what, rc, 2000 + R_MODULE(rc), R_DESCRIPTION(rc));
    return buf;
}

// ns:am2 commands used by Sphaira that libnx does not wrap.
constexpr u8 kRecordInstalled = 0x3; // ApplicationRecordType_Installed
Result nsPushRecord(Service* s, u64 tid, const ContentStorageRecord* records, u32 count) {
    const struct {
        u8 last_modified_event;
        u8 padding[0x7];
        u64 tid;
    } in = {kRecordInstalled, {0}, tid};
    return serviceDispatchIn(s, 16, in, .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_In},
                             .buffers = {{records, sizeof(*records) * count}});
}

Result nsInvalidateControlCache(Service* s, u64 tid) { return serviceDispatchIn(s, 404, tid); }

} // namespace

bool shortcutSupported() { return true; }

bool shortcutInstalled(const std::string& nroPath) {
    if (R_FAILED(nsInitialize())) return false;
    s32 count = 0;
    bool found = R_SUCCEEDED(nsCountApplicationContentMeta(titleIdFor(nroPath), &count)) && count > 0;
    nsExit();
    return found;
}

bool installShortcut(const std::string& nroPath, std::string& error, const std::function<void(const char*)>& step) {
    NacpStruct nacp{};
    std::vector<u8> icon;
    if (!readNroAssets(nroPath, nacp, icon, error)) return false;
    pkx::Bytes main, npdmBytes;
    if (!pkx::readFile(romfsFile("forwarder/main"), main) || !pkx::readFile(romfsFile("forwarder/main.npdm"), npdmBytes)) {
        error = "o carregador do atalho não está no romfs do app";
        return false;
    }
    std::vector<u8> npdm(npdmBytes.begin(), npdmBytes.end());
    const u64 tid = titleIdFor(nroPath);

    // Header key, derived on the console.
    step("Gerando chaves");
    u8 headerKey[0x20];
    Result rc = splCryptoInitialize();
    if (R_SUCCEEDED(rc)) {
        u8 kek[0x20];
        rc = splCryptoGenerateAesKek(HEADER_KEK_SRC, 0, 0, kek);
        if (R_SUCCEEDED(rc)) rc = splCryptoGenerateAesKey(kek, HEADER_KEY_SRC, headerKey);
        if (R_SUCCEEDED(rc)) rc = splCryptoGenerateAesKey(kek, HEADER_KEY_SRC + 0x10, headerKey + 0x10);
        splCryptoExit();
    }
    if (R_FAILED(rc)) {
        error = resultText("falha ao derivar a chave das NCAs", rc);
        return false;
    }
    if (!patchNpdm(npdm, tid)) {
        error = "falha ao preparar o carregador (npdm)";
        return false;
    }
    patchNacp(nacp, tid);

    std::vector<NcaEntry> ncas;
    {
        step("Criando programa");
        FileEntries exefs, romfs;
        addFile(exefs, "main", main.data(), main.size());
        addFile(exefs, "main.npdm", npdm.data(), npdm.size());
        addFile(romfs, "/nextArgv", nroPath.data(), nroPath.size());
        addFile(romfs, "/nextNroPath", nroPath.data(), nroPath.size());
        NcaHeader h{};
        Buf buf;
        buf.write(&h, sizeof(h));
        writePfs0Section(h, 0, exefs, PFS0_EXEFS_HASH_BLOCK_SIZE, buf);
        writeRomfsSection(h, 1, romfs, buf);
        finishNca(h, tid, headerKey, ContentType_Program, buf);
        ncas.emplace_back(std::move(buf.buf), NcmContentType_Program);
    }
    {
        step("Criando ícone e nome");
        FileEntries romfs;
        addFile(romfs, "/control.nacp", &nacp, sizeof(nacp));
        addFile(romfs, "/icon_AmericanEnglish.dat", icon.data(), icon.size());
        NcaHeader h{};
        Buf buf;
        buf.write(&h, sizeof(h));
        writeRomfsSection(h, 0, romfs, buf);
        finishNca(h, tid, headerKey, ContentType_Control, buf);
        ncas.emplace_back(std::move(buf.buf), NcmContentType_Control);
    }

    // Meta.
    step("Criando registro");
    CnmtHeader cnmt{};
    NcmApplicationMetaExtendedHeader extended{};
    NcmPackagedContentInfo packaged[2]{};
    u8 digest[0x20]{};
    cnmt.title_id = tid;
    cnmt.meta_type = NcmContentMetaType_Application;
    cnmt.meta_header.extended_header_size = sizeof(extended);
    cnmt.meta_header.content_count = 2;
    cnmt.meta_header.content_meta_count = 1;
    cnmt.meta_header.storage_id = NcmStorageId_SdCard;
    extended.patch_id = tid | 0x800;
    for (int i = 0; i < 2; i++) {
        std::memcpy(packaged[i].hash, ncas[i].hash, sizeof(packaged[i].hash));
        std::memcpy(&packaged[i].info.content_id, ncas[i].hash, sizeof(packaged[i].info.content_id));
        packaged[i].info.content_type = ncas[i].type;
        ncmU64ToContentInfoSize(ncas[i].data.size(), &packaged[i].info);
    }
    Buf cnmtBuf;
    cnmtBuf.write(&cnmt, sizeof(cnmt));
    cnmtBuf.write(&extended, sizeof(extended));
    cnmtBuf.write(packaged, sizeof(packaged));
    cnmtBuf.write(digest, sizeof(digest));
    FileEntries metaFiles;
    char cnmtName[40];
    std::snprintf(cnmtName, sizeof cnmtName, "Application_%016lX.cnmt", tid);
    addFile(metaFiles, cnmtName, cnmtBuf.buf.data(), cnmtBuf.buf.size());
    {
        NcaHeader h{};
        Buf buf;
        buf.write(&h, sizeof(h));
        writePfs0Section(h, 0, metaFiles, PFS0_META_HASH_BLOCK_SIZE, buf);
        finishNca(h, tid, headerKey, ContentType_Meta, buf);
        ncas.emplace_back(std::move(buf.buf), NcmContentType_Meta);
    }
    const NcaEntry& metaNca = ncas.back();

    NcmContentMetaKey key{};
    key.id = tid;
    key.version = 0;
    key.type = NcmContentMetaType_Application;
    key.install_type = NcmContentInstallType_Full;
    ContentStorageRecord record{};
    record.key = key;
    record.storage_id = NcmStorageId_SdCard;
    ContentMetaData metaData{};
    metaData.header = cnmt.meta_header;
    metaData.header.content_count++;
    metaData.header.storage_id = 0;
    metaData.extended = extended;
    std::memcpy(&metaData.infos[0].content_id, metaNca.hash, sizeof(metaData.infos[0].content_id));
    metaData.infos[0].content_type = metaNca.type;
    ncmU64ToContentInfoSize(cnmtBuf.buf.size(), &metaData.infos[0]); // same as Sphaira
    metaData.infos[1] = packaged[0].info;
    metaData.infos[2] = packaged[1].info;

    // Install.
    if (R_FAILED(rc = ncmInitialize())) {
        error = resultText("ncm indisponível", rc);
        return false;
    }
    {
        step("Gravando no cartão microSD");
        NcmContentStorage cs;
        if (R_FAILED(rc = ncmOpenContentStorage(&cs, NcmStorageId_SdCard))) {
            ncmExit();
            error = resultText("não foi possível abrir o armazenamento do microSD", rc);
            return false;
        }
        for (const auto& nca : ncas) {
            NcmContentId contentId;
            NcmPlaceHolderId placeholder;
            std::memcpy(&contentId, nca.hash, sizeof(contentId));
            rc = ncmContentStorageGeneratePlaceHolderId(&cs, &placeholder);
            if (R_SUCCEEDED(rc)) {
                ncmContentStorageDeletePlaceHolder(&cs, &placeholder);
                rc = ncmContentStorageCreatePlaceHolder(&cs, &contentId, &placeholder, s64(nca.data.size()));
            }
            if (R_SUCCEEDED(rc)) rc = ncmContentStorageWritePlaceHolder(&cs, &placeholder, 0, nca.data.data(), nca.data.size());
            if (R_SUCCEEDED(rc)) {
                ncmContentStorageDelete(&cs, &contentId);
                rc = ncmContentStorageRegister(&cs, &contentId, &placeholder);
            }
            if (R_FAILED(rc)) break;
        }
        ncmContentStorageClose(&cs);
        if (R_FAILED(rc)) {
            ncmExit();
            error = resultText("falha ao gravar as NCAs", rc);
            return false;
        }
    }
    {
        step("Atualizando o banco de títulos");
        NcmContentMetaDatabase db;
        rc = ncmOpenContentMetaDatabase(&db, NcmStorageId_SdCard);
        if (R_SUCCEEDED(rc)) {
            rc = ncmContentMetaDatabaseSet(&db, &key, &metaData, sizeof(metaData));
            if (R_SUCCEEDED(rc)) rc = ncmContentMetaDatabaseCommit(&db);
            ncmContentMetaDatabaseClose(&db);
        }
        ncmExit();
        if (R_FAILED(rc)) {
            error = resultText("falha ao registrar o título", rc);
            return false;
        }
    }
    {
        step("Criando o ícone no menu HOME");
        if (R_FAILED(rc = nsInitialize())) {
            error = resultText("ns indisponível", rc);
            return false;
        }
        Service am;
        bool own = hosversionAtLeast(3, 0, 0);
        if (own) rc = nsGetApplicationManagerInterface(&am);
        else am = *nsGetServiceSession_ApplicationManagerInterface();
        if (R_SUCCEEDED(rc)) {
            nsDeleteApplicationEntity(tid); // replaces a previous install
            rc = nsPushRecord(&am, tid, &record, 1);
            nsInvalidateControlCache(&am, tid);
            if (own) serviceClose(&am);
        }
        nsExit();
        if (R_FAILED(rc)) {
            error = resultText("falha ao criar o registro no menu HOME", rc);
            return false;
        }
    }
    return true;
}

bool removeShortcut(const std::string& nroPath, std::string& error) {
    Result rc = nsInitialize();
    if (R_SUCCEEDED(rc)) {
        rc = nsDeleteApplicationCompletely(titleIdFor(nroPath));
        nsExit();
    }
    if (R_FAILED(rc)) {
        error = resultText("falha ao remover o atalho", rc);
        return false;
    }
    return true;
}

} // namespace plat
