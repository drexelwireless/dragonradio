def paramgen():
    whiten = [True]

    clip = [0.98]

    crc_schemes = ['crc32']

    fec_schemes = [ ('none',   'rs8')

                  , ('v27',    'none')
                  , ('v27p34', 'none')
                  , ('v27p78', 'none')
                  , ('v27',    'rs8')

                  , ('v29',    'none')
                  , ('v29p34', 'none')
                  , ('v29p78', 'none')
                  , ('v29',    'rs8')
                  ]

    mod_schemes = [ ('qpsk', 'auto')
                  , ('qam256', 'auto')
                  ]

    for w in whiten:
        for crc in crc_schemes:
            for (fec0, fec1) in fec_schemes:
                for (ms, g) in mod_schemes:
                    # Only iterate over clipping parameter when using auto soft
                    # TX gain
                    if g == 'auto':
                        real_clip = clip
                    else:
                        real_clip = [1.]

                    for c in real_clip:
                        yield (w, crc, fec0, fec1, ms, g, c)

params = paramgen()
