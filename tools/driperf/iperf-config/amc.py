def paramgen():
    whiten = [True]

    clip = [0.999]

    amc_table = [ ("crc32", "v29", "none", "qpsk")
                , ("crc32", "none", "rs8", "qpsk")
                , ("crc32", "none", "rs8", "qam8")
                , ("crc32", "none", "rs8", "qam16")
                , ("crc32", "none", "rs8", "qam32")
                , ("crc32", "none", "rs8", "qam64")
                , ("crc32", "none", "rs8", "qam128")
                , ("crc32", "none", "rs8", "qam256")
                ]

    for (crc, fec0, fec1, ms) in amc_table:
        for w in whiten:
            for c in clip:
                yield (w, crc, fec0, fec1, ms, 'auto', c)

params = paramgen()
