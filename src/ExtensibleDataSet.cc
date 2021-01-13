// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "ExtensibleDataSet.hh"

/** @brief We allocate capacity in multiples of this. */
constexpr size_t GRANULARITY = 4*1024*1024;

/** @brief We chunk in multiples of this. */
constexpr size_t CHUNK_GRANULARITY = 4*1024*1024;

ExtensibleDataSet::ExtensibleDataSet(const Group& loc, const std::string& name, const H5::DataType &dt)
  : dt_(dt)
  , size_(0)
  , capacity_(0)
{
    hsize_t               dim[] = { GRANULARITY };
    hsize_t               maxdims[] = { H5S_UNLIMITED };
    hsize_t               chunk_dims[] = { CHUNK_GRANULARITY };
    H5::DataSpace         space(1, dim, maxdims);
    H5::DSetCreatPropList plist;

    plist.setChunk(1, chunk_dims);
    ds_ = loc.createDataSet(name, dt_, space, plist);
}

ExtensibleDataSet::~ExtensibleDataSet()
{
    hsize_t dim[] = { size_ };

    ds_.extend(dim);
    ds_.close();
}

void ExtensibleDataSet::reserve(size_t capacity)
{
    if (capacity > capacity_) {
        capacity_ = GRANULARITY * ((capacity + GRANULARITY - 1) / GRANULARITY);

        hsize_t dim[] = { capacity_ };

        ds_.extend(dim);
    }
}

void ExtensibleDataSet::write(const void *buf, size_t n)
{
    hsize_t count[] = { n };
    hsize_t off[] = { size_ };

    try {
        reserve(size_+n);

        // Create a *copy* of the data space.
        H5::DataSpace space(ds_.getSpace());
        // The subspace we will modify.
        H5::DataSpace memspace(1, count, NULL);

        space.selectHyperslab(H5S_SELECT_SET, count, off);
        ds_.write(buf, dt_, memspace, space);

        size_ += n;
    } catch(H5::DataSetIException &e) {
        fprintf(stderr, "HDF5 exception: %s: %s\n",
            e.getCFuncName(),
            e.getCDetailMsg());
    }
}
