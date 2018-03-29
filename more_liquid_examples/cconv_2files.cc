#include <vector>
#include <complex>
#include <stdio.h>
#include <fstream>
#include <liquid.h>

int main()
{
    std::ifstream infilea("../txdata/txed_data_4.bin",std::ifstream::binary);
    std::ifstream infileb("../emulated_channel/emulated_channel_4.bin",std::ifstream::binary);
    unsigned int N = 21504;

    std::vector<std::complex<float> > siga(N);
    std::vector<std::complex<float> > sigb(N);
    infilea.read((char*)&siga.front(),siga.size()*sizeof(std::complex<float>));
    infileb.read((char*)&sigb.front(),sigb.size()*sizeof(std::complex<float>));

    std::vector<std::complex<float> > sigc(N);

    /*// sample input vector A
    std::complex<float> sampa[4];
    sampa[0] = std::complex<float>(1.0,1.0);
    sampa[1] = std::complex<float>(2.0,1.0);
    sampa[2] = std::complex<float>(3.0,2.0);
    sampa[3] = std::complex<float>(2.0,2.0);

    // sample input vector B
    std::complex<float> sampb[4];
    sampb[0] = std::complex<float>(1.0,2.0);
    sampb[1] = std::complex<float>(4.0,2.0);
    sampb[2] = std::complex<float>(3.0,3.0);
    sampb[3] = std::complex<float>(1.0,1.0);*/

    // output vector C
    //std::complex<float> sampc[4];

    // define filter with inpulse response defined by one of the sample vectors
    firfilt_cccf q = firfilt_cccf_create(&siga[0],N);

    // push all data of second sequence into the internal buffers
    // (need to do this so the linear convolution becomes circular)
    for(unsigned int i=0;i<N;i++)
    {
        firfilt_cccf_push(q,sigb[i]);
    } 

    // now actually perform convolution
    for(unsigned int i = 0;i<N;i++)
    {
        firfilt_cccf_push(q,sigb[i]);
        firfilt_cccf_execute(q,&sigc[i]);
    }

    // get rid of the evidence
    firfilt_cccf_destroy(q);

    std::ofstream outfile;
    outfile.open("output.bin",std::ofstream::binary);
    outfile.write((const char*)&sigc.front(),sigc.size()*sizeof(std::complex<float>));
}
