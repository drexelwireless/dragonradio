#include "ExtensibleDataSet.hh"

/** @brief We allocate capacity in multiples of this. */
const size_t GRANULARITY = 4096;

/** @brief We chunk in multiples of this. */
const size_t CHUNK_GRANULARITY = 4096;

ExtensibleDataSet::ExtensibleDataSet(const H5::CommonFG& loc, const std::string& name, const H5::DataType &dt) :
    _dt(dt),
    _size(0),
    _capacity(0)
{
    hsize_t               dim[] = { GRANULARITY };
    hsize_t               chunk_dims[] = { CHUNK_GRANULARITY };
    H5::DataSpace         space(1, dim);
    H5::DSetCreatPropList plist;

    plist.setChunk(1, chunk_dims);
    _ds = loc.createDataSet(name, _dt, space, plist);
}

ExtensibleDataSet::~ExtensibleDataSet()
{
    hsize_t dim[] = { _size };

    _ds.extend(dim);
    _ds.close();
}

void ExtensibleDataSet::reserve(size_t capacity)
{
    while (_capacity < capacity)
        _capacity += GRANULARITY;

    hsize_t dim[] = { _capacity };

    _ds.extend(dim);
}

void ExtensibleDataSet::write(const void *buf, size_t n)
{
    hsize_t count[] = { n };
    hsize_t off[] = { _size };

    reserve(_size+n);

    // Create a *copy* of the data space.
    H5::DataSpace space(_ds.getSpace());
    // The subspace we will modify.
    H5::DataSpace memspace(1, count, NULL);

    space.selectHyperslab(H5S_SELECT_SET, count, off);
    _ds.write(buf, _dt, memspace, space);

    _size += n;
}
