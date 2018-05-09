#ifndef EXTENSIBLEDATASET_H_
#define EXTENSIBLEDATASET_H_

#include <string>

#include <H5Cpp.h>

class ExtensibleDataSet {
public:
    ExtensibleDataSet(const H5::CommonFG& loc, const std::string& name, const H5::DataType &dt);
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
