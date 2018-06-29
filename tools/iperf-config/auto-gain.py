def paramgen():
    whiten = [True]

    clip = [1., 0.999, 0.99, 0.98]

    crc_schemes = ['crc32']

    fec_schemes = [ ('none', 'rs8')

                  , ('v27',    'none')
                  , ('v27p34', 'none')
                  , ('v27p78', 'none')
                  ]

    mod_schemes = [ ('qpsk', 'auto')
                  , ('qam16', 'auto')
                  , ('qam64', 'auto')
                  , ('qam128', 'auto')
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
