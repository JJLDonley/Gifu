#include "base/zip.h"

#include "base/os.h"

#include <stdlib.h>
#include <string.h>

int gifu_sinflate(void *out, int cap, const void *in, int size);   // third_party/sinfl (renamed)

static u32 rd16(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8); }
static u32 rd32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }

static bool fail(String *error, const char *message) {
    if (error) *error = string_from(sv(message));
    return false;
}

// Rejects absolute paths, drive letters and any ".." component.
static bool safe_entry_name(StringView name) {
    if (name.len == 0 || name.data[0] == '/' || name.data[0] == '\\') return false;
    if (name.len >= 2 && name.data[1] == ':') return false;
    StringView rest = name;
    while (rest.len > 0) {
        isize i = 0;
        while (i < rest.len && rest.data[i] != '/' && rest.data[i] != '\\') ++i;
        if (sv_eq(sv_take(rest, i), SV(".."))) return false;
        rest = sv_drop(rest, i + 1);
    }
    return true;
}

bool zip_extract_memory(StringView data, StringView dest_dir, ZipStats *stats, String *error) {
    const u8 *z = (const u8 *)data.data;
    isize n = data.len;
    if (stats) *stats = (ZipStats){ 0 };
    // End of central directory: scan backwards (comment is at most 64 KiB).
    isize eocd = -1;
    for (isize i = n - 22; i >= 0 && i >= n - 22 - 65535; --i) {
        if (rd32(z + i) == 0x06054b50) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) return fail(error, "not a zip file (no end of central directory)");
    u32 entries = rd16(z + eocd + 10);
    u32 cd_offset = rd32(z + eocd + 16);
    if ((isize)cd_offset >= n) return fail(error, "corrupt zip (central directory offset)");
    os_make_dirs(dest_dir);

    isize p = cd_offset;
    for (u32 e = 0; e < entries; ++e) {
        if (p + 46 > n || rd32(z + p) != 0x02014b50) return fail(error, "corrupt zip (central directory entry)");
        u32 method = rd16(z + p + 10);
        u32 comp_size = rd32(z + p + 20), size = rd32(z + p + 24);
        u32 name_len = rd16(z + p + 28), extra_len = rd16(z + p + 30), comment_len = rd16(z + p + 32);
        u32 local = rd32(z + p + 42);
        StringView name = sv_from_parts((const char *)z + p + 46, name_len);
        p += 46 + name_len + extra_len + comment_len;

        if (name.len > 0 && (name.data[name.len - 1] == '/' || name.data[name.len - 1] == '\\')) continue;   // directory
        if (!safe_entry_name(name)) return fail(error, "zip entry escapes the destination folder");
        if ((isize)local + 30 > n || rd32(z + local) != 0x04034b50) return fail(error, "corrupt zip (local header)");
        isize start = local + 30 + rd16(z + local + 26) + rd16(z + local + 28);
        if (start + (isize)comp_size > n) return fail(error, "corrupt zip (truncated data)");

        u8 *out = malloc(size ? size : 1);
        if (!out) return fail(error, "out of memory");
        bool ok = true;
        if (method == 0) {
            if (comp_size != size) ok = false;
            else memcpy(out, z + start, size);
        } else if (method == 8) {
            ok = gifu_sinflate(out, (int)size, z + start, (int)comp_size) == (int)size;
        } else {
            free(out);
            return fail(error, "unsupported zip compression method");
        }
        if (!ok) {
            free(out);
            return fail(error, "corrupt zip (decompression failed)");
        }
        String path = path_join(dest_dir, name);
        os_make_dirs(path_dirname(string_sv(&path)));
        bool written = os_write_file(string_sv(&path), sv_from_parts((const char *)out, size));
        string_free(&path);
        free(out);
        if (!written) return fail(error, "cannot write extracted file");
        if (stats) {
            stats->files++;
            stats->bytes += size;
        }
    }
    return true;
}

bool zip_extract(StringView zip_path, StringView dest_dir, ZipStats *stats, String *error) {
    String data = { 0 };
    if (!os_read_file(zip_path, &data)) return fail(error, "cannot read zip file");
    bool ok = zip_extract_memory(string_sv(&data), dest_dir, stats, error);
    string_free(&data);
    return ok;
}
