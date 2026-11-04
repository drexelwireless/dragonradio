// The logger's HDF5 datasets after the 2026-11-04 cache change: 16,384-row
// chunks (ExtensibleDataSet.cc) and an 8 MB chunk cache per dataset
// (Logger.cc). Rows written across many chunk boundaries, in uneven batches,
// must read back exactly, and the heap must stay near one cache per dataset.
#include <malloc.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "ExtensibleDataSet.hh"

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } } while (0)

struct Row {
    double  timestamp;
    int64_t seq;
    char    pad[104];      // 120-byte rows, the size of the large logger records
};

static size_t heapInUse()
{
    struct mallinfo2 m = mallinfo2();
    return m.uordblks + m.hblkhd;
}

int main()
{
    const char *path = "/tmp/test_logger_chunks.h5";
    const size_t nrows = 400000;    // about 24 chunks
    H5::CompType dt(sizeof(Row));

    dt.insertMember("timestamp", HOFFSET(Row, timestamp), H5::PredType::NATIVE_DOUBLE);
    dt.insertMember("seq", HOFFSET(Row, seq), H5::PredType::NATIVE_INT64);
    dt.insertMember("pad", HOFFSET(Row, pad), H5::StrType(H5::PredType::C_S1, sizeof(Row::pad)));

    const size_t heap0 = heapInUse();
    size_t heap_max = heap0;
    {
        // The logger's file access settings (Logger.cc).
        H5::FileAccPropList acc_plist;

        acc_plist.setCache(512, 512, 2*1024*4*1024, 0.0);

        H5::H5File file(path, H5F_ACC_TRUNC, H5::FileCreatPropList::DEFAULT, acc_plist);
        ExtensibleDataSet a(file, "a", dt);
        ExtensibleDataSet b(file, "b", dt);
        std::vector<Row> batch;
        size_t written = 0;

        for (size_t n = 1; written < nrows; n = n % 97 + 1) {
            batch.resize(std::min(n, nrows - written));
            for (size_t i = 0; i < batch.size(); ++i) {
                std::memset(&batch[i], 0, sizeof(Row));
                batch[i].timestamp = 1e-3*static_cast<double>(written + i);
                batch[i].seq = static_cast<int64_t>(written + i);
            }
            a.write(batch.data(), batch.size());
            b.write(batch.data(), batch.size());
            written += batch.size();
            if (written % 10000 < 97)
                heap_max = std::max(heap_max, heapInUse());
        }
    }

    H5::H5File file(path, H5F_ACC_RDONLY);

    for (const char *name : {"a", "b"}) {
        H5::DataSet ds = file.openDataSet(name);
        hsize_t dims[1];

        ds.getSpace().getSimpleExtentDims(dims);
        CHECK(dims[0] == nrows, "%s: %llu rows, expected %zu", name, (unsigned long long) dims[0], nrows);

        H5::DSetCreatPropList plist = ds.getCreatePlist();
        hsize_t chunk[1];

        plist.getChunk(1, chunk);
        CHECK(chunk[0] == 16384, "%s: chunk of %llu rows, expected 16384", name, (unsigned long long) chunk[0]);

        std::vector<Row> rows(dims[0]);

        ds.read(rows.data(), dt);
        size_t bad = 0;

        for (size_t i = 0; i < rows.size(); ++i)
            bad += rows[i].seq != static_cast<int64_t>(i) || rows[i].timestamp != 1e-3*static_cast<double>(i);
        CHECK(bad == 0, "%s: %zu rows differ", name, bad);
    }
    std::remove(path);

    const double growth_mb = (heap_max - heap0)/1e6;

    std::printf("  heap growth while writing two datasets: %.1f MB (cache 8 MB each)\n", growth_mb);
    CHECK(growth_mb < 40.0, "heap grew %.1f MB writing two datasets", growth_mb);

    std::printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
