// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef EXTENSIBLEDATASET_H_
#define EXTENSIBLEDATASET_H_

#include <string>

#include <H5Cpp.h>

#if H5_VERSION_GE(1, 10, 0)
using Group = H5::Group;
#else
using Group = H5::CommonFG;
#endif

class ExtensibleDataSet {
public:
    ExtensibleDataSet(const Group& loc, const std::string& name, const H5::DataType &dt);
    ~ExtensibleDataSet();

    void reserve(size_t size);
    void write(const void *buf, size_t n);

private:
    H5::DataSet ds_;
    H5::DataType dt_;
    size_t size_;
    size_t capacity_;
};

#endif /* EXTENSIBLEDATASET_H_ */
