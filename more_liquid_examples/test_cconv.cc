#include <vector>
#include <complex>
#include <liquid.h>
#include <stdio.h>

int main()
{
    // sample input vector A
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
    sampb[3] = std::complex<float>(1.0,1.0);

    // output vector C
    std::complex<float> sampc[4];

    // define filter with inpulse response defined by one of the sample vectors
    firfilt_cccf q = firfilt_cccf_create(&sampa[0],4);

    // push all data of second sequence into the internal buffers
    // (need to do this so the linear convolution becomes circular)
    for(unsigned int i=0;i<4;i++)
    {
        firfilt_cccf_push(q,sampb[i]);
    } 

    // now actually perform convolution
    for(unsigned int i = 0;i<4;i++)
    {
        firfilt_cccf_push(q,sampb[i]);
        firfilt_cccf_execute(q,&sampc[i]);
    }

    // get rid of the evidence
    firfilt_cccf_destroy(q);

    printf("Calculated Output: ");
    for(unsigned int i=0;i<4;i++)
    {
        printf("%.1f+%.1f*1j ",std::real(sampc[i]),std::imag(sampc[i]));
    }
    printf("\nExpected Output: 14.0 16.0 14.0 16.0\n");
}
