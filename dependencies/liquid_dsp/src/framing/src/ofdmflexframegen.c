/*
 * Copyright (c) 2007 - 2014 Joseph Gaeddert
 *
 * This file is part of liquid.
 *
 * liquid is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * liquid is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with liquid.  If not, see <http://www.gnu.org/licenses/>.
 */

//
// ofdmflexframegen.c
//
// OFDM flexible frame generator
//

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "liquid.internal.h"
#include <sys/time.h>

#define DEBUG_OFDMFLEXFRAMEGEN            0
#define UNALLOCATED            100
#define RESERVED           101

suseconds_t time_now()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000 + tv.tv_usec;
}

// default ofdmflexframegen properties
static ofdmflexframegenprops_s ofdmflexframegenprops_default = {
    LIQUID_CRC_32,      // check
    LIQUID_FEC_NONE,    // fec0
    LIQUID_FEC_NONE,    // fec1
    LIQUID_MODEM_QPSK,  // mod_scheme
    //64                // block_size
};

void ofdmflexframegenprops_init_default(ofdmflexframegenprops_s * _props)
{
    memmove(_props, &ofdmflexframegenprops_default, sizeof(ofdmflexframegenprops_s));
}

struct ofdmflexframegen_s {
    unsigned int M;         // number of subcarriers
    unsigned int cp_len;    // cyclic prefix length
    unsigned int taper_len; // taper length
    unsigned char * p;      // subcarrier allocation (null, pilot, data)

    // constants
    unsigned int M_null;    // number of null subcarriers
    unsigned int M_pilot;   // number of pilot subcarriers
    unsigned int M_data;    // number of data subcarriers
    unsigned int M_S0;      // number of enabled subcarriers in S0
    unsigned int M_S1;      // number of enabled subcarriers in S1

    // buffers
    float complex * X;      // frequency-domain buffer

    // internal low-level objects
    ofdmframegen fg;        // frame generator object

    // options/derived lengths
    unsigned int num_symbols_header;    // number of header OFDM symbols
    unsigned int num_symbols_payload;   // number of payload OFDM symbols

    // header
    modem mod_header;                   // header modulator
    packetizer p_header;                // header packetizer
    unsigned char * header;        //header data (uncoded)
    unsigned char * header_enc;        //header data (encoded)
    unsigned char * header_mod;        //header symbols

//    unsigned char header[OFDMFLEXFRAME_H_DEC];      // header data (uncoded)
//    unsigned char header_enc[OFDMFLEXFRAME_H_ENC];  // header data (encoded)
//    unsigned char header_mod[OFDMFLEXFRAME_H_SYM];  // header symbols

    // payload
    packetizer p_payload;               // payload packetizer
    unsigned int payload_dec_len;       // payload length (num un-encoded bytes)
    modem mod_payload;                  // payload modulator
    unsigned char * payload_enc;        // payload data (encoded bytes)
    unsigned char * payload_mod;        // payload data (modulated symbols)
    unsigned int payload_enc_len;       // length of encoded payload
    unsigned int payload_mod_len;       // number of modulated symbols in payload

    //separate user payloads
    packetizer * user_packetizers;
    unsigned int * user_payload_dec_lens;
    modem * user_payload_modems;
    unsigned char ** user_payloads;
    unsigned char ** user_payload_encs;
    unsigned char ** user_payload_mods;
    unsigned int * user_payload_enc_lens;
    unsigned int * user_payload_mod_lens;
    unsigned int * user_payload_symbol_indices;
    
    //stuff related to multi-user
    int ofdma; //boolean, used to indicate single or multi-user mode
    int dummy_data;
    unsigned int num_users; //user capacity (ie max number of users, though all might not be in use at all times)
    unsigned char * subcarrier_map; //map that stores subcarrier allocations, either user ids, unallocated, or reserved
                                    //for nulls and pilots
    unsigned int * num_subcarriers;//number of subcarries allocated to each user
    unsigned int * frames_sent_since_last_use;//the number of frames generated since a subcarrier was last used
    unsigned int reallocation_delay; //the number of frames to wait before reallocating deallocated subcarriers

    //These 3 variables are used to determine the minimum number of ofdm symbols needed to 
    //transmit all user payloads successfully
    unsigned int largest_payload;//size of the largest payload
    unsigned int index_of_user_with_largest_payload;
    unsigned int index_of_user_with_least_subcarriers;

    //When using multiple users, the size of the header is variable
    //these variable replace the #defined constants that are used
    //in normal, single user, operation
    unsigned int ofdmflexframe_h_user_dynamic;
    unsigned int ofdmflexframe_h_dec_dynamic;
    unsigned int ofdmflexframe_h_enc_dynamic;
    unsigned int ofdmflexframe_h_sym_dynamic;


    
    // counters/states
    unsigned int symbol_number;         // output symbol number
    enum {
        OFDMFLEXFRAMEGEN_STATE_S0a=0,   // write S0 symbol (first)
        OFDMFLEXFRAMEGEN_STATE_S0b,     // write S0 symbol (second)
        OFDMFLEXFRAMEGEN_STATE_S1,      // write S1 symbol
        OFDMFLEXFRAMEGEN_STATE_HEADER,  // write header symbols
        OFDMFLEXFRAMEGEN_STATE_PAYLOAD  // write payload symbols
    } state;
    int frame_assembled;                // frame assembled flag
    int frame_complete;                 // frame completed flag
    unsigned int header_symbol_index;   //
    unsigned int payload_symbol_index;  //

    // properties
    ofdmflexframegenprops_s props;
};

void ofdmflexframegen_set_dummy_data(ofdmflexframegen _q, unsigned int dummy_data)
{

    _q->dummy_data = dummy_data;
}

void ofdmflexframegen_set_reallocation_delay(ofdmflexframegen _q, unsigned int delay)
{
    if(delay <= 0)
    {
        printf("Error: reallocation delay must be greater than zero.\n");
    }
    else
    {
        _q->reallocation_delay = delay;
    }
}

void ofdmflexframegen_deallocate_subcarrier(ofdmflexframegen _q, unsigned int subcarrier)
{
    if(_q->ofdma)
    {
        unsigned int user = _q->subcarrier_map[subcarrier];
        if(user != UNALLOCATED && _q->num_subcarriers[user] > 2)
        {
            _q->subcarrier_map[subcarrier] = UNALLOCATED;

            _q->num_subcarriers[user]--;
            if(_q->num_subcarriers[user] < _q->num_subcarriers[_q->index_of_user_with_least_subcarriers])
            {
                _q->index_of_user_with_least_subcarriers = user;
            }
            ofdmflexframegen_reconfigure_multi_user(_q, user);
        }
    }
    else
    {
        printf("Error: trying to deallocate subcarrier for non-ofdma frame generator.\n");
    }

}

void ofdmflexframegen_reallocate_subcarrier(ofdmflexframegen _q, unsigned int subcarrier)
{
    if(_q->ofdma)
    {
        if(_q->subcarrier_map[subcarrier] == RESERVED)
        {
            printf("Error: trying to reallocate a NULL or PILOT subcarrier.\n");
        }
        else if(_q->subcarrier_map[subcarrier] != UNALLOCATED)
        {
            printf("Error: trying to reallocate a subcarrier already in use.");
        }
        else
        {
            unsigned int user = _q->index_of_user_with_least_subcarriers;
            _q->subcarrier_map[subcarrier] = user;
            _q->frames_sent_since_last_use[subcarrier] = 0;
            _q->num_subcarriers[user]++;


            unsigned int i;
            for(i = 0; i < _q->num_users; i++)
            {
                if(_q->num_subcarriers[i] <
                        _q->num_subcarriers[_q->index_of_user_with_least_subcarriers])
                {
                    _q->index_of_user_with_least_subcarriers = i;
                }
            }
            ofdmflexframegen_reconfigure_multi_user(_q,
                    _q->index_of_user_with_least_subcarriers);
        }
    }
    else
    {
        printf("Error: trying to reallocate a subcarrer for non-ofdma frame generator.\n");
    }
}

void ofdmflexframegen_update_subcarrier_allocation(ofdmflexframegen _q, unsigned char* new_allocation)
{
    memmove(_q->p, new_allocation, _q->M*sizeof(unsigned char));
}

unsigned char* ofdmflexframegen_get_subcarrier_map(ofdmflexframegen _q)
{
    return _q->subcarrier_map;
}
unsigned char* ofdmflexframegen_get_subcarrier_allocation(ofdmflexframegen _q)
{
    return _q->p;
}
// create OFDM flexible framing generator object
//  _M          :   number of subcarriers, >10 typical
//  _cp_len     :   cyclic prefix length
//  _taper_len  :   taper length (OFDM symbol overlap)
//  _p          :   subcarrier allocation (null, pilot, data), [size: _M x 1]
//  _fgprops    :   frame properties (modulation scheme, etc.)
ofdmflexframegen ofdmflexframegen_create(unsigned int              _M,
                                         unsigned int              _cp_len,
                                         unsigned int              _taper_len,
                                         unsigned char *           _p,
                                         ofdmflexframegenprops_s * _fgprops)
{
    // validate input
    if (_M < 2) {
        fprintf(stderr,"error: ofdmflexframegen_create(), number of subcarriers must be at least 2\n");
        exit(1);
    } else if (_M % 2) {
        fprintf(stderr,"error: ofdmflexframegen_create(), number of subcarriers must be even\n");
        exit(1);
    }

    ofdmflexframegen q = (ofdmflexframegen) malloc(sizeof(struct ofdmflexframegen_s));
    q->M         = _M;          // number of subcarriers
    q->cp_len    = _cp_len;     // cyclic prefix length
    q->taper_len = _taper_len;  // taper length

    // allocate memory for transform buffers
    q->X = (float complex*) malloc((q->M)*sizeof(float complex));

    // allocate memory for subcarrier allocation IDs
    q->p = (unsigned char*) malloc((q->M)*sizeof(unsigned char));
    // allocate memory for header, encoded header, and modulated header
    q->header = (unsigned char*) malloc(OFDMFLEXFRAME_H_DEC*sizeof(unsigned char));
    q->header_enc = (unsigned char*) malloc(OFDMFLEXFRAME_H_ENC*sizeof(unsigned char));
    q->header_mod = (unsigned char*) malloc(OFDMFLEXFRAME_H_SYM*sizeof(unsigned char));

    if (_p == NULL) {
        // initialize default subcarrier allocation
        ofdmframe_init_default_sctype(q->M, q->p);
    } else {
        // copy user-defined subcarrier allocation
        memmove(q->p, _p, q->M*sizeof(unsigned char));
    }

    // validate and count subcarrier allocation
    ofdmframe_validate_sctype(q->p, q->M, &q->M_null, &q->M_pilot, &q->M_data);

    // create internal OFDM frame generator object
    q->fg = ofdmframegen_create(q->M, q->cp_len, q->taper_len, q->p);

    // create header objects
    q->mod_header = modem_create(OFDMFLEXFRAME_H_MOD);
    q->p_header   = packetizer_create(OFDMFLEXFRAME_H_DEC,
                                      OFDMFLEXFRAME_H_CRC,
                                      OFDMFLEXFRAME_H_FEC,
                                      LIQUID_FEC_NONE);
    assert(packetizer_get_enc_msg_len(q->p_header)==OFDMFLEXFRAME_H_ENC);

    // compute number of header symbols
    div_t d = div(OFDMFLEXFRAME_H_SYM, q->M_data);
    q->num_symbols_header = d.quot + (d.rem ? 1 : 0);

    // initial memory allocation for payload
    q->payload_dec_len = 1;
    q->p_payload = packetizer_create(q->payload_dec_len,
                                     LIQUID_CRC_NONE,
                                     LIQUID_FEC_NONE,
                                     LIQUID_FEC_NONE);
    q->payload_enc_len = packetizer_get_enc_msg_len(q->p_payload);
    q->payload_enc = (unsigned char*) malloc(q->payload_enc_len*sizeof(unsigned char));

    q->payload_mod_len = 1;
    q->payload_mod = (unsigned char*) malloc(q->payload_mod_len*sizeof(unsigned char));

    // create payload modem (initially QPSK, overridden by properties)
    q->mod_payload = modem_create(LIQUID_MODEM_QPSK);

    q->ofdma = 0;
    q->dummy_data = 0;
    q->reallocation_delay = 50;

    // initialize properties
    ofdmflexframegen_setprops(q, _fgprops);

    // reset
    ofdmflexframegen_reset(q);

    // return pointer to main object
    return q;
}

// create OFDM flexible framing generator object
//  _M          :   number of subcarriers, >10 typical
//  _cp_len     :   cyclic prefix length
//  _taper_len  :   taper length (OFDM symbol overlap)
//  _p          :   subcarrier allocation (null, pilot, data), [size: _M x 1]
//  _fgprops    :   frame properties (modulation scheme, etc.)
ofdmflexframegen ofdmflexframegen_create_multi_user(unsigned int              _M,
        unsigned int              _cp_len,
        unsigned int              _taper_len,
        unsigned char *           _p,
        ofdmflexframegenprops_s * _fgprops,
        unsigned int          _num_users)
{
    // validate input
    if (_M < 2) {
        fprintf(stderr,"error: ofdmflexframegen_create(), number of subcarriers must be at least 2\n");
        exit(1);
    } else if (_M % 2) {
        fprintf(stderr,"error: ofdmflexframegen_create(), number of subcarriers must be even\n");
        exit(1);
    }

    ofdmflexframegen q = (ofdmflexframegen) malloc(sizeof(struct ofdmflexframegen_s));
    q->M         = _M;          // number of subcarriers
    q->cp_len    = _cp_len;     // cyclic prefix length
    q->taper_len = _taper_len;  // taper length

    // allocate memory for transform buffers
    q->X = (float complex*) malloc((q->M)*sizeof(float complex));

    // allocate memory for subcarrier allocation IDs
    q->p = (unsigned char*) malloc((q->M)*sizeof(unsigned char));


    if (_p == NULL) {
        // initialize default subcarrier allocation
        ofdmframe_init_default_sctype(q->M, q->p);
    } else {
        // copy user-defined subcarrier allocation
        memmove(q->p, _p, q->M*sizeof(unsigned char));
    }

    // validate and count subcarrier allocation
    ofdmframe_validate_sctype(q->p, q->M, &q->M_null, &q->M_pilot, &q->M_data);

    // create internal OFDM frame generator object
    q->fg = ofdmframegen_create(q->M, q->cp_len, q->taper_len, q->p);

    // create header objects
    q->mod_header = modem_create(OFDMFLEXFRAME_H_MOD);
    //**    q->p_header   = packetizer_create(OFDMFLEXFRAME_H_DEC,
    //                                      OFDMFLEXFRAME_H_CRC,
    //                                      OFDMFLEXFRAME_H_FEC,
    //                                      LIQUID_FEC_NONE);

    //8 bytes for user-supplied header, +q->M for the subcarrier map + 2*_num_users for
    //user-specfic payload_lens
    q->ofdmflexframe_h_user_dynamic = 8 + q->M + (2*_num_users);
    q->ofdmflexframe_h_dec_dynamic = q->ofdmflexframe_h_user_dynamic + 6;

    q->p_header = packetizer_create(q->ofdmflexframe_h_dec_dynamic,
            OFDMFLEXFRAME_H_CRC,
            OFDMFLEXFRAME_H_FEC,
            LIQUID_FEC_NONE);

    //q->ofdmflexframe_h_enc_dynamic = 2 * q->ofdmflexframe_h_dec_dynamic + 9;
    q->ofdmflexframe_h_enc_dynamic = packetizer_get_enc_msg_len(q->p_header);
    assert(packetizer_get_enc_msg_len(q->p_header)==q->ofdmflexframe_h_enc_dynamic);
    q->ofdmflexframe_h_sym_dynamic = 8 * q->ofdmflexframe_h_enc_dynamic;
    q->header = (unsigned char*) malloc(q->ofdmflexframe_h_dec_dynamic*sizeof(unsigned char));
    q->header_enc = (unsigned char*) malloc(q->ofdmflexframe_h_enc_dynamic*sizeof(unsigned char));
    q->header_mod = (unsigned char*) malloc(q->ofdmflexframe_h_sym_dynamic*sizeof(unsigned char));

    assert(packetizer_get_enc_msg_len(q->p_header) == q->ofdmflexframe_h_enc_dynamic);

    // compute number of header symbols
    div_t d = div(q->ofdmflexframe_h_sym_dynamic, q->M_data);
    //**div_t d = div(OFDMFLEXFRAME_H_SYM, q->M_data);
    q->num_symbols_header = d.quot + (d.rem ? 1 : 0);
    // initial memory allocation for payload
    q->payload_dec_len = 1;
    q->p_payload = packetizer_create(q->payload_dec_len,
            LIQUID_CRC_NONE,
            LIQUID_FEC_NONE,
            LIQUID_FEC_NONE);
    q->payload_enc_len = packetizer_get_enc_msg_len(q->p_payload);
    q->payload_enc = (unsigned char*) malloc(q->payload_enc_len*sizeof(unsigned char));

    q->payload_mod_len = 1;
    q->payload_mod = (unsigned char*) malloc(q->payload_mod_len*sizeof(unsigned char));

    // create payload modem (initially QPSK, overridden by properties)
    q->mod_payload = modem_create(LIQUID_MODEM_QPSK);

    q->ofdma = 1;
    q->dummy_data = 0;
    q->reallocation_delay = 50;
    q->num_users = _num_users;
    unsigned int current_user = 0;

    q->subcarrier_map = (unsigned char*) malloc((q->M)*sizeof(unsigned char));
    q->num_subcarriers = (unsigned int*) malloc((q->num_users)*sizeof(unsigned int));
    q->frames_sent_since_last_use = (unsigned int*) malloc((q->M)*sizeof(unsigned int));

    unsigned int i;
    unsigned int sctype;
    for(i = 0; i < q->num_users; i++)
        q->num_subcarriers[i] = 0;
    for(i = 0; i < q->M; i++)
    {
        sctype = q->p[i];
        if(sctype == OFDMFRAME_SCTYPE_DATA)
        {
            q->subcarrier_map[i] = current_user;
            q->num_subcarriers[current_user]++;
            current_user = (current_user + 1) % q->num_users;
        }
        else
            q->subcarrier_map[i] = RESERVED;

        q->frames_sent_since_last_use[i] = 0;
    }
    unsigned int least = q->M;
    for(i = 0; i < q->num_users; i++)
    {
        if(q->num_subcarriers[i] < least)
        {
            least = q->num_subcarriers[i];
            q->index_of_user_with_least_subcarriers = i;
        }
    }

    q->index_of_user_with_largest_payload = 0;
    q->largest_payload = 0;

    q->user_payload_dec_lens = (unsigned int*) malloc(q->num_users*sizeof(unsigned
                int));
    q->user_packetizers = (packetizer*) malloc(q->num_users*sizeof(packetizer));
    q->user_payload_enc_lens = (unsigned int*) malloc(q->num_users*sizeof(unsigned
                int));
    q->user_payload_encs = (unsigned char**) malloc(q->num_users*sizeof(unsigned
                char*));
    q->user_payload_mod_lens = (unsigned int*) malloc(q->num_users*sizeof(unsigned
                int));
    q->user_payload_mods = (unsigned char**) malloc(q->num_users*sizeof(unsigned
                char*));
    q->user_payload_modems = (modem*) malloc(q->num_users*sizeof(modem));
    q->user_payload_symbol_indices = (unsigned int*)
        malloc(q->num_users*sizeof(unsigned int));
    q->user_payloads = (unsigned char**) malloc(q->num_users*sizeof(unsigned
                char*));

    for(i = 0; i < q->num_users; i++)
    {
        q->user_payload_dec_lens[i] = 0;
        q->user_packetizers[i] = packetizer_create(q->user_payload_dec_lens[i],
                LIQUID_CRC_NONE,
                LIQUID_CRC_NONE,
                LIQUID_FEC_NONE);
        q->user_payload_enc_lens[i] =
            packetizer_get_enc_msg_len(q->user_packetizers[i]);
        q->user_payload_encs[i] = (unsigned char*)
            malloc(q->user_payload_enc_lens[i]*sizeof(unsigned char));
        q->user_payload_mod_lens[i] = 0;
        q->user_payload_mods[i] = (unsigned char*)
            malloc(q->user_payload_mod_lens[i]*sizeof(unsigned char));
        q->user_payloads[i] = (unsigned char*)
            malloc(q->user_payload_dec_lens[i]*sizeof(unsigned char));
        q->user_payload_modems[i] = modem_create(LIQUID_MODEM_QPSK);
    }


    // initialize properties
    ofdmflexframegen_setprops(q, _fgprops);

    // reset
    ofdmflexframegen_reset_multi_user(q);


    // return pointer to main object
    return q;
}

void ofdmflexframegen_destroy(ofdmflexframegen _q)
{
    // destroy internal objects
    ofdmframegen_destroy(_q->fg);       // OFDM frame generator
    packetizer_destroy(_q->p_header);   // header packetizer
    modem_destroy(_q->mod_header);      // header modulator
    packetizer_destroy(_q->p_payload);  // payload packetizer
    modem_destroy(_q->mod_payload);     // payload modulator

    // free buffers/arrays
    free(_q->header);          //header
    free(_q->header_enc);      //encoded header bytes
    free(_q->header_mod);      //modulated header symbols

    free(_q->payload_enc);              // encoded payload bytes
    free(_q->payload_mod);              // modulated payload symbols
    free(_q->X);                        // frequency-domain buffer
    free(_q->p);                        // subcarrier allocation

    // free main object memory
    free(_q);
}

void ofdmflexframegen_destroy_multi_user(ofdmflexframegen _q)
{
    // destroy internal objects
    ofdmframegen_destroy(_q->fg);       // OFDM frame generator
    packetizer_destroy(_q->p_header);   // header packetizer
    modem_destroy(_q->mod_header);      // header modulator
    packetizer_destroy(_q->p_payload);  // payload packetizer
    modem_destroy(_q->mod_payload);     // payload modulator

    // free buffers/arrays
    free(_q->header);          //header
    free(_q->header_enc);      //encoded header bytes
    free(_q->header_mod);      //modulated header symbols
    free(_q->payload_enc);              // encoded payload bytes
    free(_q->payload_mod);              // modulated payload symbols
    free(_q->X);                        // frequency-domain buffer
    free(_q->p);                        // subcarrier allocation

    unsigned int i;
    for(i = 0; i < _q->num_users; i++)
    {
        free(_q->user_payload_encs[i]);
        free(_q->user_payload_mods[i]);
        packetizer_destroy(_q->user_packetizers[i]);
        modem_destroy(_q->user_payload_modems[i]);
    }

    free(_q->user_payload_dec_lens);
    free(_q->user_packetizers);
    free(_q->user_payload_enc_lens);
    free(_q->user_payload_encs);
    free(_q->user_payload_mod_lens);
    free(_q->user_payload_mods);
    free(_q->user_payload_modems);
    free(_q->subcarrier_map);
    free(_q->num_subcarriers);
    free(_q->frames_sent_since_last_use);
    // free main object memory
    free(_q);
}


void ofdmflexframegen_reset(ofdmflexframegen _q)
{
    // reset symbol counter and state
    _q->symbol_number = 0;
    _q->state = OFDMFLEXFRAMEGEN_STATE_S0a;
    _q->frame_assembled = 0;
    _q->frame_complete = 0;
    _q->header_symbol_index = 0;
    _q->payload_symbol_index = 0;

    // reset internal OFDM frame generator object
    // NOTE: this is important for appropriately setting the pilot phases
    ofdmframegen_reset(_q->fg);
}

void ofdmflexframegen_reset_multi_user(ofdmflexframegen _q)
{
    // reset symbol counter and state
    _q->symbol_number = 0;
    _q->state = OFDMFLEXFRAMEGEN_STATE_S0a;
    _q->frame_assembled = 0;
    _q->frame_complete = 0;
    _q->header_symbol_index = 0;
    _q->payload_symbol_index = 0;

    unsigned int i;
    for(i = 0; i < _q->num_users; i++)
        _q->user_payload_symbol_indices[i] = 0;

    // reset internal OFDM frame generator object
    // NOTE: this is important for appropriately setting the pilot phases
    ofdmframegen_reset(_q->fg);
}


// is frame assembled?
int ofdmflexframegen_is_assembled(ofdmflexframegen _q)
{
    return _q->frame_assembled;
}

void ofdmflexframegen_print(ofdmflexframegen _q)
{
    printf("ofdmflexframegen:\n");
    printf("    num subcarriers     :   %-u\n", _q->M);
    printf("      * NULL            :   %-u\n", _q->M_null);
    printf("      * pilot           :   %-u\n", _q->M_pilot);
    printf("      * data            :   %-u\n", _q->M_data);
    printf("    cyclic prefix len   :   %-u\n", _q->cp_len);
    printf("    taper len           :   %-u\n", _q->taper_len);
    printf("    properties:\n");
    printf("      * mod scheme      :   %s\n", modulation_types[_q->props.mod_scheme].fullname);
    printf("      * fec (inner)     :   %s\n", fec_scheme_str[_q->props.fec0][1]);
    printf("      * fec (outer)     :   %s\n", fec_scheme_str[_q->props.fec1][1]);
    printf("      * CRC scheme      :   %s\n", crc_scheme_str[_q->props.check][1]);
    printf("    frame assembled     :   %s\n", _q->frame_assembled ? "yes" : "no");
    if (_q->frame_assembled) {
        printf("    payload:\n");
        printf("      * decoded bytes   :   %-u\n", _q->payload_dec_len);
        printf("      * encoded bytes   :   %-u\n", _q->payload_enc_len);
        printf("      * modulated syms  :   %-u\n", _q->payload_mod_len);
        printf("    total OFDM symbols  :   %-u\n", ofdmflexframegen_getframelen(_q));
        printf("      * S0 symbols      :   %-u @ %u\n", 2, _q->M+_q->cp_len);
        printf("      * S1 symbols      :   %-u @ %u\n", 1, _q->M+_q->cp_len);
        printf("      * header symbols  :   %-u @ %u\n", _q->num_symbols_header,  _q->M+_q->cp_len);
        printf("      * payload symbols :   %-u @ %u\n", _q->num_symbols_payload, _q->M+_q->cp_len);

        // compute asymptotic spectral efficiency
        unsigned int num_bits = 8*_q->payload_dec_len;
        unsigned int num_samples = (_q->M+_q->cp_len)*(3 + _q->num_symbols_header + _q->num_symbols_payload);
        printf("    spectral efficiency :   %-6.4f b/s/Hz\n", (float)num_bits / (float)num_samples);
    }
}

// get ofdmflexframegen properties
//  _q      :   frame generator object
//  _props  :   frame generator properties structure pointer
void ofdmflexframegen_getprops(ofdmflexframegen _q,
                               ofdmflexframegenprops_s * _props)
{
    // copy properties structure to output pointer
    memmove(_props, &_q->props, sizeof(ofdmflexframegenprops_s));
}

void ofdmflexframegen_setprops(ofdmflexframegen _q,
                               ofdmflexframegenprops_s * _props)
{
    // if properties object is NULL, initialize with defaults
    if (_props == NULL) {
        ofdmflexframegen_setprops(_q, &ofdmflexframegenprops_default);
        return;
    }

    // validate input
    if (_props->check == LIQUID_CRC_UNKNOWN || _props->check >= LIQUID_CRC_NUM_SCHEMES) {
        fprintf(stderr, "error: ofdmflexframegen_setprops(), invalid/unsupported CRC scheme\n");
        exit(1);
    } else if (_props->fec0 == LIQUID_FEC_UNKNOWN || _props->fec1 == LIQUID_FEC_UNKNOWN) {
        fprintf(stderr, "error: ofdmflexframegen_setprops(), invalid/unsupported FEC scheme\n");
        exit(1);
    } else if (_props->mod_scheme == LIQUID_MODEM_UNKNOWN ) {
        fprintf(stderr, "error: ofdmflexframegen_setprops(), invalid/unsupported modulation scheme\n");
        exit(1);
    }

    // TODO : determine if re-configuration is necessary

    // copy properties to internal structure
    memmove(&_q->props, _props, sizeof(ofdmflexframegenprops_s));

    // reconfigure internal buffers, objects, etc.
    if(_q->ofdma)
    {
        unsigned int i;
        for(i = 0; i < _q->num_users; i++)
        {
            ofdmflexframegen_reconfigure_multi_user(_q, i);
        }
    }
    else
        ofdmflexframegen_reconfigure(_q);

}

// get length of frame (symbols)
//  _q              :   OFDM frame generator object
unsigned int ofdmflexframegen_getframelen(ofdmflexframegen _q)
{
    // number of S0 symbols (2)
    // number of S1 symbols (1)
    // number of header symbols
    // number of payload symbols

    return  2 + // S0 symbols
            1 + // S1 symbol
            _q->num_symbols_header +
            _q->num_symbols_payload;
}

// assemble a frame from an array of data
//  _q              :   OFDM frame generator object
//  _header         :   frame header
//  _payload        :   payload data [size: _payload_len x 1]
//  _payload_len    :   payload data length
void ofdmflexframegen_assemble(ofdmflexframegen _q,
                               unsigned char *  _header,
                               unsigned char *  _payload,
                               unsigned int     _payload_len)
{
    // check payload length and reconfigure if necessary
    if (_payload_len != _q->payload_dec_len) {
        _q->payload_dec_len = _payload_len;
        ofdmflexframegen_reconfigure(_q);
    }

    // set assembled flag
    _q->frame_assembled = 1;

    // copy user-defined header data
    memmove(_q->header, _header, OFDMFLEXFRAME_H_USER*sizeof(unsigned char));

    // encode full header
    ofdmflexframegen_encode_header(_q);

    // modulate header
    ofdmflexframegen_modulate_header(_q);

    // encode payload
    packetizer_encode(_q->p_payload, _payload, _q->payload_enc);

    // 
    // pack modem symbols
    //

    // clear payload
    memset(_q->payload_mod, 0x00, _q->payload_mod_len);

    // repack 8-bit payload bytes into 'bps'-bit payload symbols
    unsigned int bps = modulation_types[_q->props.mod_scheme].bps;
    unsigned int num_written;
    liquid_repack_bytes(_q->payload_enc,  8,  _q->payload_enc_len,
                        _q->payload_mod, bps, _q->payload_mod_len,
                        &num_written);
#if DEBUG_OFDMFLEXFRAMEGEN
    printf("wrote %u symbols (expected %u)\n", num_written, _q->payload_mod_len);
#endif
}

void ofdmflexframegen_multi_user_update_data(ofdmflexframegen _q,
        unsigned char * _payload,
        unsigned int _payload_len,
        unsigned int _user)
{
    if(_payload_len > _q->largest_payload)
    {
        _q->largest_payload = _payload_len;
        _q->index_of_user_with_largest_payload = _user;
    }

    if (_payload_len != _q->user_payload_dec_lens[_user]) {
        _q->user_payload_dec_lens[_user] = _payload_len;
        ofdmflexframegen_reconfigure_multi_user(_q, _user);
    }
    memmove(_q->user_payloads[_user], _payload, _payload_len);
}

// assemble a frame from internally stored multi-user payload data
//  _q              :   OFDM frame generator object
//  _header         :   frame header
void ofdmflexframegen_assemble_multi_user(ofdmflexframegen _q,
        unsigned char *  _header)
{

    // set assembled flag
    _q->frame_assembled = 1;

    //header structure in ofdma mode:
    //|8 bytes of user configurable data||_q->M bytes for subcarrier map|...
    //...|2*_q->num_users bytes for user payload lens||6 bytes for framing info(ofdmflexframe_encode_header() writes this)|
    //first we copy in the user header data, which should always be 8
    unsigned int n = OFDMFLEXFRAME_H_USER;
    memmove(_q->header, _header, n*sizeof(unsigned char));

    // then we copy in the subcarrier map, size _q->M
    memmove(_q->header + n, _q->subcarrier_map, _q->M*sizeof(unsigned char)); 

    //then copy user-specific payload_lens into header
    unsigned int i;
    unsigned int current_user = 0;
    for(i = n + _q->M; current_user < _q->num_users; i+=2)
    {
        _q->header[i] = (_q->user_payload_dec_lens[current_user] >> 8) & 0xff;
        _q->header[i + 1] = (_q->user_payload_dec_lens[current_user] ) & 0xff;
        current_user++;
    }

    // encode full header
    ofdmflexframegen_encode_header(_q);


    // modulate header
    ofdmflexframegen_modulate_header(_q);

    unsigned int bps = modulation_types[_q->props.mod_scheme].bps;
    unsigned int num_written[_q->num_users];
    unsigned int total_num_written = 0;
    unsigned int total_expected = 0;

    // encode user payloads
    for(i = 0; i < _q->num_users; i++)
    {
        packetizer_encode(_q->user_packetizers[i], _q->user_payloads[i], _q->user_payload_encs[i]);

        // 
        // pack modem symbols
        //

        // clear payload
        memset(_q->user_payload_mods[i], 0x00, _q->user_payload_mod_lens[i]);

        // repack 8-bit payload bytes into 'bps'-bit payload symbols
        liquid_repack_bytes(_q->user_payload_encs[i],  8,  _q->user_payload_enc_lens[i],
                _q->user_payload_mods[i], bps, _q->user_payload_mod_lens[i],
                &num_written[i]);
        total_num_written += num_written[i];
        total_expected += _q->user_payload_mod_lens[i];
    }
#if DEBUG_OFDMFLEXFRAMEGEN
    printf("wrote %u symbols (expected %u)\n", total_num_written, total_expected);
#endif
}

// write symbols of assembled frame
//  _q              :   OFDM frame generator object
//  _buffer         :   output buffer [size: N+cp_len x 1]
int ofdmflexframegen_writesymbol(ofdmflexframegen       _q,
                                 liquid_float_complex * _buffer)
{
    // check if frame is actually assembled
    if ( !_q->frame_assembled ) {
        fprintf(stderr,"warning: ofdmflexframegen_writesymbol(), frame not assembled\n");
        return 1;
    }

    // increment symbol counter
    _q->symbol_number++;
    //printf("writesymbol(): %u\n", _q->symbol_number);

    switch (_q->state) {
    case OFDMFLEXFRAMEGEN_STATE_S0a:
        // write S0 symbol (first)
        ofdmflexframegen_write_S0a(_q, _buffer);
        break;

    case OFDMFLEXFRAMEGEN_STATE_S0b:
        // write S0 symbol (second)
        ofdmflexframegen_write_S0b(_q, _buffer);
        break;

    case OFDMFLEXFRAMEGEN_STATE_S1:
        // write S1 symbols
        ofdmflexframegen_write_S1(_q, _buffer);
        break;

    case OFDMFLEXFRAMEGEN_STATE_HEADER:
        // write header symbols
        ofdmflexframegen_write_header(_q, _buffer);
        break;

    case OFDMFLEXFRAMEGEN_STATE_PAYLOAD:
        // write payload symbols
        ofdmflexframegen_write_payload(_q, _buffer);
        break;

    default:
        fprintf(stderr,"error: ofdmflexframegen_writesymbol(), unknown/unsupported internal state\n");
        exit(1);
    }

    if (_q->frame_complete) {
        // reset framing object
#if DEBUG_OFDMFLEXFRAMEGEN
        printf(" ...resetting...\n");
#endif
        if(_q->ofdma)
            ofdmflexframegen_reset_multi_user(_q);
        else
            ofdmflexframegen_reset(_q);

        return 1;
    }

    return 0;
}


//
// internal
//

// reconfigure internal buffers, objects, etc.
void ofdmflexframegen_reconfigure(ofdmflexframegen _q)
{
    // re-create payload packetizer
    _q->p_payload = packetizer_recreate(_q->p_payload,
                                        _q->payload_dec_len,
                                        _q->props.check,
                                        _q->props.fec0,
                                        _q->props.fec1);

    // re-allocate memory for encoded message
    _q->payload_enc_len = packetizer_get_enc_msg_len(_q->p_payload);
    _q->payload_enc = (unsigned char*) realloc(_q->payload_enc,
                                               _q->payload_enc_len*sizeof(unsigned char));
#if DEBUG_OFDMFLEXFRAMEGEN
    //printf(">>>> payload : %u (%u encoded)\n", _q->props.payload_len, _q->payload_enc_len);
#endif

    // re-create modem
    // TODO : only do this if necessary
    _q->mod_payload = modem_recreate(_q->mod_payload, _q->props.mod_scheme);

    // re-allocate memory for payload modem symbols
    unsigned int bps = modulation_types[_q->props.mod_scheme].bps;
    div_t d = div(8*_q->payload_enc_len, bps);
    _q->payload_mod_len = d.quot + (d.rem ? 1 : 0);
    _q->payload_mod = (unsigned char*)realloc(_q->payload_mod,
                                              _q->payload_mod_len*sizeof(unsigned char));

    // re-compute number of payload OFDM symbols
    d = div(_q->payload_mod_len, _q->M_data);
    _q->num_symbols_payload = d.quot + (d.rem ? 1 : 0);
}

void ofdmflexframegen_reconfigure_multi_user(ofdmflexframegen _q, unsigned int user)
{
    div_t d;
    unsigned int all_payload_mod_len = 0;
    unsigned int i;
    // re-create payload packetizer
    _q->user_packetizers[user] = packetizer_recreate(_q->user_packetizers[user],
            _q->user_payload_dec_lens[user],
            _q->props.check,
            _q->props.fec0,
            _q->props.fec1);

    //re-allocate memory for decoded message
    _q->user_payloads[user] = (unsigned char*) realloc(_q->user_payloads[user], 
            _q->user_payload_dec_lens[user]*sizeof(unsigned char));


    // re-allocate memory for encoded message
    _q->user_payload_enc_lens[user] = packetizer_get_enc_msg_len(_q->user_packetizers[user]);
    _q->user_payload_encs[user] = (unsigned char*) realloc(_q->user_payload_encs[user], 
            _q->user_payload_enc_lens[user]*sizeof(unsigned char));


    // re-create modem
    _q->user_payload_modems[user] = modem_recreate(_q->user_payload_modems[user], _q->props.mod_scheme);

    for(i = 0; i < _q->num_users; i++)
    {
        // re-allocate memory for payload modem symbols
        unsigned int bps = modulation_types[_q->props.mod_scheme].bps;
        d = div(8*_q->user_payload_enc_lens[i], bps);
        _q->user_payload_mod_lens[i] = d.quot + (d.rem ? 1 : 0);
        _q->user_payload_mods[i] = (unsigned char*)realloc(_q->user_payload_mods[i],
                _q->user_payload_mod_lens[i]*sizeof(unsigned char));

        all_payload_mod_len += _q->user_payload_mod_lens[i];
    }
    // re-compute number of payload OFDM symbols
    // old way, divide total number of symbols by number of data subcarriers
    //d = div(all_payload_mod_len, _q->M_data);
    //_q->num_symbols_payload = d.quot + (d.rem ? 1 : 0);
    // new way, divide number of symbols of user with largest payload by
    // smallest number of subcarriers used by any user
    // long explanation:
    //         The number of subcarriers availabe for each user can be different
    //         if the number of users doesn't divide evenly into the number of subcarriers.
    //         ex. 120 subcarriers, 82 data, 12 pilot, 26 NULL
    //         3 of the users will get 16 subcarriers will the other 2 will get 17 each
    //         To make sure we generate enough ofdm symbols to transmit all the data
    //         assume the user with the largest payload also has the least number of subcarriers.
    //         Do the div with those 2 numbers (largest user_payload_mod and smallest number of subcarriers)
    //         to determine the number of necessary ofdm symbols
    d = div(_q->user_payload_mod_lens[_q->index_of_user_with_largest_payload],
            _q->num_subcarriers[_q->index_of_user_with_least_subcarriers]);
    _q->num_symbols_payload = d.quot + (d.rem ? 1 : 0);
}


// encode header
void ofdmflexframegen_encode_header(ofdmflexframegen _q)
{
    // first 'n' bytes user data
    unsigned int n;
    if(_q->ofdma)
        n = _q->ofdmflexframe_h_user_dynamic;
    else
        n = OFDMFLEXFRAME_H_USER;

    // first byte is for expansion/version validation
    _q->header[n+0] = OFDMFLEXFRAME_VERSION;

    // add payload length
    _q->header[n+1] = (_q->payload_dec_len >> 8) & 0xff;
    _q->header[n+2] = (_q->payload_dec_len     ) & 0xff;

    // add modulation scheme/depth (pack into single byte)
    _q->header[n+3]  = _q->props.mod_scheme;

    // add CRC, forward error-correction schemes
    //  CRC     : most-significant 3 bits of [n+4]
    //  fec0    : least-significant 5 bits of [n+4]
    //  fec1    : least-significant 5 bits of [n+5]
    _q->header[n+4]  = (_q->props.check & 0x07) << 5;
    _q->header[n+4] |= (_q->props.fec0) & 0x1f;
    _q->header[n+5]  = (_q->props.fec1) & 0x1f;

    // run packet encoder
    packetizer_encode(_q->p_header, _q->header, _q->header_enc);

    // scramble header
    if(_q->ofdma)
        scramble_data(_q->header_enc, _q->ofdmflexframe_h_enc_dynamic);
    else
        scramble_data(_q->header_enc, OFDMFLEXFRAME_H_ENC);

#if 0
    // print header (decoded)
    unsigned int i;
    printf("header tx (dec) : ");
    for (i=0; i<OFDMFLEXFRAME_H_DEC; i++)
        printf("%.2X ", _q->header[i]);
    printf("\n");

    // print header (encoded)
    printf("header tx (enc) : ");
    for (i=0; i<OFDMFLEXFRAME_H_ENC; i++)
        printf("%.2X ", _q->header_enc[i]);
    printf("\n");
#endif
}

// modulate header
void ofdmflexframegen_modulate_header(ofdmflexframegen _q)
{
    // repack 8-bit header bytes into 'bps'-bit payload symbols
    unsigned int bps = modulation_types[OFDMFLEXFRAME_H_MOD].bps;
    unsigned int num_written;
    unsigned int enc_len = _q->ofdma? _q->ofdmflexframe_h_enc_dynamic : OFDMFLEXFRAME_H_ENC;
    unsigned int sym_len = _q->ofdma? _q->ofdmflexframe_h_sym_dynamic : OFDMFLEXFRAME_H_SYM;

    liquid_repack_bytes(_q->header_enc, 8, enc_len,
           _q->header_mod, bps, sym_len, &num_written);
    /*
    if(_q->ofdma)
   liquid_repack_bytes(_q->header_enc, 8, _q->ofdmflexframe_h_enc_dynamic,
       _q->header_mod, bps, _q->ofdmflexframe_h_sym_dynamic,
       &num_written);
    else
   liquid_repack_bytes(_q->header_enc, 8,   OFDMFLEXFRAME_H_ENC,
                        _q->header_mod, bps, OFDMFLEXFRAME_H_SYM,
                        &num_written);
   */
}

// write first S0 symbol
void ofdmflexframegen_write_S0a(ofdmflexframegen _q,
                                float complex * _buffer)
{
#if DEBUG_OFDMFLEXFRAMEGEN
    printf("writing S0[a] symbol\n");
#endif

    // write S0 symbol into front of buffer
    ofdmframegen_write_S0a(_q->fg, _buffer);

    // update state
    _q->state = OFDMFLEXFRAMEGEN_STATE_S0b;
}

// write second S0 symbol
void ofdmflexframegen_write_S0b(ofdmflexframegen _q,
                                float complex * _buffer)
{
#if DEBUG_OFDMFLEXFRAMEGEN
    printf("writing S0[b] symbol\n");
#endif

    // write S0 symbol into front of buffer
    ofdmframegen_write_S0b(_q->fg, _buffer);

    // update state
    _q->state = OFDMFLEXFRAMEGEN_STATE_S1;
}

// write S1 symbol
void ofdmflexframegen_write_S1(ofdmflexframegen _q,
                               float complex * _buffer)
{
#if DEBUG_OFDMFLEXFRAMEGEN
    printf("writing S1 symbol\n");
#endif

    // write S1 symbol into end of buffer
    ofdmframegen_write_S1(_q->fg, _buffer);

    // update state
    _q->symbol_number = 0;
    _q->state = OFDMFLEXFRAMEGEN_STATE_HEADER;
}

// write header symbol
void ofdmflexframegen_write_header(ofdmflexframegen _q,
                                   float complex * _buffer)
{
#if DEBUG_OFDMFLEXFRAMEGEN
    printf("writing header symbol\n");
#endif

    // load data onto data subcarriers
    unsigned int i;
    int sctype;
    unsigned int num_header_symbols = _q->ofdma? _q->ofdmflexframe_h_sym_dynamic : OFDMFLEXFRAME_H_SYM;

    for (i=0; i<_q->M; i++) {
        sctype = _q->p[i];
        if (sctype == OFDMFRAME_SCTYPE_DATA) {
            // load...
            if(_q->header_symbol_index < num_header_symbols) {
                // modulate header symbol onto data subcarrier
                modem_modulate(_q->mod_header, _q->header_mod[_q->header_symbol_index++], &_q->X[i]);
                //printf("  writing symbol %3u / %3u (x = %8.5f + j%8.5f)\n", _q->header_symbol_index, num_header_symbols, crealf(_q->X[i]), cimagf(_q->X[i]));
            } else {
                // load random symbol
                unsigned int sym = modem_gen_rand_sym(_q->mod_payload);
                modem_modulate(_q->mod_payload, sym, &_q->X[i]);
            }

        } else {
            // ignore subcarrier (ofdmframegen handles nulls and pilots)
            _q->X[i] = 0.0f;
        }

    }

    // write symbol
    ofdmframegen_writesymbol(_q->fg, _q->X, _buffer);

    // check state
    if (_q->symbol_number == _q->num_symbols_header) {
        _q->symbol_number = 0;
        _q->state = OFDMFLEXFRAMEGEN_STATE_PAYLOAD;
    }
}

// write payload symbol
void ofdmflexframegen_write_payload(ofdmflexframegen _q,
                                    float complex * _buffer)
{
#if DEBUG_OFDMFLEXFRAMEGEN
    printf("writing payload symbol\n");
#endif

    // load data onto data subcarriers
    unsigned int i;
    unsigned int j;
    int sctype;
    for (i=0; i<_q->M; i++) {
        //
        sctype = _q->p[i];

        // 
        if (sctype == OFDMFRAME_SCTYPE_DATA) {
            // load...
            if(_q->ofdma)
            {
                j = _q->subcarrier_map[i];
                /*  printf("symbol index: %u\n", _q->user_payload_symbol_indices[j]);
                    printf("mod len: %u\n", _q->user_payload_mod_lens[j]);
                 */
                if(_q->user_payload_symbol_indices[j] < _q->user_payload_mod_lens[j] && j != UNALLOCATED)
                {
                    modem_modulate(_q->user_payload_modems[j],
                            _q->user_payload_mods[j][_q->user_payload_symbol_indices[j]++],
                            &_q->X[i]);
                }
                else if(_q->user_payload_mod_lens[j] == 336 && j != UNALLOCATED && _q->dummy_data)//If the
                        //payload_mod_len for a user is 336, no data has been
                        //supplied for the user, ie the user is inactive.
                    {
                        unsigned int sym = modem_gen_rand_sym(_q->user_payload_modems[j]);
                        modem_modulate(_q->user_payload_modems[j], sym, &_q->X[i]);
                    }
                else
                {
                    //   printf("unmodulated symbol for user %u\n", j);
                    // load random symbol
                    // unsigned int sym = modem_gen_rand_sym(_q->mod_payload);
                    //modem_modulate(_q->mod_payload, sym, &_q->X[i]);
                    //
                    //old code above, previously we loaded random symbols
                    //changing to use zeros instead
                    //         printf("done modulating symbols for user %u, using 0.0\n", j);
                    _q->X[i] = 0.0f;
                }
            }
            else
            {
                if (_q->payload_symbol_index < _q->payload_mod_len) {
                    // modulate payload symbol onto data subcarrier
                    modem_modulate(_q->mod_payload,
                            _q->payload_mod[_q->payload_symbol_index++], &_q->X[i]);
                } else {
                    // load random symbol
                    //unsigned int sym = 0;
                    //modem_modulate(_q->mod_payload, sym, &_q->X[i]);
                    //
                    //old code above, previously we loaded random symbols
                    //changing to use zeros instead
                    _q->X[i] = 0.0f;
                }
            }

        } else {
            // ignore subcarrier (ofdmframegen handles nulls and pilots)
            _q->X[i] = 0.0f;
        }
    }

    // write symbol
    ofdmframegen_writesymbol(_q->fg, _q->X, _buffer);

    // check to see if this is the last symbol in the payload
    if (_q->symbol_number == _q->num_symbols_payload)
    {
        _q->frame_complete = 1;
        if(_q->ofdma)
        {
            //Look at all unallocated subcarriers
            //If a subcarrier hasn't been used for the last 50 frames,
            //reallocate it to one of the users
            unsigned int k;
            for(k = 0; k < _q->M; k++)
            {
                if(_q->subcarrier_map[k] == UNALLOCATED)
                {
                    if(_q->frames_sent_since_last_use[k] >= _q->reallocation_delay)
                    {
                        ofdmflexframegen_reallocate_subcarrier(_q, k);
                    }
                    else
                        _q->frames_sent_since_last_use[k]++;
                }
            }
        }
    }

}


void ofdmflexframegen_print_sctype(ofdmflexframegen _q)
{
    printf("Subcarriers: %u\n", _q->M);
    printf("Pilots: %u\n", _q->M_pilot);
    printf("Nulls: %u\n", _q->M_null);
    printf("Data: %u\n", _q->M_data);
    ofdmframe_print_sctype(_q->p, _q->M);
}

