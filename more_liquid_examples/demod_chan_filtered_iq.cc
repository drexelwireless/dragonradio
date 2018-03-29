#include <fstream>
#include <vector>
#include <complex>
#include <stdio.h>
#include <liquid.h>
#include <liquid/multichannelrx.h>

std::ofstream outfile;
std::ofstream outfile2;

int rxCallback(
        unsigned char *  _header,
        int              _header_valid,
        unsigned char *  _payload,
        unsigned int     _payload_len,
        int              _payload_valid,
        framesyncstats_s _stats,
        void *           _userdata,
        liquid_float_complex* G,
        liquid_float_complex* G_hat,
        unsigned int M
        )
{
    if(_header_valid)
    {
        if(_payload_valid)
        {
            printf("DEMODULATED %u BYTES\n",_payload_len);

            // vectorize channel measurements/estiamtes
            std::vector<std::complex<float> > G_vec(&G[0],&G[0]+M);
            std::vector<std::complex<float> > G_hat_vec(&G_hat[0],&G_hat[0]+M);
            
            // save off vector to binary file
            outfile.write((const char*)&G_vec.front(),G_vec.size()*sizeof(std::complex<float>));
            outfile2.write((const char*)&G_hat_vec.front(),G_hat_vec.size()*sizeof(std::complex<float>));
            
        }
        else
        {
            printf("PAYLOAD INVALID\n");
        }
    }
    else
    {
        printf("HEADER INVALID\n");
    }
}

int main()
{
    // input data file info
    char file_name[] = "channel_sim_output.bin";
    unsigned int N = 25000;
    
    // build mcrx object for OFDM modulation
    // (assumes ofdm setup used in full-radio)
    framesync_callback callback[1];
    callback[0] = rxCallback;
    void* userdata[1];
    userdata[0] = NULL;
    multichannelrx* mcrx = new multichannelrx(1,480,6,4,(unsigned char*)NULL,userdata,callback);

    // pull out channel sim data into a vector
    std::ifstream infile(file_name,std::ifstream::binary);
    std::vector<std::complex<float> > rx_data_vec(N);
    infile.read((char*)&rx_data_vec.front(),rx_data_vec.size()*sizeof(std::complex<float>));
    infile.close();

    outfile.open("./channel_G.bin",std::ofstream::binary);
    outfile2.open("./channel_G_hat.bin",std::ofstream::binary);

    for(std::vector<std::complex<float> >::iterator it=rx_data_vec.begin();it!=rx_data_vec.end();it++)
    {
        std::complex<float> sample = *it;
        mcrx->Execute(&sample,1);
    }
    
    outfile.close();
    printf("Channel data is in ./channel.bin\n");
}
