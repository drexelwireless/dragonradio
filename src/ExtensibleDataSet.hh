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
    H5::DataSet _ds;
    H5::DataType _dt;
    size_t _size;
    size_t _capacity;
};

#endif /* EXTENSIBLEDATASET_H_ */
